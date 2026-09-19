# LiFePO4 Tester 24V 100Ah – ESP32 D1 mini

Tester per batterie LiFePO4 24 V (8S) 100 Ah con **ESP32 D1 mini**, **partitore di tensione** e **shunt FL-P 100 A / 75 mV** letto da un **ADS1115** (16 bit, I2C; in alternativa un INA226).
Il tester crea una rete Wi-Fi e serve una web-app che si apre dal Galaxy A16 (o da qualsiasi telefono/PC) senza installare nulla.

```
LiFePO4_Tester_ESP32/
├── README.md                 ← questa guida
├── firmware/
│   ├── platformio.ini        ← progetto PlatformIO (VS Code)
│   └── LiFePO4_Tester/
│       ├── LiFePO4_Tester.ino  ← firmware (apribile anche con Arduino IDE)
│       ├── config.h            ← pin, valori shunt/partitore, Wi-Fi
│       ├── web_ui.h            ← web-app incorporata (HTML/JS in italiano)
│       └── web_icons.h         ← icone PNG dell'app (generate)
├── app_android/              ← app Android nativa opzionale (wrapper Kotlin)
├── docs/
│   ├── HARDWARE.md           ← lista componenti, pin, dettagli elettrici, sicurezza
│   ├── schema_cablaggio.svg  ← schema di cablaggio (ADS1115)
│   └── schema_cablaggio_ina226.svg ← variante con INA226
└── tools/
    ├── make_icons.py         ← rigenera le icone
    ├── confronta_csv.py      ← grafico PNG + riepilogo dai CSV dei test (PC)
    └── mock_server.py        ← simulatore per provare la web-app sul PC senza ESP32
```

## Cosa fa

