/*
 * =====================================================================
 *  LiFePO4 Tester 24V 100Ah  -  ESP32 D1 mini
 * ---------------------------------------------------------------------
 *  - Tensione : partitore 120k/10k su GPIO34 (in alternativa canale A2 dell'ADS1115 o VBUS dell'INA226)
 *  - Corrente : shunt FL-P 100A/75mV letto da un ADS1115 (I2C, 16 bit, PGA +-256 mV) oppure da un INA226
 *  - Coulomb counting (Ah/Wh in/out), stima SOC, resistenza interna
 *  - Test di capacità con cutoff, relè di stacco carico, log CSV
 *  - Access point Wi-Fi + web-app per il telefono (http://192.168.4.1), grafico persistente, storico test
 *  - Aggiornamento firmware via Wi-Fi (POST /api/update), rifiutato durante un test
 *  - Nessuna libreria esterna: solo il core ESP32 per Arduino
 * =====================================================================
 */
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Wire.h>
#include <Preferences.h>
#include <LittleFS.h>
#include <Update.h>
#include <math.h>
#include "config.h"
#include "web_ui.h"
#include "web_icons.h"

// ===================================================================== dati
struct Config {
  float    vGainAdc = 1.0f, vGainIna = 1.0f, vOff = 0.0f;
  float    iGain = 1.0f, iOff = 0.0f;
  bool     invertI = false;
  float    capAh   = DEF_CAPACITY_AH;
  float    cutoffV = DEF_CUTOFF_V;
  uint16_t logInt  = DEF_LOG_INTERVAL;
  bool     relayEn = false;
  bool     ntcEn   = false;              // NTC collegato su GPIO35
  uint8_t  vSrc    = 0;                 // 0 = partitore ADC, 1 = INA226 VBUS
  float    vHigh = DEF_VHIGH_ALARM, iMax = DEF_IMAX_ALARM, tMax = DEF_TMAX_ALARM;
  char     staSsid[33] = "";
  char     staPass[65] = "";
} cfg;

struct Meas {
  float vAdc = 0, vIna = 0, v = 0, i = 0, p = 0, mvShunt = 0, tC = NAN;
  float mvShuntAvg = 0;          // media mobile lenta (~10 s): un caricabatterie ha ondulazione, l'istantaneo non va bene per tarare
  bool  inaOk = false, hasTemp = false, adcSat = false;
  float ahOut = 0, ahIn = 0, whOut = 0, whIn = 0, socAh = -1;
  float rint = NAN;
} m;

enum TestState : uint8_t { T_IDLE, T_RUN, T_DONE };
struct Test {
  TestState state = T_IDLE;
  uint32_t  tStart = 0, elapsed = 0, lastLog = 0, logN = 0, rintAt = 0;
  float     ah = 0, wh = 0, vstart = 0, vmin = 0, vend = 0, iSum = 0, imax = 0, rint = NAN, vOcv = 0;
  uint32_t  iN = 0, cutoffCnt = 0, noLoadCnt = 0;
  bool      sawLoad = false;
  char      reason[8] = "";
  char      name[24] = "";       // nome batteria (dall'app)
  uint32_t  ts = 0;              // epoch (secondi) inviato dal telefono all'avvio
  uint32_t  id = 0;              // numero progressivo del test
  float     vLast = 0;           // ultima tensione valida (> 5 V) prima di un eventuale stacco
} test;

struct LastTest {
  float ah = 0, wh = 0, vstart = 0, vend = 0, iavg = 0, rint = 0;
  uint32_t dur = 0, ts = 0, id = 0;
  char reason[8] = "";
  char name[24] = "";
} last;

#define HISTORY_FILE   "/history.jsonl"
#define HISTORY_MAX    60     // righe conservate nello storico
#define CSV_KEEP       12     // file CSV conservati (gli altri vengono cancellati)
static char csvPath[16];     // "/t<id>.csv" del test corrente

enum AlarmBit : uint16_t { AL_VLOW = 1, AL_VHIGH = 2, AL_IHIGH = 4, AL_THIGH = 8, AL_INA = 16, AL_RANGE = 32, AL_ADC = 64 };
uint16_t alarms = 0;
bool     relayState = false;

Preferences prefs;
WebServer  server(80);
DNSServer  dns;
static char jbuf[2304];

// ---- storico in RAM per il grafico persistente: lo tiene l'ESP32, cosi' chi apre l'app (telefono o PC)
//      vede tutta la curva delle ultime ore e non solo i punti raccolti da quella pagina
#define TREND_DT   20                       // secondi per punto (media dell'intervallo)
#define TREND_N    1080                     // 1080 x 20 s = 6 ore, copre un test completo
static uint16_t trV[TREND_N];               // tensione in mV
static int16_t  trI[TREND_N];               // corrente in centesimi di ampere
static uint16_t trHead = 0, trCount = 0;
static uint32_t trLastMs = 0;
static bool     otaBlocked = false;

