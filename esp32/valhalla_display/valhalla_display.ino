// Valhalla Deck v3 - Waveshare ESP32-S3-Touch-LCD-2.8 (V1 CST328 / V2 CST3530, rilevato in automatico)
// Dati: n8n "Valhalla Display Feed" (HTTPS + X-Valhalla-Token). Wi-Fi multi-rete configurabile dal telefono.
//
// Home: ora, prossimo allenamento, macro del giorno e 4 icone (Macro, Training, PR, Peso).
// Sotto-pagine: freccia in alto a sinistra o swipe a destra = home, swipe a sinistra = pagina successiva.
// Pagina STATO (nascosta): 5 tap rapidi sull'orologio della home. Da li': aggiorna, suoni, PIN, reti Wi-Fi.
// PIN: blocca all'avvio, dopo 1 min di inattivita' e con una pressione breve del tasto PWR (tasca).
// Tasto PWR: acceso a batteria con una pressione; breve = blocca/spegne schermo o risveglia; 3 s = spegni.
// Suoni: avviso su nuovo pasto, allenamento, PR e kcal sforate (muti 22:30-07:00, disattivabili).
// v2.1: registra serie (Training > Registra serie) e pasti preferiti (Macro > + Pasto) su Notion via n8n,
//       celebrazione PR, coach proteine, streak, alza per accendere, scuoti per aggiornare, tasca (tocchi ignorati),
//       ora dall'RTC anche senza rete.
// v4.1: riconnessione Wi-Fi affidabile e reti gestite dal Deck (Stato > WI-FI), pannello notifiche (scorri giu'
//       dall'alto), Stato con uno swipe in su dalla home, percentuale batteria filtrata.
// v4: progressione carichi, finestra digiuno integratori, pasto consigliato, aggiornamento firmware da Notion,
//     coda offline dei log, riepilogo della settimana (workflow n8n "Valhalla Deck Plus").
// v3: grafica "Forgia" (font Eccentric + logo), scheda Atleta, "E' un PR?", acqua (+200/+500/+1 L, festa a 3 L),
//     integrazione giornaliera da Notion; feed e log extra dal workflow n8n "Valhalla Deck Extra".
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WiFiManager.h>
#include <Preferences.h>
#include <esp_sleep.h>
#include <time.h>
#include "ui.h"
#include "root_ca.h"
#include "audio.h"
#include "sensors.h"
#include <Update.h>
#include <esp_sntp.h>
#include "Display_ST7789.h"   // driver Waveshare (LCD + touch); per ultimo perche' definisce macro generiche

#define PIN_PWR_KEY   6     // tasto accensione, attivo basso
#define PIN_PWR_HOLD  7     // mantiene accesa l'alimentazione da batteria
#define PIN_BAT_ADC   8     // partitore 1/3 sulla batteria

// URL n8n e password del portale stanno in secrets.h (escluso da Git): copia secrets.example.h e compilalo
#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "Manca secrets.h: copia secrets.example.h in secrets.h e inserisci i tuoi valori"
#endif
static const char *DEFAULT_URL = SECRET_DEFAULT_URL;
static const char *AP_NAME = "Valhalla-Setup";
static const char *AP_PASS = SECRET_AP_PASS;
static const char *TZ_ROME = "CET-1CEST,M3.5.0,M10.5.0/3";
static const uint32_t REFRESH_MS = 5UL * 60 * 1000;
static const uint32_t STANDBY_MS = 60UL * 1000;
static const uint32_t SCREEN_OFF_MS = 5UL * 60 * 1000;
static const int BL_ACTIVE = 70, BL_STANDBY = 15;
static const int MAX_NETS = 6;

LGFX_Sprite spr;
Preferences prefs;
SemaphoreHandle_t dataLock;
Data data;                    // protetto da dataLock
Extra ext;                    // atleta / acqua / integrazioni, protetto da dataLock
Plus plus;                    // progressione / digiuno / settimana / firmware, protetto da dataLock
String qBuf;                  // coda offline: righe "kind\tid\tbody", salvata in NVS ("q"), protetta da dataLock
String fwInstalled;           // id del firmware installato dalla pagina Notion
volatile bool otaReq = false, plusNow = false;
volatile int queueSent = 0, queueRejected = 0;        // per il toast "inviati N dati offline"
volatile bool otaFailed = false;
long localMeal = 0;           // pasto registrato dal Deck (prima che Notion lo riporti)
Status status;                // scritto dal task di rete (campi semplici, letture concorrenti accettabili)
volatile bool refreshNow = true, portalActive = false;
volatile uint32_t lastOkMs = 0;
String cfgUrl, cfgToken, cfgPin;
Ui ui;                        // stato UI (solo dal loop)

// Invio di un log (serie o pasto): la UI compila il body, il task di rete fa la POST.
// state: 0 libero, 1 da inviare, 3 in invio, 2 risposta pronta (la legge il loop)
struct LogReq { volatile int state = 0; char body[192]; char id[33]; int code = 0; char resp[320]; int kind = 0; uint32_t at = 0; int ml = 0; float kg = 0; int reps = 0; };
enum { LOG_SET = 1, LOG_MEAL = 2, LOG_WATER = 3, LOG_SUPP = 4, LOG_PRS = 5, LOG_WEEK = 6 };   // 3-5: webhook "valhalla-log-x", 6: "valhalla-week"
static const char *logPath(int kind) { return kind == LOG_WEEK ? "valhalla-week" : kind >= LOG_WATER ? "valhalla-log-x" : "valhalla-log"; }
static LogReq req;
volatile bool quietNext = false;      // dopo un log: il prossimo aggiornamento non suona (il suono l'ha gia' fatto il log)
volatile bool rtcSaveReq = false;     // NTP sincronizzato: il loop salva l'ora nell'RTC (I2C solo dal loop)
// PR rilevato dal task di rete: la celebrazione la avvia il loop
volatile bool celPending = false; char celPName[24]; float celPE1 = 0, celPDelta = 0;
static char lastPrLift[10] = ""; static float lastPrE1 = 0;   // PR appena salvato dal Deck: il feed non lo ri-festeggia

static void queueCelebration(const char *name, float e1, float delta) {
  snprintf(celPName, sizeof celPName, "%s", name);
  celPE1 = e1; celPDelta = delta;
  celPending = true;
}

// Registro delle notifiche (pannello che scende dall'alto). Mai chiamare tenendo dataLock.
static void notify(const char *txt, int kind) {
  char hm[6] = "--:--";
  struct tm t;
  if (getLocalTime(&t, 0)) strftime(hm, sizeof hm, "%H:%M", &t);
  xSemaphoreTake(dataLock, portMAX_DELAY);
  Notifs &N = ui.notif;
  Notif &e = N.n[N.head];
  snprintf(e.hm, sizeof e.hm, "%s", hm);
  snprintf(e.txt, sizeof e.txt, "%s", txt);
  e.kind = kind;
  N.head = (N.head + 1) % MAX_NOTIF;
  if (N.count < MAX_NOTIF) N.count++;
  N.unread++;
  xSemaphoreGive(dataLock);
}

// Suono d'avviso in base a cosa e' cambiato nei dati (un solo suono, il piu' importante) + notifica
static void announce(const Data &o, const Data &n) {
  if (!o.valid) return;
  char m[48];
  if (n.npr > o.npr) {
    snprintf(m, sizeof m, "Nuovo PR: %s %s kg e1RM", n.pr_name[n.npr - 1], kg(n.pr_e1[n.npr - 1])); notify(m, NK_TRAIN);
    queueCelebration(n.pr_name[n.npr - 1], n.pr_e1[n.npr - 1], 0); return;
  }
  for (int i = 0; i < n.npr; i++)
    for (int j = 0; j < o.npr; j++)
      if (!strcmp(n.pr_name[i], o.pr_name[j]) && n.pr_e1[i] > o.pr_e1[j] + 0.05f) {
        snprintf(m, sizeof m, "Nuovo PR: %s %s kg e1RM", n.pr_name[i], kg(n.pr_e1[i])); notify(m, NK_TRAIN);
        queueCelebration(n.pr_name[i], n.pr_e1[i], n.pr_e1[i] - o.pr_e1[j]); return;
      }
  bool overNow = n.kcal[1] > 0 && n.kcal[0] > n.kcal[1], overBefore = o.kcal[1] > 0 && o.kcal[0] > o.kcal[1];
  if (overNow && !overBefore) { snprintf(m, sizeof m, "Kcal sforate: %s / %s", fmt(n.kcal[0]), fmt(n.kcal[1])); notify(m, NK_WARN); playSound(SND_WARN); return; }
  if (n.pasti > o.pasti) { snprintf(m, sizeof m, "Pasto su Notion (%d oggi, %s kcal)", n.pasti, fmt(n.kcal[0])); notify(m, NK_MEAL); playSound(SND_MEAL); return; }
  if (n.has_last && (!o.has_last || strcmp(n.last_d, o.last_d) || strcmp(n.last_g, o.last_g))) {
    snprintf(m, sizeof m, "Allenamento registrato: %s", n.last_g); notify(m, NK_TRAIN); playSound(SND_TRAIN); return;
  }
  if (fabsf(n.peso - o.peso) > 0.01f || strcmp(n.peso_d, o.peso_d)) { snprintf(m, sizeof m, "Nuovo peso: %s kg", fmt(n.peso, 1)); notify(m, NK_INFO); playSound(SND_TICK); }
}

