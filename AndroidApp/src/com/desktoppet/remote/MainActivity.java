package com.desktoppet.remote;

import android.Manifest;
import android.annotation.SuppressLint;
import android.app.Activity;
import android.app.AlertDialog;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothGatt;
import android.bluetooth.BluetoothGattCallback;
import android.bluetooth.BluetoothGattCharacteristic;
import android.bluetooth.BluetoothGattDescriptor;
import android.bluetooth.BluetoothGattService;
import android.bluetooth.BluetoothManager;
import android.bluetooth.BluetoothProfile;
import android.bluetooth.le.BluetoothLeScanner;
import android.bluetooth.le.ScanCallback;
import android.bluetooth.le.ScanResult;
import android.bluetooth.le.ScanSettings;
import android.content.Context;
import android.content.DialogInterface;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.location.LocationManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.provider.Settings;
import android.view.Gravity;
import android.view.View;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Date;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.UUID;

/**
 * 桌面宠物 BLE 蓝牙遥控器。
 *
 * 与官方 demo（eciot_bletool）一致：
 *   1. BLE 扫描附近设备（不是找已配对设备）
 *   2. connectGatt(ctx, false, cb) 直连
 *   3. discoverServices() 后取
 *        写特征值 0000fff2-0000-1000-8000-00805f9b34fb
 *        通知特征值 0000fff1-0000-1000-8000-00805f9b34fb
 *   4. 单字节无应答写（WRITE_TYPE_NO_RESPONSE）
 *
 * 协议：每按一个按钮发送 1 个 ASCII 字节，不加换行、不加回车。
 */
public class MainActivity extends Activity {

    /** 写数据特征值（手机 -> 模块） */
    private static final UUID UUID_WRITE =
            UUID.fromString("0000fff2-0000-1000-8000-00805f9b34fb");
    /** 通知特征值（模块 -> 手机） */
    private static final UUID UUID_NOTIFY =
            UUID.fromString("0000fff1-0000-1000-8000-00805f9b34fb");

    private static final int REQ_PERM = 1;
    private static final int REQ_ENABLE = 2;
    private static final int MAX_LOG_LINES = 60;
    private static final long SCAN_TIMEOUT_MS = 12000L;

    /* ================= 指令表（与固件一一对应） ================= */

    private static class Cmd {
        final String label;
        final char code;

        Cmd(String label, char code) {
            this.label = label;
            this.code = code;
        }
    }

    private static final String[] GROUP_TITLE = {
            "姿态", "移动", "特技", "灯光与状态", "调速"
    };

    private static final Cmd[][] GROUPS = {
            {
                    new Cmd("放松趴下", ')'),
                    new Cmd("蹲下", '0'),
                    new Cmd("直立", '1'),
                    new Cmd("趴下", '2'),
            },
            {
                    new Cmd("前进", '3'),
                    new Cmd("后退", '4'),
                    new Cmd("左转", '5'),
                    new Cmd("右转", '6'),
                    new Cmd("摇摆", '7'),
            },
            {
                    new Cmd("前跳", 'A'),
                    new Cmd("后跳", 'B'),
                    new Cmd("打招呼", 'C'),
                    new Cmd("伸懒腰", 'H'),
                    new Cmd("拉伸", 'I'),
                    new Cmd("摇尾巴", '@'),
            },
            {
                    new Cmd("开灯", 'D'),
                    new Cmd("关灯", 'E'),
                    new Cmd("呼吸灯开", 'F'),
                    new Cmd("呼吸灯关", 'G'),
                    new Cmd("显示电量", 'P'),
            },
            {
                    new Cmd("走路加速", '8'),
                    new Cmd("摇摆加速", '9'),
            }
    };

    /* ================= 扫描到的设备 ================= */

    private static class Found {
        BluetoothDevice dev;
        String name;
        int rssi;

        Found(BluetoothDevice dev, String name, int rssi) {
            this.dev = dev;
            this.name = name;
            this.rssi = rssi;
        }
    }

    /* ================= 运行状态 ================= */