// ===================================================================== NVS
void loadConfig() {
  prefs.begin("tester", true);
  cfg.vGainAdc = prefs.getFloat("vgA", 1.0f);
  cfg.vGainIna = prefs.getFloat("vgI", 1.0f);
  cfg.vOff     = prefs.getFloat("voff", 0.0f);
  cfg.iGain    = prefs.getFloat("ig", 1.0f);
  cfg.iOff     = prefs.getFloat("ioff", 0.0f);
  cfg.invertI  = prefs.getBool("inv", false);
  cfg.capAh    = prefs.getFloat("cap", DEF_CAPACITY_AH);
  cfg.cutoffV  = prefs.getFloat("cut", DEF_CUTOFF_V);
  cfg.logInt   = prefs.getUShort("logint", DEF_LOG_INTERVAL);
  cfg.relayEn  = prefs.getBool("relay", false);
  cfg.ntcEn    = prefs.getBool("ntc", false);
  cfg.vSrc     = prefs.getUChar("vsrc", 0);
  cfg.vHigh    = prefs.getFloat("vhigh", DEF_VHIGH_ALARM);
  cfg.iMax     = prefs.getFloat("imax", DEF_IMAX_ALARM);
  cfg.tMax     = prefs.getFloat("tmax", DEF_TMAX_ALARM);
  prefs.getString("ssid", cfg.staSsid, sizeof(cfg.staSsid));
  prefs.getString("pass", cfg.staPass, sizeof(cfg.staPass));
  prefs.end();
}
void saveConfig() {
  prefs.begin("tester", false);
  prefs.putFloat("vgA", cfg.vGainAdc);  prefs.putFloat("vgI", cfg.vGainIna); prefs.putFloat("voff", cfg.vOff);
  prefs.putFloat("ig", cfg.iGain);      prefs.putFloat("ioff", cfg.iOff);    prefs.putBool("inv", cfg.invertI);
  prefs.putFloat("cap", cfg.capAh);     prefs.putFloat("cut", cfg.cutoffV);  prefs.putUShort("logint", cfg.logInt);
  prefs.putBool("relay", cfg.relayEn);  prefs.putUChar("vsrc", cfg.vSrc);   prefs.putBool("ntc", cfg.ntcEn);
  prefs.putFloat("vhigh", cfg.vHigh);   prefs.putFloat("imax", cfg.iMax);    prefs.putFloat("tmax", cfg.tMax);
  prefs.putString("ssid", cfg.staSsid); prefs.putString("pass", cfg.staPass);
  prefs.end();
}
void loadCounters() {
  prefs.begin("cnt", true);
  m.ahOut = prefs.getFloat("ahOut", 0); m.ahIn = prefs.getFloat("ahIn", 0);
  m.whOut = prefs.getFloat("whOut", 0); m.whIn = prefs.getFloat("whIn", 0);
  m.socAh = prefs.getFloat("socAh", -1);
  prefs.end();
}
void saveCounters() {
  static float lastSoc = -100, lastAh = -100;
  if (fabsf(m.socAh - lastSoc) < 0.01f && fabsf(m.ahOut + m.ahIn - lastAh) < 0.01f) return;
  lastSoc = m.socAh; lastAh = m.ahOut + m.ahIn;
  prefs.begin("cnt", false);
  prefs.putFloat("ahOut", m.ahOut); prefs.putFloat("ahIn", m.ahIn);
  prefs.putFloat("whOut", m.whOut); prefs.putFloat("whIn", m.whIn);
  prefs.putFloat("socAh", m.socAh);
  prefs.end();
}
void loadLast() {
  prefs.begin("last", true);
  last.ah = prefs.getFloat("ah", 0); last.wh = prefs.getFloat("wh", 0); last.dur = prefs.getUInt("dur", 0);
  last.vstart = prefs.getFloat("vs", 0); last.vend = prefs.getFloat("ve", 0);
  last.iavg = prefs.getFloat("ia", 0); last.rint = prefs.getFloat("r", 0);
  last.ts = prefs.getUInt("ts", 0); last.id = prefs.getUInt("id", 0);
  prefs.getString("rs", last.reason, sizeof(last.reason));
  prefs.getString("nm", last.name, sizeof(last.name));
  prefs.end();
}
void saveLast() {
  prefs.begin("last", false);
  prefs.putFloat("ah", last.ah); prefs.putFloat("wh", last.wh); prefs.putUInt("dur", last.dur);
  prefs.putFloat("vs", last.vstart); prefs.putFloat("ve", last.vend);
  prefs.putFloat("ia", last.iavg); prefs.putFloat("r", last.rint); prefs.putString("rs", last.reason);
  prefs.putUInt("ts", last.ts); prefs.putUInt("id", last.id); prefs.putString("nm", last.name);
  prefs.end();
}
uint32_t nextTestId() {
  prefs.begin("last", false);
  uint32_t id = prefs.getUInt("nextid", 1);
  prefs.putUInt("nextid", id + 1);
  prefs.end();
  return id;
}

// ===================================================================== sensore I2C (ADS1115 oppure INA226)
static bool i2cWrite16(uint8_t addr, uint8_t reg, uint16_t val) {
  Wire.beginTransmission(addr);
  Wire.write(reg); Wire.write(val >> 8); Wire.write(val & 0xFF);
  return Wire.endTransmission() == 0;
}
static bool i2cRead16(uint8_t addr, uint8_t reg, uint16_t &val) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(addr, (uint8_t)2) != 2) return false;
  val = ((uint16_t)Wire.read() << 8) | Wire.read();
  return true;
}

#if CURRENT_SENSOR == SENSOR_INA226
bool sensorInit() {
  uint16_t id = 0;
  if (!i2cRead16(INA226_ADDR, 0xFE, id) || id != 0x5449) return false;   // Manufacturer ID "TI"
  i2cWrite16(INA226_ADDR, 0x00, 0x8000);                                  // reset
  delay(2);
  return i2cWrite16(INA226_ADDR, 0x00, INA226_CONFIG);
}
// mvShunt: tensione shunt in mV; vAlt: tensione alternativa grezza (VBUS) in V. false = errore I2C
bool sensorRead(float &mvShunt, float &vAlt, bool &newI, bool &newV) {
  uint16_t raw;
  if (!i2cRead16(INA226_ADDR, 0x01, raw)) return false;
  mvShunt = (int16_t)raw * 0.0025f; newI = true;
  if (i2cRead16(INA226_ADDR, 0x02, raw)) { vAlt = raw * 0.00125f; newV = true; }
  return true;
}
#else
// ADS1115 in single-shot: 3 conversioni A0-A1 (shunt, PGA ±256 mV) poi 1 conversione A2 (partitore, PGA ±4.096 V).
// Funziona anche con un ADS1015 (12 bit): il risultato è allineato a sinistra, la formula resta valida.
static uint8_t adsPhase = 0;
static bool adsStart(uint8_t phase) {
  uint16_t c = 0x8000                                            // OS = avvia conversione
             | (phase == 3 ? (0x6 << 12) | (0x1 << 9)            // A2-GND, ±4.096 V
                           : ((uint16_t)ADS_MUX_SHUNT << 12) | (0x5 << 9))   // coppia shunt (config.h), ±0.256 V
             | 0x0100                                            // single-shot
             | ((uint16_t)ADS1115_DR << 5)
             | 0x0003;                                           // comparatore disabilitato
  return i2cWrite16(ADS1115_ADDR, 0x01, c);
}
bool sensorInit() {
  uint16_t c;
  if (!i2cRead16(ADS1115_ADDR, 0x01, c)) return false;
  adsPhase = 0;
  return adsStart(adsPhase);
}
// mvShunt: tensione shunt in mV; vAlt: tensione batteria grezza da A2 (già moltiplicata per il partitore). false = errore I2C
bool sensorRead(float &mvShunt, float &vAlt, bool &newI, bool &newV) {
  uint16_t c, raw;
  if (!i2cRead16(ADS1115_ADDR, 0x01, c)) return false;
  if (!(c & 0x8000)) return true;                                // conversione ancora in corso
  if (!i2cRead16(ADS1115_ADDR, 0x00, raw)) return false;
  if (adsPhase == 3) { vAlt = (int16_t)raw * 0.000125f * ((R1_OHM + R2_OHM) / R2_OHM); newV = true; }
  else               { mvShunt = (int16_t)raw * 0.0078125f; newI = true; }
  adsPhase = (adsPhase + 1) & 3;
  return adsStart(adsPhase);
}
#endif

// ===================================================================== relè / LED
void setRelay(bool on) {
  relayState = on;
  digitalWrite(PIN_RELAY, (on ^ !RELAY_ACTIVE_HIGH) ? HIGH : LOW);
  digitalWrite(PIN_LED, on ? HIGH : LOW);          // LED acceso = relè chiuso (carico collegato)
  Serial.printf("[RELE] %s\n", on ? "CHIUSO (carico ON)" : "APERTO (carico OFF)");
}

// ===================================================================== SOC da OCV (LiFePO4, a riposo)
float ocvToSocPct(float vCell) {
  static const float tv[] = {2.90f, 3.00f, 3.13f, 3.20f, 3.22f, 3.25f, 3.26f, 3.27f, 3.30f, 3.32f, 3.35f, 3.40f};
  static const float ts[] = {0, 2, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100};
  const int n = sizeof(tv) / sizeof(tv[0]);
  if (vCell <= tv[0]) return 0;
  if (vCell >= tv[n - 1]) return 100;
  for (int k = 0; k < n - 1; k++)
    if (vCell < tv[k + 1]) return ts[k] + (ts[k + 1] - ts[k]) * (vCell - tv[k]) / (tv[k + 1] - tv[k]);
  return 100;
}
void clampSoc() {
  if (m.socAh < 0) m.socAh = 0;
  if (m.socAh > cfg.capAh) m.socAh = cfg.capAh;
}

