# ESP32 LiFePO4 Battery Tester

**A self-hosted capacity tester and diagnostic logger for 24 V (8S) LiFePO4 packs, built to find the cell that makes a BMS cut off early.**

ESP32 firmware (C++), a 16-bit measurement chain (shunt + ADS1115), coulomb counting with open-circuit-voltage SOC correction, internal-resistance estimation, BMS-trip detection and a phone/PC web app served by the board itself. No cloud, no extra libraries.

> 🇮🇹 [Versione italiana](README.it.md) · Full build and operating guide: [docs/USER_GUIDE.md](docs/USER_GUIDE.md) · Wiring and parts: [docs/HARDWARE.md](docs/HARDWARE.md)

<p align="center">
  <img src="docs/img/app-monitor.png" alt="Monitor tab: live voltage, current, power, Ah in and out, persistent chart" width="260">
  <img src="docs/img/app-test.png" alt="Test tab: test setup and result of the last completed test" width="260">
</p>

<sub>The web app served by the ESP32 (UI in Italian). Left: live monitor during the charge of pack 3bk1. Right: test setup and the last completed test.</sub>

## Why this exists

A batch of identical 24 V / 100 Ah packs: some shut down well before the others under the same load. A BMS trip is almost always a **weak or unbalanced cell**: the pack voltage is still 23-25 V when that one cell reaches 2.5 V and the BMS opens the circuit. A multimeter cannot see that, and a commercial tester that logs the full discharge was out of proportion for the job. So I built one that measures the discharge, detects the trip and ranks the packs.

## What it does

| Function | How |
|---|---|
| Voltage, current, power, temperature | Divider + ADS1115 (or ESP32 ADC fallback), 100 A / 75 mV shunt, optional NTC |
| Capacity test | Integrates Ah and Wh at ~10 Hz, stops at a configurable cutoff (default 20.0 V = 2.5 V/cell), opens a load relay if fitted |
| **BMS-trip detection** | A collapse to ~0 V from above the cutoff ends the test as "BMS tripped" and stores the voltage just before the collapse |
| SOC | Coulomb counting, corrected from open-circuit voltage after 10 min at rest |
| Internal resistance | DC step method, from the voltage change when the load closes |
| History and comparison | Last 60 tests ranked by delivered Ah, suspicious packs highlighted, per-test CSV, overlay chart (also `tools/confronta_csv.py` on PC) |
| Robustness | A running test is checkpointed to flash every 30 s and **resumes after a power cut or crash**; the downtime is marked in the CSV |
| Operations | On-board Wi-Fi access point and web app (PWA), calibration from the app, firmware update over Wi-Fi (refused while a test runs), alarms for over/under-voltage, overcurrent, over-temperature, shunt over-range, missing sensor |

## Architecture

```
 Battery B+ ──[R1 120k]──┬──[R2 10k]── GND        ADS1115 (I2C, 16 bit)
                         │ 100 nF                   ├─ A0-A3: shunt differential (PGA ±256 mV)
                         ├────────────► GPIO34      └─ A2:    divider node     (PGA ±4.096 V)
                         │             (ESP32 ADC fallback)
 B−  ──[ shunt 100 A / 75 mV ]── load  ──► sense leads ► ADS1115
                                                          │
                      ESP32 D1 mini ◄─────── I2C ─────────┘
                       ├─ 10 Hz sampling, coulomb counter, test state machine
                       ├─ LittleFS: CSV logs, history, checkpoint of the running test
                       ├─ HTTP/JSON API + embedded web app (Wi-Fi AP, optional home Wi-Fi)
                       └─ relay (load on/off), NTC, status LED
```

## Engineering notes

**Measurement chain.** A 100 A / 75 mV shunt gives 0.75 mV per amp. The ESP32's own 12-bit ADC would give about 1 A of resolution and has a dead zone below ~150 mV, so the shunt is read by an ADS1115 at ±256 mV full scale: 7.8125 µV per bit, i.e. **about 10 mA per bit**, differential and signed (charge and discharge). The same chip reads the voltage divider (ratio 13:1, 125 µV per bit at ±4.096 V, 1.6 mV per bit at the battery). The divider node stays at 2.25 V with a full 29.2 V pack, inside the linear range of the 11 dB ESP32 ADC fallback.

**Noise and filtering.** The ESP32 ADC is noisy with Wi-Fi on, so the fallback path averages 128 samples per reading and applies an exponential filter (~1 s). A step larger than 3 V bypasses the filter, so a BMS trip is visible instantly instead of being smeared out.

**Why single-shot ADS1115.** The firmware alternates three shunt conversions with one voltage conversion, so one chip serves both channels with a different gain for each.

**Calibration.** Gain and offset are calibrated from the app against a multimeter and a clamp meter and stored in flash. Current calibration uses a ~10 s average rather than a single sample, because charger ripple can make one sample wrong by up to 10 %. On my unit the first calibration moved the voltage gain by about 0.5 % and the current gain by about 3.5 % (clamp at 9.6 A), which is why the app calibration is not optional.