// ---------------- configurazione persistente ----------------
static String loadNets() { return prefs.getString("nets", ""); }   // "ssid\tpass\n" ripetuto

static void addNet(const String &ssid, const String &pass) {
  if (!ssid.length()) return;
  String out = ssid + "\t" + pass + "\n";
  int n = 1, start = 0;
  String all = loadNets();
  while (start < (int)all.length() && n < MAX_NETS) {
    int nl = all.indexOf('\n', start);
    if (nl < 0) break;
    String line = all.substring(start, nl);
    if (!line.startsWith(ssid + "\t")) { out += line + "\n"; n++; }   // l'ultima rete inserita va in testa
    start = nl + 1;
  }
  prefs.putString("nets", out);
}

// ---------------- batteria ----------------
static float batteryVolts() {
  uint32_t mv = 0;
  for (int i = 0; i < 32; i++) { mv += analogReadMilliVolts(PIN_BAT_ADC); delayMicroseconds(150); }   // media su ~5 ms: smussa i picchi del Wi-Fi
  return mv / 32.0f * 3.0f / 1000.0f / 0.9945f;   // partitore e correzione dal driver Waveshare
}

// La scheda non porta lo stato del caricatore su un GPIO: lo si deduce dalla tensione.
// Attaccare il cavo alza subito la tensione (>70 mV sopra la media recente), staccarlo la abbassa; poi conta il trend su 4 min.
// shortcut: subito dopo una ricarica completa, staccata, puo' restare "in carica" qualche minuto (>= 4,17 V); per lo stato vero serve il pin CHG del caricatore
static void updateCharging(float v, uint32_t now) {
  static float base = 0, hist[8];                   // base: media mobile, cosi' il rumore non conta come "salto"
  static int n = 0;
  static uint32_t lastHist = 0;
  if (v < 3.0f) { status.charging = false; base = 0; n = 0; return; }   // nessuna batteria
  if (base > 0 && v - base > 0.07f) { status.charging = true; base = v; }
  else if (base > 0 && base - v > 0.07f) { status.charging = false; base = v; }
  else base = base <= 0 ? v : base * 0.7f + v * 0.3f;
  if (!lastHist || now - lastHist >= 30000) {
    lastHist = now;
    memmove(hist, hist + 1, sizeof hist - sizeof hist[0]);
    hist[7] = v;
    if (n < 8) n++;
    if (n == 8) {
      float d = hist[7] - hist[0];
      if (d > 0.015f) status.charging = true; else if (d < -0.01f) status.charging = false;
    }
  }
  if (v >= 4.17f) status.charging = true;            // a fine carica il caricatore tiene la cella a ~4,2 V
}

static int batteryPct(float v);
// Percentuale mostrata: tensione filtrata (~30 s); in scarica puo' solo scendere (il rumore non la fa ballare).
// In carica la tensione e' quella del caricatore, non della cella: si parte dall'ultimo valore prima del cavo.
// shortcut: in carica la stima e' la tensione meno ~0,15 V (meno verso fine carica); per il valore vero serve un fuel gauge
static int smoothPct(float v, bool charging, uint32_t now) {
  static float ema = 0;
  static int shown = -1, before = -1;
  static bool wasCharging = false;
  static uint32_t settleUntil = 0;
  if (v < 3.0f) { ema = 0; shown = -1; wasCharging = false; return -1; }
  if (charging && !wasCharging) before = shown;
  if (!charging && wasCharging) { ema = v; settleUntil = now + 90000; }   // cavo staccato: la cella si assesta, si riparte
  wasCharging = charging;
  ema = ema <= 0 ? v : ema * 0.85f + v * 0.15f;
  if (charging) {
    float off = 0.15f * fminf(1.0f, fmaxf(0.1f, (4.20f - ema) / 0.20f));   // in fase finale (tensione costante) lo scarto cala
    int est = min(99, batteryPct(ema - off));
    shown = max(before, est);
    return shown;
  }
  int p = batteryPct(ema);
  if (shown < 0 || (int32_t)(settleUntil - now) > 0 || p < shown || p > shown + 8) shown = p;
  return shown;
}

static int batteryPct(float v) {
  if (v < 3.0f) return -1;   // nessuna batteria collegata
  static const float V[] = {3.30, 3.50, 3.60, 3.70, 3.80, 3.90, 4.00, 4.10, 4.20};
  static const int P[] = {0, 6, 15, 32, 50, 65, 78, 90, 100};
  if (v <= V[0]) return 0;
  for (int i = 1; i < 9; i++)
    if (v <= V[i]) return P[i - 1] + (int)((v - V[i - 1]) / (V[i] - V[i - 1]) * (P[i] - P[i - 1]));
  return 100;
}

// ---------------- rete (task su core 0, non blocca la UI) ----------------
// GET o POST (postBody != nullptr) verso n8n; ritorna il codice HTTP (<0 = errore di rete/TLS)
static int n8nRequest(const String &url, const char *postBody, String &resp) {
  WiFiClientSecure client;
  client.setCACert(ROOT_CA);
  HTTPClient http;
  http.setTimeout(20000);
  if (!http.begin(client, url)) return -1000;
  http.addHeader("X-Valhalla-Token", cfgToken);
  int code;
  if (postBody) { http.addHeader("Content-Type", "application/json"); code = http.POST((uint8_t *)postBody, strlen(postBody)); }
  else code = http.GET();
  if (code > 0) resp = http.getString();
  else snprintf(status.msg, sizeof status.msg, "rete/TLS: %s", http.errorToString(code).c_str());
  http.end();
  return code;
}

// URL fratello sullo stesso n8n: .../webhook/valhalla-deck -> .../webhook/<name>
static String sibling(const char *name) { return cfgUrl.substring(0, cfgUrl.lastIndexOf('/') + 1) + name; }

static bool sameDay(long a, long b) {
  time_t ta = a, tb = b; struct tm x, y;
  localtime_r(&ta, &x); localtime_r(&tb, &y);
  return x.tm_year == y.tm_year && x.tm_yday == y.tm_yday;
}

static bool fetchData() {
  String body;
  int code = n8nRequest(cfgUrl, nullptr, body);
  // Deck API non ancora pubblicata: si usa il vecchio feed (niente log/streak, il resto funziona)
  if (code == 404 && cfgUrl.endsWith("/valhalla-deck")) code = n8nRequest(sibling("valhalla-display"), nullptr, body);
  if (code == -1000) { snprintf(status.msg, sizeof status.msg, "URL non valido"); return false; }
  if (code != 200) {
    if (code > 0) snprintf(status.msg, sizeof status.msg, "n8n: HTTP %d%s", code, code == 403 ? " (token?)" : "");
    return false;
  }
  Data fresh;
  if (!parseData(body.c_str(), fresh)) { snprintf(status.msg, sizeof status.msg, "risposta non valida"); return false; }
  xSemaphoreTake(dataLock, portMAX_DELAY);
  Data old = data;
  data = fresh;
  xSemaphoreGive(dataLock);
  if (quietNext) quietNext = false; else announce(old, fresh);
  prefs.putString("cache", body);   // mostrata subito al prossimo avvio, anche offline
  lastOkMs = millis();
  // feed extra (atleta, acqua, integrazioni): se il workflow non c'e' le pagine nuove dicono "non attivo"
  String bx;
  if (n8nRequest(sibling("valhalla-deck-x"), nullptr, bx) == 200) {
    Extra fx;
    if (parseExtra(bx.c_str(), fx)) {
      xSemaphoreTake(dataLock, portMAX_DELAY);
      if (req.state == 0) ext = fx;                 // con un invio in corso si tiene il valore locale (acqua ottimistica)
      xSemaphoreGive(dataLock);
      prefs.putString("cachex", bx);
    }
  }
  // feed plus ogni 15 min (8 query Notion): progressione, ultimo pasto, digiuno, settimana, firmware
  static uint32_t lastPlus = 0;
  if (!lastPlus || plusNow || millis() - lastPlus > 15UL * 60 * 1000) {
    String bp;
    Plus fp;
    if (n8nRequest(sibling("valhalla-deck-p"), nullptr, bp) == 200 && parsePlus(bp.c_str(), fp)) {
      xSemaphoreTake(dataLock, portMAX_DELAY);
      if (localMeal > fp.meal && sameDay(localMeal, time(nullptr))) fp.meal = localMeal;
      plus = fp;
      status.fwNew = plus.fwId[0] && fwInstalled != plus.fwId;
      xSemaphoreGive(dataLock);
      prefs.putString("cachep", bp);
    }
    lastPlus = millis(); plusNow = false;
  }
  status.msg[0] = 0;
  return true;
}

