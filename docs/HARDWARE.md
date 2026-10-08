# Hardware – LiFePO4 Tester 24 V 100 Ah

> 🇮🇹 Versione italiana: [HARDWARE.it.md](HARDWARE.it.md)

Full diagram: [wiring_diagram.svg](wiring_diagram.svg) (open it in a browser).

## 1. Parts list

| # | Part | Notes |
|---|------|-------|
| 1 | **ESP32 D1 mini** (WEMOS/LOLIN, ESP32-WROOM-32) | powered from USB or 5 V on the VCC/5V pin |
| 2 | **FL-P shunt 100 A / 75 mV** | 0.75 mV per amp. Two large bolts (current) and two small screws (sense) |
| 3 | **ADS1115 module** (16-bit, 4 channels, PGA, I2C) | reads the 75 mV of the shunt with PGA ±256 mV: 7.8 µV/bit → ~10 mA resolution, charge and discharge. Channel A2 can read the divider at 125 µV/bit. Firmware default (`CURRENT_SENSOR SENSOR_ADS1115`). See §4 |
| 3b | *(alternative)* **INA226 module** (CJMCU-226) | 2.5 µV/bit → ~3 mA; also measures VBUS up to 36 V. **The on-board R100 resistor must be removed**. Enabled with `CURRENT_SENSOR SENSOR_INA226` in `config.h` (see §4b) |
| 4 | **R1 = 120 kΩ 1 %** and **R2 = 10 kΩ 1 %** (metal film) | voltage divider |
| 5 | **C = 100 nF** ceramic | filter on the divider node |
| 6 | *(if you do not use a power bank)* **XL4015 buck converter** (4–38 V in, 5 A) or LM2596 (up to 40 V) set to 5.0 V | 29.2 V of a charging battery is below the 38 V limit. **Do not use an MP1584** (28 V max) |
| 7 | 1 A fuse on the electronics supply branch | in-line fuse holder |
| 8 | Fuse on B+ towards the load, just above the test current (25 A for a 20 A test, 125 A for a 100 A test) | always, close to the battery |
| 9 | *(optional)* **1-channel 3 V relay module, opto-isolated, active high** (Songle SRD-03VDC type) | VCC = ESP32 **3V3** (not 5 V), IN = GPIO26. Typical contact rating 10 A 250 VAC / **10 A 30 VDC**: on its own it is fine up to ~5 A; above that use it to drive a 24 V 40 A automotive relay or a DC contactor |
| 10 | *(optional)* 10 kΩ β 3950 NTC + 10 kΩ resistor | battery temperature |
| 11 | Cable sized for the test current (≥ 16 mm² for 100 A), lugs, clamps | |
| 12 | Twisted pair 2 × 0.5 mm² for the shunt sense wires | keep it short |

## 2. ESP32 D1 mini pins

| ESP32 pin | Function | Connected to |
|-----------|----------|--------------|
| GPIO34 (ADC1_CH6, input only) | Battery voltage | R1/R2 divider node + 100 nF to GND |
| GPIO35 (ADC1_CH7, input only) | Temperature (opt.) | NTC / 10 kΩ pull-up node |
| GPIO21 | I2C SDA | ADS1115 SDA (or INA226 SDA) |
| GPIO22 | I2C SCL | ADS1115 SCL (or INA226 SCL) |
| GPIO26 | Relay control (opt.) | IN of the relay module (active high; configurable in `config.h`) |
| GPIO2 | Relay LED | on-board LED (or LED + 330 Ω to GND): on while the relay is closed, i.e. load connected |
| 3V3 | Sensor supply | ADS1115 VDD (or INA226 VCC), 3 V relay module |
| 5V / VCC | Board supply | buck OUT+ (only if no power bank is used) |
| GND | Common ground | ADS1115/INA226 GND, R2, NTC, buck OUT− |

ADC2 cannot be used while Wi-Fi is on, which is why the analog pins are GPIO34/35 (ADC1).

## 3. Voltage divider (GPIO34)

```
B+ ──[ R1 120 kΩ ]──┬──[ R2 10 kΩ ]── GND (B− battery side)
                    │
                    ├──[ 100 nF ]── GND
                    │
                  GPIO34
```

