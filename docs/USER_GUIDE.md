# ESP32 LiFePO4 Battery Tester (24 V / 100 Ah)

> 🇮🇹 Versione italiana: [USER_GUIDE.it.md](USER_GUIDE.it.md) · Back to the [project overview](../README.md)

A capacity tester for 24 V (8S) LiFePO4 batteries built around an **ESP32 D1 mini**, a **voltage divider** and an **FL-P 100 A / 75 mV shunt** read by an **ADS1115** (16-bit I2C ADC; an INA226 is supported as an alternative).

The tester creates its own Wi-Fi network and serves a web app, so any phone or PC can use it without installing anything. It was built to compare a batch of identical batteries and find the ones whose BMS cuts off early because of a weak or unbalanced cell.

**The web UI is in Italian.** Labels quoted in this guide are given as they appear on screen, with the English meaning in brackets.

```
esp32-lifepo4-battery-tester/
├── README.md / README.it.md      ← project overview (English / Italian)
├── docs/USER_GUIDE.md            ← this guide (English; Italian: USER_GUIDE.it.md)
├── firmware/
│   ├── platformio.ini            ← PlatformIO project (VS Code)
│   └── LiFePO4_Tester/
│       ├── LiFePO4_Tester.ino    ← firmware (also opens in the Arduino IDE)
│       ├── config.h              ← pins, shunt/divider values, Wi-Fi defaults
│       ├── web_ui.h              ← embedded web app (HTML/JS)
│       └── web_icons.h           ← app icons (generated)
├── app_android/                  ← optional Android app (plain Java, self-updating, no Gradle)
├── docs/
│   ├── HARDWARE.md               ← parts list, pinout, electrical details, safety
│   ├── wiring_diagram.svg        ← wiring diagram (ADS1115)
│   └── wiring_diagram_ina226.svg ← INA226 variant
└── tools/
    ├── make_icons.py             ← regenerates the icons
    ├── confronta_csv.py          ← PNG chart + summary from the test CSV files (PC)
    └── mock_server.py            ← API simulator to try the web app without an ESP32
```

## Features

* Measures **voltage** (divider on the ESP32 ADC, or the same divider read by the ADS1115), **current** (shunt + ADS1115), **power** and **temperature** (optional NTC).
* **Coulomb counting**: Ah and Wh in and out, **SOC** estimate with open-circuit-voltage correction at rest.
* **Capacity test**: started from the app, integrates the delivered Ah, stops by itself at the **cutoff voltage** (default 20.0 V = 2.5 V/cell) and, if a relay is fitted, disconnects the load. Stores the result (Ah, Wh, duration, internal resistance, % of nominal) and a downloadable **CSV log**.
* **BMS trip detection**: if the pack voltage collapses to zero from above the cutoff, the test ends as "BMS tripped" and the voltage just before the collapse is saved.
* **Test history and comparison**: each test has a name and a timestamp; a table ranks the batteries by delivered Ah and highlights the suspicious ones; an overlay chart draws one curve per test.
* **Persistent live chart**: the ESP32 keeps the last 6 hours in RAM, so the curve is still there when the app is closed and reopened, on any device.
* **DC internal resistance** estimated from load steps.
* **Alarms**: low/high voltage, overcurrent, temperature, shunt over-range, I2C sensor missing.
* **Calibration from the app** against a multimeter or clamp meter, stored in flash.
* **Firmware update over Wi-Fi**, refused while a test is running.
* **Test survives a reboot**: the running test is saved to flash every 30 s; after a power cut or a crash the tester resumes it (same id, same CSV, Ah/Wh kept, relay closed again) instead of losing hours of work. The downtime is not counted and is marked in the CSV; `/api/status` reports `boot` (reset reason) and `resumed`.
* No external libraries: builds with the ESP32 Arduino core alone.

## 1. Hardware

See [docs/HARDWARE.md](HARDWARE.md) and [docs/wiring_diagram.svg](wiring_diagram.svg). In short:

