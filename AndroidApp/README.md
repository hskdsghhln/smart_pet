# 桌宠遥控（Android BLE 上位机）

通过手机蓝牙控制 STM32 桌面宠物的动作。对应固件 `HardWare/PetAction.c` 里的蓝牙指令表。

- 包名：`com.desktoppet.remote`
- 最低系统：Android 5.0（API 21）
- 传输：**BLE（低功耗蓝牙）**，不是经典蓝牙 SPP
- 协议：**每按一次按钮发送 1 个 ASCII 字节**，不加换行、不加回车

安装包：`DeskPetRemote.apk`（约 33 KB）

---

## 一、和官方 demo 的对应关系

本工程的 BLE 实现参考官方 `eciot_bletool_for_Android_V1.0.10_Java` 的 `ECBLE.java`，
关键参数完全一致：

| 项目 | 值 |
| --- | --- |
| 扫描 | BLE 扫描（`BluetoothLeScanner` / `SCAN_MODE_LOW_LATENCY`） |
| 连接 | `device.connectGatt(ctx, **false**, callback)`（`false` = 直接连，不等广播） |
| 服务发现 | 连上后立即 `discoverServices()` |
| **写特征值** | `0000fff2-0000-1000-8000-00805f9b34fb` |
| **通知特征值** | `0000fff1-0000-1000-8000-00805f9b34fb` |
| 写入方式 | `WRITE_TYPE_NO_RESPONSE`（无应答写） |
| 配对 | **不需要** |

> 上一版用了经典蓝牙 SPP（`createRfcommSocketToServiceRecord`），
> 对这个模块完全不适用——BLE 设备根本不会出现在"已配对设备"列表里，
> 这也是当时连不上的原因。

## 二、安装

把 `DeskPetRemote.apk` 传到手机，点击安装。首次需要在系统里允许"安装未知来源应用"。

## 三、使用步骤

1. **给桌宠上电**，模块开始广播
2. 打开 App，点右上角 **扫描**
3. 弹出列表，会实时刷出附近的 BLE 设备（名称 + 信号强度 + MAC）
4. **点一下你的设备**（通常名字里带 `EC`、`JDY`、`HM`、`AT` 之类，或者一串 MAC）
5. 状态灯变绿、显示"已连接 xxx"就可以按按钮了

**不需要去系统蓝牙里配对**，扫到直接点就连上。

扫描 12 秒后自动停止。如果列表是空的：

- 确认宠物已上电（模块指示灯在闪）
- 手机离模块 1 米以内
- 确认手机蓝牙已打开
- **Android 11 及以下**：必须打开系统"定位"开关，否则扫不到任何 BLE 设备（这是安卓的限制，不是 bug）

## 四、界面说明

按钮分 5 组，底部是发送记录（时间戳 + 指令名 + 实际字节 + 十六进制）。

| 分组 | 按钮 |
| --- | --- |
| 姿态 | 放松趴下、蹲下、直立、趴下 |
| 移动 | 前进、后退、左转、右转、摇摆 |
| 特技 | 前跳、后跳、打招呼、伸懒腰、拉伸、摇尾巴 |
| 灯光与状态 | 开灯、关灯、呼吸灯开、呼吸灯关、显示电量 |
| 调速 | 走路加速、摇摆加速 |

连接过程中会打印服务发现结果，如果没找到 `FFF2` 会直接报错
"没找到写特征值 FFF2，这个设备可能不是宠物模块"，可以用来排除连错设备。

## 五、指令对照表