* Ratio 13:1. At 29.2 V (full battery on charge) the pin sees 2.25 V; at 32 V it sees 2.46 V. The ESP32 ADC with 11 dB attenuation is linear up to ~2.5 V.
* Divider current: 30 V / 130 kΩ ≈ 0.23 mA (negligible).
* The firmware reads millivolts with the ESP32 factory calibration (`analogReadMilliVolts`), averages 128 samples every 100 ms and applies an exponential moving average (time constant ~1 s, `V_FILTER_ALPHA` in `config.h`). A step larger than 3 V, such as a BMS trip, bypasses the filter and is seen immediately. The ESP32 ADC cannot read below ~142 mV: readings under 160 mV are reported as 0 V. If the voltage of a resting battery jitters by more than a few hundredths of a volt, check the 100 nF capacitor and the divider wiring (a loose contact can easily cause more than 1 V of noise). The remaining error (≈1–2 %) is removed by calibrating from the app against a multimeter.
* **More accurate alternative**: the same divider node also goes to **A2 of the ADS1115** (16-bit, 125 µV/bit → 1.6 mV at the battery, gain error < 0.15 %). In the app: *Impostazioni → Sorgente misura → ADS1115 canale A2* (Settings → Measurement source). With the INA226 the equivalent is the VBS pin wired to B+ (1.25 mV/bit). The ESP32 ADC remains available as a fallback.

## 4. Shunt + ADS1115 (default configuration)

### Why not read the shunt directly with the ESP32
75 mV full scale on the ESP32 ADC (12-bit, ≈0.8 mV/bit, dead zone below ~150 mV) would give about 1 A of resolution, errors of ±10 A and no reading at all while charging. An ADC with an amplifier is needed: the ADS1115 with PGA ±256 mV has 7.8 µV per bit → ~10 mA with the 75 mV / 100 A shunt, a differential input and a sign.

### ADS1115 wiring
```
                sense screws
B− ──┤ SHUNT ├── LOAD −
    (bolt)    (bolt)
      │          │
   − input    + input        (short twisted pair)
```
| ADS1115 pin | Connect to |
|-------------|------------|
| VDD | ESP32 3V3 |
| GND | GND (common ground, B− battery side) |
| SCL | GPIO22 |
| SDA | GPIO21 |
| ADDR | GND → address 0x48 (to VDD = 0x49; change `ADS1115_ADDR` if needed) |
| ALRT | not connected |
| **A0** | shunt sense screw, **load side** (+ input) |
| A1 | unused with `ADS_MUX_SHUNT 1`; it is the − input with `ADS_MUX_SHUNT 0` |
| A2 | divider node (optional, the same point that goes to GPIO34) |
| **A3** | shunt sense screw, **battery side** (− input with `ADS_MUX_SHUNT 1`) |

The differential pair is chosen with `ADS_MUX_SHUNT` in `config.h` (0 = A0−A1, **1 = A0−A3, as shipped**, 2 = A1−A3, 3 = A2−A3). The repository uses A0−A3 because input A1 of the author's module is damaged; any pair works on a healthy module. The serial command `ads` prints the voltage on each input and is the quickest way to find a dead channel: a healthy shunt input reads ~0 mV, a damaged one sits near VDD.

* The shunt goes in the **negative** lead, between B− and the negative of the load/charger.
* **+ input on the load side, − input on the battery side** → during discharge the load side is higher by +I·R and the reading is positive. If it comes out reversed do not rewire: the app has *Inverti segno* (invert sign).
* The tester ground goes on **B−, battery side**. This keeps the − input at 0 V while the + input moves by ±75 mV, inside the −0.3 V…VDD limit of the ADS1115. Without this ground reference the inputs float and the reading saturates.
* The firmware performs 3 conversions of the shunt (PGA ±256 mV) and 1 of the divider (PGA ±4.096 V) in single-shot mode at 32 SPS: no library, no conflict with Wi-Fi. Readings above 250 mV are treated as a fault and are never integrated into the Ah count.
* **ADS1115 or ADS1015?** Some cheap packs contain the ADS1015 (12-bit) with the same pinout. Chip marking: **BOGI** = ADS1115, **BRPI** = ADS1015. The firmware works with both (same formula) but with the ADS1015 the resolution drops to ~160 mA.
* With VDD = 3.3 V the module's I2C pull-ups are already at 3.3 V, which is what the ESP32 needs.

## 4b. Alternative: INA226
The INA226 has a dedicated amplifier with ±81.92 mV full scale and 2.5 µV per bit (~3 mA). Enable it with `#define CURRENT_SENSOR SENSOR_INA226` in `config.h`. Diagram: [wiring_diagram_ina226.svg](wiring_diagram_ina226.svg).

### Modifying the INA226 module
CJMCU-226 modules carry a 0.1 Ω resistor (marked **R100**) between IN+ and IN−. It must be **desoldered** (or cut), otherwise it sits in parallel with the shunt and the measurements are wrong. Some modules also have two 10 Ω resistors in series with the inputs: leave those in place.

