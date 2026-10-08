# Builds the APK without Gradle, using only the Android SDK tools
# (aapt2, javac, d8, zipalign, apksigner). The result is a ~30 kB APK, small enough to be
# hosted on the tester's flash for the app self-update.
#
#   pwsh app_android\build.ps1 -VersionCode 6 -VersionName 1.5
#
# Output: app_android\build\LiFePO4_Tester.apk  (also copied to the repository root)
#
# Optional local files (ignored by Git):
#   tester.properties    defaultAddress=192.168.1.50      address opened on a fresh install
#   keystore.properties  storePassword=... keyPassword=... [keyAlias=...]  for app\lifepo4tester.jks
param(
    [string]$Sdk = "$env:LOCALAPPDATA\Android\Sdk",
    [string]$Jdk = "C:\Program Files\Android\Android Studio\jbr",
    [string]$VersionName = "1.5",
    [int]$VersionCode = 6
)
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$bt   = (Get-ChildItem "$Sdk\build-tools" -Directory | Sort-Object Name -Descending | Select-Object -First 1).FullName
$jar  = (Get-ChildItem "$Sdk\platforms" -Directory | Sort-Object Name -Descending | Select-Object -First 1).FullName + "\android.jar"
$out  = "$here\build"
$env:JAVA_HOME = $Jdk
$env:PATH = "$Jdk\bin;$env:PATH"

function Run([string]$exe, [string[]]$a) {
    & $exe @a
    if ($LASTEXITCODE -ne 0) { throw "$([IO.Path]::GetFileName($exe)) failed (exit $LASTEXITCODE)" }
}
function Props([string]$path) {
    $h = @{}
    if (Test-Path $path) {
        foreach ($l in Get-Content $path) {
            if ($l -match '^\s*([^#=\s]+)\s*=\s*(.*)$') { $h[$Matches[1]] = $Matches[2].Trim() }
        }
    }
    return $h
}

if (Test-Path $out) { Remove-Item $out -Recurse -Force -Confirm:$false }
$genPkg = "$out\gen\com\lifepo4tester\app"
New-Item -ItemType Directory -Force $genPkg, "$out\classes", "$out\dex" | Out-Null

# 0. build-time defaults -> Defaults.java (keeps the LAN address out of the repository)
$tp = Props "$here\tester.properties"
$addr = if ($tp.ContainsKey('defaultAddress') -and $tp['defaultAddress']) { $tp['defaultAddress'] } else { '192.168.4.1' }
if ($addr -notmatch '^[A-Za-z0-9\.\-:\[\]]+$') { throw "defaultAddress non valido: $addr" }
Set-Content -Path "$genPkg\Defaults.java" -Encoding ASCII -Value @"
package com.lifepo4tester.app;
final class Defaults {
    static final String ADDRESS = "$addr";
    private Defaults() { }
}
"@

# 1. resources + manifest -> code-less apk, and R.java
Run "$bt\aapt2.exe" @('compile', '--dir', "$here\res", '-o', "$out\res.zip")
Run "$bt\aapt2.exe" @('link', '-o', "$out\app-unsigned.apk", '-I', $jar, '--manifest', "$here\AndroidManifest.xml",
    '--java', "$out\gen", '--min-sdk-version', '26', '--target-sdk-version', '34',
    '--version-code', "$VersionCode", '--version-name', $VersionName, "$out\res.zip")

# 2. Java -> class -> dex
$sources = @(Get-ChildItem "$here\src", "$out\gen" -Recurse -Filter *.java | ForEach-Object FullName)
# (android.jar goes on the classpath, not the bootclasspath: its LambdaMetafactory is a stub and javac would not compile lambdas)
Run "$Jdk\bin\javac.exe" (@('-nowarn', '-Xlint:-options', '--release', '8', '-classpath', $jar, '-d', "$out\classes") + $sources)
$classes = @(Get-ChildItem "$out\classes" -Recurse -Filter *.class | ForEach-Object FullName)
Run "$bt\d8.bat" (@('--release', '--lib', $jar, '--min-api', '26', '--output', "$out\dex") + $classes)

# 3. classes.dex into the apk (aapt works with relative paths)
Push-Location "$out\dex"
try { Run "$bt\aapt.exe" @('add', "$out\app-unsigned.apk", 'classes.dex') } finally { Pop-Location }

# 4. align and sign. The same key must always be used, or Android refuses to update an installed app.
$kp = Props "$here\keystore.properties"
$ks = @("$here\app\lifepo4tester.jks", "$here\lifepo4tester.jks") | Where-Object { Test-Path $_ } | Select-Object -First 1
if ($ks -and $kp.ContainsKey('storePassword')) {
    $alias = if ($kp.ContainsKey('keyAlias')) { $kp['keyAlias'] } else { 'lifepo4' }
    $sp = $kp['storePassword']; $kpw = if ($kp.ContainsKey('keyPassword')) { $kp['keyPassword'] } else { $sp }
} else {
    # no personal keystore: create a throw-away local one (fine for a first install on your own phones)
    $ks = "$here\lifepo4tester.jks"; $alias = 'lifepo4'; $sp = 'android'; $kpw = 'android'
    if (-not (Test-Path $ks)) {
        Run "$Jdk\bin\keytool.exe" @('-genkeypair', '-keystore', $ks, '-alias', $alias, '-keyalg', 'RSA', '-keysize', '2048',
            '-validity', '10000', '-storepass', $sp, '-keypass', $kpw, '-dname', 'CN=LiFePO4 Tester')
    }
}
Run "$bt\zipalign.exe" @('-f', '-p', '4', "$out\app-unsigned.apk", "$out\app-aligned.apk")
Run "$bt\apksigner.bat" @('sign', '--ks', $ks, '--ks-key-alias', $alias, '--ks-pass', "pass:$sp", '--key-pass', "pass:$kpw",
    '--out', "$out\LiFePO4_Tester.apk", "$out\app-aligned.apk")
Run "$bt\apksigner.bat" @('verify', "$out\LiFePO4_Tester.apk")
Copy-Item "$out\LiFePO4_Tester.apk" (Join-Path (Split-Path $here -Parent) 'LiFePO4_Tester.apk') -Force

Write-Host "versionCode $VersionCode ($VersionName), default address $addr"
Get-Item "$out\LiFePO4_Tester.apk" | Select-Object FullName, Length
Write-Host "Publish for self-update:  curl -F `"apk=@app_android/build/LiFePO4_Tester.apk`" `"http://<tester-ip>/api/app/upload?version=$VersionCode`""