static int lineCount(const String &q) { int n = 0; for (int i = 0; i < (int)q.length(); i++) n += q[i] == '\n'; return n; }

// Log non inviato per mancanza di rete: in coda (max 20, i piu' vecchi si perdono), riprovato dal task di rete
static void enqueueReq() {
  xSemaphoreTake(dataLock, portMAX_DELAY);
  while (lineCount(qBuf) >= 20) qBuf = qBuf.substring(qBuf.indexOf('\n') + 1);
  qBuf += String(req.kind) + "\t" + req.id + "\t" + req.body + "\n";
  status.queued = lineCount(qBuf);
  String copy = qBuf;
  xSemaphoreGive(dataLock);
  prefs.putString("q", copy);
}

// Un log della coda per volta; errori di rete/5xx: riprova tra 1 min, altrimenti (inviato o rifiutato) esce dalla coda
static void flushQueue() {
  static uint32_t nextTry = 0;
  if (req.state != 0 || (int32_t)(millis() - nextTry) < 0) return;
  xSemaphoreTake(dataLock, portMAX_DELAY);
  int nl = qBuf.indexOf('\n');
  String line = nl > 0 ? qBuf.substring(0, nl) : String("");
  xSemaphoreGive(dataLock);
  if (!line.length()) return;
  int t1 = line.indexOf('\t'), t2 = line.indexOf('\t', t1 + 1);
  int kind = line.substring(0, t1).toInt();
  String body = line.substring(t2 + 1), resp;
  int code = n8nRequest(sibling(logPath(kind)), body.c_str(), resp);
  if (code < 0 || code >= 500) { nextTry = millis() + 60000; return; }
  JsonDocument doc;
  bool ok = code == 200 && !deserializeJson(doc, resp) && (doc["ok"] | false);
  xSemaphoreTake(dataLock, portMAX_DELAY);
  if (qBuf.startsWith(line)) qBuf = qBuf.substring(qBuf.indexOf('\n') + 1);   // la coda cresce solo in coda: la prima riga e' ancora questa
  status.queued = lineCount(qBuf);
  String copy = qBuf;
  bool empty = status.queued == 0;
  xSemaphoreGive(dataLock);
  prefs.putString("q", copy);
  if (ok) queueSent = queueSent + 1; else queueRejected = queueRejected + 1;
  if (empty) { quietNext = true; refreshNow = true; plusNow = true; }
}

// Scrive il firmware scaricato nella partizione OTA e conta i byte per la barra
struct OtaSink : public Stream {
  size_t n = 0; int total = 0;
  size_t write(uint8_t c) { return write(&c, 1); }
  size_t write(const uint8_t *b, size_t len) {
    size_t w = Update.write((uint8_t *)b, len);
    n += w;
    ui.otaPct = total > 0 ? min(99, (int)(n * 100 / total)) : min(99, (int)(n / 15000));   // senza dimensione: ~1,5 MB
    return w;
  }
  int available() { return 0; }
  int read() { return -1; }
  int peek() { return -1; }
  void flush() {}
};

static void otaFail(const char *m) {
  snprintf(ui.otaMsg, sizeof ui.otaMsg, "%s", m);
  Update.abort();
  ui.otaPct = -1;
  otaFailed = true;
}

static void doOta() {
  ui.otaPct = 0;
  snprintf(ui.otaMsg, sizeof ui.otaMsg, "scarico %s", plus.fwName);
  WiFiClientSecure client;
  client.setCACert(ROOT_CA);
  HTTPClient http;
  http.setTimeout(30000);
  if (!http.begin(client, sibling("valhalla-fw"))) return otaFail("URL non valido");
  http.addHeader("X-Valhalla-Token", cfgToken);
  int code = http.GET();
  if (code != 200) { http.end(); char m[32]; snprintf(m, sizeof m, "download: HTTP %d", code); return otaFail(m); }
  int total = http.getSize();
  if (!Update.begin(total > 0 ? total : UPDATE_SIZE_UNKNOWN)) { http.end(); return otaFail("spazio OTA insufficiente"); }
  OtaSink sink; sink.total = total;
  int w = http.writeToStream(&sink);
  http.end();
  // il motivo vero (byte ricevuti / errore di Update) a schermo: senza non si capisce se e' il file o la rete
  char why[40];
  if (w <= 0 || (total > 0 && (int)sink.n != total)) { snprintf(why, sizeof why, "download interrotto %d/%d KB", (int)(sink.n / 1024), total / 1024); return otaFail(why); }
  if (!Update.end(true)) { snprintf(why, sizeof why, "FW: %s", Update.errorString()); return otaFail(why); }
  prefs.putString("fwid", plus.fwId);
  ui.otaPct = 100;
  snprintf(ui.otaMsg, sizeof ui.otaMsg, "fatto, riavvio...");
  delay(1500);
  ESP.restart();
}

static void postLog() {
  req.state = 3;
  String resp;
  int code = n8nRequest(sibling(logPath(req.kind)), req.body, resp);
  req.code = code;
  snprintf(req.resp, sizeof req.resp, "%s", resp.c_str());
  req.state = 2;
}

// ---------------- Wi-Fi: riconnessione e reti gestite dal Deck ----------------
volatile int wifiCmd = 0;                   // 1 = cerca reti, 2 = collegati a ui.wifi.target con connPass
static char connPass[65] = "";

static bool knownPass(const String &all, const char *ssid, String &pass) {
  int start = 0;
  while (start < (int)all.length()) {
    int nl = all.indexOf('\n', start);
    if (nl < 0) break;
    String line = all.substring(start, nl);
    int tab = line.indexOf('\t');
    if (tab > 0 && line.substring(0, tab) == ssid) { pass = line.substring(tab + 1); return true; }
    start = nl + 1;
  }
  return false;
}

// Scansione (nel task di rete): reti uniche, dalla piu' forte, segnando quelle salvate
static void scanNets() {
  xSemaphoreTake(dataLock, portMAX_DELAY); ui.wifi.scanning = true; xSemaphoreGive(dataLock);
  int found = WiFi.scanNetworks();
  String all = loadNets(), pw;
  WifiNet list[MAX_NETS_SCAN];
  int n = 0;
  for (int i = 0; i < found; i++) {
    String ss = WiFi.SSID(i);
    if (!ss.length()) continue;
    int rssi = WiFi.RSSI(i), k = 0;
    while (k < n && strcmp(list[k].ssid, ss.c_str())) k++;
    if (k < n) { if (rssi > list[k].rssi) list[k].rssi = rssi; continue; }   // stessa rete su piu' access point
    if (n == MAX_NETS_SCAN) { int w = 0; for (int j = 1; j < n; j++) if (list[j].rssi < list[w].rssi) w = j; if (rssi <= list[w].rssi) continue; k = w; } else k = n++;
    snprintf(list[k].ssid, sizeof list[k].ssid, "%s", ss.c_str());
    list[k].rssi = rssi;
    list[k].open = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
    list[k].saved = knownPass(all, list[k].ssid, pw);
  }
  WiFi.scanDelete();
  for (int i = 1; i < n; i++) for (int j = i; j > 0 && list[j].rssi > list[j - 1].rssi; j--) { WifiNet t = list[j]; list[j] = list[j - 1]; list[j - 1] = t; }
  xSemaphoreTake(dataLock, portMAX_DELAY);
  memcpy(ui.wifi.net, list, sizeof list);
  ui.wifi.n = n; ui.wifi.scanning = false;
  xSemaphoreGive(dataLock);
}

static bool joinNet(const char *ssid, const char *pass, uint32_t ms) {
  WiFi.disconnect();
  vTaskDelay(pdMS_TO_TICKS(100));
  WiFi.begin(ssid, pass);
  uint32_t t0 = millis();
  while (millis() - t0 < ms) {
    if (WiFi.status() == WL_CONNECTED) return true;
    vTaskDelay(pdMS_TO_TICKS(250));
  }
  return false;
}

// Rete persa o all'avvio: cerca le reti salvate in zona e prova dalla piu' forte (WiFiMulti a volte non ci riprovava piu')
static bool reconnectKnown() {
  scanNets();
  String all = loadNets();
  char ssid[MAX_NETS_SCAN][33]; int cand = 0;
  xSemaphoreTake(dataLock, portMAX_DELAY);
  for (int i = 0; i < ui.wifi.n; i++) if (ui.wifi.net[i].saved) snprintf(ssid[cand++], 33, "%s", ui.wifi.net[i].ssid);
  xSemaphoreGive(dataLock);
  if (!cand) { snprintf(status.msg, sizeof status.msg, "nessuna rete nota in zona"); return false; }
  for (int i = 0; i < cand; i++) {
    String pass;
    knownPass(all, ssid[i], pass);
    if (joinNet(ssid[i], pass.c_str(), 12000)) return true;
  }
  snprintf(status.msg, sizeof status.msg, "reti note in zona, ma connessione fallita");
  return false;
}