### INA226 wiring
```
                sense screws
B− ──┤ SHUNT ├── LOAD −
    (bolt)    (bolt)
      │          │
     IN−        IN+          (short twisted pair)
```
* The shunt goes in the **negative** lead, between B− and the negative of the load/charger.
* **IN+ on the load side, IN− on the battery side** → discharge = positive current. If reversed, use *Inverti segno* in the app.
* The tester ground goes on **B−, battery side**, the same side as IN−. IN+ then moves by ±75 mV at most with respect to GND, within the INA226 limits.
* Take the sense wires from the **small screws** of the shunt, not from the power bolts.
* Module VBS → B+ (for the alternative voltage measurement). If the module has no VBS pin, leave the voltage source on "Partitore" (divider).
* I2C address 0x40 (A0 and A1 to GND, default). Configurable in `config.h`.

## 5. Power supply
* **Recommended for bench use: a USB power bank** on the ESP32 USB port. No buck, no 1 A fuse, no concern about 29 V while charging, and the tester stays alive when the BMS of the battery under test trips. The USB ground is tied to B− through the tester wiring (correct). Consumption is 100–160 mA with Wi-Fi on: a 10,000 mAh power bank lasts 30–40 h. If the power bank switches off because the load is too small, enable its low-current/trickle mode.
* XL4015 (or LM2596) buck: IN+ from the B+ bus through the 1 A fuse, IN− to the common ground, output set to **5.0 V** with the trimmer, measured with a multimeter BEFORE connecting the ESP32 (out of the box the trimmer may be set to 20–30 V and would destroy the board). OUT+ to the ESP32 5V/VCC pin, OUT− to GND. On the two-trimmer CC/CV version leave the current trimmer at maximum.
* Tester consumption ≈ 0.1–0.2 A at 5 V → about 40–50 mA from a 24 V battery (≈1 Ah per day). If the tester stays connected for weeks to an idle battery, add a switch.
* Never power from USB and from the buck at the same time: one source only.

## 6. Load-disconnect relay (optional)
* 1-channel **3 V** relay module, opto-isolated, active-high trigger (e.g. Songle SRD-03VDC-SL-C):
  * **VCC → ESP32 3V3** (the coil is rated 3 V: on 5 V it overheats and fails), **GND → GND**, **IN → GPIO26**. The module draws ~120–130 mA with the relay closed, which the D1 mini's 3.3 V regulator can supply.
  * Contacts: **COM** and **NO** in series with the B+ line to the load (NC unused). At rest the load is disconnected; the firmware closes the relay when the test starts.
* **Maximum load**: the relay body says "10A 250VAC / 10A 30VDC". In DC at 24 V the arc on opening wears the contacts, so use the module on its own only **up to about 5 A**. For 10–20 A tests let the module drive a **24 V 40 A automotive relay** (coil on the module's NO contact, fed from the 24 V; pin 85 to ground, contacts 30/87 in the B+ line); for 50–100 A use a **DC contactor** driven the same way.
* With a 5 V module instead of the 3 V one: VCC to the 5V pin, IN to GPIO26 (3.3 V is enough for the active-high trigger of most opto-isolated modules).
* `RELAY_ACTIVE_HIGH 0` in `config.h` inverts the logic for active-low modules.
* When the relay is enabled in the app, the firmware closes it at test start and opens it at cutoff, on manual stop and on alarm (high voltage, overcurrent, temperature). The on-board LED mirrors the relay state.

## 7. NTC temperature sensor (optional)
```
3V3 ──[ 10 kΩ ]──┬──[ NTC 10 kΩ ]── GND
                 │
               GPIO35
```
Glue the NTC to a cell or to a battery terminal and enable **Impostazioni → NTC collegato** (Settings → NTC connected) in the app. It is off by default, because a floating pin would give random readings and false alarms. β and resistor values are in `config.h`.

## 8. Safety
* A 100 Ah LiFePO4 battery can deliver thousands of amps into a short circuit: always fit a **fuse on B+ close to the battery**, use properly sized cables and tight lugs.
* The shunt gets hot: at 100 A it dissipates 7.5 W. Keep it in free air, not in a small closed box.
* Do not exceed 100 A (the shunt rating): the app reports "shunt fuori scala" (shunt over-range) above 81 mV.
* Suggested cutoff for 8S: 20.0 V (2.50 V/cell). The battery's BMS usually trips at about the same level; when the goal is to find batteries whose BMS trips early, leaving the cutoff at 20.0 V makes sure the BMS is actually exercised.
* A water-cooled resistive load produces a little hydrogen by electrolysis and a lot of heat: plastic tub, ventilated room, never unattended near flammable material.
* Do not charge below 0 °C (one more reason to fit the NTC).
