# Android app – LiFePO4 Tester

> 🇮🇹 Versione italiana: [README.it.md](README.it.md)

A tiny native Android app (plain Java, ~25 kB) that shows the web app served by the ESP32 full screen and finds the tester by itself. It is optional: the tester works from any browser.

What it adds over a browser tab:

* **Finds the tester**: at start it tries, in parallel, the last working address, the build-time default address, `lifepo4tester.local`, the tester's access point (192.168.4.1) and mDNS discovery. The first one that answers `/api/status` is opened.
* **Picks the right network**: every address is tried on Android's default network (home Wi-Fi, or mobile data + a VPN such as Tailscale when you are away) and also forced onto Wi-Fi, because the tester's own access point has no internet and Android would otherwise route around it.
* **Updates itself**: the APK is hosted on the tester. When the tester holds a newer `versionCode`, the app offers to download and install it, no cable needed.
* **Saves CSV logs** to the phone's Download folder.
* Back button: page history, then a small menu (*Ricarica* = reload, *Cambia indirizzo* = change address, *Esci* = exit).

## Building

No Gradle: the script uses only the Android SDK tools (aapt2, javac, d8, zipalign, apksigner). You need the Android SDK and a JDK (Android Studio provides both).

```bash
pwsh app_android/build.ps1 -VersionCode 7 -VersionName 1.5.1
```

Output: `app_android/build/LiFePO4_Tester.apk`, also copied to the repository root. Increase `VersionCode` at every release: it is what the self-update compares.

Optional local files, both ignored by Git:

| File | Content | Effect |
|------|---------|--------|
| `app_android/tester.properties` | `defaultAddress=192.168.1.50` | address tried on a fresh install (default: 192.168.4.1) |
| `app_android/keystore.properties` | `storePassword=...`, `keyPassword=...`, optional `keyAlias=...` | signs with your keystore `app_android/app/lifepo4tester.jks` (or `app_android/lifepo4tester.jks`) |

Without a keystore the script creates a local one. Keep the keystore: Android only updates an installed app if the new APK is signed with the same key.

## First installation

Copy the APK to the phone and open it, or with USB debugging enabled:

```bash
adb install -r LiFePO4_Tester.apk
```

Android asks to allow installs from this source; Play Protect may ask for confirmation (*More details → Install anyway*). On Samsung phones *Auto Blocker* (Settings → Security and privacy) blocks both sideloading and USB debugging until it is switched off.

## Publishing an update (self-update)

Upload the new APK to the tester (refused while a test is running):

```bash
curl -F "apk=@app_android/build/LiFePO4_Tester.apk" "http://<tester-ip>/api/app/upload?version=7"
```

At the next start every phone compares its own `versionCode` with `GET /api/app` and, if the tester's is higher, shows *Aggiornamento disponibile* (update available). The first time Android asks to allow this app to install apps (*Settings → allow from this source*); after that it is one tap.

The web UI itself lives in the firmware, so it changes with every firmware update and needs no app release.

## Technical notes

* Package `com.lifepo4tester.app`, minSdk 26, plain `Activity` + `WebView`, no libraries.
* Permissions: INTERNET, ACCESS/CHANGE_NETWORK_STATE (network selection), REQUEST_INSTALL_PACKAGES (self-update).
* `usesCleartextTraffic="true"` because the tester speaks plain HTTP on the local network.
* A `WebChromeClient` is set on the WebView: without it JavaScript `confirm()` / `prompt()` are ignored and the start/stop and rename buttons would silently do nothing.
* An always-on VPN must be switched off only when the phone is connected directly to the tester's access point; on the home network and away from home (with a subnet route to the tester) it is what makes the app work.
