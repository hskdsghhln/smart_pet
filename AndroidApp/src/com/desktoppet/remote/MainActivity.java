package com.desktoppet.remote;

import android.Manifest;
import android.app.Activity;
import android.app.AlertDialog;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothSocket;
import android.content.DialogInterface;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.provider.Settings;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import java.io.IOException;
import java.io.OutputStream;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Date;
import java.util.List;
import java.util.Locale;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * 桌面宠物蓝牙遥控器。
 *
 * 通信方式：经典蓝牙 SPP（串口透传），对应板子上的 HC-05 / HC-06 模块。
 * 协议：每按一个按钮发送 1 个 ASCII 字节，不加换行、不加回车。
 *       这与固件 PetAction_Judgmengt() 里的蓝牙分支完全对应。
 */
public class MainActivity extends Activity {

    /** 标准串口透传 UUID，HC-05/HC-06 通用 */
    private static final UUID SPP_UUID =
            UUID.fromString("00001101-0000-1000-8000-00805F9B34FB");

    private static final int REQ_PERM = 1;
    private static final int REQ_ENABLE = 2;
    private static final int MAX_LOG_LINES = 60;

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

    /* ================= 运行状态 ================= */

    private final Handler ui = new Handler(Looper.getMainLooper());
    private final ExecutorService io = Executors.newSingleThreadExecutor();
    private final SimpleDateFormat ts = new SimpleDateFormat("HH:mm:ss", Locale.US);
    private final StringBuilder logBuf = new StringBuilder();

    private BluetoothAdapter adapter;
    private BluetoothSocket socket;
    private OutputStream out;

    private TextView tvStatus;
    private TextView tvLog;
    private View dot;
    private Button btnConnect;
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
        btnConnect = (Button) findViewById(R.id.btnConnect);
        logScroll = (ScrollView) findViewById(R.id.logScroll);

        adapter = BluetoothAdapter.getDefaultAdapter();

        buildGrid();