    private final Handler ui = new Handler(Looper.getMainLooper());
    private final SimpleDateFormat ts = new SimpleDateFormat("HH:mm:ss", Locale.US);
    private final StringBuilder logBuf = new StringBuilder();

    private final LinkedHashMap<String, Found> foundMap = new LinkedHashMap<String, Found>();
    private final List<String> foundKeys = new ArrayList<String>();

    private BluetoothAdapter adapter;
    private BluetoothLeScanner scanner;
    private boolean scanning;

    private BluetoothGatt gatt;
    private BluetoothGattCharacteristic writeChar;
    private boolean ready;          // 服务发现完成、可以发指令
    private String deviceLabel = "";

    private AlertDialog scanDialog;
    private ArrayAdapter<String> scanAdapter;

    private TextView tvStatus;
    private TextView tvLog;
    private View dot;
    private Button btnScan;
    private ScrollView logScroll;

    private Button lastBtn;

    /* ================= 生命周期 ================= */

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        tvStatus = (TextView) findViewById(R.id.status);
        tvLog = (TextView) findViewById(R.id.log);
        dot = findViewById(R.id.dot);
        btnScan = (Button) findViewById(R.id.btnScan);
        logScroll = (ScrollView) findViewById(R.id.logScroll);

        BluetoothManager bm = (BluetoothManager) getSystemService(Context.BLUETOOTH_SERVICE);
        adapter = bm == null ? null : bm.getAdapter();

        buildGrid();