**Failure handling.** A saturated shunt reading is never integrated as current. The test ends with an explicit reason (`cutoff`, `BMS tripped`, `no load`, `stopped`). The tester should be powered from a power bank, not the pack under test: when the BMS trips, the terminals go to 0 V and a battery-powered tester would lose the result.

**Hardware faults found along the way.** My ADS1115 module has damaged A1 and A2 inputs, so the repository ships with the shunt on A0-A3 (`ADS_MUX_SHUNT 1`) and a diagnostic serial command (`ads`) that prints every input; the `i2c` and `adc` commands helped isolate the fault.

## Results

Eight 24 V / 100 Ah packs from the same batch, each discharged at about 20 A (average 19.3-22.1 A) down to a 20.0 V cutoff or until the BMS opened the circuit, after a full charge and a rest. Data logged by the tester (CSV and history in the app).

| Rank | Pack | Delivered Ah | End | Final voltage | Verdict |
|---|---|---|---|---|---|
| 1 | 5bk2 | 91.3 | BMS | 21.1 V | good |
| 2 | 1bk2 | 85.8 | - | - | good |
| 3 | 1bk1 | 85.3 | cutoff | 20.0 V | good |
| 4 | 2bk1 | 85.2 (45.0 + 40.2) | BMS | 22.0 V | good (test split, see note) |
| 5 | 4bk2 | 84.6 | cutoff | 20.0 V | good |
| 6 | 3bk2 | 84.5 | cutoff | 20.0 V | good |
| 7 | 2bk2 | 84.3 | cutoff | 20.0 V | good |
| 8 | **3bk1** | **74.9** | **BMS** | **24.2 V** | **defective** |

Seven packs fall within 7 Ah of each other (84.3-91.3 Ah); the eighth delivers 74.9 Ah, about 87 % of the average of the seven. Note: 2bk1 was interrupted at 45 Ah by a loose terminal (the tester ended it as "no load") and resumed as a second test; the two parts are summed here.

**The defective pack.** 3bk1's curve sits below all the others from about 30 Ah (25.73 V against 25.85-26.0 V), and the gap widens: at 74 Ah it read 24.3 V while the others were at 25.0-25.2 V. The BMS opened at 24.2 V, i.e. one cell had reached 2.5 V while the other seven were still near 3.1 V. On charge it behaves the same way: the BMS starts chopping at only 27.6-28.1 V (one cell already at its overvoltage limit) and the pack takes back only about 88 % of the discharged Ah, against 94-95 % for the healthy packs and 89 % for the most unbalanced healthy one. Both ends of the curve point to one cell with reduced capacity, not just a balancing problem. This is the case the tester was built to find.

![Voltage and current of the eight packs: voltage vs delivered Ah, voltage vs time, current vs time. Dashed lines mark the packs flagged as suspicious, red dots mark an early BMS trip.](docs/img/comparison-8-packs.png)

<sub>Generated by `tools/confronta_csv.py` from the CSV logs (chart labels in Italian). 3bk1 is the grey dashed curve that ends at 24.2 V.</sub>

The internal-resistance estimate from the load step varied between 2 and 45 mOhm across tests and I do not consider it reliable enough to rank packs (contact resistance of the load connection dominates); the capacity and end-of-discharge voltage are the trusted outputs.

## Quick start

```bash
# firmware (PlatformIO)
cd firmware && pio run -t upload && pio device monitor

# try the web app without hardware (API simulator, accelerated discharge)
python tools/mock_server.py 8080       # then open http://localhost:8080
```

Join the Wi-Fi network `LiFePO4-Tester`, open `http://192.168.4.1`, calibrate, start a test. Set your own access-point password in `firmware/LiFePO4_Tester/config_local.h` (ignored by Git). Everything else is in the [user guide](docs/USER_GUIDE.md).

## Repository layout

```
firmware/        ESP32 firmware (PlatformIO / Arduino IDE), embedded web app
app_android/     optional Android wrapper (plain Java, self-updating)
docs/            hardware guide, wiring diagrams (ADS1115 and INA226), user guide
tools/           CSV comparison (matplotlib), API simulator, icon generator
```

## Limits and honest caveats

* **No authentication.** Anyone who reaches the tester can start a test or upload firmware: keep it on a trusted network, never port-forward it. For remote access I use a Tailscale subnet route to the tester's address only.
* Accuracy is bounded by the calibration references (multimeter, clamp meter) and by the shunt's tolerance and temperature drift; this is a comparison tool for packs tested under the same conditions, not a metrology instrument.
* The web UI is in Italian.
* Built with AI coding assistance for parts of the firmware and tooling; the circuit design, test methodology, bench validation and calibration are mine.

## Safety

A 100 Ah LiFePO4 pack can deliver thousands of amps into a short circuit. Fit a fuse on B+ close to the battery, size cables for the test current and read [docs/HARDWARE.md](docs/HARDWARE.md) before wiring. Hobby project, provided as is, without warranty.

## License

MIT, see [LICENSE](LICENSE).