// Comandi dalla pagina WI-FI
static void wifiCommands() {
  if (wifiCmd == 1) { wifiCmd = 0; scanNets(); }
  if (wifiCmd == 2) {
    wifiCmd = 0;
    char target[33];
    xSemaphoreTake(dataLock, portMAX_DELAY); snprintf(target, sizeof target, "%s", ui.wifi.target); ui.wifi.conn = 1; xSemaphoreGive(dataLock);
    bool ok = joinNet(target, connPass, 15000);
    if (ok) { addNet(target, connPass); refreshNow = true; }
    xSemaphoreTake(dataLock, portMAX_DELAY);
    ui.wifi.conn = ok ? 2 : 3;
    snprintf(ui.wifi.msg, sizeof ui.wifi.msg, ok ? "connesso a %s, rete salvata" : "%s: connessione fallita", target);
    xSemaphoreGive(dataLock);
    if (ok) scanNets();
  }
}

static void netTask(void *) {
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);                   // le reti le salva il Deck in NVS, non il driver
  WiFi.setAutoReconnect(true);
  bool timeSet = false, everConnected = false;
  uint32_t nextFetch = 0, lostSince = 0, nextTry = 0;
  for (;;) {
    if (portalActive) { vTaskDelay(pdMS_TO_TICKS(500)); continue; }
    wifiCommands();
    if (WiFi.status() != WL_CONNECTED) {
      status.wifi = false;
      if (!lostSince) lostSince = millis();
      // all'avvio subito; dopo una caduta si lascia 8 s alla riconnessione automatica, poi si cerca da soli ogni 20 s
      if ((!everConnected || millis() - lostSince > 8000) && (int32_t)(millis() - nextTry) >= 0) {
        if (!reconnectKnown()) nextTry = millis() + 20000;
      }
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }
    if (lostSince && everConnected) refreshNow = true;   // tornata la rete: dati freschi e coda offline
    lostSince = 0; everConnected = true; nextTry = 0;
    status.wifi = true;
    status.rssi = WiFi.RSSI();
    snprintf(status.ssid, sizeof status.ssid, "%s", WiFi.SSID().c_str());
    snprintf(status.ip, sizeof status.ip, "%s", WiFi.localIP().toString().c_str());
    if (!timeSet) { configTzTime(TZ_ROME, "pool.ntp.org", "time.google.com"); timeSet = true; }
    if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) rtcSaveReq = true;   // a ogni sincronizzazione (~1 h)
    if (otaReq) { otaReq = false; doOta(); }
    if (req.state == 1) postLog();
    else flushQueue();
    if (refreshNow || (int32_t)(millis() - nextFetch) >= 0) {
      refreshNow = false;
      nextFetch = millis() + (fetchData() ? REFRESH_MS : 30000);   // in errore riprova dopo 30 s
    }
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

// ---------------- portale di configurazione ----------------
static void showMessage(const char *l1, const char *l2, const char *l3) {
  Pal P = pal(spr);
  spr.fillScreen(P.bg);
  text(spr, "CONFIGURAZIONE", UW / 2, 30, &fonts::FreeSansBold12pt7b, P.acc, top_center);
  text(spr, l1, UW / 2, 110, &fonts::DejaVu18, P.white, top_center);
  text(spr, l2, UW / 2, 150, &fonts::DejaVu12, P.gold, top_center);
  text(spr, l3, UW / 2, 175, &fonts::DejaVu12, P.dim, top_center);
  LCD_addWindow(0, 0, UW - 1, UH - 1, (uint16_t *)spr.getBuffer());
}

static void runPortal() {
  portalActive = true;
  WiFi.disconnect();
  char l2[48];
  snprintf(l2, sizeof l2, "Rete: %s  pwd: %s", AP_NAME, AP_PASS);
  showMessage("Collegati col telefono", l2, "poi apri 192.168.4.1");
  WiFiManager wm;
  wm.setConfigPortalTimeout(300);
  wm.setTitle("Valhalla Display");
  WiFiManagerParameter pUrl("url", "URL n8n", cfgUrl.c_str(), 200);
  WiFiManagerParameter pTok("token", "X-Valhalla-Token (vuoto = non cambiare)", "", 100);
  wm.addParameter(&pUrl);
  wm.addParameter(&pTok);
  bool ok = wm.startConfigPortal(AP_NAME, AP_PASS);
  if (ok) {
    addNet(wm.getWiFiSSID(), wm.getWiFiPass());
    if (strlen(pUrl.getValue())) prefs.putString("url", pUrl.getValue());
    if (strlen(pTok.getValue())) prefs.putString("token", pTok.getValue());
    showMessage("Salvato", wm.getWiFiSSID().c_str(), "riavvio...");
  } else {
    showMessage("Annullato", "", "riavvio...");
  }
  delay(1500);
  ESP.restart();   // riparte pulito con la nuova lista di reti
}

// ---------------- alimentazione ----------------
static void powerOff() {
  showMessage("Spegnimento", "", "");
  delay(600);
  Set_Backlight(0);
  digitalWrite(PIN_PWR_HOLD, LOW);   // a batteria la scheda si spegne qui
  delay(300);
  // ancora vivi = alimentati da USB: deep sleep, il tasto PWR la risveglia
  esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_PWR_KEY, 0);
  esp_deep_sleep_start();
}

// ---------------- navigazione e blocco ----------------
static int page = PAGE_HOME, notifPrev = PAGE_HOME;
static bool locked = false;
static uint32_t lastTouch = 0, lockoutUntil = 0;
static int badTries = 0;
static char newPin[5] = "";

static void setLockMsg(const char *m) { snprintf(ui.lockMsg, sizeof ui.lockMsg, "%s", m); }

static void showLock(int mode) {
  ui.lockMode = mode;
  ui.entry[0] = 0;
  setLockMsg(mode == LOCK_UNLOCK ? "Inserisci PIN" : mode == LOCK_SET1 ? "Nuovo PIN (0000 = disattiva)" : "Conferma PIN");
  page = PAGE_LOCK;
}

static void lockNow() {
  if (!ui.pinSet) return;
  locked = true;
  showLock(LOCK_UNLOCK);
}

static void pinComplete(uint32_t now) {
  if (ui.lockMode == LOCK_UNLOCK) {
    if (cfgPin == ui.entry) {
      locked = false; badTries = 0; page = PAGE_HOME;
      playSound(SND_UNLOCK, false);
    } else {
      playSound(SND_ERROR, false);
      if (++badTries >= 5) { badTries = 0; lockoutUntil = now + 30000; setLockMsg("Attendi 30 s"); }
      else { char b[32]; snprintf(b, sizeof b, "PIN errato (%d/5)", badTries); setLockMsg(b); }
      ui.entry[0] = 0;
    }
  } else if (ui.lockMode == LOCK_SET1) {
    snprintf(newPin, sizeof newPin, "%s", ui.entry);
    showLock(LOCK_SET2);
  } else {
    if (strcmp(newPin, ui.entry) != 0) { showLock(LOCK_SET1); setLockMsg("Non coincidono, riprova"); return; }
    if (strcmp(newPin, "0000") == 0) { prefs.remove("pin"); cfgPin = ""; ui.pinSet = false; }
    else { prefs.putString("pin", newPin); cfgPin = newPin; ui.pinSet = true; }
    playSound(SND_UNLOCK, false);
    ui.lockMode = LOCK_UNLOCK;
    page = PAGE_INFO;
  }
}

static void keypadTap(char k, uint32_t now) {
  if (!k) return;
  if ((int32_t)(lockoutUntil - now) > 0) {
    char b[32]; snprintf(b, sizeof b, "Attendi %d s", (int)((lockoutUntil - now) / 1000) + 1); setLockMsg(b);
    return;
  }
  size_t n = strlen(ui.entry);
  if (k == 'C') ui.entry[0] = 0;
  else if (k == '<') { if (n) ui.entry[n - 1] = 0; }
  else if (n < 4) { ui.entry[n] = k; ui.entry[n + 1] = 0; if (n + 1 == 4) pinComplete(now); }
}

// ---------------- log serie / pasti, toast, celebrazione ----------------
static void toast(const char *m, bool ok, uint32_t now) {
  snprintf(ui.toast, sizeof ui.toast, "%s", m);
  ui.toastOk = ok;
  ui.toastUntil = now + 3500;
}

// Festa a tutto schermo: luci (retroilluminazione che lampeggia) + fanfara
static void celebrate(const char *title, const char *unit, const char *name, float value, float delta, uint32_t now, bool save = false) {
  snprintf(ui.celTitle, sizeof ui.celTitle, "%s", title);
  snprintf(ui.celUnit, sizeof ui.celUnit, "%s", unit);
  snprintf(ui.celName, sizeof ui.celName, "%s", name);
  ui.celE1 = value; ui.celDelta = delta; ui.celSave = save;
  ui.celStart = now; ui.celUntil = now + (save ? 12000 : 6000);
  playSound(SND_FANFARE, false);
}