        btnConnect.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                if (out != null) {
                    disconnect(getString(R.string.not_connected));
                } else {
                    chooseDevice();
                }
            }
        });

        setConnected(false, getString(R.string.not_connected));
        appendLog("就绪：先在系统蓝牙里配对 HC-05/HC-06，再点右上角「连接」");
    }

    @Override
    protected void onDestroy() {
        closeQuietly();
        io.shutdownNow();
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
                        // 占位，保证最后一行按钮宽度与上面一致
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

    /* ================= 发送 ================= */

    private void send(final Cmd cmd, final Button btn) {
        if (out == null) {
            toast(getString(R.string.need_connect));
            return;
        }

        // 高亮当前指令（移动类指令会一直执行到被下一条替换）
        if (lastBtn != null && lastBtn != btn) {
            lastBtn.setBackgroundResource(R.drawable.sel_btn);
        }
        btn.setBackgroundResource(R.drawable.sel_btn_primary);
        lastBtn = btn;

        io.execute(new Runnable() {
            @Override
            public void run() {
                try {
                    out.write(cmd.code);   // 单字节，绝不加换行
                    out.flush();
                    final String head = "→ " + cmd.label;
                    final String tail = "  '" + cmd.code + "'  0x"
                            + Integer.toHexString(cmd.code).toUpperCase(Locale.US);
                    ui.post(new Runnable() {
                        @Override
                        public void run() {
                            appendLog(head + tail);
                        }
                    });
                } catch (final IOException e) {
                    ui.post(new Runnable() {
                        @Override
                        public void run() {
                            appendLog("× 发送失败：" + e.getMessage());
                            disconnect(getString(R.string.lost));
                        }
                    });
                }
            }
        });
    }

    /* ================= 蓝牙连接 ================= */

    private void chooseDevice() {
        if (adapter == null) {
            toast(getString(R.string.no_bt));
            return;
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M
                && checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT)
                != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[]{Manifest.permission.BLUETOOTH_CONNECT}, REQ_PERM);
            return;
        }
        if (!adapter.isEnabled()) {
            startActivityForResult(
                    new Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE), REQ_ENABLE);
            return;
        }
        showDeviceList();
    }

    private void showDeviceList() {
        Set<BluetoothDevice> bonded;
        try {
            bonded = adapter.getBondedDevices();
        } catch (SecurityException e) {
            bonded = null;
        }

        if (bonded == null || bonded.isEmpty()) {
            new AlertDialog.Builder(this)
                    .setTitle(R.string.no_paired_title)
                    .setMessage(R.string.no_paired_msg)
                    .setPositiveButton(R.string.open_settings,
                            new DialogInterface.OnClickListener() {
                                @Override
                                public void onClick(DialogInterface d, int w) {
                                    openBtSettings();
                                }
                            })
                    .setNegativeButton(R.string.cancel, null)
                    .show();
            return;
        }

        final List<BluetoothDevice> devs = new ArrayList<BluetoothDevice>();
        final List<String> items = new ArrayList<String>();

        for (BluetoothDevice d : bonded) {
            String name;
            try {
                name = d.getName();
            } catch (SecurityException e) {
                name = null;
            }
            if (name == null || name.length() == 0) {
                name = "未知设备";
            }
            devs.add(d);
            items.add(name + "\n" + d.getAddress());
        }

        new AlertDialog.Builder(this)
                .setTitle(R.string.pick_device)
                .setItems(items.toArray(new String[0]),
                        new DialogInterface.OnClickListener() {
                            @Override
                            public void onClick(DialogInterface dlg, int which) {
                                connect(devs.get(which));
                            }
                        })
                .setNeutralButton(R.string.open_settings,
                        new DialogInterface.OnClickListener() {
                            @Override
                            public void onClick(DialogInterface d, int w) {
                                openBtSettings();
                            }
                        })
                .show();
    }

    private void connect(final BluetoothDevice dev) {
        setConnected(false, getString(R.string.connecting));
        appendLog("正在连接 " + dev.getAddress() + " …");

        io.execute(new Runnable() {
            @Override
            public void run() {
                try {
                    try {
                        adapter.cancelDiscovery();
                    } catch (Exception ignore) {
                    }

                    BluetoothSocket s = dev.createRfcommSocketToServiceRecord(SPP_UUID);
                    s.connect();

                    socket = s;
                    out = s.getOutputStream();

                    ui.post(new Runnable() {
                        @Override
                        public void run() {
                            setConnected(true, getString(R.string.connected));
                            appendLog("√ 连接成功，可以开始操作");
                        }
                    });
                } catch (final Exception e) {
                    closeQuietly();
                    ui.post(new Runnable() {
                        @Override
                        public void run() {
                            setConnected(false, getString(R.string.connect_fail));
                            appendLog("× 连接失败：" + e.getClass().getSimpleName()
                                    + (e.getMessage() == null ? "" : (" " + e.getMessage())));
                        }
                    });
                }
            }
        });
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

    /* ================= 状态与日志 ================= */

    private void setConnected(boolean on, String text) {
        tvStatus.setText(text);
        tvStatus.setTextColor(col(on ? R.color.green : R.color.muted));
        dot.setBackgroundResource(on ? R.drawable.dot_on : R.drawable.dot_off);
        btnConnect.setText(on ? R.string.disconnect : R.string.connect);
    }

    private void disconnect(String reason) {
        closeQuietly();
        setConnected(false, reason);
        if (lastBtn != null) {
            lastBtn.setBackgroundResource(R.drawable.sel_btn);
            lastBtn = null;
        }
    }

    private void closeQuietly() {
        try {
            if (out != null) {
                out.close();
            }
        } catch (Exception ignore) {
        }
        try {
            if (socket != null) {
                socket.close();
            }
        } catch (Exception ignore) {
        }
        out = null;
        socket = null;
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

    /* ================= 权限与结果回调 ================= */

    @Override
    public void onRequestPermissionsResult(int req, String[] perms, int[] grant) {
        if (req == REQ_PERM) {
            if (grant.length > 0 && grant[0] == PackageManager.PERMISSION_GRANTED) {
                chooseDevice();
            } else {
                toast("没有蓝牙权限，无法连接设备");
            }
        }
    }

    @Override
    protected void onActivityResult(int req, int res, Intent data) {
        if (req == REQ_ENABLE) {
            if (res == RESULT_OK) {
                showDeviceList();
            } else {
                toast("蓝牙未开启");
            }
        }
    }
}