* Misura **tensione** (partitore su ADC ESP32, oppure lo stesso partitore letto dall'ADS1115 a 16 bit), **corrente** (shunt + ADS1115), **potenza**, **temperatura** (NTC opzionale).
* **Coulomb counting**: Ah e Wh scaricati/caricati, stima **SOC** con correzione da tensione a riposo (OCV LiFePO4).
* **Test di capacità**: si avvia dall'app, integra gli Ah erogati, si ferma da solo al **cutoff** (default 20,0 V = 2,5 V/cella) e, se c'è il relè, stacca il carico. Salva l'ultimo risultato (Ah, Wh, durata, R interna, % della nominale) e un **log CSV** scaricabile.
* **Resistenza interna DC** stimata dai gradini di carico.
* **Allarmi**: tensione bassa/alta, sovracorrente, temperatura, shunt fuori scala, sensore I2C assente.
* **Calibrazione dall'app** contro multimetro/pinza, valori salvati in flash.
* Nessuna libreria esterna: compila con il solo core ESP32.

## 1. Hardware

Segui [docs/HARDWARE.md](docs/HARDWARE.md) e [docs/schema_cablaggio.svg](docs/schema_cablaggio.svg). In sintesi:

| Segnale | Collegamento |
|---------|--------------|
| Tensione | B+ → R1 120 kΩ → nodo → R2 10 kΩ → GND, 100 nF sul nodo. Il nodo va a **GPIO34** e anche ad **A2** dell'ADS1115 |
| Corrente | shunt sul **negativo**; viti sense → **A0 (lato carico)** / **A3 (lato batteria)** dell'ADS1115 (su questo modulo A1 è guasto: coppia scelta con `ADS_MUX_SHUNT` in `config.h`; il cablaggio originale era A0/A1); ADS1115 SDA→**GPIO21**, SCL→**GPIO22**, VDD→3V3, GND→GND, ADDR→GND |
| Massa | GND del tester sul **B− lato batteria** (prima dello shunt) |
| Alimentazione | **power bank USB** nella porta USB dell'ESP32 (più semplice, ~30–40 h con 10.000 mAh), oppure buck **XL4015 / LM2596** 24→5 V sul pin 5V, regolato a 5,0 V prima di collegarlo (non MP1584: max 28 V). Mai tutti e due insieme |
| Relè (opz.) | modulo relè **3 V**: VCC→**3V3**, GND→GND, IN→**GPIO26**; contatti COM/NO in serie al B+ del carico. Da solo fino a ~5 A, oltre pilota un relè automotive 40 A o un contattore |
| NTC (opz.) | 3V3 → 10 kΩ → **GPIO35** → NTC 10 kΩ → GND |

Il firmware è impostato per l'**ADS1115** (`CURRENT_SENSOR SENSOR_ADS1115` in `config.h`). Se invece usi un INA226: cambia il define, collega IN+ lato carico / IN− lato batteria, VBS a B+, e **rimuovi la resistenza R100** saldata sul modulo. Schema INA226: [docs/schema_cablaggio_ina226.svg](docs/schema_cablaggio_ina226.svg).

## 2. Caricare il firmware

### Con PlatformIO (consigliato)
1. Installa VS Code + estensione PlatformIO.
2. *File → Open Folder* → `firmware/`.
3. Collega l'ESP32 via USB e premi **Upload** (freccia in basso). La board è già impostata (`wemos_d1_mini32`).
4. *Serial Monitor* a 115200 baud per vedere i log.

Da terminale, dentro `firmware/`:
```bash
pio run -t upload && pio device monitor
```

### Con Arduino IDE
1. Installa il core **esp32 by Espressif** dal Board Manager (versione 2.x o 3.x).
2. Apri `firmware/LiFePO4_Tester/LiFePO4_Tester.ino`.
3. Scheda: **WEMOS D1 MINI ESP32**; Partition Scheme: *Default 4MB with spiffs*; Upload Speed 921600 (o 460800 se dà errore).
4. Carica. Non servono librerie aggiuntive.

Il filesystem LittleFS per i log viene formattato da solo al primo avvio: non serve nessun "upload filesystem".

## 3. Primo avvio e app sul Galaxy A16

1. Accendi il tester. Sul telefono: *Impostazioni → Connessioni → Wi-Fi* → rete **`LiFePO4-Tester`**, password **`lifepo4test`**.
2. Samsung avviserà "Internet potrebbe non essere disponibile": scegli **Mantieni connessione** (il tester risponde ai controlli di Android, quindi di solito non compare nemmeno).
3. Apri Chrome e vai su **http://192.168.4.1**.
4. Menu **⋮ → Aggiungi a schermata Home**: compare l'icona *LiFePO4 Tester* come un'app. Aprendola da lì si vede il tester a schermo intero.

Suggerimenti Samsung:
* Se il telefono si stacca dall'access point per tornare ai dati mobili, in *Wi-Fi → ⋮ → Impostazioni avanzate* disattiva **Passa a dati mobili** / **Cambia rete automaticamente** finché usi il tester.
* Se preferisci tenere internet sul telefono: nell'app *Impostazioni → Wi-Fi di casa* inserisci SSID e password della tua rete. L'ESP32 si collega anche lì, l'IP appare nella pagina Impostazioni (e sul monitor seriale) e lo apri con il telefono sulla rete di casa. Aggiungi anche quello alla schermata Home.
* L'app funziona anche su PC: http://192.168.4.1 o http://lifepo4tester.local (mDNS, sul PC e iPhone; Android non lo risolve).
* La rete di casa si può impostare anche dal **monitor seriale** (115200 baud) con il comando `wifi NomeRete|Password` (altri comandi: `wifi off`, `ip`, `relay 0/1`, `reboot`). L'IP assegnato dal router può cambiare nel tempo: se l'app non lo trova più, leggilo nella pagina Impostazioni via access point oppure con il comando `ip` sul seriale, o assegna un IP fisso all'ESP32 dal router.
* Conviene riservare l'indirizzo dell'ESP32 nel router (prenotazione DHCP / associazione IP-MAC), così non cambia nel tempo. I dati della propria installazione (rete, IP, MAC) si possono annotare in `NOTE_LOCALI.md`, che è escluso dal repository.
* Comandi diagnostici dal monitor seriale: `i2c` (scansione del bus, l'ADS1115 deve comparire a 0x48), `ads` (tensione su ogni ingresso A0–A3 e differenziale dello shunt: con lo shunt collegato e riferito a massa A0 e A1 devono leggere entrambi ~0 mV), `adc` (millivolt grezzi su GPIO34/35; ~142 mV è il minimo dell'ADC e corrisponde a 0 V).

### Accesso da fuori casa (Tailscale)
L'ESP32 non può eseguire Tailscale, quindi fa da ponte un computer sempre acceso sulla stessa rete (es. un Raspberry Pi con Tailscale): annuncia a Tailscale la rotta verso il solo tester (`<IP-del-tester>/32`, da approvare nella console Tailscale). Da qualsiasi dispositivo con Tailscale acceso si usa **lo stesso indirizzo di casa**: app, collegamenti e API non cambiano.

* Sul Raspberry: `sudo tailscale set --advertise-routes=<IP-del-tester>/32`; inoltro IPv4 permanente con `net.ipv4.ip_forward=1` in `/etc/sysctl.d/99-tailscale.conf`.
* Effetto collaterale: un dispositivo con Tailscale acceso passa dal Raspberry **anche quando è in casa**, perché la rotta /32 è più specifica di quella della rete locale. Se il Raspberry è spento e il tester non risponde, basta spegnere Tailscale su quel dispositivo per tornare al collegamento diretto.
* Sicurezza: il tester **non ha password** (chi lo raggiunge può avviare o fermare test e caricare firmware). Non aprire mai porte sul router verso di lui; tutti i dispositivi della rete Tailscale, compresi quelli di altri utenti, ora lo raggiungono: se serve, limitare con le regole di accesso (ACL) di Tailscale.

Vuoi un'APK vera con icona e senza barra del browser? Vedi [app_android/README.md](app_android/README.md) (serve Android Studio).

## 4. Calibrazione (5 minuti)

Nell'app, scheda **Impostazioni**:

1. **Tensione**: leggi la batteria con un multimetro, scrivi il valore in *Tensione reale* e premi *Calibra guadagno tensione*. Con il nodo del partitore collegato ad A2, imposta prima *Sorgente misura = ADS1115 canale A2* (più preciso), poi calibra.
2. **Corrente – zero**: senza nessun carico/caricabatterie collegato (0 A nello shunt) premi *Azzera con corrente = 0*.
3. **Corrente – guadagno**: con una corrente stabile di almeno qualche ampere (carico di test o caricabatterie), misurala con pinza o multimetro, aspetta che nell'app il valore *media 10 s* sia fermo, scrivi la corrente in *Corrente reale* e premi *Calibra guadagno corrente*. La taratura usa la media di ~10 s, non la lettura istantanea (un caricabatterie ha ondulazione: l'istantanea sbaglierebbe anche del 10 %), vale sia in carica sia in scarica e il segno non importa. Con una pinza fai la misura nei due versi e usa la media. Valori attuali (19/09/2026): guadagno 1,035 e offset −0,065 A, ricavati da una pinza a 9,6 A. Se la scarica appare con segno negativo, spunta *Inverti segno* e salva.
4. **SOC**: dopo una carica completa premi *SOC = 100 %* nella scheda Monitor.