static void sendLog(JsonDocument &b, int kind, const char *id, uint32_t now) {
  if (req.state != 0) return;                      // un invio alla volta
  serializeJson(b, req.body, sizeof req.body);
  snprintf(req.id, sizeof req.id, "%s", id);
  req.kind = kind; req.at = now;
  ui.busy = true;
  req.state = 1;
}

// Risposta di /valhalla-log (vedi n8n "Valhalla Deck API")
// Senza rete il log va in coda e a schermo si fa come se fosse andato (lo si invia appena torna la rete)
static void goOffline(uint32_t now) {
  enqueueReq();
  ui.busy = false; ui.favArmed = -1; ui.supArmed = -1;
  xSemaphoreTake(dataLock, portMAX_DELAY);
  if (req.kind == LOG_SET)
    for (int i = 0; i < data.nplan; i++) if (!strcmp(data.plan[i].id, req.id)) { data.plan[i].used = req.kg; data.plan[i].reps = req.reps; }
  if (req.kind == LOG_SUPP) for (int i = 0; i < ext.nsup; i++) if (!strcmp(ext.sup[i].id, req.id)) ext.sup[i].taken = true;
  if (req.kind == LOG_MEAL) { localMeal = time(nullptr); plus.meal = localMeal; }
  xSemaphoreGive(dataLock);
  if (req.kind == LOG_PRS) { snprintf(lastPrLift, sizeof lastPrLift, "%s", ui.celLift); lastPrE1 = ui.celE1; ui.celUntil = 0; }
  if (req.kind == LOG_SET && !locked) page = PAGE_LOG;
  toast("Offline: salvato, lo invio appena c'e' rete", true, now);
  playSound(SND_TICK, false);
}

static void handleLogResult(uint32_t now) {
  if (req.code < 0 && req.kind != LOG_WEEK) return goOffline(now);   // la settimana non va in coda: si riprova a mano
  JsonDocument doc;
  bool parsed = req.code == 200 && !deserializeJson(doc, req.resp);
  ui.busy = false; ui.favArmed = -1; ui.wkArmed = false;
  if (!parsed || !(doc["ok"] | false)) {
    char m[48];
    if (parsed) snprintf(m, sizeof m, "%s", (const char *)(doc["err"] | "errore"));
    else if (req.code == 404) snprintf(m, sizeof m, "%s", req.kind == LOG_WEEK ? "workflow Load Week non pubblicato" : "Deck API non pubblicata");
    else if (req.code > 0) snprintf(m, sizeof m, "n8n: HTTP %d", req.code);
    else snprintf(m, sizeof m, "rete non raggiungibile");
    if (req.kind == LOG_WATER) {                    // annulla l'aggiunta ottimistica
      xSemaphoreTake(dataLock, portMAX_DELAY);
      ext.h2o = max(0, ext.h2o - req.ml);
      xSemaphoreGive(dataLock);
    }
    if (req.kind == LOG_PRS) ui.celUntil = 0;
    ui.supArmed = -1;
    toast(m, false, now);
    playSound(SND_ERROR, false);
    return;
  }
  char m[48];
  if (req.kind == LOG_SET) {
    float kgv = doc["kg"] | 0.0f, e1 = doc["e1rm"] | 0.0f, best = doc["best"] | 0.0f;
    int reps = doc["reps"] | 0;
    xSemaphoreTake(dataLock, portMAX_DELAY);       // segna subito la serie come fatta, senza aspettare il feed
    for (int i = 0; i < data.nplan; i++)
      if (!strcmp(data.plan[i].id, req.id)) { data.plan[i].used = kgv; data.plan[i].reps = reps; }
    xSemaphoreGive(dataLock);
    if (doc["pr"] | false) {
      celebrate("NUOVO PR!", "kg e1RM", doc["ex"] | "", e1, e1 - best, now);
    } else {
      snprintf(m, sizeof m, "Salvata  e1RM %s kg", kg(e1));
      toast(m, true, now);
      playSound(SND_TRAIN, false);
    }
    if (!locked) page = PAGE_LOG;
  } else if (req.kind == LOG_MEAL) {
    xSemaphoreTake(dataLock, portMAX_DELAY);
    localMeal = time(nullptr); plus.meal = localMeal;   // il digiuno riparte da adesso
    xSemaphoreGive(dataLock);
    plusNow = true;
    snprintf(m, sizeof m, "%s  +%d kcal", (const char *)(doc["label"] | ""), (int)(doc["kcal"] | 0));
    toast(m, true, now);
    playSound(SND_MEAL, false);
    if (!locked) page = PAGE_MACRO;
  } else if (req.kind == LOG_WATER) {
    int tot = doc["tot"] | 0;
    xSemaphoreTake(dataLock, portMAX_DELAY);
    if (tot > 0) ext.h2o = tot;                     // il totale vero di Notion (anche acqua segnata altrove)
    xSemaphoreGive(dataLock);
    if ((int32_t)(ui.celUntil - now) <= 0) { snprintf(m, sizeof m, "+%d ml  -  %s L oggi", req.ml, fmt(tot / 1000.0f, 2)); toast(m, true, now); }
  } else if (req.kind == LOG_SUPP) {
    xSemaphoreTake(dataLock, portMAX_DELAY);
    for (int i = 0; i < ext.nsup; i++) if (!strcmp(ext.sup[i].id, req.id)) ext.sup[i].taken = true;
    bool all = ext.nsup > 0 && supTaken(ext) == ext.nsup;
    xSemaphoreGive(dataLock);
    snprintf(m, sizeof m, "%s  assunto", (const char *)(doc["name"] | ""));
    toast(all ? "Integrazione completa!" : m, true, now);
    playSound(all ? SND_TRAIN : SND_UNLOCK, false);
  } else if (req.kind == LOG_WEEK) {
    snprintf(m, sizeof m, "Settimana %d pronta: %d esercizi", (int)(doc["week"] | 0), (int)(doc["n"] | 0));
    toast(m, true, now);
    char nt[48];
    int miss = doc["miss"] | 0;
    if (miss) snprintf(nt, sizeof nt, "Sett. %d pronta, %d esercizi senza carico fatto", (int)(doc["week"] | 0), miss);
    else snprintf(nt, sizeof nt, "Load Tracker sett. %d pronto (%s-%s)", (int)(doc["week"] | 0), (const char *)(doc["d"][0] | ""), (const char *)(doc["d"][1] | ""));
    notify(nt, NK_TRAIN);
    playSound(SND_TRAIN, false);
  } else {                                          // LOG_PRS
    snprintf(lastPrLift, sizeof lastPrLift, "%s", ui.celLift); lastPrE1 = ui.celE1;
    ui.celUntil = 0;
    snprintf(m, sizeof m, "PR %s salvato su Notion", (const char *)(doc["lift"] | ""));
    toast(m, true, now);
    playSound(SND_TRAIN, false);
    if (!locked) page = PAGE_PR;
  }
  quietNext = true;                                 // il feed aggiornato non deve risuonare
  refreshNow = true;
}

static void pollLog(uint32_t now) {
  if (req.state == 2) { handleLogResult(now); req.state = 0; return; }
  // mai partito (Wi-Fi assente): si annulla; se e' gia' in invio si aspetta il timeout HTTP
  if (req.state == 1 && now - req.at > 30000) { req.state = 0; goOffline(now); }
  if (queueSent || queueRejected) {
    char m[48];
    if (queueRejected) snprintf(m, sizeof m, "Offline: %d inviati, %d rifiutati", (int)queueSent, (int)queueRejected);
    else snprintf(m, sizeof m, "Inviati %d dati salvati offline", (int)queueSent);
    toast(m, queueRejected == 0, now); notify(m, queueRejected ? NK_WARN : NK_INFO);
    queueSent = 0; queueRejected = 0;
  }
  if (otaFailed) { otaFailed = false; toast(ui.otaMsg, false, now); playSound(SND_ERROR, false); }
}

// ordine delle sotto-pagine per lo swipe a sinistra
static int nextSubPage(int p) {
  if (p >= PAGE_MACRO && p < PAGE_WEEK) return p + 1;
  return PAGE_MACRO;
}