// ===================================================================== JSON helpers
static void jsonEscape(const char *in, char *out, size_t n) {
  size_t o = 0;
  for (; *in && o + 2 < n; in++) {
    if (*in == '"' || *in == '\\') out[o++] = '\\';
    if ((uint8_t)*in < 0x20) continue;
    out[o++] = *in;
  }
  out[o] = 0;
}
static void fnum(char *b, size_t n, float v, int dec) {
  if (isnan(v) || isinf(v)) strncpy(b, "null", n); else snprintf(b, n, "%.*f", dec, v);
}

// ===================================================================== storico test
void historyAppend() {
  char nm[48];
  jsonEscape(last.name, nm, sizeof(nm));
  File f = LittleFS.open(HISTORY_FILE, FILE_APPEND);
  if (!f) return;
  f.printf("{\"id\":%lu,\"name\":\"%s\",\"ts\":%lu,\"reason\":\"%s\",\"ah\":%.3f,\"wh\":%.1f,\"dur\":%lu,"
           "\"vstart\":%.3f,\"vend\":%.3f,\"iavg\":%.3f,\"rint\":%.5f}\n",
           (unsigned long)last.id, nm, (unsigned long)last.ts, last.reason, last.ah, last.wh,
           (unsigned long)last.dur, last.vstart, last.vend, last.iavg, last.rint);
  f.close();
}
// tiene al massimo HISTORY_MAX righe (riscrive il file scartando le più vecchie)
void historyTrim() {
  File f = LittleFS.open(HISTORY_FILE, FILE_READ);
  if (!f) return;
  uint16_t n = 0;
  while (f.available()) if (f.read() == '\n') n++;
  if (n <= HISTORY_MAX) { f.close(); return; }
  f.seek(0);
  File t = LittleFS.open("/history.tmp", FILE_WRITE);
  uint16_t skip = n - HISTORY_MAX, k = 0;
  while (f.available()) { String l = f.readStringUntil('\n'); if (k++ >= skip && l.length()) t.println(l); }
  f.close(); t.close();
  LittleFS.remove(HISTORY_FILE);
  LittleFS.rename("/history.tmp", HISTORY_FILE);
}

// ===================================================================== log CSV
void logLine() {
  File f = LittleFS.open(csvPath, FILE_APPEND);
  if (!f) return;
  char tb[12];
  if (m.hasTemp) snprintf(tb, sizeof(tb), "%.1f", m.tC); else strcpy(tb, "");
  f.printf("%lu,%.3f,%.3f,%.1f,%.4f,%.2f,%s\n", (unsigned long)test.elapsed, m.v, m.i, m.p, test.ah, test.wh, tb);
  f.close();
  test.logN++;
  test.lastLog = millis();
}

// ===================================================================== test di capacità
void testStart(const char *name, uint32_t ts) {
  test = Test();
  test.state  = T_RUN;
  test.tStart = millis();
  test.vstart = test.vmin = test.vOcv = test.vLast = m.v;
  test.ts = ts;
  test.id = nextTestId();
  strncpy(test.name, name, sizeof(test.name) - 1);
  m.ahOut = m.ahIn = m.whOut = m.whIn = 0;
  snprintf(csvPath, sizeof(csvPath), "/t%lu.csv", (unsigned long)test.id);
  if (test.id > CSV_KEEP) { char old[16]; snprintf(old, sizeof(old), "/t%lu.csv", (unsigned long)(test.id - CSV_KEEP)); LittleFS.remove(old); }
  LittleFS.remove(csvPath);
  File f = LittleFS.open(csvPath, FILE_WRITE);
  if (f) { f.printf("# test %lu, batteria: %s, ts: %lu\n", (unsigned long)test.id, test.name, (unsigned long)ts); f.println("t_s,V,I_A,P_W,Ah,Wh,T_C"); f.close(); }
  if (cfg.relayEn) {
    setRelay(true);
    test.rintAt = millis() + 2000;   // misura R interna 2 s dopo la chiusura del relè
  }
  logLine();
  Serial.printf("[TEST] #%lu '%s' avviato\n", (unsigned long)test.id, test.name);
}
void testEnd(const char *reason) {
  if (cfg.relayEn) setRelay(false);
  test.state = T_DONE;
  test.vend  = (strcmp(reason, "bms") == 0) ? test.vLast : m.v;   // per lo stacco BMS conta la tensione prima del crollo
  strncpy(test.reason, reason, sizeof(test.reason) - 1);
  logLine();
  last.ah = test.ah; last.wh = test.wh; last.dur = test.elapsed;
  last.vstart = test.vstart; last.vend = test.vend;
  last.iavg = test.iN ? test.iSum / test.iN : 0;
  last.rint = isnan(test.rint) ? (isnan(m.rint) ? 0 : m.rint) : test.rint;
  last.ts = test.ts; last.id = test.id;
  strncpy(last.reason, reason, sizeof(last.reason) - 1);
  strncpy(last.name, test.name, sizeof(last.name) - 1);
  saveLast();
  historyAppend();
  historyTrim();
  if (strcmp(reason, "cutoff") == 0) m.socAh = 0;    // cutoff raggiunto = batteria vuota
  saveCounters();
  Serial.printf("[TEST] terminato (%s): %.2f Ah, %.0f Wh in %lu s\n", reason, test.ah, test.wh, (unsigned long)test.elapsed);
}