        btnScan.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                if (gatt != null) {
                    disconnect(getString(R.string.not_connected));
                } else {
                    startScanFlow();
                }
            }
        });

        setState(STATE_IDLE, getString(R.string.not_connected));
        appendLog("就绪：点右上角「扫描」搜索附近的宠物设备");
    }

    @Override
    protected void onDestroy() {
        stopScan();
        closeGatt();
        super.onDestroy();
    }

    /* ================= 界面构建 ================= */

    private int dp(float v) {
        return (int) (v * getResources().getDisplayMetrics().density + 0.5f);
    }

    @SuppressWarnings("deprecation")
    private int col(int id) {
        return getResources().getColor(id);
    }

    private void buildGrid() {
        LinearLayout grid = (LinearLayout) findViewById(R.id.grid);
        grid.removeAllViews();

        final int PER_ROW = 4;

        for (int g = 0; g < GROUPS.length; g++) {
            TextView title = new TextView(this);
            title.setText(GROUP_TITLE[g]);
            title.setTextColor(col(R.color.muted));
            title.setTextSize(12f);
            LinearLayout.LayoutParams tp = new LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.WRAP_CONTENT,
                    LinearLayout.LayoutParams.WRAP_CONTENT);
            tp.topMargin = dp(16f);
            tp.bottomMargin = dp(2f);
            title.setLayoutParams(tp);
            grid.addView(title);

            Cmd[] cmds = GROUPS[g];
            int rows = (cmds.length + PER_ROW - 1) / PER_ROW;

            for (int r = 0; r < rows; r++) {
                LinearLayout row = new LinearLayout(this);
                row.setOrientation(LinearLayout.HORIZONTAL);
                LinearLayout.LayoutParams rp = new LinearLayout.LayoutParams(
                        LinearLayout.LayoutParams.MATCH_PARENT,
                        LinearLayout.LayoutParams.WRAP_CONTENT);
                rp.topMargin = dp(8f);
                row.setLayoutParams(rp);

                for (int c = 0; c < PER_ROW; c++) {
                    int idx = r * PER_ROW + c;
                    LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                            0, dp(58f), 1f);
                    lp.setMargins(dp(4f), 0, dp(4f), 0);

                    if (idx < cmds.length) {
                        row.addView(makeCmdButton(cmds[idx]), lp);
                    } else {
                        row.addView(new View(this), lp);
                    }
                }
                grid.addView(row);
            }
        }
    }

    private Button makeCmdButton(final Cmd cmd) {
        Button btn = new Button(this);
        btn.setText(cmd.label);
        btn.setTextSize(13f);
        btn.setTextColor(col(R.color.text));
        btn.setAllCaps(false);
        btn.setGravity(Gravity.CENTER);
        btn.setBackgroundResource(R.drawable.sel_btn);
        btn.setPadding(dp(2f), dp(2f), dp(2f), dp(2f));
        btn.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                send(cmd, (Button) v);
            }
        });
        return btn;
    }

    /* ================= 权限 ================= */

    private String[] requiredPermissions() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            return new String[]{
                    Manifest.permission.BLUETOOTH_SCAN,
                    Manifest.permission.BLUETOOTH_CONNECT
            };
        }
        return new String[]{Manifest.permission.ACCESS_FINE_LOCATION};
    }

    private boolean hasPermissions() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.M) {
            return true;
        }
        for (String p : requiredPermissions()) {
            if (checkSelfPermission(p) != PackageManager.PERMISSION_GRANTED) {
                return false;
            }
        }
        return true;
    }

    /* ================= 扫描 ================= */

    private void startScanFlow() {
        if (adapter == null) {
            toast(getString(R.string.no_bt));
            return;
        }
        if (!hasPermissions()) {
            requestPermissions(requiredPermissions(), REQ_PERM);
            return;
        }
        if (!adapter.isEnabled()) {
            // 还需要 NEARBY/蓝牙开启权限，这里直接跳系统开关
            try {
                startActivityForResult(
                        new Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE), REQ_ENABLE);
            } catch (Exception e) {
                openBtSettings();
            }
            return;
        }
        // Android 11 及以下扫描 BLE 必须打开系统定位
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.S && !isLocationEnabled()) {
            new AlertDialog.Builder(this)
                    .setTitle(R.string.need_location_title)
                    .setMessage(R.string.need_location_msg)
                    .setPositiveButton(R.string.open_settings,
                            new DialogInterface.OnClickListener() {
                                @Override
                                public void onClick(DialogInterface d, int w) {
                                    try {
                                        startActivity(new Intent(Settings.ACTION_LOCATION_SOURCE_SETTINGS));
                                    } catch (Exception ignore) {
                                        openBtSettings();
                                    }
                                }
                            })
                    .setNegativeButton(R.string.cancel, null)
                    .show();
            return;
        }
        showScanDialog();
        beginScan();
    }

    private boolean isLocationEnabled() {
        try {
            LocationManager lm = (LocationManager) getSystemService(Context.LOCATION_SERVICE);
            if (lm == null) {
                return true;
            }
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
                return lm.isLocationEnabled();
            }
            return lm.isProviderEnabled(LocationManager.GPS_PROVIDER)
                    || lm.isProviderEnabled(LocationManager.NETWORK_PROVIDER);
        } catch (Exception e) {
            return true;
        }
    }

    private void showScanDialog() {
        foundMap.clear();
        foundKeys.clear();

        scanAdapter = new ArrayAdapter<String>(this,
                android.R.layout.simple_list_item_1, new ArrayList<String>());

        scanDialog = new AlertDialog.Builder(this)
                .setTitle(R.string.scanning)
                .setAdapter(scanAdapter, new DialogInterface.OnClickListener() {
                    @Override
                    public void onClick(DialogInterface d, int which) {
                        if (which < 0 || which >= foundKeys.size()) {
                            return;
                        }
                        Found f = foundMap.get(foundKeys.get(which));
                        stopScan();
                        d.dismiss();
                        if (f != null) {
                            connect(f.dev, f.name);
                        }
                    }
                })
                .setNegativeButton(R.string.cancel, new DialogInterface.OnClickListener() {
                    @Override
                    public void onClick(DialogInterface d, int w) {
                        stopScan();
                    }
                })
                .setOnDismissListener(new DialogInterface.OnDismissListener() {
                    @Override
                    public void onDismiss(DialogInterface d) {
                        stopScan();
                        if (gatt == null) {
                            setState(STATE_IDLE, getString(R.string.not_connected));
                        }
                    }
                })
                .create();
        scanDialog.show();
    }

    @SuppressLint("MissingPermission")
    private void beginScan() {
        if (adapter == null) {
            return;
        }
        scanner = adapter.getBluetoothLeScanner();
        if (scanner == null) {
            toast(getString(R.string.bt_off_hint));
            return;
        }
        ScanSettings settings = new ScanSettings.Builder()
                .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
                .build();
        try {
            scanner.startScan(null, settings, scanCallback);
            scanning = true;
            setState(STATE_SCANNING, getString(R.string.scanning));
            appendLog("开始扫描附近的 BLE 设备 …");
            ui.postDelayed(stopScanRunnable, SCAN_TIMEOUT_MS);
        } catch (SecurityException e) {
            toast(getString(R.string.need_perm));
        }
    }

    @SuppressLint("MissingPermission")
    private void stopScan() {
        ui.removeCallbacks(stopScanRunnable);
        if (!scanning || scanner == null) {
            scanning = false;
            return;
        }
        try {
            scanner.stopScan(scanCallback);
        } catch (Exception ignore) {
        }
        scanning = false;
        if (gatt == null && (scanDialog == null || !scanDialog.isShowing())) {
            setState(STATE_IDLE, getString(R.string.not_connected));
        }
        if (foundKeys.isEmpty()) {
            appendLog("扫描结束，没有发现设备（确认宠物已上电、距离 1 米内）");
        } else {
            appendLog("扫描结束，共发现 " + foundKeys.size() + " 个设备");
        }
    }

    private final Runnable stopScanRunnable = new Runnable() {
        @Override
        public void run() {
            if (scanning) {
                stopScan();
            }
        }
    };

    private final ScanCallback scanCallback = new ScanCallback() {
        @Override
        public void onScanResult(int callbackType, ScanResult result) {
            onDevice(result);
        }

        @Override
        public void onBatchScanResults(List<ScanResult> results) {
            for (ScanResult r : results) {
                onDevice(r);
            }
        }

        @Override
        public void onScanFailed(int errorCode) {
            appendLog("扫描失败，错误码 " + errorCode);
            toast(getString(R.string.scan_failed));
        }
    };

    @SuppressLint("MissingPermission")
    private void onDevice(ScanResult result) {
        if (result == null || result.getDevice() == null) {
            return;
        }
        BluetoothDevice dev = result.getDevice();
        String mac = dev.getAddress();
        if (mac == null) {
            return;
        }

        String name;
        try {
            name = dev.getName();
        } catch (SecurityException e) {
            name = null;
        }
        if (name == null || name.length() == 0) {
            try {
                name = result.getScanRecord() == null
                        ? null : result.getScanRecord().getDeviceName();
            } catch (Exception e) {
                name = null;
            }
        }
        if (name == null || name.length() == 0) {
            name = getString(R.string.unknown_device);
        }

        Found old = foundMap.get(mac);
        if (old == null) {
            foundMap.put(mac, new Found(dev, name, result.getRssi()));
            foundKeys.add(mac);
            appendLog("发现 " + name + "  " + mac + "  " + result.getRssi() + "dBm");
        } else {
            old.rssi = result.getRssi();
            old.name = name;
        }
        refreshScanList();
    }

    private void refreshScanList() {
        if (scanAdapter == null) {
            return;
        }
        scanAdapter.clear();
        for (String k : foundKeys) {
            Found f = foundMap.get(k);
            if (f == null) {
                continue;
            }
            scanAdapter.add(f.name + "    " + f.rssi + " dBm\n" + k);
        }
        scanAdapter.notifyDataSetChanged();
    }

    /* ================= GATT 连接 ================= */

    @SuppressLint("MissingPermission")
    private void connect(BluetoothDevice dev, String name) {
        deviceLabel = name;
        setState(STATE_CONNECTING, getString(R.string.connecting));
        appendLog("正在连接 " + name + " …");

        closeGatt();
        ready = false;
        writeChar = null;

        try {
            gatt = dev.connectGatt(this, false, gattCallback);
        } catch (SecurityException e) {
            setState(STATE_IDLE, getString(R.string.connect_fail));
            toast(getString(R.string.need_perm));
            return;
        }
        if (gatt == null) {
            setState(STATE_IDLE, getString(R.string.connect_fail));
            appendLog("connectGatt 返回空，可能是系统蓝牙异常");
        }
    }

    private final BluetoothGattCallback gattCallback = new BluetoothGattCallback() {

        @SuppressLint("MissingPermission")
        @Override
        public void onConnectionStateChange(BluetoothGatt g, final int status, final int newState) {
            if (status != BluetoothGatt.GATT_SUCCESS) {
                final String msg = "连接状态异常 status=" + status;
                ui.post(new Runnable() {
                    @Override
                    public void run() {
                        appendLog("× " + msg);
                        closeGatt();
                        setState(STATE_IDLE, getString(R.string.connect_fail));
                    }
                });
                return;
            }
            if (newState == BluetoothProfile.STATE_CONNECTED) {
                ui.post(new Runnable() {
                    @Override
                    public void run() {
                        appendLog("已连上，正在发现服务 …");
                    }
                });
                try {
                    g.discoverServices();
                } catch (Exception e) {
                    ui.post(new Runnable() {
                        @Override
                        public void run() {
                            appendLog("× discoverServices 失败：" + e.getMessage());
                        }
                    });
                }
                return;
            }
            if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                ui.post(new Runnable() {
                    @Override
                    public void run() {
                        appendLog("设备已断开");
                        closeGatt();
                        setState(STATE_IDLE, getString(R.string.lost));
                    }
                });
            }
        }

        @Override
        public void onServicesDiscovered(final BluetoothGatt g, int status) {
            if (status != BluetoothGatt.GATT_SUCCESS) {
                ui.post(new Runnable() {
                    @Override
                    public void run() {
                        appendLog("× 服务发现失败 status=" + status);
                        closeGatt();
                        setState(STATE_IDLE, getString(R.string.connect_fail));
                    }
                });
                return;
            }

            BluetoothGattCharacteristic w = null;
            BluetoothGattCharacteristic n = null;
            StringBuilder sb = new StringBuilder();

            List<BluetoothGattService> services = g.getServices();
            for (BluetoothGattService s : services) {
                sb.append(s.getUuid().toString()).append('\n');
                for (BluetoothGattCharacteristic c : s.getCharacteristics()) {
                    if (UUID_WRITE.equals(c.getUuid())) {
                        w = c;
                    }
                    if (UUID_NOTIFY.equals(c.getUuid())) {
                        n = c;
                    }
                }
            }

            final BluetoothGattCharacteristic notify = n;
            final BluetoothGattCharacteristic write = w;

            ui.post(new Runnable() {
                @Override
                public void run() {
                    if (write == null) {
                        appendLog("× 没找到写特征值 FFF2，这个设备可能不是宠物模块");
                        disconnect(getString(R.string.connect_fail));
                        return;
                    }
                    writeChar = write;
                    ready = true;
                    setState(STATE_READY, getString(R.string.connected) + " " + deviceLabel);
                    appendLog("√ 已就绪（写特征值 FFF2）");
                }
            });

            if (notify != null) {
                try {
                    g.setCharacteristicNotification(notify, true);
                    for (BluetoothGattDescriptor d : notify.getDescriptors()) {
                        d.setValue(BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE);
                        g.writeDescriptor(d);
                    }
                } catch (Exception ignore) {
                }
            }
        }
    };

    /* ================= 发送 ================= */

    @SuppressLint("MissingPermission")
    private void send(final Cmd cmd, Button btn) {
        if (!ready || gatt == null || writeChar == null) {
            toast(getString(R.string.need_connect));
            return;
        }

        if (lastBtn != null && lastBtn != btn) {
            lastBtn.setBackgroundResource(R.drawable.sel_btn);
        }
        btn.setBackgroundResource(R.drawable.sel_btn_primary);
        lastBtn = btn;

        final byte[] data = new byte[]{(byte) cmd.code};
        boolean ok;
        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                ok = gatt.writeCharacteristic(writeChar, data,
                        BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE)
                        == BluetoothGatt.GATT_SUCCESS;
            } else {
                writeChar.setWriteType(BluetoothGattCharacteristic.WRITE_TYPE_NO_RESPONSE);
                writeChar.setValue(data);
                ok = gatt.writeCharacteristic(writeChar);
            }
        } catch (SecurityException e) {
            ok = false;
        } catch (Exception e) {
            ok = false;
        }

        if (ok) {
            appendLog("→ " + cmd.label + "  '" + cmd.code + "'  0x"
                    + Integer.toHexString(cmd.code).toUpperCase(Locale.US));
        } else {
            appendLog("× " + cmd.label + " 写入失败（GATT 忙或连接已断）");
        }
    }

    /* ================= 状态与日志 ================= */

    private static final int STATE_IDLE = 0;
    private static final int STATE_SCANNING = 1;
    private static final int STATE_CONNECTING = 2;
    private static final int STATE_READY = 3;

    private void setState(int state, String text) {
        tvStatus.setText(text);
        switch (state) {
            case STATE_READY:
                tvStatus.setTextColor(col(R.color.green));
                dot.setBackgroundResource(R.drawable.dot_on);
                btnScan.setText(R.string.disconnect);
                break;
            case STATE_SCANNING:
            case STATE_CONNECTING:
                tvStatus.setTextColor(col(R.color.amber));
                dot.setBackgroundResource(R.drawable.dot_busy);
                btnScan.setText(R.string.disconnect);
                break;
            default:
                tvStatus.setTextColor(col(R.color.muted));
                dot.setBackgroundResource(R.drawable.dot_off);
                btnScan.setText(R.string.scan);
                break;
        }
    }

    private void disconnect(String reason) {
        closeGatt();
        setState(STATE_IDLE, reason);
        if (lastBtn != null) {
            lastBtn.setBackgroundResource(R.drawable.sel_btn);
            lastBtn = null;
        }
    }

    @SuppressLint("MissingPermission")
    private void closeGatt() {
        ready = false;
        writeChar = null;
        if (gatt != null) {
            try {
                gatt.disconnect();
            } catch (Exception ignore) {
            }
            try {
                gatt.close();
            } catch (Exception ignore) {
            }
            gatt = null;
        }
    }

    private void openBtSettings() {
        try {
            startActivity(new Intent(Settings.ACTION_BLUETOOTH_SETTINGS));
        } catch (Exception e) {
            try {
                startActivity(new Intent(Settings.ACTION_SETTINGS));
            } catch (Exception ignore) {
            }
        }
    }

    private void appendLog(String s) {
        logBuf.append(ts.format(new Date())).append("  ").append(s).append('\n');

        int lines = 0;
        for (int i = 0; i < logBuf.length(); i++) {
            if (logBuf.charAt(i) == '\n') {
                lines++;
            }
        }
        if (lines > MAX_LOG_LINES) {
            int cut = 0;
            for (int k = 0; k < lines - MAX_LOG_LINES; k++) {
                int p = logBuf.indexOf("\n", cut);
                if (p < 0) {
                    break;
                }
                cut = p + 1;
            }
            logBuf.delete(0, cut);
        }

        tvLog.setText(logBuf.toString());
        logScroll.post(new Runnable() {
            @Override
            public void run() {
                logScroll.fullScroll(View.FOCUS_DOWN);
            }
        });
    }

    private void toast(String s) {
        Toast.makeText(this, s, Toast.LENGTH_SHORT).show();
    }

    /* ================= 回调 ================= */

    @Override
    public void onRequestPermissionsResult(int req, String[] perms, int[] grant) {
        if (req == REQ_PERM) {
            boolean all = grant.length > 0;
            for (int g : grant) {
                if (g != PackageManager.PERMISSION_GRANTED) {
                    all = false;
                    break;
                }
            }
            if (all) {
                startScanFlow();
            } else {
                toast(getString(R.string.need_perm));
            }
        }
    }

    @Override
    protected void onActivityResult(int req, int res, Intent data) {
        if (req == REQ_ENABLE) {
            if (res == RESULT_OK) {
                startScanFlow();
            } else {
                toast(getString(R.string.bt_off_hint));
            }
        }
    }
}