static void handleTap(int x, int y, int dx, int dy, uint32_t now) {
  bool swipe = abs(dx) > 50 && abs(dx) > abs(dy);
  if (page == PAGE_LOCK) {
    if (ui.lockMode != LOCK_UNLOCK && swipe && dx > 0) { ui.lockMode = LOCK_UNLOCK; page = PAGE_INFO; return; }  // annulla
    if (!swipe) keypadTap(keypadKey(x, y), now);
    return;
  }
  // swipe verticali: giu' dal bordo alto = notifiche (ovunque), su dalla home = Stato
  bool vswipe = abs(dy) > 60 && abs(dy) > abs(dx);
  int y0 = y - dy;
  if (page == PAGE_NOTIF) {
    if ((vswipe && dy < 0) || (swipe && dx > 0) || inRect(x, y, 0, 0, BACK_W, BACK_H)) page = notifPrev;
    return;
  }
  if (vswipe && dy > 0 && y0 < 50 && page != PAGE_KBD) {
    notifPrev = page; page = PAGE_NOTIF;
    xSemaphoreTake(dataLock, portMAX_DELAY); ui.notif.unread = 0; xSemaphoreGive(dataLock);
    return;
  }
  if (vswipe) {
    if (dy < 0 && page == PAGE_HOME) { page = PAGE_INFO; playSound(SND_TICK, false); }
    return;                                         // uno swipe non e' mai un tocco sul pulsante sotto il dito
  }
  if (page == PAGE_HOME) {
    if (swipe) { page = dx < 0 ? PAGE_MACRO : PAGE_HOME; return; }
    if (inRect(x, y, STREAK_BX, 0, STREAK_BW, LOCK_BH)) { page = PAGE_WEEK; return; }   // fiamma: la settimana
    if (inRect(x, y, LOCK_BX, 0, LOCK_BW, LOCK_BH)) {          // lucchetto: blocca e va al salvaschermo
      if (!ui.pinSet) { showLock(LOCK_SET1); setLockMsg("Imposta un PIN per bloccare"); return; }
      lockNow(); lastTouch = now - STANDBY_MS - 1;
      return;
    }
    int t = homeTile(x, y);
    if (t >= 0) { page = t; return; }
    if (inRect(x, y, 0, NEXT_Y, UW, NEXT_H)) { page = PAGE_TRAIN; return; }
    if (inRect(x, y, 0, MACRO_Y, UW, MACRO_H)) { page = PAGE_MACRO; return; }
    return;
  }
  if ((swipe && dx > 0) || inRect(x, y, 0, 0, BACK_W, BACK_H)) {   // indietro: alla pagina madre
    page = page == PAGE_LOG ? PAGE_TRAIN : page == PAGE_SET ? PAGE_LOG : page == PAGE_FAV ? PAGE_MACRO : page == PAGE_PRC ? PAGE_PR
         : page == PAGE_WIFI ? PAGE_INFO : page == PAGE_KBD ? PAGE_WIFI : PAGE_HOME;
    ui.supArmed = -1;
    return;
  }
  if (page == PAGE_MACRO && !swipe && inRect(x, y, MB_X, MB_Y, MB_W, MB_H) && data.nfav > 0) {
    ui.favArmed = -1; page = PAGE_FAV; return;
  }
  if (page == PAGE_TRAIN && !swipe && inRect(x, y, TB_X, TB_Y, TB_W, TB_H) && data.nplan > 0) { page = PAGE_LOG; return; }
  if (page == PAGE_PR && !swipe && inRect(x, y, 12, PRB_Y, UW - 24, PRB_H) && data.valid) {   // "E' un PR?"
    ui.prcLift = min(ui.prcLift, prLiftCount(data) - 1);
    ui.prcKg = data.npr > 0 ? data.pr_w[ui.prcLift] : 60; ui.prcReps = data.npr > 0 ? (int)data.pr_reps[ui.prcLift] : 5;
    page = PAGE_PRC; return;
  }
  if (page == PAGE_PRC && !swipe) {
    int c = prcChip(x, y);
    if (c >= 0 && c < prLiftCount(data)) {
      ui.prcLift = c;
      ui.prcKg = data.npr > 0 ? data.pr_w[c] : ui.prcKg; ui.prcReps = data.npr > 0 ? (int)data.pr_reps[c] : ui.prcReps;
      playSound(SND_TICK, false); return;
    }
    switch (setButton(x, y)) {
      case SB_KG_M: ui.prcKg = max(2.5f, ui.prcKg - 2.5f); break;
      case SB_KG_P: ui.prcKg = min(500.0f, ui.prcKg + 2.5f); break;
      case SB_REP_M: ui.prcReps = max(1, ui.prcReps - 1); break;
      case SB_REP_P: ui.prcReps = min(20, ui.prcReps + 1); break;
      case SB_SEND: {
        float e1 = epley(ui.prcKg, ui.prcReps), best = prLiftBest(data, ui.prcLift);
        const char *lift = prLiftName(data, ui.prcLift);
        char nm[24]; snprintf(nm, sizeof nm, "%s  %s x %d", lift, kg(ui.prcKg), ui.prcReps);
        if (e1 > best + 0.05f) {                    // festa + proposta di salvarlo nel PR Tracker
          celebrate("NUOVO PR!", "kg e1RM", nm, e1, best > 0 ? e1 - best : 0, now, true);
          snprintf(ui.celLift, sizeof ui.celLift, "%s", lift); ui.celKg = ui.prcKg; ui.celReps = ui.prcReps;
        } else {
          char m[48]; snprintf(m, sizeof m, "e1RM %s: mancano %s kg", fmt(e1, 1), fmt(best - e1 + 0.1f, 1));
          toast(m, false, now); playSound(SND_WARN, false);
        }
        return;
      }
      default: return;
    }
    playSound(SND_TICK, false);
    return;
  }
  if (page == PAGE_H2O && !swipe && !ui.busy) {
    int ml = h2oButton(x, y);
    if (ml && ext.valid) {
      xSemaphoreTake(dataLock, portMAX_DELAY);
      int before = ext.h2o, goal = ext.h2oGoal;
      ext.h2o += ml;                                // subito a schermo; si annulla se l'invio fallisce
      xSemaphoreGive(dataLock);
      JsonDocument b; b["t"] = "water"; b["ml"] = ml;
      sendLog(b, LOG_WATER, "", now);
      req.ml = ml;
      if (before < goal && before + ml >= goal) {
        char nm[24]; snprintf(nm, sizeof nm, "obiettivo %s L", fmt(goal / 1000.0f, 1));
        celebrate("IDRATATO!", "L", nm, (before + ml) / 1000.0f, 0, now);
      } else playSound(SND_WATER, false);
    }
    return;
  }
  if (page == PAGE_WEEK && !swipe && plus.wkValid && inRect(x, y, 12, WKB_Y, UW - 24, WKB_H)) {
    if (ui.busy) return;
    if (!ui.wkArmed) { ui.wkArmed = true; playSound(SND_TICK, false); return; }   // secondo tocco: crea la settimana su Notion
    JsonDocument b; b["t"] = "week";
    sendLog(b, LOG_WEEK, "", now);
    return;
  }
  if (page == PAGE_SUP && !swipe) {
    if (ui.busy) return;
    xSemaphoreTake(dataLock, portMAX_DELAY);
    int i = supRow(ext, y);
    if (i >= 0 && ext.sup[i].taken) { ui.supArmed = -1; xSemaphoreGive(dataLock); toast("gia' assunto oggi", true, now); return; }
    if (i >= 0 && ui.supArmed == i) {               // secondo tocco: segna come assunto
      JsonDocument b; b["t"] = "supp"; b["id"] = ext.sup[i].id;
      sendLog(b, LOG_SUPP, ext.sup[i].id, now);
    } else ui.supArmed = i;
    xSemaphoreGive(dataLock);
    if (i >= 0) playSound(SND_TICK, false);
    return;
  }
  if (page == PAGE_LOG) {
    if (swipe) return;
    xSemaphoreTake(dataLock, portMAX_DELAY);
    int i = logRow(data, y);
    if (i >= 0) {
      const PlanEx &e = data.plan[i];
      ui.setIdx = i;
      char why[40];
      ui.setKg = e.used >= 0 ? e.used : suggestKg(e, plus, why, sizeof why);   // consiglio di progressione
      ui.setReps = (int)(e.reps >= 0 ? e.reps : e.rmax > 0 ? e.rmax : 8);
      page = PAGE_SET;
    }
    xSemaphoreGive(dataLock);
    return;
  }
  if (page == PAGE_SET) {
    if (swipe || ui.busy) return;
    switch (setButton(x, y)) {
      case SB_KG_M: ui.setKg = max(0.0f, ui.setKg - 2.5f); break;
      case SB_KG_P: ui.setKg = min(500.0f, ui.setKg + 2.5f); break;
      case SB_REP_M: ui.setReps = max(1, ui.setReps - 1); break;
      case SB_REP_P: ui.setReps = min(50, ui.setReps + 1); break;
      case SB_SEND: {
        xSemaphoreTake(dataLock, portMAX_DELAY);
        if (ui.setIdx < data.nplan) {
          const PlanEx &e = data.plan[ui.setIdx];
          JsonDocument b;
          b["t"] = "set"; b["id"] = e.id; b["name"] = e.name; b["kg"] = ui.setKg; b["reps"] = ui.setReps;
          sendLog(b, LOG_SET, e.id, now);
          req.kg = ui.setKg; req.reps = ui.setReps;      // per segnarla fatta anche offline
        }
        xSemaphoreGive(dataLock);
        break;
      }
      default: return;
    }
    playSound(SND_TICK, false);
    return;
  }
  if (page == PAGE_FAV) {
    if (swipe || ui.busy) return;
    xSemaphoreTake(dataLock, portMAX_DELAY);
    int i = favRow(data, y);
    if (i >= 0 && ui.favArmed == i) {               // secondo tocco sullo stesso pasto: invia
      JsonDocument b;
      b["t"] = "meal"; b["label"] = data.fav[i].name; b["i"] = i;
      sendLog(b, LOG_MEAL, "", now);
    } else ui.favArmed = i;                         // primo tocco: evidenzia
    xSemaphoreGive(dataLock);
    if (i >= 0) playSound(SND_TICK, false);
    return;
  }
  if (page == PAGE_WIFI) {
    int b = wifiButton(x, y);
    if (b == WB_SCAN) { ui.wifi.msg[0] = 0; wifiCmd = 1; playSound(SND_TICK, false); return; }
    if (b == WB_SETUP) { runPortal(); return; }      // URL n8n / token (e reti) dal telefono
    xSemaphoreTake(dataLock, portMAX_DELAY);
    int i = wifiRow(ui.wifi, y);
    WifiNet n = i >= 0 ? ui.wifi.net[i] : WifiNet{};
    xSemaphoreGive(dataLock);
    if (i < 0 || ui.wifi.conn == 1) return;
    if (status.wifi && !strcmp(n.ssid, status.ssid)) { toast("gia' connesso", true, now); return; }
    snprintf(ui.wifi.target, sizeof ui.wifi.target, "%s", n.ssid);
    String pass;
    if (n.open || knownPass(loadNets(), n.ssid, pass)) {      // salvata o aperta: subito
      snprintf(connPass, sizeof connPass, "%s", pass.c_str());
      ui.wifi.msg[0] = 0; ui.wifi.conn = 1; wifiCmd = 2;
    } else { ui.wifi.pass[0] = 0; ui.wifi.kb = 0; page = PAGE_KBD; }   // nuova protetta: password
    playSound(SND_TICK, false);
    return;
  }
  if (page == PAGE_KBD) {
    int k = kbKey(ui.wifi.kb, x, y);
    size_t len = strlen(ui.wifi.pass);
    if (k == KB_NONE) return;
    if (k == KB_DEL) { if (len) ui.wifi.pass[len - 1] = 0; }
    else if (k == KB_SHIFT) ui.wifi.kb = ui.wifi.kb == 1 ? 0 : 1;
    else if (k == KB_MODE) ui.wifi.kb = ui.wifi.kb == 2 ? 0 : 2;
    else if (k == KB_OK) {
      if (len < 8) { toast("WPA: almeno 8 caratteri", false, now); playSound(SND_ERROR, false); return; }
      snprintf(connPass, sizeof connPass, "%s", ui.wifi.pass);
      ui.wifi.msg[0] = 0; ui.wifi.conn = 1; wifiCmd = 2; page = PAGE_WIFI;
    } else if (len < sizeof ui.wifi.pass - 1) {
      ui.wifi.pass[len] = (char)k; ui.wifi.pass[len + 1] = 0;
      if (ui.wifi.kb == 1) ui.wifi.kb = 0;          // maiuscola solo per un carattere
    }
    playSound(SND_TICK, false);
    return;
  }
  if (page == PAGE_INFO) {
    switch (infoButton(x, y)) {
      case IB_REFRESH:
        if (status.fwNew) {                         // "INSTALLA FW": doppio tocco
          if (!ui.fwArmed) { ui.fwArmed = true; playSound(SND_TICK, false); }
          else { ui.fwArmed = false; otaReq = true; ui.otaPct = 0; snprintf(ui.otaMsg, sizeof ui.otaMsg, "avvio..."); }
          break;
        }
        refreshNow = true; plusNow = true; playSound(SND_TICK, false); break;
      case IB_SOUND: ui.sound = !ui.sound; soundEnabled = ui.sound; prefs.putBool("snd", ui.sound); playSound(SND_TICK, false); break;
      case IB_PIN: showLock(LOCK_SET1); break;
      case IB_WIFI: page = PAGE_WIFI; ui.wifi.msg[0] = 0; ui.wifi.conn = 0; wifiCmd = 1; break;
      default: break;
    }
    return;
  }
  if (swipe && dx < 0 && page >= PAGE_MACRO && page <= PAGE_WEEK) { page = nextSubPage(page); ui.supArmed = -1; }
}