// ===================================================================== campionamento (ogni SAMPLE_MS)
void sample() {
  static uint32_t lastUs = 0;
  static uint8_t  inaFail = 0;
  static uint32_t inaRetry = 0;
  uint32_t nowUs = micros();
  float dt = lastUs ? (nowUs - lastUs) / 1e6f : 0.0f;
  lastUs = nowUs;

  // --- tensione dal partitore ---
  uint32_t acc = 0;
  for (int k = 0; k < ADC_SAMPLES; k++) acc += analogReadMilliVolts(PIN_ADC_VBAT);
  float mv = acc / (float)ADC_SAMPLES;
  m.adcSat = mv > 3050.0f;
  float vRawAdc = (mv < 160.0f) ? 0.0f : mv / 1000.0f * ((R1_OHM + R2_OHM) / R2_OHM) * cfg.vGainAdc + cfg.vOff;
  // filtro anti-rumore; i salti veri (collegamento batteria, stacco BMS) non vengono rallentati
  static float vFilt = -1.0f;
  if (vFilt < 0 || fabsf(vRawAdc - vFilt) > V_FILTER_STEP) vFilt = vRawAdc;
  else vFilt += V_FILTER_ALPHA * (vRawAdc - vFilt);
  m.vAdc = vFilt;

  // --- sensore I2C: shunt + tensione alternativa ---
  if (!m.inaOk && millis() - inaRetry > 5000) { inaRetry = millis(); m.inaOk = sensorInit(); inaFail = 0; }
  if (m.inaOk) {
    bool newI = false, newV = false;
    float mv = m.mvShunt, va = 0;
    if (sensorRead(mv, va, newI, newV)) {
      inaFail = 0;
      if (newI) {
        m.mvShunt = mv;
        if (fabsf(mv - m.mvShuntAvg) > 3.0f) m.mvShuntAvg = mv;          // gradino vero (>4 A): riparte subito
        else m.mvShuntAvg += 0.013f * (mv - m.mvShuntAvg);                // ~7,5 campioni/s -> costante di tempo ~10 s
      }
      if (newV) m.vIna = va * cfg.vGainIna;
    } else if (++inaFail > 10) { m.inaOk = false; m.mvShunt = 0; }
  }
  // oltre 250 mV l'ADC e' a fondo scala: ingresso scollegato o invertito, non una corrente reale
  bool shuntSat = fabsf(m.mvShunt) > 250.0f;
  float iCal = shuntSat ? 0.0f : m.mvShunt * (SHUNT_RATED_A / SHUNT_RATED_MV) * cfg.iGain + cfg.iOff;
  m.i = cfg.invertI ? -iCal : iCal;
  m.v = (cfg.vSrc == 1 && m.inaOk) ? m.vIna : m.vAdc;
  m.p = m.v * m.i;

  // --- integrazione Ah / Wh ---
  if (dt > 0 && dt < 1.0f && m.inaOk) {
    float ah = m.i * dt / 3600.0f, wh = m.p * dt / 3600.0f;
    if (m.i >= 0) { m.ahOut += ah; m.whOut += wh; } else { m.ahIn -= ah; m.whIn -= wh; }
    if (m.socAh >= 0) { m.socAh -= ah; clampSoc(); }
    if (test.state == T_RUN && m.i > 0) { test.ah += ah; test.wh += wh; }
  }
}

// ===================================================================== tick 1 s
void tick() {
  static float    vPrev = 0, iPrev = 0;
  static uint32_t restMs = 0;
  static bool     restCorrected = false;
  static uint8_t  sec = 0;

  // --- storico per il grafico: media su TREND_DT secondi ---
  static float trSumV = 0, trSumI = 0; static uint8_t trK = 0;
  trSumV += m.v; trSumI += m.i;
  if (++trK >= TREND_DT) {
    float av = trSumV / trK, ai = trSumI / trK;
    trV[trHead] = (uint16_t)constrain(lroundf(av * 1000.0f), 0L, 65535L);
    trI[trHead] = (int16_t)constrain(lroundf(ai * 100.0f), -32768L, 32767L);
    trHead = (trHead + 1) % TREND_N; if (trCount < TREND_N) trCount++;
    trLastMs = millis(); trSumV = trSumI = 0; trK = 0;
  }

  // --- temperatura NTC ---
  uint32_t tacc = 0;
  for (int k = 0; k < 8; k++) tacc += analogReadMilliVolts(PIN_ADC_NTC);
  float tmv = tacc / 8.0f;
  if (!cfg.ntcEn || tmv > 3100.0f || tmv < 50.0f) { m.hasTemp = false; m.tC = NAN; }   // pin libero = lettura casuale: solo se abilitato
  else {
    float r = NTC_PULLUP_OHM * tmv / (3300.0f - tmv);
    m.tC = 1.0f / (1.0f / 298.15f + logf(r / NTC_R25_OHM) / NTC_BETA) - 273.15f;
    m.hasTemp = true;
  }

  // --- SOC iniziale / correzione a riposo da OCV ---
  if (m.socAh < 0 && m.v > 15.0f) m.socAh = ocvToSocPct(m.v / CELLS_SERIES) * cfg.capAh / 100.0f;
  if (fabsf(m.i) < REST_CURRENT_A) restMs += 1000; else { restMs = 0; restCorrected = false; }
  if (restMs >= REST_TIME_MS && !restCorrected) {
    float pct = ocvToSocPct(m.v / CELLS_SERIES);
    if (pct >= 95.0f || pct <= 15.0f) { m.socAh = pct * cfg.capAh / 100.0f; restCorrected = true; }
  }
  if (m.v / CELLS_SERIES > 3.55f && m.i < 0 && -m.i < cfg.capAh * 0.02f) m.socAh = cfg.capAh;   // fine carica (C/50)

  // --- resistenza interna da gradini di carico ---
  float di = m.i - iPrev;
  if (fabsf(di) > 3.0f && vPrev > 10.0f) {
    float r = fabsf((m.v - vPrev) / di);
    if (r > 0.0005f && r < 0.5f) m.rint = isnan(m.rint) ? r : 0.7f * m.rint + 0.3f * r;
  }
  vPrev = m.v; iPrev = m.i;

  // --- allarmi ---
  alarms = 0;
  if (m.v < cfg.cutoffV && m.v > 5.0f) alarms |= AL_VLOW;
  if (m.v > cfg.vHigh)                  alarms |= AL_VHIGH;
  if (fabsf(m.i) > cfg.iMax)            alarms |= AL_IHIGH;
  if (m.hasTemp && m.tC > cfg.tMax)     alarms |= AL_THIGH;
  if (!m.inaOk)                         alarms |= AL_INA;
  if (fabsf(m.mvShunt) > 81.0f)         alarms |= AL_RANGE;
  if (m.adcSat)                         alarms |= AL_ADC;

  // --- test ---
  if (test.state == T_RUN) {
    test.elapsed = (millis() - test.tStart) / 1000;
    if (m.v < test.vmin) test.vmin = m.v;
    if (m.i > 0.2f) { test.iSum += m.i; test.iN++; test.sawLoad = true; test.noLoadCnt = 0; }
    else if (test.sawLoad) test.noLoadCnt++;
    if (m.i > test.imax) test.imax = m.i;
    if (test.rintAt && millis() >= test.rintAt) {
      if (m.i > 1.0f) { float r = (test.vOcv - m.v) / m.i; if (r > 0 && r < 0.5f) test.rint = r; }
      test.rintAt = 0;
    }
    if (m.v > 5.0f) test.vLast = m.v;
    bool bmsTrip = (m.v < 5.0f && test.vLast > cfg.cutoffV + 0.5f && fabsf(m.i) < 0.5f && test.elapsed > 5);
    if (m.v <= cfg.cutoffV) test.cutoffCnt++; else test.cutoffCnt = 0;
    if (bmsTrip)                    testEnd("bms");      // tensione crollata a zero da sopra il cutoff: ha staccato il BMS
    else if (test.cutoffCnt >= 3)   testEnd("cutoff");
    else if (test.noLoadCnt >= 120) testEnd("noload");
    else if (millis() - test.lastLog >= (uint32_t)cfg.logInt * 1000UL) logLine();
    if (alarms & (AL_VHIGH | AL_IHIGH | AL_THIGH)) { if (cfg.relayEn) setRelay(false); }
  } else if (alarms & (AL_VHIGH | AL_IHIGH | AL_THIGH)) {
    if (cfg.relayEn && relayState) setRelay(false);
  }

  sec++;

  if (sec % 5 == 0)
    Serial.printf("V=%.3f I=%.3f P=%.1f SOC=%.1f%% Ah_out=%.3f Ah_in=%.3f T=%.1f INA=%d test=%d alarm=0x%02X\n",
                  m.v, m.i, m.p, m.socAh / cfg.capAh * 100.0f, m.ahOut, m.ahIn, m.tC, m.inaOk, test.state, alarms);
}

