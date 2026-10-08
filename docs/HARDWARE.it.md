> 🇬🇧 English version: see the file with the same name without `.it`.

# Hardware – LiFePO4 Tester 24V 100Ah

Schema completo: [wiring_diagram.svg](wiring_diagram.svg) (aprilo con il browser).

## 1. Lista componenti

| # | Componente | Note |
|---|-----------|------|
| 1 | **ESP32 D1 mini** (WEMOS/LOLIN, ESP32-WROOM-32) | alimentato a 5 V sul pin VCC/5V |
| 2 | **Shunt FL-P 100 A / 75 mV** | 0,75 mV per ampere. Due bulloni grandi (corrente) e due viti piccole (sense) |
| 3 | **Modulo ADS1115** (16 bit, 4 canali, PGA, I2C) | legge i 75 mV dello shunt con PGA ±256 mV: 7,8 µV/bit → ~10 mA di risoluzione, carica e scarica. Il canale A2 legge il partitore con 125 µV/bit. Configurato di default nel firmware (`CURRENT_SENSOR SENSOR_ADS1115`). Vedi §4 |
| 3b | *(alternativa)* **Modulo INA226** (CJMCU-226) | 2,5 µV/bit → ~3 mA; misura anche VBUS fino a 36 V. **Va rimossa la resistenza R100 di bordo**. Si attiva con `CURRENT_SENSOR SENSOR_INA226` in `config.h` (vedi §4b) |
| 4 | **R1 = 120 kΩ 1 %** e **R2 = 10 kΩ 1 %** (metal film) | partitore di tensione |
| 5 | **C = 100 nF** ceramico | filtro sul nodo del partitore |
| 6 | *(se non usi il power bank)* **Buck converter XL4015** (4–38 V in, 5 A) o LM2596 (fino a 40 V) regolato a 5,0 V | il modulo XL4015 che hai va bene: 29,2 V di batteria in carica sono sotto il limite di 38 V. **Non usare MP1584** (max 28 V) |
| 7 | Fusibile 1 A sul ramo di alimentazione dell'elettronica | porta-fusibile in linea |
| 8 | Fusibile 125 A (o ≥ corrente di test) sul B+ verso il carico | obbligatorio se testi sopra i 30–40 A |
| 9 | *(opzionale)* **Modulo relè 1 canale 3 V con optoisolatore, trigger alto** (tipo ARCELI, relè Songle SRD-03VDC) | VCC = **3V3** dell'ESP32 (non 5 V), IN = GPIO26. Contatto tipico 10 A 250 VAC / **10 A 30 VDC**: da solo va bene fino a ~5 A di test; sopra usalo come pilota di un relè automotive 24 V 40 A o di un contattore DC |
| 10 | *(opzionale)* NTC 10 kΩ β 3950 + resistenza 10 kΩ | temperatura batteria |
| 11 | Cavo sezione adeguata per la corrente di test (≥ 16 mm² per 100 A), capicorda, morsetti | |
| 12 | Filo twistato 2 × 0,5 mm² per i sense dello shunt | tenere corto |

## 2. Pin ESP32 D1 mini

| Pin ESP32 | Funzione | Collegato a |
|-----------|----------|-------------|
| GPIO34 (ADC1_CH6, solo ingresso) | Tensione batteria | nodo R1/R2 del partitore + 100 nF a GND |
| GPIO35 (ADC1_CH7, solo ingresso) | Temperatura (opz.) | nodo NTC / pull-up 10 kΩ |
| GPIO21 | I2C SDA | ADS1115 SDA (o INA226 SDA) |
| GPIO22 | I2C SCL | ADS1115 SCL (o INA226 SCL) |
| GPIO26 | Comando relè (opz.) | IN del modulo relè (attivo alto; modificabile in `config.h`) |
| GPIO2 | LED relè (opz.) | LED + 330 Ω verso GND: acceso quando il relè è chiuso, cioè carico collegato |
| 3V3 | Alimentazione sensore | VDD ADS1115 (o VCC INA226) |
| 5V / VCC | Alimentazione scheda | OUT+ del buck |
| GND | Massa comune | GND ADS1115/INA226, R2, NTC, OUT− del buck (se usato) |

Gli ADC del Wi-Fi (ADC2) non si possono usare mentre il Wi-Fi è acceso: per questo i pin analogici sono GPIO34/35 (ADC1).

## 3. Partitore di tensione (GPIO34)

```
B+ ──[ R1 120 kΩ ]──┬──[ R2 10 kΩ ]── GND (B− lato batteria)
                    │
                    ├──[ 100 nF ]── GND
                    │
                  GPIO34
```