static void wake(uint32_t now) {
  if (now - lastTouch > SCREEN_OFF_MS) refreshNow = true;   // dopo lo schermo spento: dati freschi
  lastTouch = now;
  if (locked) showLock(LOCK_UNLOCK);
}

// ---------------- setup / loop ----------------
void setup() {
  pinMode(PIN_PWR_HOLD, OUTPUT);
  digitalWrite(PIN_PWR_HOLD, HIGH);  // prima di tutto: tiene accesa l'alimentazione da batteria
  pinMode(PIN_PWR_KEY, INPUT);
  Serial.begin(115200);
  analogSetPinAttenuation(PIN_BAT_ADC, ADC_11db);

  Backlight_Init();
  Set_Backlight(0);
  LCD_Init();                        // inizializza anche il touch (CST328 o CST3530)
  spr.setPsram(true);
  spr.setColorDepth(16);
  if (!spr.createSprite(UW, UH)) { Serial.println("PSRAM non disponibile"); }

  dataLock = xSemaphoreCreateMutex();
  prefs.begin("valhalla", false);
  cfgUrl = prefs.getString("url", DEFAULT_URL);
  if (cfgUrl.endsWith("/valhalla-display")) cfgUrl = cfgUrl.substring(0, cfgUrl.length() - 7) + "deck";   // v2 -> v2.1
  cfgToken = prefs.getString("token", "");
  cfgPin = prefs.getString("pin", "");
  ui.pinSet = cfgPin.length() == 4;
  ui.sound = prefs.getBool("snd", true);
  soundEnabled = ui.sound;
  String cache = prefs.getString("cache", "");
  if (cache.length()) parseData(cache.c_str(), data);
  String cachex = prefs.getString("cachex", "");
  if (cachex.length()) parseExtra(cachex.c_str(), ext);
  String cachep = prefs.getString("cachep", "");
  if (cachep.length()) parsePlus(cachep.c_str(), plus);
  qBuf = prefs.getString("q", "");
  status.queued = lineCount(qBuf);
  fwInstalled = prefs.getString("fwid", "");
  status.fwNew = plus.fwId[0] && fwInstalled != plus.fwId;

  setenv("TZ", TZ_ROME, 1); tzset();                // ora locale corretta anche prima dell'NTP
  status.imu_ok = imuBegin();                       // avvia anche il bus I2C condiviso con l'RTC
  status.rtc_ok = rtcLoad();                        // ora giusta subito, anche senza rete
  imu.zSign = prefs.getFloat("imuz", 1.0f);

  for (int l = 0; l <= 16; l += 2) {                // logo in dissolvenza all'avvio
    drawSplash(spr, l);
    LCD_addWindow(0, 0, UW - 1, UH - 1, (uint16_t *)spr.getBuffer());
    if (l == 0) Set_Backlight(BL_ACTIVE);
    delay(60);
  }
  delay(700);
  if (!cfgToken.length() || !loadNets().length()) runPortal();   // primo avvio: non ritorna
  audioBegin();
  lockNow();                                                     // con PIN impostato si parte bloccati
  xTaskCreatePinnedToCore(netTask, "net", 16384, nullptr, 1, nullptr, 0);
}