// ===================================================================== HTTP handlers
void sendNoCache() { server.sendHeader("Cache-Control", "no-store"); }

void handleStatus() {
  const char *st = test.state == T_RUN ? "RUN" : (test.state == T_DONE ? "DONE" : "IDLE");
  char tb[12], rb[12], trb[12], lrb[12], ssid[80], tnm[48], lnm[48];
  jsonEscape(test.name, tnm, sizeof(tnm));
  jsonEscape(last.name, lnm, sizeof(lnm));
  fnum(tb, 12, m.hasTemp ? m.tC : NAN, 1);
  fnum(rb, 12, m.rint, 5);
  fnum(trb, 12, test.rint, 5);
  fnum(lrb, 12, last.rint > 0 ? last.rint : NAN, 5);
  jsonEscape(cfg.staSsid, ssid, sizeof(ssid));
  char al[96] = ""; int ao = 0;
  const char *names[] = {"vlow", "vhigh", "ihigh", "thigh", "ina", "range", "adc"};
  for (int k = 0; k < 7; k++)
    if (alarms & (1 << k)) ao += snprintf(al + ao, sizeof(al) - ao, "%s\"%s\"", ao ? "," : "", names[k]);
  bool sta = WiFi.status() == WL_CONNECTED;
  float soc = m.socAh < 0 ? 0 : m.socAh / cfg.capAh * 100.0f;

  snprintf(jbuf, sizeof(jbuf),
    "{\"fw\":\"%s\",\"sensor\":\"%s\",\"up\":%lu,\"v\":%.3f,\"vadc\":%.3f,\"vina\":%.3f,\"i\":%.3f,\"p\":%.1f,\"mvsh\":%.3f,\"i_med\":%.3f,\"t\":%s,"
    "\"soc\":%.1f,\"ah_rem\":%.2f,\"cap\":%.1f,\"ah_out\":%.3f,\"ah_in\":%.3f,\"wh_out\":%.1f,\"wh_in\":%.1f,"
    "\"rint\":%s,\"relay\":%d,\"ina_ok\":%d,\"alarm\":[%s],"
    "\"test\":{\"state\":\"%s\",\"name\":\"%s\",\"id\":%lu,\"ts\":%lu,\"reason\":\"%s\",\"elapsed\":%lu,\"ah\":%.4f,\"wh\":%.2f,\"vstart\":%.3f,\"vmin\":%.3f,"
    "\"iavg\":%.3f,\"imax\":%.2f,\"rint\":%s,\"cutoff\":%.2f,\"logn\":%lu},"
    "\"last\":{\"name\":\"%s\",\"id\":%lu,\"ts\":%lu,\"reason\":\"%s\",\"ah\":%.3f,\"wh\":%.1f,\"dur\":%lu,\"iavg\":%.3f,\"vstart\":%.3f,\"vend\":%.3f,\"rint\":%s},"
    "\"wifi\":{\"ap_ip\":\"%s\",\"sta_ip\":\"%s\",\"ssid\":\"%s\",\"rssi\":%d}}",
    FW_VERSION, SENSOR_NAME, (unsigned long)(millis() / 1000), m.v, m.vAdc, m.vIna, m.i, m.p, m.mvShunt,
    (cfg.invertI ? -1.0f : 1.0f) * (m.mvShuntAvg * (SHUNT_RATED_A / SHUNT_RATED_MV) * cfg.iGain + cfg.iOff), tb,
    soc, m.socAh < 0 ? 0 : m.socAh, cfg.capAh, m.ahOut, m.ahIn, m.whOut, m.whIn,
    rb, relayState, m.inaOk, al,
    st, tnm, (unsigned long)test.id, (unsigned long)test.ts, test.reason, (unsigned long)test.elapsed, test.ah, test.wh, test.vstart, test.vmin,
    test.iN ? test.iSum / test.iN : 0.0f, test.imax, trb, cfg.cutoffV, (unsigned long)test.logN,
    lnm, (unsigned long)last.id, (unsigned long)last.ts, last.reason, last.ah, last.wh, (unsigned long)last.dur, last.iavg, last.vstart, last.vend, lrb,
    WiFi.softAPIP().toString().c_str(), sta ? WiFi.localIP().toString().c_str() : "", ssid, sta ? WiFi.RSSI() : 0);
  sendNoCache();
  server.send(200, "application/json", jbuf);
}

void handleConfigGet() {
  char ssid[80];
  jsonEscape(cfg.staSsid, ssid, sizeof(ssid));
  snprintf(jbuf, sizeof(jbuf),
    "{\"vgain\":%.5f,\"voff\":%.4f,\"igain\":%.5f,\"ioff\":%.4f,\"inv\":%d,\"cap\":%.1f,\"cutoff\":%.2f,\"logint\":%u,"
    "\"relay_en\":%d,\"ntc_en\":%d,\"vsrc\":%u,\"vhigh\":%.2f,\"imax\":%.1f,\"tmax\":%.1f,\"ssid\":\"%s\",\"cells\":%d}",
    cfg.vSrc == 1 ? cfg.vGainIna : cfg.vGainAdc, cfg.vOff, cfg.iGain, cfg.iOff, cfg.invertI, cfg.capAh, cfg.cutoffV,
    cfg.logInt, cfg.relayEn, cfg.ntcEn, cfg.vSrc, cfg.vHigh, cfg.iMax, cfg.tMax, ssid, CELLS_SERIES);
  sendNoCache();
  server.send(200, "application/json", jbuf);
}

static float argF(const char *name, float cur, float lo, float hi) {
  if (!server.hasArg(name)) return cur;
  float v = server.arg(name).toFloat();
  return (v >= lo && v <= hi) ? v : cur;
}
void handleConfigPost() {
  cfg.capAh   = argF("cap", cfg.capAh, 1, 5000);
  cfg.cutoffV = argF("cutoff", cfg.cutoffV, 10, 30);
  cfg.logInt  = (uint16_t)argF("logint", cfg.logInt, 1, 3600);
  cfg.vHigh   = argF("vhigh", cfg.vHigh, 15, 40);
  cfg.iMax    = argF("imax", cfg.iMax, 1, 500);
  cfg.tMax    = argF("tmax", cfg.tMax, 20, 120);
  if (server.hasArg("relay_en")) cfg.relayEn = server.arg("relay_en").toInt() != 0;
  if (server.hasArg("ntc_en"))   cfg.ntcEn = server.arg("ntc_en").toInt() != 0;
  if (server.hasArg("inv"))      cfg.invertI = server.arg("inv").toInt() != 0;
  if (server.hasArg("vsrc"))     cfg.vSrc = server.arg("vsrc").toInt() ? 1 : 0;
  if (server.hasArg("voff"))     cfg.vOff = argF("voff", cfg.vOff, -5, 5);
  clampSoc();
  saveConfig();
  server.send(200, "text/plain", "OK");
}