| Signal | Connection |
|--------|------------|
| Voltage | B+ → R1 120 kΩ → node → R2 10 kΩ → GND, 100 nF on the node. The node goes to **GPIO34** and, optionally, to **A2** of the ADS1115 |
| Current | shunt in the **negative** lead; sense screws → ADS1115 **+ input on the load side**, **− input on the battery side**; ADS1115 SDA→**GPIO21**, SCL→**GPIO22**, VDD→3V3, GND→GND, ADDR→GND |
| Ground | tester GND on **B−, battery side** of the shunt |
| Power | a **USB power bank** on the ESP32 USB port (simplest, ~30–40 h from 10,000 mAh), or an **XL4015 / LM2596** buck converter 24→5 V on the 5V pin, set to 5.0 V *before* connecting it (not an MP1584: 28 V max). Never both at once |
| Relay (optional) | **3 V** relay module: VCC→**3V3**, GND→GND, IN→**GPIO26**; COM/NO contacts in series with the load B+. On its own up to ~5 A DC; above that let it drive a 40 A automotive relay or a DC contactor |
| NTC (optional) | 3V3 → 10 kΩ → **GPIO35** → 10 kΩ NTC → GND |

The differential pair used for the shunt is chosen with `ADS_MUX_SHUNT` in `config.h` (0 = A0−A1, 1 = A0−A3, 2 = A1−A3, 3 = A2−A3). **The repository ships with `1` (A0−A3)** because the author's module has a damaged A1 input; with a healthy module either setting works, just wire the sense leads accordingly.

To use an INA226 instead, set `CURRENT_SENSOR SENSOR_INA226`, wire IN+ to the load side and IN− to the battery side, VBS to B+, and **remove the on-board R100 resistor** of the module. Diagram: [docs/wiring_diagram_ina226.svg](wiring_diagram_ina226.svg).

## 2. Flashing the firmware

### PlatformIO (recommended)
1. Install VS Code and the PlatformIO extension.
2. *File → Open Folder* → `firmware/`.
3. Connect the ESP32 over USB and press **Upload**. The board is already set (`wemos_d1_mini32`).
4. *Serial Monitor* at 115200 baud shows the logs.

From a terminal, inside `firmware/`:
```bash
pio run -t upload && pio device monitor
```

### Arduino IDE
1. Install the **esp32 by Espressif** core from the Board Manager (2.x or 3.x).
2. Open `firmware/LiFePO4_Tester/LiFePO4_Tester.ino`.
3. Board: **WEMOS D1 MINI ESP32**; Partition Scheme: *Default 4MB with spiffs*; Upload Speed 921600 (460800 if it fails).
4. Upload. No additional libraries are needed.

The LittleFS filesystem used for the logs is formatted automatically on first boot.

### Your own access-point password
The default AP password in `config.h` is a placeholder. Create `firmware/LiFePO4_Tester/config_local.h` (ignored by Git) to set your own without committing it:
```c
#pragma once
#define AP_PASS "your-password-min-8-chars"
```

### Updating over Wi-Fi
Once a firmware with this feature is on the board, no cable is needed (the update is refused with HTTP 409 while a test is running):
```bash
pio run && curl -F "firmware=@.pio/build/d1mini_esp32/firmware.bin" http://<tester-ip>/api/update
```

## 3. First start

