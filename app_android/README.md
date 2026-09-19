# App Android – LiFePO4 Tester

App Android nativa (Kotlin) che mostra a schermo intero la web-app servita dall'ESP32, con icona propria,
senza barra del browser, e ricorda l'indirizzo del tester (192.168.4.1 oppure l'IP di casa).

**L'APK già compilato e firmato è nella cartella principale: `LiFePO4_Tester.apk`** (circa 2,6 MB, Android 8.0 o superiore).

## Installare l'APK sul Galaxy A16

1. Copia `LiFePO4_Tester.apk` sul telefono (cavo USB, Quick Share, Google Drive, o inviatelo via e-mail/WhatsApp).
2. Sul telefono apri il file con "Archivio" (File personali). Alla prima volta Android chiede di **consentire l'installazione da questa origine**: attiva e conferma.
3. Se compare "App bloccata da Play Protect" (succede con le app non pubblicate sullo store) tocca **Maggiori dettagli → Installa comunque**.
4. Apri l'app: cerca di collegarsi a `http://192.168.4.1/`. Collega prima il telefono al Wi-Fi **LiFePO4-Tester** (password `lifepo4test`).

Con il telefono collegato al PC via USB e "Debug USB" attivo, in alternativa:
```bash
adb install -r LiFePO4_Tester.apk
```

## Uso
* Menu ⋮ → **Indirizzo tester…** per inserire l'IP di casa dell'ESP32 (quello mostrato nella pagina Impostazioni del tester).
* Menu ⋮ → **Ricarica** se la pagina non risponde.
* Se il tester non è raggiungibile compare un messaggio: collegati alla rete Wi-Fi `LiFePO4-Tester` e tocca lo schermo per riprovare.
* L'app forza il proprio traffico sulla rete Wi-Fi (dalla versione 1.1), quindi funziona anche se Android tiene i dati mobili come rete predefinita.
* **VPN (es. Tailscale) e access point del tester**: quando il telefono è collegato direttamente alla rete `LiFePO4-Tester` (192.168.4.1), una VPN sempre attiva cattura il traffico e la pagina resta bianca: lì va spenta. Con l'ESP32 sulla rete di casa il problema **non c'è** (verificato con Tailscale acceso).

## Ricompilare (se modifichi qualcosa)

Il progetto è completo: si apre direttamente in **Android Studio** (*File → Open* → cartella `app_android`), poi *Build → Generate App Bundles or APKs → Generate APKs*.

Da riga di comando, con Android Studio installato (usa il suo Java):
```bash
set JAVA_HOME=C:\Program Files\Android\Android Studio\jbr
gradle --no-daemon assembleRelease
```
L'APK esce in `app/build/outputs/apk/release/app-release.apk`.

Versione app: 1.2 (versionCode 3). Versioni usate: Gradle 8.11.1, Android Gradle Plugin 8.9.2, Kotlin 2.1.20, compileSdk 36, minSdk 26.

## Firma
L'APK si firma con un keystore personale `app/lifepo4tester.jks` (alias `lifepo4`), che **non è incluso nel repository**. Per crearne uno: `keytool -genkeypair -keystore app/lifepo4tester.jks -alias lifepo4 -keyalg RSA -keysize 2048 -validity 10000`, poi adeguare le password in `app/build.gradle.kts`.
Serve per installare aggiornamenti sopra la versione già presente sul telefono: se la perdi, dovrai disinstallare l'app prima di installare una nuova build. È una chiave per uso personale, non per il Play Store.

## Note tecniche
* `usesCleartextTraffic="true"` e `network_security_config.xml` servono perché il tester parla HTTP in chiaro sulla rete locale.
* Unico permesso richiesto: INTERNET.
* `local.properties` contiene il percorso dell'SDK su questo PC; Android Studio lo rigenera da solo su un altro PC.