| 按钮 | 发送字节 | ASCII | 十六进制 | 说明 |
| --- | --- | --- | --- | --- |
| 放松趴下 | `)` | 0x29 | 29 | 四肢放松贴地，表情=睡觉 |
| 蹲下 | `0` | 0x30 | 30 | 压低身体 |
| 直立 | `1` | 0x31 | 31 | 站立，**同时停止一切移动** |
| 趴下 | `2` | 0x32 | 32 | 身体贴地 |
| 前进 | `3` | 0x33 | 33 | **持续前进** |
| 后退 | `4` | 0x34 | 34 | **持续后退** |
| 左转 | `5` | 0x35 | 35 | **持续左转** |
| 右转 | `6` | 0x36 | 36 | **持续右转** |
| 摇摆 | `7` | 0x37 | 37 | 左右摇摆 |
| 走路加速 | `8` | 0x38 | 38 | 循环切档 550→500→450→400→350→300ms |
| 摇摆加速 | `9` | 0x39 | 39 | 循环切档 9→8→7→6→5→4 |
| 摇尾巴 | `@` | 0x40 | 40 | **开关**，再按一次关闭 |
| 前跳 | `A` | 0x41 | 41 | 向前跳一下 |
| 后跳 | `B` | 0x42 | 42 | 向后跳一下 |
| 打招呼 | `C` | 0x43 | 43 | 挥手 |
| 开灯 | `D` | 0x44 | 44 | 点亮全部 LED |
| 关灯 | `E` | 0x45 | 45 | 熄灭全部 LED |
| 呼吸灯开 | `F` | 0x46 | 46 | 开启呼吸效果 |
| 呼吸灯关 | `G` | 0x47 | 47 | 关闭呼吸效果 |
| 伸懒腰 | `H` | 0x48 | 48 | 前腿伸展 |
| 拉伸 | `I` | 0x49 | 49 | 身体拉伸 |
| 显示电量 | `P` | 0x50 | 50 | **开关**，切换"Power:xx"显示 |

## 六、容易踩的点

- **移动类指令是持续的**：按了"前进"它就一直走，直到你按别的（比如"直立"）。
  这是固件里蓝牙通道 `Sustainedmove=1` 的设计。
- **摇尾巴 / 显示电量 是开关**，按一次开、再按一次关。
- **调速是循环的**，按到最快再按会回到最慢。
- **写入失败**：日志会显示"写入失败（GATT 忙或连接已断）"。BLE 两次写之间需要
  间隔，连点太快可能丢一条，等一下再按。
- 手机和模块的有效距离通常 10 米以内，中间隔墙会掉。

## 七、重新编译

```powershell
cd AndroidApp
powershell -ExecutionPolicy Bypass -File build_apk.ps1
```

脚本不依赖 Gradle / Android Studio，直接用 SDK 里的 `aapt` + `d8` + `apksigner` 打包。
会自动搜索以下工具：

| 用途 | 需要 |
| --- | --- |
| 编译 Java | 带 `javac` 的 JDK 8 |
| 打 dex / 签名 | Java 11 及以上的运行时 |
| 资源 / 对齐 | Android SDK `build-tools` |
| 框架 | Android SDK `platforms/android-35` |

本机自动匹配到的：

- JDK 8：`C:\Program Files\Android\jdk\jdk-8.0.302.8-hotspot\jdk8u302-b08`
- JRE 17：`%LOCALAPPDATA%\Android\jre17\jdk-17.0.20.1+1-jre`
- SDK：`%LOCALAPPDATA%\Android\Sdk`

> `build_apk.ps1` 开头带 UTF-8 BOM。如果编辑后 BOM 丢了，
> PowerShell 5.1 会按 ANSI 解码，中文注释可能把语法搞坏。

## 八、工程结构

```
AndroidApp/
├── AndroidManifest.xml      BLE 权限、uses-feature
├── build_apk.ps1            打包脚本
├── make_icon.ps1            生成应用图标
├── debug.keystore           调试签名（请保留，换掉会导致无法覆盖安装）
├── res/
│   ├── layout/activity_main.xml
│   ├── drawable/            按钮/卡片/状态灯背景、图标
│   └── values/              颜色、字符串
└── src/com/desktoppet/remote/MainActivity.java
```

## 九、改指令表

所有按钮都在 `MainActivity.java` 顶部：

```java
private static final Cmd[][] GROUPS = {
    { new Cmd("放松趴下", ')'), ... },
    ...
};
```

加一个按钮就是在对应数组里加一行 `new Cmd("名字", '指令字符')`，
按钮会自动排版（每行 4 个）。前提是固件里 `Command_IsValid()` 白名单允许这个字节。

如果要换模块，改这两个常量即可：

```java
private static final UUID UUID_WRITE  = UUID.fromString("0000fff2-...");
private static final UUID UUID_NOTIFY = UUID.fromString("0000fff1-...");
```