* Rapporto 13:1. A 29,2 V (batteria piena in carica) il pin vede 2,25 V; a 32 V vede 2,46 V. L'ADC ESP32 con attenuazione 11 dB è lineare fino a ~2,5 V.
* Corrente assorbita dal partitore: 30 V / 130 kΩ ≈ 0,23 mA (trascurabile).
* Il firmware legge in millivolt con la calibrazione di fabbrica dell'ESP32 (`analogReadMilliVolts`), fa una media di 128 letture ogni 100 ms e applica una media mobile esponenziale (costante di tempo ~1 s, `V_FILTER_ALPHA` in `config.h`). Un salto oltre 3 V, come lo stacco del BMS, scavalca il filtro e viene visto subito. Se la tensione a batteria ferma oscilla di più di qualche centesimo di volt, controlla il condensatore da 100 nF sul nodo e i fili del partitore: il 19/09/2026 un contatto incerto dava 1,6 V di oscillazione. L'errore residuo (≈1–2 %) si toglie con la calibrazione dall'app contro un multimetro.
* **Alternativa più precisa**: lo stesso nodo del partitore va anche all'ingresso **A2 dell'ADS1115** (16 bit, 125 µV/bit → 1,6 mV sulla batteria, errore di guadagno < 0,15 %). Nell'app, *Impostazioni → Sorgente misura → ADS1115 canale A2*. Con l'INA226 l'equivalente è il pin VBS collegato a B+ (1,25 mV/bit). L'ADC dell'ESP32 resta come riserva.

## 4. Shunt + ADS1115 (configurazione di default)

### Perché non leggere lo shunt direttamente con l'ESP32
75 mV a fondo scala sull'ADC dell'ESP32 (12 bit, ≈0,8 mV/bit, zona morta sotto ~100 mV) darebbe circa 1 A di risoluzione, errori di ±10 A e nessuna lettura in carica. Serve un ADC con amplificatore: l'ADS1115 con PGA ±256 mV ha 7,8 µV per bit → ~10 mA con lo shunt da 75 mV/100 A, ingresso differenziale e segno.

### Collegamento ADS1115
```
                 viti sense
B− ──┤ SHUNT ├── CARICO −
   (bullone)  (bullone)
      │            │
     A1           A0         (filo twistato, corto)
```
| Pin ADS1115 | Collegare a |
|-------------|-------------|
| VDD | 3V3 dell'ESP32 |
| GND | GND (massa comune, B− lato batteria) |
| SCL | GPIO22 |
| SDA | GPIO21 |
| ADDR | GND → indirizzo 0x48 (a VDD = 0x49, se serve cambia `ADS1115_ADDR`) |
| ALRT | non collegato |
| **A0** | vite sense dello shunt **lato carico** |
| A1 | **non usato su questo modulo**: ingresso guasto (resta inchiodato a 3,3 V). Nel cablaggio originale era il sense lato batteria |
| **A2** | nodo del partitore (lo stesso punto che va a GPIO34) |
| **A3** | vite sense dello shunt **lato batteria** |

La coppia differenziale dello shunt si sceglie con `ADS_MUX_SHUNT` in `config.h` (0 = A0−A1, **1 = A0−A3 in uso**, 2 = A1−A3, 3 = A2−A3). Con un modulo sano si può tornare a 0 e rimettere il filo su A1.

* Lo shunt sta sul **negativo**, tra B− e il negativo del carico/caricabatterie.
* **A0 lato carico, A1 lato batteria** → in scarica il lato carico è più alto di +I·R e la lettura A0−A1 è positiva. Se ti risulta al contrario non ricablare: nell'app c'è *Inverti segno*.
* La massa del tester va sul **B− lato batteria**. Così A1 sta a 0 V e A0 si muove di ±75 mV, dentro il limite −0,3 V…VDD dell'ADS1115.
* Il firmware fa 3 conversioni dello shunt (PGA ±256 mV) e 1 del partitore (PGA ±4,096 V) in single-shot a 32 SPS: nessuna libreria, nessun conflitto con il Wi-Fi.
* **ADS1115 o ADS1015?** Alcuni pacchi economici contengono l'ADS1015 (12 bit) con la stessa piedinatura. Marcatura sul chip: **BOGI** = ADS1115, **BRPI** = ADS1015. Il firmware funziona con entrambi (formula identica), ma con l'ADS1015 la risoluzione scende a ~160 mA.
* Con VDD = 3,3 V i pull-up I2C del modulo sono già a 3,3 V: perfetto per l'ESP32.

## 4b. Alternativa: INA226
L'INA226 ha un amplificatore dedicato con fondo scala ±81,92 mV e 2,5 µV per bit (~3 mA). Si attiva con `#define CURRENT_SENSOR SENSOR_INA226` in `config.h`. Schema: [wiring_diagram_ina226.svg](wiring_diagram_ina226.svg).

### Modifica del modulo INA226
I moduli CJMCU-226 hanno saldata a bordo una resistenza da 0,1 Ω (siglata **R100**) tra IN+ e IN−. Va **dissaldata** (o tagliata), altrimenti si mette in parallelo allo shunt e le misure sono sbagliate. Alcuni moduli hanno anche due resistenze da 10 Ω in serie agli ingressi: quelle vanno lasciate.