void handleCal() {
  String type = server.arg("type");
  float actual = server.arg("actual").toFloat();
  if (type == "vgain") {
    if (actual <= 0) { server.send(400, "text/plain", "valore non valido"); return; }
    if (cfg.vSrc == 1 && m.inaOk) { float raw = m.vIna / cfg.vGainIna; if (raw > 1) cfg.vGainIna = actual / raw; }
    else { float raw = (m.vAdc - cfg.vOff) / cfg.vGainAdc; if (raw > 1) cfg.vGainAdc = actual / raw; }
  } else if (type == "izero") {
    float raw = m.mvShuntAvg * (SHUNT_RATED_A / SHUNT_RATED_MV) * cfg.iGain;
    if (fabsf(raw) > 1.0f) { server.send(400, "text/plain", "c'e' corrente nello shunt: azzera solo a carico e caricabatterie staccati"); return; }
    cfg.iOff = -raw;
  } else if (type == "igain") {
    float raw = m.mvShuntAvg * (SHUNT_RATED_A / SHUNT_RATED_MV);      // media ~10 s, non l'istantaneo
    if (fabsf(raw) < 1.0f || actual == 0) { server.send(400, "text/plain", "corrente troppo bassa per tarare (serve almeno 1 A)"); return; }
    float wanted = copysignf(fabsf(actual), raw);                       // vale sia in carica sia in scarica, qualunque segno si scriva
    float g = (wanted - cfg.iOff) / raw;
    if (g < 0.8f || g > 1.25f) { server.send(400, "text/plain", "valore incoerente con la lettura (guadagno fuori da 0.8-1.25)"); return; }
    cfg.iGain = g;
  } else if (type == "set") {
    // impostazione esplicita di valori calcolati all'esterno su una media lunga
    if (server.hasArg("igain")) { float g = server.arg("igain").toFloat(); if (g < 0.8f || g > 1.25f) { server.send(400, "text/plain", "igain fuori da 0.8-1.25"); return; } cfg.iGain = g; }
    if (server.hasArg("ioff"))  { float o = server.arg("ioff").toFloat();  if (fabsf(o) > 2.0f)       { server.send(400, "text/plain", "ioff oltre 2 A"); return; } cfg.iOff = o; }
  } else if (type == "reset") {
    cfg.vGainAdc = cfg.vGainIna = cfg.iGain = 1.0f; cfg.vOff = cfg.iOff = 0.0f;
  } else { server.send(400, "text/plain", "tipo sconosciuto"); return; }
  saveConfig();
  server.send(200, "text/plain", "OK");
}

void handleTest() {
  String c = server.arg("cmd");
  if (c == "start") {
    if (test.state == T_RUN) { server.send(409, "text/plain", "test già in corso"); return; }
    String nm = server.arg("name"); nm.trim();
    if (!nm.length()) nm = "batteria";
    testStart(nm.c_str(), (uint32_t)server.arg("ts").toInt());
  }
  else if (c == "stop") { if (test.state == T_RUN) testEnd("manual"); }
  else if (c == "reset") { if (test.state != T_RUN) test = Test(); }
  else { server.send(400, "text/plain", "cmd?"); return; }
  server.send(200, "text/plain", "OK");
}
void handleSoc() {
  float pct = server.arg("value").toFloat();
  if (pct < 0 || pct > 100) { server.send(400, "text/plain", "0-100"); return; }
  m.socAh = pct * cfg.capAh / 100.0f;
  saveCounters();
  server.send(200, "text/plain", "OK");
}
void handleCounters() {
  if (server.arg("cmd") == "reset") { m.ahOut = m.ahIn = m.whOut = m.whIn = 0; saveCounters(); }
  server.send(200, "text/plain", "OK");
}
void handleRelay() {
  setRelay(server.arg("state").toInt() != 0);
  server.send(200, "text/plain", relayState ? "ON" : "OFF");
}
void handleLogGet() {
  uint32_t id = server.hasArg("id") ? (uint32_t)server.arg("id").toInt() : (test.id ? test.id : last.id);
  char path[16], fn[40];
  snprintf(path, sizeof(path), "/t%lu.csv", (unsigned long)id);
  File f = LittleFS.open(path, FILE_READ);
  if (!f) { server.send(404, "text/plain", "nessun log per questo test"); return; }
  snprintf(fn, sizeof(fn), "attachment; filename=\"test_%lu.csv\"", (unsigned long)id);
  server.sendHeader("Content-Disposition", fn);
  server.streamFile(f, "text/csv");
  f.close();
}
void handleLogPost() {
  if (server.arg("cmd") == "clear" && test.state != T_RUN) {
    File root = LittleFS.open("/");
    File e;
    String names[CSV_KEEP + 4]; int n = 0;
    while ((e = root.openNextFile()) && n < CSV_KEEP + 4) { String nm = e.name(); if (nm.startsWith("t") && nm.endsWith(".csv")) names[n++] = "/" + nm; else if (nm.startsWith("/t") && nm.endsWith(".csv")) names[n++] = nm; }
    for (int k = 0; k < n; k++) LittleFS.remove(names[k]);
  }
  server.send(200, "text/plain", "OK");
}
void handleHistoryGet() {
  File f = LittleFS.open(HISTORY_FILE, FILE_READ);
  String out = "[";
  if (f) {
    bool first = true;
    while (f.available()) {
      String l = f.readStringUntil('\n'); l.trim();
      if (!l.length()) continue;
      if (!first) out += ',';
      out += l; first = false;
    }
    f.close();
  }
  out += "]";
  sendNoCache();
  server.send(200, "application/json", out);
}
// elimina un solo test dallo storico (riga JSON con quell'id) e il suo CSV
bool historyDelete(uint32_t id) {
  File f = LittleFS.open(HISTORY_FILE, FILE_READ);
  if (!f) return false;
  char key[24]; snprintf(key, sizeof(key), "{\"id\":%lu,", (unsigned long)id);
  File t = LittleFS.open("/history.tmp", FILE_WRITE);
  bool found = false;
  while (f.available()) {
    String l = f.readStringUntil('\n'); l.trim();
    if (!l.length()) continue;
    if (l.startsWith(key)) { found = true; continue; }
    t.println(l);
  }
  f.close(); t.close();
  LittleFS.remove(HISTORY_FILE);
  LittleFS.rename("/history.tmp", HISTORY_FILE);
  char path[16]; snprintf(path, sizeof(path), "/t%lu.csv", (unsigned long)id);
  LittleFS.remove(path);
  return found;
}
void handleHistoryPost() {
  String c = server.arg("cmd");
  if (test.state == T_RUN) { server.send(409, "text/plain", "test in corso"); return; }
  if (c == "clear") LittleFS.remove(HISTORY_FILE);
  else if (c == "delete") {
    uint32_t id = (uint32_t)server.arg("id").toInt();
    if (!id) { server.send(400, "text/plain", "id?"); return; }
    if (!historyDelete(id)) { server.send(404, "text/plain", "test non trovato"); return; }
    Serial.printf("[STORICO] test #%lu eliminato\n", (unsigned long)id);
  }
  server.send(200, "text/plain", "OK");
}
void handleWifi() {
  strncpy(cfg.staSsid, server.arg("ssid").c_str(), sizeof(cfg.staSsid) - 1);
  strncpy(cfg.staPass, server.arg("pass").c_str(), sizeof(cfg.staPass) - 1);
  saveConfig();
  saveCounters();
  server.send(200, "text/plain", "OK, riavvio");
  delay(500);
  ESP.restart();
}
void handleReboot() {
  saveCounters();
  server.send(200, "text/plain", "riavvio");
  delay(300);
  ESP.restart();
}