Le calibrazioni restano in flash anche togliendo alimentazione.

## 5. Test di capacità

1. Carica completamente la batteria e lasciala riposare 30 min.
2. Collega un **carico costante**. Scegli la corrente in base al tempo che hai (capacità 100 Ah, tensione media 25,6 V):

   | C-rate | Corrente | Durata | Potenza | R equivalente | Note |
   |---|---|---|---|---|---|
   | 0,1C | 10 A | ~10 h | 256 W | 2,6 Ω | preciso, lento |
   | **0,2C** | **20 A** | **~5 h** | **512 W** | **1,3 Ω** | **standard dei produttori, consigliato** |
   | 0,3C | 30 A | ~3,3 h | 770 W | 0,85 Ω | buon compromesso |
   | 0,5C | 50 A | ~2 h | 1,3 kW | 0,5 Ω | veloce; capacità letta 2–3 % più bassa, cavi 16 mm² |
   | 1C | 100 A | ~1 h | 2,6 kW | 0,26 Ω | limite dello shunt e di molti BMS, sconsigliato |

   **Carico a immersione con filo nichel-cromo** (1,3 Ω per 20 A): filo Ni-Cr **Ø 2 mm, ~3,7 m** (oppure 4 fili Ø 1 mm da 3,8 m in parallelo) avvolto a spirale larga su un tubo di PVC e immerso in **almeno 50 L** di acqua, meglio se demineralizzata (512 W scaldano 50 L di ~9 °C all'ora; 10 L bollirebbero in 1,5 h). Stai sotto 3 W/cm² di superficie del filo, capi fissati con capicorda a vite fuori dall'acqua, secchio di plastica, stanza aerata (leggera elettrolisi). La corrente si regola spostando il morsetto lungo il filo leggendo il tester. Alternativa senza filo nudo: riscaldatore a immersione **24 V** per camion/barche da 300–600 W.

   Carichi pratici: **inverter 24 V + stufetta** (500 W ≈ 22 A, 1200 W ≈ 52 A; a fine scarica la corrente sale del ~25 % perché l'inverter è a potenza costante), **lampade da camion 24 V** (H4 75 W ≈ 3 A ciascuna, 7 in parallelo ≈ 20 A), resistenze di potenza solo fino a ~10 A (servono 100 W di dissipazione ogni 4 A). Con l'inverter, il modulo relè può staccare la stufetta sul lato 230 V o comandare l'ingresso "remote" dell'inverter, senza relè automotive. Cavi: 20 A → 4 mm², 30 A → 6 mm², 50 A → 16 mm².
3. Scheda **Test**: imposta *cutoff* (20,0 V consigliato), *capacità nominale* (100), *intervallo log* (10 s), spunta *Usa relè* se lo hai montato, premi **Avvia test**.
4. Il tester integra gli Ah, mostra durata, corrente media, stima grezza della capacità finale. Al cutoff (tensione sotto soglia per 3 s) chiude il test, apre il relè e salva il risultato. Senza relè, stacca tu il carico quando vedi "DONE".
5. **Scarica CSV** per aprire il log in Excel (colonne: secondi, V, A, W, Ah, Wh, °C).

Una LiFePO4 nuova da 100 Ah a 0,1–0,2 C deve dare **≥ 95–100 Ah** fino a 2,5 V/cella. Sotto l'80 % è da considerare a fine vita.

### Confrontare più batterie (es. 10 identiche per trovare quelle difettose)

Il difetto tipico "il BMS stacca prima delle altre" è quasi sempre **una cella debole o sbilanciata**: il BMS interviene quando quella cella tocca 2,5 V mentre le altre sono ancora a 3,0–3,2 V, quindi la tensione di pacco al momento dello stacco è 23–25 V invece di 20 V. Il tester lo riconosce da solo:

* quando la tensione crolla a zero da sopra il cutoff, il test si chiude con esito **"BMS ha staccato"** e in *V fine* viene salvata la tensione dell'istante prima del crollo;
* ogni test ha un **nome** (lo inserisci prima di premere Avvia, es. `B01`…`B10`) e la data/ora presa dal telefono;
* la sezione **Storico e confronto batterie** ordina i test per Ah erogati e colora in rosso le batterie il cui BMS ha staccato **in anticipo** (tensione finale oltre 1,5 V sopra il cutoff) oppure con meno del 90 % della migliore. Un esito "BMS a fine" (giallo) è normale: con il cutoff a 20,0 V, cioè sulla soglia del BMS, a fine scarica può intervenire per primo l'uno o l'altro. Ogni riga ha il suo CSV e un cestino 🗑 per eliminare **solo quel test** (con conferma); "Cancella TUTTO lo storico" chiede due conferme.

Procedura consigliata:
1. Carica tutte le batterie con lo **stesso caricabatterie** fino a fine carica, lasciandole 30 min a riposo prima del test.
2. Alimenta il tester con il **power bank**, non dal buck: quando il BMS stacca, la tensione ai morsetti va a zero e un tester alimentato dalla batteria si spegnerebbe perdendo il risultato.
3. Stessa corrente per tutte (20 A), stesso cutoff (20,0 V), stesso nome-schema (`B01`…`B10`).
4. Leggi la tabella: sana = esito OK, ≥ 95 Ah, V fine ≈ 20 V, R interna 10–20 mΩ. Sospetta = esito BMS con V fine 23–25 V, Ah ridotti, R interna più alta.
5. Le sospette: prova una **carica di bilanciamento** (lasciale sul caricabatterie a 29,2 V per 8–12 ore, così il BMS bilancia le celle) e ripeti il test. Se staccano ancora presto con V fine alta, una cella è difettosa: batteria da reclamare in garanzia.
6. Dopo lo stacco del BMS la batteria può restare "spenta" finché non vede il caricabatterie: è normale.

I dati restano nello storico (ultimi 60 test, ultimi 12 CSV) anche togliendo alimentazione.

### Grafico di confronto
Nella scheda **Test**, sotto lo storico, il pulsante **Carica le curve dai CSV** disegna una curva per ogni test di cui il tester ha ancora il CSV. Asse orizzontale: Ah erogati oppure tempo; asse verticale: tensione, corrente, potenza, oppure **calo di tensione ogni 10 minuti**, che trasforma un crollo improvviso della tensione in un picco ben visibile. Le batterie sane arrivano a ~100 Ah e scendono a 20 V; una curva che finisce presto e in alto con il pallino rosso è una batteria il cui BMS ha staccato per una cella debole.

Sul PC, con i CSV scaricati dall'app (o direttamente dal tester se il PC è sul suo Wi-Fi):
```bash
python tools/confronta_csv.py cartella_con_i_csv
python tools/confronta_csv.py --tester 192.168.4.1
```
Produce `confronto.png` (due grafici: V/Ah e V/tempo, tratteggio per le batterie sospette) e `confronto_riepilogo.csv` da aprire in Excel. Richiede `matplotlib` (già installato su questo PC).

## 6. Provare la web-app senza ESP32

```bash
python tools/mock_server.py 8080
```
poi apri http://localhost:8080 : un simulatore risponde alle stesse API del firmware (scarica accelerata per vedere il test finire in pochi minuti).

### Grafico persistente e aggiornamento via Wi-Fi
Il grafico della scheda Monitor non vive nella pagina ma nell'ESP32, che tiene in memoria le ultime **6 ore** (un punto ogni 20 s). Chiudendo e riaprendo l'app, da telefono o da PC, la curva c'è ancora e prosegue in diretta; i pulsanti sotto il grafico scelgono la finestra (10 min, 1 ora, tutto) e una linea tratteggiata segna l'avvio del test. Lo storico si perde solo se l'ESP32 viene spento o riavviato; la curva completa di ogni test resta comunque nel suo CSV.

Il firmware si aggiorna senza cavo (mai durante un test, viene rifiutato):
```bash
pio run && curl -F "firmware=@.pio/build/d1mini_esp32/firmware.bin" http://<IP-del-tester>/api/update
```

## 7. API (per chi vuole integrare)

| Metodo | URL | Parametri | Descrizione |
|--------|-----|-----------|-------------|
| GET | `/api/status` | – | JSON con tutte le misure, stato test, ultimo test, Wi-Fi |
| GET | `/api/config` | – | configurazione corrente |
| POST | `/api/config` | `cap, cutoff, logint, relay_en, inv, vsrc, vhigh, imax, tmax` | salva impostazioni |
| POST | `/api/cal` | `type=vgain\|izero\|igain\|reset`, `actual=` | calibrazione |
| POST | `/api/test` | `cmd=start\|stop\|reset`, `name=`, `ts=` (epoch) | test di capacità |
| POST | `/api/soc` | `value=0..100` | imposta SOC |
| POST | `/api/counters` | `cmd=reset` | azzera Ah/Wh |
| POST | `/api/relay` | `state=0\|1` | relè manuale |
| GET | `/api/log` | `id=` (opz.) | scarica il CSV del test indicato (default: ultimo) |
| POST | `/api/log` | `cmd=clear` | cancella tutti i CSV |
| GET | `/api/history` | – | storico dei test (array JSON: id, name, ts, reason, ah, wh, dur, vstart, vend, iavg, rint) |
| POST | `/api/history` | `cmd=clear` oppure `cmd=delete&id=N` | cancella tutto lo storico, oppure un solo test (riga + CSV) |
| POST | `/api/wifi` | `ssid, pass` | rete di casa (riavvia) |
| POST | `/api/reboot` | – | riavvio |
| GET | `/api/trend` | – | storico per il grafico: un punto ogni 20 s, ultime 6 ore (`v` in mV, `i` in centesimi di A, `age` = secondi dall'ultimo punto, `tstart` = secondi dall'avvio del test o −1) |
| POST | `/api/update` | file `firmware` (multipart) | aggiornamento firmware via Wi-Fi; rifiutato con 409 se c'è un test in corso |

I parametri POST sono `application/x-www-form-urlencoded`. Le risposte hanno `Access-Control-Allow-Origin: *`.

## 8. Problemi comuni

| Sintomo | Causa / rimedio |
|---------|-----------------|
| Allarme **SENSORE I2C NON TROVATO** | SDA/SCL invertiti, manca 3V3 o GND, ADDR non a GND (indirizzo diverso da 0x48). Controlla sul monitor seriale `[ADS1115] ...` |
| Allarme **SHUNT FUORI SCALA** senza corrente, comando `ads` con A0−A1 saturo | lo shunt non ha riferimento a massa (manca il filo GND del tester sul bullone lato batteria) oppure un filo sense A0/A1 è staccato. Una lettura satura non viene né mostrata né integrata come corrente |
| Corrente sempre 0 | fili sense sui bulloni invece che sulle viti? A0/A1 scambiati con A2? (con INA226: R100 del modulo non rimossa?) |
| Corrente a scatti di ~0,16 A | il modulo è un ADS1015 (12 bit) invece di ADS1115: marcatura chip BRPI vs BOGI |
| Corrente negativa in scarica | *Impostazioni → Inverti segno* |
| Corrente rumorosa a vuoto | normale ±20–50 mA; fai *Azzera con corrente = 0*. Fili sense twistati e lontani dal buck |
| Tensione sballata del 5–10 % | resistenze non 1 %: calibra dall'app. Se ancora instabile passa a *ADS1115 canale A2* |
| Allarme **PARTITORE FUORI SCALA** | nodo del partitore sopra 3,05 V: R1/R2 sbagliate |
| Telefono "disconnesso" nell'app o pagina bianca | Samsung è tornato ai dati mobili (vedi §3) oppure, solo se sei collegato all'access point del tester, c'è una **VPN attiva** (es. Tailscale) che cattura il traffico: spegnila o usa la rete di casa, dove la VPN non dà problemi |
| Test finisce con "BMS ha staccato" | la batteria si è spenta da sola prima del cutoff: cella debole o sbilanciata (vedi §5). Se invece hai staccato tu un cavo, il tester non può distinguerlo |
| Test finisce subito con "noload" | nessuna corrente > 0,2 A per 2 minuti dopo l'avvio con carico: carico non collegato o relè non chiude |
| Non compila (Arduino IDE) | core esp32 troppo vecchio (< 2.0): aggiornalo dal Board Manager |

## 9. Personalizzazioni rapide (`config.h`)
* `AP_SSID` / `AP_PASS` – nome e password della rete del tester.
* `R1_OHM` / `R2_OHM` – se usi altre resistenze.
* `SHUNT_RATED_A` / `SHUNT_RATED_MV` – per altri shunt (es. 200 A/75 mV).
* `CURRENT_SENSOR` – `SENSOR_ADS1115` (default) oppure `SENSOR_INA226`; `ADS1115_ADDR`, `ADS1115_DR`.
* `CELLS_SERIES` – 4 per batterie 12 V, 16 per 48 V (ricalcola R1: il nodo del partitore non deve superare 2,5 V).
* `PIN_RELAY`, `RELAY_ACTIVE_HIGH`, `PIN_LED`.
