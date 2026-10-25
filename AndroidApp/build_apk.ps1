# ============================================================
#  桌宠遥控 —— 免 Gradle 打包脚本
#  用法：  powershell -ExecutionPolicy Bypass -File build_apk.ps1
#  输出：  DeskPetRemote.apk
# ============================================================
$ErrorActionPreference = "Stop"
$here = if ($PSScriptRoot) { $PSScriptRoot } else { (Get-Location).Path }

# ---------- 1. 定位工具链 ----------
$sdk = if ($env:ANDROID_HOME) { $env:ANDROID_HOME }
       elseif ($env:ANDROID_SDK_ROOT) { $env:ANDROID_SDK_ROOT }
       else { Join-Path $env:LOCALAPPDATA "Android\Sdk" }
if (-not (Test-Path $sdk)) { throw "找不到 Android SDK：$sdk" }

$jdkCandidates = @(
    "C:\Program Files\Android\jdk\jdk-8.0.302.8-hotspot\jdk8u302-b08",
    (Join-Path $env:ProgramFiles "Android\Android Studio\jbr"),
    $env:JAVA_HOME
)
$jdk = $null
foreach ($c in $jdkCandidates) {
    if ($c -and (Test-Path (Join-Path $c "bin\javac.exe"))) { $jdk = $c; break }
}
if (-not $jdk) { throw "找不到带 javac 的 JDK，请在脚本里手改 `$jdkCandidates" }

$bt = Join-Path $sdk "build-tools\34.0.0"
if (-not (Test-Path $bt)) {
    $bt = Get-ChildItem (Join-Path $sdk "build-tools") -Directory |
          Sort-Object Name -Descending | Select-Object -First 1 -ExpandProperty FullName
}
$stableJar = Join-Path $sdk "platforms\android-35\android.jar"
if (Test-Path $stableJar) {
    $androidJar = $stableJar
} else {
    $androidJar = Get-ChildItem (Join-Path $sdk "platforms") -Directory |
                  Sort-Object Name -Descending |
                  ForEach-Object { Join-Path $_.FullName "android.jar" } |
                  Where-Object { Test-Path $_ } | Select-Object -First 1
}
if (-not $androidJar) { throw "找不到 android.jar" }

$env:JAVA_HOME = $jdk
$env:PATH = "$jdk\bin;$env:PATH"