// storico per il grafico, inviato a blocchi per non occupare memoria
void handleTrend() {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  sendNoCache();
  server.send(200, "application/json", "");
  char b[128];
  snprintf(b, sizeof(b), "{\"dt\":%d,\"n\":%u,\"age\":%lu,\"tstart\":%ld,\"v\":[", TREND_DT, trCount,
           (unsigned long)(trCount ? (millis() - trLastMs) / 1000 : 0), test.state == T_RUN ? (long)test.elapsed : -1L);
  server.sendContent(b);
  String chunk; chunk.reserve(1300);
  for (int pass = 0; pass < 2; pass++) {
    for (uint16_t k = 0; k < trCount; k++) {
      uint16_t idx = (trHead + TREND_N - trCount + k) % TREND_N;
      if (pass == 0) chunk += String(trV[idx]); else chunk += String(trI[idx]);
      if (k + 1 < trCount) chunk += ',';
      if (chunk.length() > 1200) { server.sendContent(chunk); chunk = ""; }
    }
    chunk += (pass == 0) ? "],\"i\":[" : "]}";
  }
  server.sendContent(chunk);
  server.sendContent("");
}

// aggiornamento firmware via Wi-Fi:  curl -F "firmware=@firmware.bin" http://<ip>/api/update
void handleUpdateDone() {
  if (otaBlocked) { server.send(409, "text/plain", "test in corso: aggiornamento rifiutato"); return; }
  bool ok = !Update.hasError();
  server.send(ok ? 200 : 500, "text/plain", ok ? "OK, riavvio" : "ERRORE aggiornamento");
  if (ok) { saveCounters(); delay(600); ESP.restart(); }
}
void handleUpdateData() {
  HTTPUpload &up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    otaBlocked = (test.state == T_RUN);                     // mai durante un test: il riavvio lo butterebbe via
    if (!otaBlocked) { Serial.printf("[OTA] ricevo %s\n", up.filename.c_str()); Update.begin(UPDATE_SIZE_UNKNOWN); }
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (!otaBlocked) Update.write(up.buf, up.currentSize);
  } else if (up.status == UPLOAD_FILE_END) {
    if (!otaBlocked) { bool ok = Update.end(true); Serial.printf("[OTA] %s, %u byte\n", ok ? "completato" : "FALLITO", up.totalSize); }
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    if (!otaBlocked) Update.abort();
  }
}

void webSetup() {
  server.enableCORS(true);
  server.on("/", HTTP_GET, []() { server.send_P(200, "text/html", WEB_INDEX); });
  server.on("/index.html", HTTP_GET, []() { server.send_P(200, "text/html", WEB_INDEX); });
  server.on("/manifest.json", HTTP_GET, []() { server.send_P(200, "application/manifest+json", WEB_MANIFEST); });
  server.on("/sw.js", HTTP_GET, []() { server.send_P(200, "application/javascript", WEB_SW); });
  server.on("/icon-192.png", HTTP_GET, []() { server.send_P(200, "image/png", (PGM_P)ICON_192_PNG, sizeof(ICON_192_PNG)); });
  server.on("/icon-512.png", HTTP_GET, []() { server.send_P(200, "image/png", (PGM_P)ICON_512_PNG, sizeof(ICON_512_PNG)); });
  server.on("/api/status",   HTTP_GET,  handleStatus);
  server.on("/api/config",   HTTP_GET,  handleConfigGet);
  server.on("/api/config",   HTTP_POST, handleConfigPost);
  server.on("/api/cal",      HTTP_POST, handleCal);
  server.on("/api/test",     HTTP_POST, handleTest);
  server.on("/api/soc",      HTTP_POST, handleSoc);
  server.on("/api/counters", HTTP_POST, handleCounters);
  server.on("/api/relay",    HTTP_POST, handleRelay);
  server.on("/api/log",      HTTP_GET,  handleLogGet);
  server.on("/api/log",      HTTP_POST, handleLogPost);
  server.on("/api/history",  HTTP_GET,  handleHistoryGet);
  server.on("/api/history",  HTTP_POST, handleHistoryPost);
  server.on("/api/wifi",     HTTP_POST, handleWifi);
  server.on("/api/reboot",   HTTP_POST, handleReboot);
  server.on("/api/trend",    HTTP_GET,  handleTrend);
  server.on("/api/update",   HTTP_POST, handleUpdateDone, handleUpdateData);
  // Android crede di avere internet -> usa il Wi-Fi del tester come rete predefinita
  server.on("/generate_204", []() { server.send(204, "text/plain", ""); });
  server.on("/gen_204",      []() { server.send(204, "text/plain", ""); });
  server.on("/connecttest.txt", []() { server.send(200, "text/plain", "Microsoft Connect Test"); });
  server.on("/hotspot-detect.html", []() { server.send(200, "text/html", "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>"); });
  server.onNotFound([]() {
    if (server.method() == HTTP_OPTIONS) { server.send(204); return; }
    server.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/", true);
    server.send(302, "text/plain", "");
  });
  server.begin();
}

// ===================================================================== Wi-Fi
void wifiSetup() {
  WiFi.persistent(false);
  WiFi.mode(cfg.staSsid[0] ? WIFI_AP_STA : WIFI_AP);
  WiFi.setSleep(false);
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
  WiFi.softAP(AP_SSID, AP_PASS);
  if (cfg.staSsid[0]) {
    WiFi.setAutoReconnect(true);
    WiFi.begin(cfg.staSsid, cfg.staPass);
    Serial.printf("[WIFI] connessione a '%s'...\n", cfg.staSsid);
  }
  dns.start(53, "*", WiFi.softAPIP());
  if (MDNS.begin(MDNS_NAME)) MDNS.addService("http", "tcp", 80);
  Serial.printf("[WIFI] AP '%s' pass '%s' -> http://%s\n", AP_SSID, AP_PASS, WiFi.softAPIP().toString().c_str());
}
void wifiMaintain() {
  static uint32_t lastTry = 0;
  static bool wasConn = false;
  bool conn = WiFi.status() == WL_CONNECTED;
  if (conn && !wasConn) Serial.printf("[WIFI] connesso, IP casa: http://%s\n", WiFi.localIP().toString().c_str());
  wasConn = conn;
  if (cfg.staSsid[0] && !conn && millis() - lastTry > 60000) { lastTry = millis(); WiFi.begin(cfg.staSsid, cfg.staPass); }
}