1. Power the tester. On the phone join the Wi-Fi network **`LiFePO4-Tester`** with the password set in `AP_PASS`.
2. Android may warn that the network has no internet: choose to stay connected (the tester answers Android's connectivity check, so the warning usually does not appear).
3. Open **http://192.168.4.1** in the browser.
4. Browser menu → *Add to Home screen* to get an app-like icon.

Tips:
* To keep internet on the phone, open *Impostazioni → Wi-Fi di casa* (Settings → Home Wi-Fi) in the app and enter your network. The ESP32 joins it as well; its address is shown in the Settings page and on the serial monitor. The access point stays on as a fallback.
* On a PC the app is also reachable at http://lifepo4tester.local (mDNS; Android does not resolve it).
* The home network can also be set from the **serial monitor** (115200 baud) with `wifi SSID|password`. Other commands: `wifi off`, `ip`, `relay 0/1`, `reboot`, and the diagnostics `i2c` (bus scan, the ADS1115 must show at 0x48), `ads` (voltage on each input A0–A3 and the shunt differential) and `adc` (raw millivolts on GPIO34/35; ~142 mV is the ADC floor and means 0 V).
* Reserve the ESP32's address in your router (DHCP reservation) so it does not change. Personal details of your installation can be kept in `NOTE_LOCALI.md`, which is ignored by Git.
* A VPN that is always on (e.g. Tailscale) captures the traffic when the phone is connected directly to the tester's access point, and the page stays blank: switch it off there. On the home network it is not a problem.

### Remote access (Tailscale)
The ESP32 cannot run Tailscale, so an always-on computer on the same LAN (for example a Raspberry Pi) acts as a bridge by advertising a route to the tester only:
```bash
sudo tailscale set --advertise-routes=<tester-ip>/32
echo 'net.ipv4.ip_forward = 1' | sudo tee /etc/sysctl.d/99-tailscale.conf
```
Approve the route in the Tailscale admin console. From then on any device with Tailscale enabled uses **the same address as at home**, so the app and the API calls do not change.

* Side effect: a device with Tailscale on goes through the bridge **even when it is at home**, because the /32 route is more specific than the LAN route. If the bridge is off and the tester seems unreachable, turn Tailscale off on that device.
* **Security: the tester has no authentication.** Anyone who can reach it can start or stop a test and upload firmware. Never forward a router port to it, and restrict access with Tailscale ACLs if your tailnet is shared.

A native Android wrapper with its own icon and no browser bar is in [app_android/](../app_android/README.md).

## 4. Calibration (5 minutes)

In the app, **Impostazioni** (Settings) tab:

1. **Voltage**: read the battery with a multimeter, type the value in *Tensione reale* (actual voltage) and press *Calibra guadagno tensione* (calibrate voltage gain).
2. **Current zero**: with no load and no charger connected press *Azzera con corrente = 0* (zero at no current).
3. **Current gain**: with a steady current of at least a few amps, measure it with a clamp or multimeter, wait until the *media 10 s* (10 s average) value is stable, type the value in *Corrente reale* (actual current) and press *Calibra guadagno corrente*. The calibration uses a ~10 s average, not the instantaneous reading, because a charger's ripple would make a single sample wrong by up to 10 %. It works while charging or discharging and the sign does not matter. With a DC clamp, measure in both orientations and use the mean.
4. If discharge shows as negative, tick *Inverti segno* (invert sign) instead of rewiring.

Calibration values are kept in flash.

## 5. Capacity test

1. Fully charge the battery, **disconnect the charger** and let it rest. Use the same rest time for every battery you want to compare (15–30 min).
2. Prepare a **constant load**. The internal resistance is taken from the voltage step when the current appears: at relay closure or, without a relay, when you connect the load by hand *after* pressing start (within 5 minutes, otherwise the test is cancelled as "no load"). A load that is already drawing current at the start gives no step and no resistance value. Pick the current for the time you have (100 Ah, 25.6 V average):

   | C-rate | Current | Duration | Power | Equivalent R | Notes |
   |---|---|---|---|---|---|
   | 0.1C | 10 A | ~10 h | 256 W | 2.6 Ω | accurate, slow |
   | **0.2C** | **20 A** | **~5 h** | **512 W** | **1.3 Ω** | **manufacturers' standard, recommended** |
   | 0.3C | 30 A | ~3.3 h | 770 W | 0.85 Ω | good compromise |
   | 0.5C | 50 A | ~2 h | 1.3 kW | 0.5 Ω | fast; capacity reads 2–3 % lower, 16 mm² cables |
   | 1C | 100 A | ~1 h | 2.6 kW | 0.26 Ω | limit of the shunt and of many BMSs, not recommended |

   **Water-cooled nichrome load** (1.3 Ω for 20 A): Ni-Cr wire **Ø 2 mm, ~3.7 m** (or four Ø 1 mm wires of 3.8 m in parallel) coiled loosely on a PVC pipe and immersed in **at least 50 L** of water, preferably demineralised (512 W heat 50 L by ~9 °C per hour; 10 L would boil in 1.5 h). Keep the wire surface load below 3 W/cm², clamp the ends with screw lugs out of the water (nichrome cannot be soldered; a poor joint shows up as "no load"), use a plastic tub and a ventilated room (slight electrolysis). Adjust the current by moving the clamp along the wire while reading the tester. A 24 V truck/boat immersion heater is an alternative with no bare wire.

   Other practical loads: a **24 V inverter plus a space heater** (constant power, so the current rises ~25 % towards the end), **24 V truck bulbs** (H4 75 W ≈ 3 A each), power resistors only up to ~10 A. Cables: 20 A → 4 mm², 30 A → 6 mm², 50 A → 16 mm².
3. **Test** tab: set the cutoff (20.0 V suggested), nominal capacity, log interval (10 s), tick *Usa relè* (use relay) if fitted, type a unique battery name and press **Avvia test** (start test).
4. The tester integrates the Ah and shows duration, average current and a rough estimate of the final capacity. At the cutoff (voltage below threshold for 3 s) it ends the test, opens the relay and stores the result.
5. **Scarica CSV** (download CSV) gives the log for a spreadsheet (columns: seconds, V, A, W, Ah, Wh, °C).

A new 100 Ah LiFePO4 at 0.1–0.2C should deliver **≥ 95–100 Ah** down to 2.5 V/cell. Below 80 % it is at end of life.

### Comparing several batteries

The typical defect "the BMS cuts off earlier than the others" is almost always **a weak or unbalanced cell**: the BMS trips when that cell reaches 2.5 V while the others are still at 3.0–3.2 V, so the pack voltage at the trip is 23–25 V instead of 20 V.

* When the voltage collapses to zero from above the cutoff the test ends as **"BMS ha staccato"** (BMS tripped) and *V fine* (final voltage) holds the voltage just before the collapse.
* The **Storico e confronto batterie** (history and comparison) table ranks the tests by delivered Ah. Red means the BMS tripped **early** (final voltage more than 1.5 V above the cutoff) or the battery delivered less than 90 % of the best one. A yellow "BMS a fine" (BMS at the end) is normal: with the cutoff at 20.0 V, right on the BMS threshold, either one may act first.
* Each row has its own CSV and a bin icon that deletes **only that test**; clearing the whole history asks for two confirmations.

Suggested procedure: same charger, same rest time, same current, same cutoff and a naming scheme (`B01`…`B10`) for all batteries. Power the tester from a power bank rather than from the battery under test: when the BMS trips, the terminal voltage goes to zero and a battery-powered tester would lose the result. For a suspicious battery try a long balancing charge and repeat the test; if it still trips early with a high final voltage, a cell is faulty.

History (last 60 tests, last 12 CSV files) survives power loss.

### Comparison chart
In the **Test** tab, *Carica le curve dai CSV* (load the curves from the CSV files) draws one curve per stored test. Horizontal axis: delivered Ah or time; vertical axis: voltage, current, power, or **voltage drop per 10 minutes**, which turns a sudden sag into a clearly visible peak.

On a PC, from downloaded CSV files or straight from the tester:
```bash
python tools/confronta_csv.py folder_with_csv_files
python tools/confronta_csv.py --tester <tester-ip>
```
It writes `confronto.png` (voltage vs Ah, voltage vs time, current vs time; dashed lines for suspicious batteries) and `confronto_riepilogo.csv`. Requires `matplotlib`.

## 6. Trying the web app without an ESP32

```bash
python tools/mock_server.py 8080
```
then open http://localhost:8080. The simulator answers the same API as the firmware, with an accelerated discharge.

## 7. API

| Method | URL | Parameters | Description |
|--------|-----|------------|-------------|
| GET | `/api/status` | – | JSON with all measurements, test state, last test, Wi-Fi; `boot` = reset reason (`poweron`, `brownout`, `wdt`, `panic`, `sw`), `resumed` = 1 if the running test was resumed after a reboot |
| GET | `/api/config` | – | current configuration and calibration |
| POST | `/api/config` | `cap, cutoff, logint, relay_en, ntc_en, inv, vsrc, vhigh, imax, tmax` | save settings |
| POST | `/api/cal` | `type=vgain\|izero\|igain\|reset`, `actual=`; or `type=set&igain=&ioff=` | calibration |
| POST | `/api/test` | `cmd=start\|stop\|reset`, `name=`, `ts=` (epoch) | capacity test |
| POST | `/api/soc` | `value=0..100` | set SOC |
| POST | `/api/counters` | `cmd=reset` | reset Ah/Wh counters |
| POST | `/api/relay` | `state=0\|1` | manual relay |
| GET | `/api/log` | `id=` (optional) | CSV of the given test (default: latest) |
| POST | `/api/log` | `cmd=clear` | delete all CSV files |
| GET | `/api/history` | – | test history (JSON array: id, name, ts, reason, ah, wh, dur, vstart, vend, iavg, rint) |
| POST | `/api/history` | `cmd=clear`, `cmd=delete&id=N` or `cmd=rename&id=N&name=X` | clear the history, delete one test (row + CSV), or rename one (row and CSV header; pencil icon in the app) |
| GET | `/api/trend` | – | chart history: one point every 20 s, last 6 h (`v` in mV, `i` in centiamps, `age` = seconds since the last point, `tstart` = seconds since the test started, or −1) |
| POST | `/api/wifi` | `ssid, pass` | home network (reboots) |
| GET | `/api/app` | – | version (`versionCode`) and size of the Android APK hosted on the tester, for the app self-update |
| GET | `/app.apk` | – | the hosted APK |
| POST | `/api/app/upload?version=N` | multipart file `apk` | publish a new APK for the self-update; refused while a test is running |
| POST | `/api/update` | multipart file `firmware` | firmware update over Wi-Fi; HTTP 409 while a test is running |
| POST | `/api/reboot` | – | reboot |

POST parameters are `application/x-www-form-urlencoded`. Responses carry `Access-Control-Allow-Origin: *`. **There is no authentication**: keep the tester on a trusted network.

## 8. Troubleshooting

| Symptom | Cause / fix |
|---------|-------------|
| Alarm **SENSORE I2C NON TROVATO** (I2C sensor not found) | SDA/SCL swapped, missing 3V3 or GND, a loose wire, ADDR not tied to GND (address other than 0x48). Run `i2c` on the serial monitor |
| Alarm **SHUNT FUORI SCALA** (shunt over-range) with no current | the shunt has no ground reference (missing wire from tester GND to the battery-side bolt), a sense wire is open, or an ADS1115 input is damaged. Run `ads`: both shunt inputs must read ~0 mV. A saturated reading is never shown or integrated as a current |
| Current always 0 | sense wires on the power bolts instead of the small screws; wrong pair in `ADS_MUX_SHUNT`; with an INA226, the on-board R100 not removed |
| Current in ~0.16 A steps | the module is an ADS1015 (12-bit), chip marking BRPI instead of BOGI |
| Negative current while discharging | *Impostazioni → Inverti segno* |
| Voltage off by a few percent | resistor tolerance: calibrate from the app |
| Voltage jitters by more than a few hundredths of a volt | check the 100 nF capacitor on the divider node and the divider wiring |
| Alarm **PARTITORE FUORI SCALA** (divider over-range) | divider node above 3.05 V: wrong R1/R2 |
| Page blank or "disconnesso" | the phone fell back to mobile data, or an always-on VPN is capturing traffic while connected to the tester's access point |
| Tester rebooted during a test | the test resumes by itself (a `# ripreso dopo riavvio` line appears in the CSV); check `boot` in `/api/status`: `brownout` or `poweron` = power supply problem, use a power bank; `wdt`/`panic` = firmware issue, report it |
| Test ends with "BMS ha staccato" | the battery switched itself off before the cutoff: weak or unbalanced cell. A cable pulled by hand looks the same to the tester |
| Test ends with "Carico assente" (`noload`) | no current above 0.2 A within 5 minutes of the start, or for 2 minutes after a load had been seen: load not connected, poor joint, or relay not closing |
| Arduino IDE build fails | esp32 core older than 2.0: update it |

## 9. Quick customisation (`config.h`)
* `AP_SSID` / `AP_PASS` – name and password of the tester's network (override them in `config_local.h`).
* `R1_OHM` / `R2_OHM` – for different divider resistors.
* `SHUNT_RATED_A` / `SHUNT_RATED_MV` – for other shunts (e.g. 200 A / 75 mV).
* `CURRENT_SENSOR` – `SENSOR_ADS1115` (default) or `SENSOR_INA226`; `ADS1115_ADDR`, `ADS1115_DR`, `ADS_MUX_SHUNT`.
* `CELLS_SERIES` – 4 for 12 V batteries, 16 for 48 V (recalculate R1: the divider node must stay below 2.5 V).
* `V_FILTER_ALPHA`, `V_FILTER_STEP` – voltage smoothing; a step larger than `V_FILTER_STEP` bypasses the filter so a BMS trip is seen at once.
* `PIN_RELAY`, `RELAY_ACTIVE_HIGH`, `PIN_LED` (the LED mirrors the relay state).

## Safety
A 100 Ah LiFePO4 pack can deliver thousands of amps into a short circuit. Always fit a **fuse on B+ close to the battery**, use properly sized cables and tight lugs, and read [docs/HARDWARE.md](HARDWARE.md) before wiring anything. This is a hobby project provided as is, with no warranty.