### Collegamento INA226
```
                 viti sense
B− ──┤ SHUNT ├── CARICO −
   (bullone)  (bullone)
      │            │
     IN−          IN+        (filo twistato, corto)
```
* Lo shunt sta sul **negativo**, tra B− e il negativo del carico/caricabatterie.
* **IN+ sul lato carico, IN− sul lato batteria** → scarica = corrente positiva. Se risulta al contrario non ricablare: nell'app c'è *Inverti segno*.
* La massa (GND) del tester va sul **B− lato batteria**, cioè sullo stesso lato di IN−. Con il tester a massa lì, IN+ si sposta al massimo di ±75 mV rispetto a GND, entro i limiti dell'INA226.
* I fili sense vanno presi dalle **viti piccole** dello shunt, non dai bulloni di potenza.
* VBS del modulo → B+ (per la misura di tensione alternativa). Se il modulo non ha il pin VBS, lascia la sorgente tensione su "Partitore".
* Indirizzo I2C 0x40 (A0 e A1 a GND, default). Modificabile in `config.h`.

## 5. Alimentazione
* Buck XL4015 (o LM2596): IN+ dal bus B+ tramite fusibile 1 A, IN− alla massa comune, OUT regolato a **5,0 V** con il trimmer, misurando con il multimetro PRIMA di collegare l'ESP32 (di fabbrica il trimmer può essere su 20–30 V e brucerebbe la scheda). OUT+ al pin 5V/VCC dell'ESP32, OUT− al GND. Se il tuo XL4015 ha due trimmer (versione CC/CV), quello della corrente lo lasci al massimo.
* Consumo tester ≈ 0,1–0,2 A a 5 V → circa 40–50 mA dalla batteria a 24 V (≈1 Ah al giorno). Se il tester resta collegato per settimane a batteria ferma, prevedi un interruttore.
* **Alternativa consigliata per uso da banco: power bank USB** collegato alla porta USB dell'ESP32. Niente buck, niente fusibile da 1 A, nessun problema con i 29 V in carica. Il GND USB resta collegato al B− tramite i fili del tester (corretto). Consumo 100–160 mA con Wi-Fi acceso: un power bank da 10.000 mAh dura 30–40 h. Se il power bank si spegne per "carico troppo basso", attiva la sua modalità bassa corrente/trickle.
* Non alimentare contemporaneamente da USB e dal buck: una sola sorgente alla volta.

## 6. Relè di stacco carico (opzionale)
* Modulo relè 1 canale **3 V** con optoisolatore e trigger a livello alto (es. ARCELI, relè Songle SRD-03VDC-SL-C):
  * **VCC → 3V3** dell'ESP32 (la bobina è da 3 V: con 5 V scalda e si rovina), **GND → GND**, **IN → GPIO26**. Il modulo assorbe ~120–130 mA quando il relè è chiuso: il regolatore 3,3 V della D1 mini lo regge.
  * Contatti: **COM** e **NO** in serie al B+ verso il carico (NC resta libero). A riposo il carico è staccato, il firmware chiude il relè all'avvio del test.
* **Carico massimo**: sul corpo del relè c'è scritto "10A 250VAC / 10A 30VDC". In corrente continua a 24 V l'arco all'apertura consuma i contatti, quindi usa il modulo da solo **fino a circa 5 A**. Per test a 10–20 A fai comandare al modulo un **relè automotive 24 V da 40 A** (bobina sul contatto NO del modulo, alimentata dai 24 V); per 50–100 A serve un **contattore DC** comandato allo stesso modo.
* Se usi un modulo da 5 V al posto di quello da 3 V: VCC al pin 5V, IN a GPIO26 (3,3 V basta per il trigger alto della maggior parte dei moduli optoisolati).
* Con `RELAY_ACTIVE_HIGH 0` in `config.h` si inverte la logica per i moduli attivi bassi.
* Se il relè è abilitato nell'app, il firmware lo chiude all'avvio del test, lo apre al cutoff, allo stop manuale e in caso di allarme (tensione alta, sovracorrente, temperatura).

## 7. NTC temperatura (opzionale)
```
3V3 ──[ 10 kΩ ]──┬──[ NTC 10 kΩ ]── GND
                 │
               GPIO35
```
Incolla l'NTC su una cella o sul terminale della batteria e attiva **Impostazioni → NTC collegato** nell'app (di default è disattivato: un pin libero darebbe letture casuali e falsi allarmi). Parametri β e resistenze in `config.h`.

## 8. Sicurezza
* Una LiFePO4 da 100 Ah eroga migliaia di ampere in cortocircuito: **fusibile sul B+ vicino alla batteria** sempre, cavi dimensionati, capicorda serrati.
* Lo shunt scalda: a 100 A dissipa 7,5 W. Lascialo in aria, non chiuderlo in una scatola piccola.
* Non superare 100 A (portata dello shunt): l'app segnala "shunt fuori scala" sopra 81 mV.
* Cutoff consigliato per 8S: 20,0 V (2,50 V/cella). Il BMS della batteria di solito stacca a 2,5 V/cella: meglio che il tester stacchi prima del BMS.
* Non caricare sotto 0 °C (l'NTC serve anche a questo).
