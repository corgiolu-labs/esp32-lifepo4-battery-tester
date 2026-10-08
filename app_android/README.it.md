> 🇬🇧 English version: see the file with the same name without `.it`.

# App Android – LiFePO4 Tester

Piccola app Android nativa (Java puro, ~25 kB) che mostra a schermo intero la web-app servita dall'ESP32 e trova il tester da sola. È facoltativa: il tester funziona da qualsiasi browser.

Cosa aggiunge rispetto al browser:

* **Trova il tester**: all'avvio prova in parallelo l'ultimo indirizzo funzionante, l'indirizzo di default scelto alla compilazione, `lifepo4tester.local`, l'access point del tester (192.168.4.1) e la ricerca mDNS. Apre il primo che risponde a `/api/status`.
* **Sceglie la rete giusta**: ogni indirizzo viene provato sulla rete predefinita di Android (Wi-Fi di casa, oppure dati mobili + VPN come Tailscale quando sei fuori) e anche forzando il Wi-Fi, perché l'access point del tester non ha internet e Android altrimenti lo scavalca.
* **Si aggiorna da sola**: l'APK è ospitato sul tester. Quando il tester ha un `versionCode` più alto, l'app propone di scaricarlo e installarlo, senza cavo.
* **Salva i CSV** nella cartella Download del telefono.
* Tasto Indietro: storia della pagina, poi un piccolo menu (*Ricarica*, *Cambia indirizzo*, *Esci*).

## Compilazione

Niente Gradle: lo script usa solo gli strumenti dell'SDK Android (aapt2, javac, d8, zipalign, apksigner). Servono l'SDK Android e un JDK (Android Studio li porta entrambi).

```bash
pwsh app_android/build.ps1 -VersionCode 7 -VersionName 1.5.1
```

Risultato: `app_android/build/LiFePO4_Tester.apk`, copiato anche nella cartella principale. Aumenta `VersionCode` a ogni rilascio: è il numero che l'auto-aggiornamento confronta.

File locali facoltativi, entrambi esclusi da Git:

| File | Contenuto | Effetto |
|------|-----------|---------|
| `app_android/tester.properties` | `defaultAddress=192.168.1.50` | indirizzo provato alla prima installazione (default: 192.168.4.1) |
| `app_android/keystore.properties` | `storePassword=...`, `keyPassword=...`, facoltativo `keyAlias=...` | firma con il tuo keystore `app_android/app/lifepo4tester.jks` (oppure `app_android/lifepo4tester.jks`) |

Senza keystore lo script ne crea uno locale. Conserva il keystore: Android aggiorna un'app installata solo se il nuovo APK è firmato con la stessa chiave.

## Prima installazione

Copia l'APK sul telefono e aprilo, oppure con il debug USB attivo:

```bash
adb install -r LiFePO4_Tester.apk
```

Android chiede di consentire l'installazione da questa origine; Play Protect può chiedere conferma (*Maggiori dettagli → Installa comunque*). Sui Samsung il *Blocco automatico* (Impostazioni → Sicurezza e privacy) blocca sia l'installazione sia il debug USB finché non lo disattivi.

## Pubblicare un aggiornamento (auto-aggiornamento)

Carica il nuovo APK sul tester (rifiutato se c'è un test in corso):

```bash
curl -F "apk=@app_android/build/LiFePO4_Tester.apk" "http://<IP-del-tester>/api/app/upload?version=7"
```

Al prossimo avvio ogni telefono confronta il proprio `versionCode` con `GET /api/app` e, se quello del tester è più alto, mostra *Aggiornamento disponibile*. La prima volta Android chiede di autorizzare questa app a installare app (*Impostazioni → consenti da questa origine*); da lì in poi basta un tocco.

La web-app vive nel firmware, quindi cambia a ogni aggiornamento del firmware senza bisogno di rilasciare l'app.

## Note tecniche

* Pacchetto `com.lifepo4tester.app`, minSdk 26, `Activity` + `WebView` semplici, nessuna libreria.
* Permessi: INTERNET, ACCESS/CHANGE_NETWORK_STATE (scelta della rete), REQUEST_INSTALL_PACKAGES (auto-aggiornamento).
* `usesCleartextTraffic="true"` perché il tester parla HTTP in chiaro sulla rete locale.
* Sulla WebView è impostato un `WebChromeClient`: senza, `confirm()` / `prompt()` di JavaScript vengono ignorati e i pulsanti Avvia/Ferma e la matita «rinomina» non farebbero nulla.
* Una VPN sempre attiva va spenta solo quando il telefono è collegato direttamente all'access point del tester; sulla rete di casa e fuori casa (con la rotta verso il tester) è proprio ciò che fa funzionare l'app.