// ===================================================================== comandi da porta seriale (115200)
//   wifi <ssid>|<password>   collega il tester alla rete di casa (riavvia)
//   wifi off                 dimentica la rete di casa (riavvia)
//   ip                       mostra gli indirizzi
//   relay 0|1                relè manuale
//   reboot
void serialCommands() {
  static char buf[120]; static uint8_t n = 0;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r') continue;
    if (c != '\n') { if (n < sizeof(buf) - 1) buf[n++] = c; continue; }
    buf[n] = 0; n = 0;
    String line = String(buf); line.trim();
    if (line.startsWith("wifi ")) {
      String arg = line.substring(5); arg.trim();
      if (arg == "off") { cfg.staSsid[0] = 0; cfg.staPass[0] = 0; }
      else {
        int sep = arg.indexOf('|');
        if (sep < 1) { Serial.println("[CMD] uso: wifi <ssid>|<password>"); continue; }
        strncpy(cfg.staSsid, arg.substring(0, sep).c_str(), sizeof(cfg.staSsid) - 1); cfg.staSsid[sizeof(cfg.staSsid) - 1] = 0;
        strncpy(cfg.staPass, arg.substring(sep + 1).c_str(), sizeof(cfg.staPass) - 1); cfg.staPass[sizeof(cfg.staPass) - 1] = 0;
      }
      saveConfig(); saveCounters();
      Serial.printf("[CMD] rete di casa '%s' salvata, riavvio...\n", cfg.staSsid);
      delay(300); ESP.restart();
    } else if (line == "ip") {
      Serial.printf("[CMD] AP http://%s  casa: %s http://%s (rssi %d)\n", WiFi.softAPIP().toString().c_str(),
                    WiFi.status() == WL_CONNECTED ? "connesso" : "non connesso", WiFi.localIP().toString().c_str(), WiFi.RSSI());
    } else if (line.startsWith("relay ")) {
      setRelay(line.substring(6).toInt() != 0);
    } else if (line == "i2c") {
      // scansione del bus: dice se l'ADS1115 c'e' e a quale indirizzo (0x48 ADDR->GND, 0x49 VDD, 0x4A SDA, 0x4B SCL)
      int n = 0;
      Serial.printf("[I2C] scansione SDA=GPIO%d SCL=GPIO%d ...\n", PIN_SDA, PIN_SCL);
      for (uint8_t a = 1; a < 127; a++) {
        Wire.beginTransmission(a);
        uint8_t e = Wire.endTransmission();
        if (e == 0) { Serial.printf("[I2C] dispositivo trovato a 0x%02X\n", a); n++; }
      }
      Serial.printf("[I2C] fine scansione: %d dispositivi. Livelli a riposo SDA=%d SCL=%d (devono essere 1)\n",
                    n, digitalRead(PIN_SDA), digitalRead(PIN_SCL));
    } else if (line == "ads") {
#if CURRENT_SENSOR == SENSOR_ADS1115
      // MUX 100..111 = A0..A3 riferiti a GND (PGA +-4.096 V), MUX 000 = A0-A1 (PGA +-0.256 V)
      const uint8_t mux[5] = {4, 5, 6, 7, ADS_MUX_SHUNT};
      static const char *pair[4] = {"A0-A1", "A0-A3", "A1-A3", "A2-A3"};
      static const uint8_t pPos[4] = {0, 0, 1, 2}, pNeg[4] = {1, 3, 3, 3};
      char lab[5][40];
      for (int k = 0; k < 4; k++) {
        const char *role = (k == pPos[ADS_MUX_SHUNT]) ? "sense lato carico" : (k == pNeg[ADS_MUX_SHUNT]) ? "sense lato batteria"
                         : (k == 2) ? "nodo partitore" : "non usato";
        snprintf(lab[k], sizeof(lab[k]), "A%d (%s)", k, role);
      }
      snprintf(lab[4], sizeof(lab[4]), "%s (shunt)", pair[ADS_MUX_SHUNT]);
      const char *nm[5] = {lab[0], lab[1], lab[2], lab[3], lab[4]};
      for (int k = 0; k < 5; k++) {
        bool diff = (k == 4);
        uint16_t c = 0x8000 | ((uint16_t)mux[k] << 12) | ((diff ? 0x5 : 0x1) << 9) | 0x0100 | (4 << 5) | 0x0003, raw = 0, st = 0;
        // ogni passo I2C va controllato: un chip che non risponde NON deve sembrare "0 mV"
        bool okW = i2cWrite16(ADS1115_ADDR, 0x01, c);
        delay(15);
        bool okR = okW && i2cRead16(ADS1115_ADDR, 0x01, st) && i2cRead16(ADS1115_ADDR, 0x00, raw);
        if (!okR) { Serial.printf("[ADS] %-26s = NESSUNA RISPOSTA I2C (chip assente dal bus)\n", nm[k]); continue; }
        float mv = (int16_t)raw * (diff ? 0.0078125f : 0.125f);
        Serial.printf("[ADS] %-26s = %9.3f mV%s\n", nm[k], mv, (raw == 0x7FFF || raw == 0x8000) ? "   <-- SATURO" : "");
      }
      adsStart(adsPhase);   // riprende il ciclo normale di conversioni
#else
      Serial.println("[ADS] firmware compilato per INA226");
#endif
    } else if (line == "adc") {
      uint32_t a = 0, b = 0;
      for (int k = 0; k < 32; k++) { a += analogReadMilliVolts(PIN_ADC_VBAT); b += analogReadMilliVolts(PIN_ADC_NTC); }
      Serial.printf("[ADC] GPIO%d (partitore) = %lu mV   GPIO%d (NTC) = %lu mV\n",
                    PIN_ADC_VBAT, (unsigned long)(a / 32), PIN_ADC_NTC, (unsigned long)(b / 32));
    } else if (line == "reboot") {
      saveCounters(); ESP.restart();
    } else if (line.length()) {
      Serial.println("[CMD] comandi: wifi <ssid>|<pass> | wifi off | ip | i2c | ads | adc | relay 0/1 | reboot");
    }
  }
}

// ===================================================================== setup / loop
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.printf("\n=== LiFePO4 Tester v%s ===\n", FW_VERSION);

  pinMode(PIN_RELAY, OUTPUT);
  pinMode(PIN_LED, OUTPUT);
  setRelay(false);

  analogReadResolution(12);
  analogSetPinAttenuation(PIN_ADC_VBAT, ADC_11db);
  analogSetPinAttenuation(PIN_ADC_NTC, ADC_11db);

  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(400000);
  m.inaOk = sensorInit();
  Serial.printf("[%s] %s\n", SENSOR_NAME, m.inaOk ? "trovato" : "NON trovato (controlla cablaggio I2C e indirizzo)");

  if (!LittleFS.begin(true)) Serial.println("[FS] LittleFS non disponibile");

  loadConfig();
  loadCounters();
  loadLast();
  snprintf(csvPath, sizeof(csvPath), "/t%lu.csv", (unsigned long)last.id);
  clampSoc();
  wifiSetup();
  webSetup();
  Serial.println("[OK] pronto");
}

void loop() {
  static uint32_t lastSample = 0, lastTick = 0, lastNvs = 0;
  dns.processNextRequest();
  server.handleClient();
  uint32_t now = millis();
  if (now - lastSample >= SAMPLE_MS) { lastSample = now; sample(); }
  if (now - lastTick >= 1000)        { lastTick = now; tick(); }
  if (now - lastNvs >= NVS_SAVE_MS)  { lastNvs = now; saveCounters(); }
  wifiMaintain();
  serialCommands();
}