# d8 / apksigner 需要 Java 11+，而 javac 用 JDK 8 就够。
# 这里另找一个高版本 Java 运行时专门跑 d8 和签名。
function Get-JavaMajor([string]$exe) {
    # 走 cmd /c 把 stderr 合进 stdout，避免 PS 把 "java -version" 的输出当成致命错误
    $txt = (& cmd /c "`"$exe`" -version 2>&1" | Out-String)
    if ($txt -match 'version "(\d+)') { return [int]$Matches[1] }
    return 0
}
$runHome = $jdk
$probe = @(
    (Join-Path $env:LOCALAPPDATA "Android\jre17"),
    (Join-Path $env:ProgramFiles "Android\Android Studio\jbr"),
    $env:JAVA_HOME
)
foreach ($p in $probe) {
    if (-not $p -or -not (Test-Path $p)) { continue }
    $cand = Get-ChildItem $p -Recurse -Filter "java.exe" -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -like "*\bin\java.exe" } | Select-Object -First 1
    if (-not $cand) { continue }
    if ((Get-JavaMajor $cand.FullName) -ge 11) {
        $runHome = Split-Path (Split-Path $cand.FullName -Parent) -Parent
        break
    }
}

Write-Host "JDK      : $jdk"
Write-Host "运行时   : $runHome"
Write-Host "SDK      : $sdk"
Write-Host "build-tools: $bt"
Write-Host "android.jar: $androidJar"
Write-Host ""

# ---------- 2. 准备目录 ----------
$build  = Join-Path $here "build"
$gen    = Join-Path $build "gen"
$cls    = Join-Path $build "classes"
$dexDir = Join-Path $build "dex"
if (Test-Path $build) { Remove-Item $build -Recurse -Force }
foreach ($d in @($gen, $cls, $dexDir)) { New-Item -ItemType Directory -Path $d -Force | Out-Null }

$manifest = Join-Path $here "AndroidManifest.xml"
$resDir   = Join-Path $here "res"
$srcDir   = Join-Path $here "src"

# ---------- 3. aapt：编译资源 + 生成 R.java + 打未签名 APK ----------
Write-Host "[1/6] aapt 打包资源 ..."
$unsigned = Join-Path $build "app-unsigned.apk"
& (Join-Path $bt "aapt.exe") package -f -m `
    -J $gen -M $manifest -S $resDir -I $androidJar -F $unsigned
if ($LASTEXITCODE -ne 0) { throw "aapt 失败（退出码 $LASTEXITCODE）" }

# ---------- 4. javac：编译 Java ----------
Write-Host "[2/6] javac 编译 Java 源码 ..."
$srcs = @()
$srcs += Get-ChildItem $srcDir -Recurse -Filter *.java | ForEach-Object { $_.FullName }
$srcs += Get-ChildItem $gen -Recurse -Filter *.java | ForEach-Object { $_.FullName }
Write-Host ("      共 {0} 个源文件" -f $srcs.Count)
& (Join-Path $jdk "bin\javac.exe") -encoding UTF-8 -source 8 -target 8 `
    -bootclasspath $androidJar -classpath $androidJar -d $cls $srcs
if ($LASTEXITCODE -ne 0) { throw "javac 失败（退出码 $LASTEXITCODE）" }

# ---------- 5. d8：生成 classes.dex ----------
Write-Host "[3/6] d8 生成 classes.dex ..."
# 切到 Java 11+ 运行时（d8 的 class 版本是 55）
$env:JAVA_HOME = $runHome
$env:PATH = "$runHome\bin;$env:PATH"
$classFiles = Get-ChildItem $cls -Recurse -Filter *.class | ForEach-Object { $_.FullName }
& (Join-Path $bt "d8.bat") --min-api 21 --lib $androidJar --output $dexDir $classFiles
if ($LASTEXITCODE -ne 0) { throw "d8 失败（退出码 $LASTEXITCODE）" }
$dexFile = Join-Path $dexDir "classes.dex"
if (-not (Test-Path $dexFile)) { throw "d8 没有产出 classes.dex" }

# ---------- 6. 把 dex 塞进 APK ----------
Write-Host "[4/6] 写入 classes.dex ..."
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [System.IO.Compression.ZipFile]::Open($unsigned, 'Update')
$old = $zip.GetEntry("classes.dex")
if ($old) { $old.Delete() }
[System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
    $zip, $dexFile, "classes.dex",
    [System.IO.Compression.CompressionLevel]::Optimal) | Out-Null
$zip.Dispose()

# ---------- 7. zipalign ----------
Write-Host "[5/6] zipalign 对齐 ..."
$aligned = Join-Path $build "app-aligned.apk"
& (Join-Path $bt "zipalign.exe") -f -p 4 $unsigned $aligned
if ($LASTEXITCODE -ne 0) { throw "zipalign 失败" }

# ---------- 8. 签名 ----------
Write-Host "[6/6] 签名 ..."
$ks = Join-Path $here "debug.keystore"
if (-not (Test-Path $ks)) {
    Write-Host "      生成调试签名证书 ..."
    & (Join-Path $jdk "bin\keytool.exe") -genkeypair -v `
        -keystore $ks -storepass android -keypass android `
        -alias androiddebugkey -keyalg RSA -keysize 2048 -validity 10000 `
        -dname "CN=DeskPetDebug, O=Android, C=CN"
    if ($LASTEXITCODE -ne 0) { throw "keytool 失败" }
}

$outApk = Join-Path $here "DeskPetRemote.apk"
if (Test-Path $outApk) { Remove-Item $outApk -Force }
& (Join-Path $bt "apksigner.bat") sign `
    --ks $ks --ks-pass pass:android --key-pass pass:android `
    --out $outApk $aligned
if ($LASTEXITCODE -ne 0) { throw "apksigner 失败" }

# ---------- 9. 校验 ----------
& (Join-Path $bt "apksigner.bat") verify $outApk
if ($LASTEXITCODE -ne 0) { throw "签名校验未通过" }

$size = (Get-Item $outApk).Length
Write-Host ""
Write-Host ("完成： {0}" -f $outApk)
Write-Host ("大小： {0:N1} KB" -f ($size / 1KB))