void loop() {
  static uint32_t lastDraw = 0, lastBat = 0, keyDown = 0;
  static bool down = false, keyArmed = false, wasStandby = false;
  static uint16_t x0, y0, lx, ly;
  static int bl = -1;
  uint32_t now = millis();
  if (!lastTouch) lastTouch = now;

  // tasto PWR: armato solo dopo un rilascio, cosi' la pressione d'accensione non spegne subito.
  // Pressione breve = blocco + schermo spento (tasca) oppure risveglio; 3 s = spegnimento.
  bool key = digitalRead(PIN_PWR_KEY) == LOW;
  if (key && keyArmed) { if (!keyDown) keyDown = now; else if (now - keyDown > 3000) powerOff(); }
  if (!key) {
    if (keyArmed && keyDown && now - keyDown < 1000) {
      if (now - lastTouch > STANDBY_MS) wake(now);
      else { lockNow(); lastTouch = now - SCREEN_OFF_MS - 1; }
      lastDraw = 0;
    }
    keyArmed = true; keyDown = 0;
  }

  // touch: tap / swipe valutati al rilascio
  uint16_t tx[5], ty[5]; uint8_t cnt = 0;
  Touch_Read_Data();
  bool pressed = Touch_Get_XY(tx, ty, nullptr, &cnt, 5) && cnt > 0;
  if (pressed) {
    if (!down) { down = true; x0 = tx[0]; y0 = ty[0]; }
    lx = tx[0]; ly = ty[0];
  } else if (down) {
    down = false;
    bool wasIdle = now - lastTouch > STANDBY_MS;
    if (wasIdle) { if (!imu.pocket) wake(now); }     // in tasca i tocchi a schermo spento si ignorano
    else {
      lastTouch = now;
      if (imuLearnFromTouch()) prefs.putFloat("imuz", imu.zSign);
      if ((int32_t)(ui.celUntil - now) > 0) {      // celebrazione: "salva" oppure chiudi
        if (ui.celSave && !ui.busy && inRect(lx, ly, CEL_SAVE_X, CEL_SAVE_Y, CEL_SAVE_W, CEL_SAVE_H)) {
          JsonDocument b; b["t"] = "prs"; b["lift"] = ui.celLift; b["kg"] = ui.celKg; b["reps"] = ui.celReps;
          sendLog(b, LOG_PRS, "", now);
          ui.celUntil = now + 30000;                // resta a schermo ("salvo...") fino alla risposta
        } else if (!ui.busy) ui.celUntil = 0;
      }
      else handleTap(lx, ly, (int)lx - (int)x0, (int)ly - (int)y0, now);
    }
    lastDraw = 0;
  }

  // gesti: alza per accendere, scuoti per aggiornare (mai in tasca)
  imuUpdate(now);
  status.imu_z = imu.z; status.pocket = imu.pocket;
  bool idleNow = now - lastTouch > STANDBY_MS;
  if (imu.raised) { imu.raised = false; if (idleNow && !imu.pocket) { wake(now); lastDraw = 0; } }
  if (imu.shook) {
    imu.shook = false;
    if (!idleNow && !imu.pocket && !locked) { refreshNow = true; toast("Aggiorno...", true, now); playSound(SND_TICK, false); lastDraw = 0; }
  }
  if (rtcSaveReq) { rtcSaveReq = false; rtcSave(); status.rtc_ok = true; }
  pollLog(now);
  if (celPending) {                                  // PR arrivato dal feed: festa (e schermo acceso, non di notte)
    celPending = false;
    bool dup = !strcmp(celPName, lastPrLift) && fabsf(celPE1 - lastPrE1) < 1.0f;
    if (!dup && !quietHours()) { celebrate("NUOVO PR!", "kg e1RM", celPName, celPE1, celPDelta, now); wake(now); }
    lastDraw = 0;
  }
  status.epoch = (long)time(nullptr);
  // finestra digiuno: un avviso quando un integratore del momento attuale si puo' prendere a stomaco vuoto
  static uint32_t nextFast = 0;
  static int fastDay = -1; static uint32_t fastMask = 0;
  static bool recapPending = false, fwToast = false;
  if ((int32_t)(now - nextFast) >= 0) {
    nextFast = now + 60000;
    struct tm t;
    if (getLocalTime(&t, 0)) {
      status.hour = t.tm_hour + t.tm_min / 60.0f;
      if (t.tm_yday != fastDay) { fastDay = t.tm_yday; fastMask = 0; }
      char nm[24] = "";
      int cur = currentMoment(status);
      xSemaphoreTake(dataLock, portMAX_DELAY);
      for (int i = 0; i < ext.nsup && i < 32; i++) {
        const Sup &u = ext.sup[i];
        if (u.taken || u.mom != cur || (fastMask & (1u << i)) || fastLeft(plus, u.id, status.epoch) != 0) continue;
        fastMask |= 1u << i;
        snprintf(nm, sizeof nm, "%s", u.name);
      }
      bool week = plus.wkValid;
      xSemaphoreGive(dataLock);
      if (nm[0] && !quietHours()) {
        char m[48]; snprintf(m, sizeof m, "%s: stomaco vuoto, ora si", nm);
        toast(m, true, now); playSound(SND_COACH); notify(m, NK_INFO);
      }
      // domenica sera: riepilogo della settimana, una volta
      if (t.tm_wday == 0 && t.tm_hour >= 19 && week && prefs.getInt("recapd", -1) != t.tm_yday) { prefs.putInt("recapd", t.tm_yday); recapPending = true; }
    }
  }
  if (recapPending && !locked && now - lastTouch < STANDBY_MS) {
    recapPending = false;
    page = PAGE_WEEK;
    notify(plus.target ? "Settimana in target!" : "Riepilogo della settimana pronto", NK_TRAIN);
    if (plus.target) celebrate("SETTIMANA TOP!", "allenamenti", "settimana in target", (float)plus.allen[0], 0, now);
    lastDraw = 0;
  }
  if (status.fwNew && !fwToast) { fwToast = true; toast("Firmware nuovo: Stato > INSTALLA FW", true, now); notify("Firmware nuovo: Stato > INSTALLA FW", NK_INFO); }
  if (page != PAGE_INFO) ui.fwArmed = false;
  if (page != PAGE_WEEK) ui.wkArmed = false;
  // coach proteine (anche a schermo spento): promemoria sonoro, al massimo ogni 2 h, se si e' indietro col ritmo
  static uint32_t nextCoach = 30UL * 60 * 1000;
  if ((int32_t)(now - nextCoach) >= 0) {
    nextCoach = now + 60000;
    struct tm t;
    if (getLocalTime(&t, 0)) status.hour = t.tm_hour + t.tm_min / 60.0f;
    xSemaphoreTake(dataLock, portMAX_DELAY);
    int def = coachDeficit(data, status);
    xSemaphoreGive(dataLock);
    if (def > 0) {
      char m[48]; snprintf(m, sizeof m, "Coach: mancano %d g di proteine", def);
      playSound(SND_COACH); notify(m, NK_WARN); nextCoach = now + 2UL * 3600 * 1000;
    }
  }

  if (now - lastBat > 5000 || !lastBat) {
    lastBat = now;
    status.bat_v = batteryVolts();
    updateCharging(status.bat_v, now);
    status.bat_pct = smoothPct(status.bat_v, status.charging, now);
  }

  if (ui.otaPct >= 0) {                              // aggiornamento firmware: solo la barra, schermo acceso
    if (bl != BL_ACTIVE) { Set_Backlight(BL_ACTIVE); bl = BL_ACTIVE; }
    drawOta(spr, ui);
    LCD_addWindow(0, 0, UW - 1, UH - 1, (uint16_t *)spr.getBuffer());
    lastTouch = now;
    delay(100);
    return;
  }
  uint32_t idle = now - lastTouch;
  bool standby = idle > STANDBY_MS;
  if (standby && !wasStandby) {                      // entrata in standby: blocca e chiude i dialoghi
    if (page == PAGE_LOCK && ui.lockMode != LOCK_UNLOCK) { ui.lockMode = LOCK_UNLOCK; page = PAGE_INFO; }
    lockNow();
  }
  wasStandby = standby;
  int want = idle > SCREEN_OFF_MS ? 0 : standby ? BL_STANDBY : BL_ACTIVE;
  if (!standby && (int32_t)(ui.celUntil - now) > 0 && celFlash(ui, now)) want = 100;   // luci della festa
  if (want != bl) { Set_Backlight(want); bl = want; }
  if (want == 0) { delay(50); return; }

  bool anim = !standby && (int32_t)(ui.celUntil - now) > 0;
  if (standby || anim || !lastDraw || now - lastDraw >= 1000) {
    struct tm t;
    if (getLocalTime(&t, 0)) {
      status.hour = t.tm_hour + t.tm_min / 60.0f;
      static const char *G[] = {"Dom", "Lun", "Mar", "Mer", "Gio", "Ven", "Sab"};
      static const char *M[] = {"Gen", "Feb", "Mar", "Apr", "Mag", "Giu", "Lug", "Ago", "Set", "Ott", "Nov", "Dic"};
      strftime(status.hm, sizeof status.hm, "%H:%M", &t);
      snprintf(status.date, sizeof status.date, "%s %d %s", G[t.tm_wday], t.tm_mday, M[t.tm_mon]);
    }
    status.age_min = lastOkMs ? (int)((now - lastOkMs) / 60000) : -1;
    xSemaphoreTake(dataLock, portMAX_DELAY);
    if (standby) {
      static uint32_t nextStrike = 0, flashUntil = 0;
      if (now > nextStrike) { nextStrike = now + random(3000, 8000); flashUntil = now + 120; }
      drawStandby(spr, now, now < flashUntil, data, ext, status, locked);
    } else if (anim) {
      drawCelebration(spr, ui, now);
    } else {
      drawPage(spr, page, data, ext, plus, status, ui);
      if ((int32_t)(ui.toastUntil - now) > 0) drawToast(spr, ui);
    }
    xSemaphoreGive(dataLock);
    LCD_addWindow(0, 0, UW - 1, UH - 1, (uint16_t *)spr.getBuffer());
    lastDraw = now;
  }
  delay(standby ? 40 : 20);
}
