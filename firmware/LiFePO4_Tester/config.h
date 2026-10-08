#pragma once
// =====================================================================
//  LiFePO4 Tester 24V 100Ah  -  configurazione hardware
//  Scheda: ESP32 D1 mini (WEMOS / LOLIN D1 mini ESP32, ESP32-WROOM-32)
// =====================================================================

#define FW_VERSION        "1.3.0"

// Optional local overrides that must NOT be committed (e.g. your own AP password):
// create "config_local.h" next to this file, it is listed in .gitignore.
#if __has_include("config_local.h")
  #include "config_local.h"
#endif

// ---------------- Pin ----------------
#define PIN_ADC_VBAT      34      // ADC1_CH6, solo ingresso: partitore tensione batteria
#define PIN_ADC_NTC       35      // ADC1_CH7, solo ingresso: NTC 10k (opzionale)
#define PIN_SDA           21      // I2C INA226
#define PIN_SCL           22
#define PIN_RELAY         26      // uscita relè/MOSFET che stacca il carico (opzionale)
#define PIN_LED           2       // LED: acceso quando il relè è chiuso (carico collegato)
#define RELAY_ACTIVE_HIGH 1       // 1 = relè si chiude con livello alto, 0 = con livello basso

// ---------------- Partitore di tensione ----------------
// Vbat ---[R1]---+---[R2]--- GND      nodo centrale -> GPIO34 (+ 100nF verso GND)
// 120k / 10k  ->  rapporto 13 : a 30 V il pin vede 2.31 V (ADC in atten. 11 dB)
#define R1_OHM            120000.0f
#define R2_OHM            10000.0f
#define ADC_SAMPLES       128     // oversampling per lettura (l'ADC dell'ESP32 e' rumoroso, soprattutto con il Wi-Fi attivo)
#define V_FILTER_ALPHA    0.10f   // media mobile esponenziale sulla tensione: 0.10 a 10 Hz = costante di tempo ~1 s
#define V_FILTER_STEP     3.0f    // un salto oltre questa soglia (es. stacco del BMS) scavalca il filtro e passa subito

// ---------------- Shunt ----------------
// FL-P 100 A / 75 mV  ->  0.75 mV per ampere
#define SHUNT_RATED_A     100.0f
#define SHUNT_RATED_MV    75.0f

// ---------------- Sensore I2C per lo shunt ----------------
// SENSOR_ADS1115 : modulo ADS1115 (16 bit, 4 canali, PGA). A0-A1 sullo shunt, A2 sul partitore.
// SENSOR_INA226  : modulo INA226 (IN+/IN- sullo shunt, VBS su B+).
#define SENSOR_INA226     0
#define SENSOR_ADS1115    1
#define CURRENT_SENSOR    SENSOR_ADS1115

// --- ADS1115 ---
#define ADS1115_ADDR      0x48    // ADDR a GND = 0x48, a VDD = 0x49, a SDA = 0x4A, a SCL = 0x4B
#define ADS1115_DR        2       // data rate: 0=8, 1=16, 2=32, 3=64, 4=128 SPS (più basso = meno rumore)
// Coppia differenziale usata per lo shunt (campo MUX dell'ADS1115):
//   0 = A0(+) - A1(-)   cablaggio originale
//   1 = A0(+) - A3(-)   <-- IN USO: l'ingresso A1 di questo modulo e' guasto (inchiodato a VDD), il sense lato batteria va su A3
//   2 = A1(+) - A3(-)
//   3 = A2(+) - A3(-)   (in questo caso il partitore non puo' stare su A2)
// L'ingresso (+) va sulla vite sense LATO CARICO, l'ingresso (-) su quella LATO BATTERIA. Il partitore resta su A2.
#define ADS_MUX_SHUNT     1
// shunt su A0(+)/A1(-) con PGA ±256 mV -> 7.8125 uV/bit (~10 mA);  partitore su A2 con PGA ±4.096 V -> 125 uV/bit

// --- INA226 ---
#define INA226_ADDR       0x40    // A0=A1=GND (default modulo CJMCU-226)
#define INA226_CONFIG     0x4727  // AVG=64, VBUS 1.1ms, VSH 1.1ms, continuo shunt+bus (~140 ms/campione)

#if CURRENT_SENSOR == SENSOR_ADS1115
  #define SENSOR_NAME     "ADS1115"
#else
  #define SENSOR_NAME     "INA226"
#endif

// ---------------- NTC (opzionale) ----------------
// 3.3V ---[10k]---+---[NTC 10k]--- GND     nodo -> GPIO35
#define NTC_PULLUP_OHM    10000.0f
#define NTC_R25_OHM       10000.0f
#define NTC_BETA          3950.0f

// ---------------- Batteria (valori di default, modificabili dall'app) ----------------
#define DEF_CAPACITY_AH   100.0f
#define DEF_CUTOFF_V      20.0f   // 2.50 V/cella x 8 celle
#define DEF_VHIGH_ALARM   29.6f   // 3.70 V/cella
#define DEF_IMAX_ALARM    100.0f
#define DEF_TMAX_ALARM    55.0f
#define DEF_LOG_INTERVAL  10      // secondi tra righe di log durante il test
#define CELLS_SERIES      8

// ---------------- Wi-Fi ----------------
#ifndef AP_SSID
  #define AP_SSID         "LiFePO4-Tester"
#endif
#ifndef AP_PASS
  #define AP_PASS         "change-me-1234"  // min 8 chars: override it in config_local.h
#endif
#define MDNS_NAME         "lifepo4tester"   // http://lifepo4tester.local (PC / iPhone)

// ---------------- Timing ----------------
#define SAMPLE_MS         100     // periodo campionamento corrente/tensione
#define NVS_SAVE_MS       120000  // salvataggio contatori Ah/SOC in flash
#define REST_CURRENT_A    0.30f   // sotto questa corrente la batteria è "a riposo"
#define REST_TIME_MS      600000  // 10 min a riposo -> correzione SOC da OCV
