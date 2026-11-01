// UI del Valhalla Deck: parsing dei JSON compatti + disegno delle pagine su uno sprite 240x320.
// Header-only e senza dipendenze dall'hardware: lo stesso codice compila anche su PC per l'anteprima.
// Stile "Forgia": nero carbone, arancio Valhalla per titoli e azioni, ghiaccio per i dati, oro per PR e traguardi.
#pragma once
#include <LovyanGFX.hpp>
#include <ArduinoJson.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "assets.h"   // font display (E16/E22/E30/E64) e logo, generati da tools/make_assets.py

#define FW_VERSION "4.2"

static const int UW = 240, UH = 320;
enum { PAGE_HOME, PAGE_MACRO, PAGE_TRAIN, PAGE_PR, PAGE_ATH, PAGE_H2O, PAGE_SUP, PAGE_WEEK,   // home + sotto-pagine (ordine dello swipe)
       PAGE_INFO, PAGE_LOCK, PAGE_LOG, PAGE_SET, PAGE_FAV, PAGE_PRC, PAGE_NOTIF, PAGE_WIFI, PAGE_KBD, PAGE_COUNT };
enum { LOCK_UNLOCK, LOCK_SET1, LOCK_SET2 };
static const int MAX_PS = 30, MAX_PLAN = 10, MAX_FAV = 6, MAX_SUP = 12, MAX_HIST = 40;

struct Ex { char name[40]; float kg, reps; char delta[8]; };
struct PlanEx { char id[33], name[26]; float rmin, rmax, pres, used, reps; };   // used/reps = -1: da fare
struct Fav { char name[26]; float kcal, p, c, f; };
struct Data {
  bool valid = false;
  char upd[6] = "", day[4] = "OFF";
  int pasti = 0;
  float kcal[2] = {0, 0}, p[2] = {0, 0}, c[2] = {0, 0}, f[2] = {0, 0};
  float peso = 0, peso_d7 = 0; char peso_d[6] = "";
  int nps = 0; float ps[MAX_PS];            // storico peso
  int w_pesi = 0, w_cardio = 0, w_pesi30 = 0, w_cardio30 = 0;
  int npr = 0; char pr_name[4][10]; float pr_w[4], pr_reps[4], pr_e1[4];
  bool has_last = false; char last_g[16] = "", last_d[6] = ""; int last_ago = 0; int nex = 0; Ex ex[4];
  int nnext = 0; char next_l[3][10], next_g[3][16];
  int streak = 0; int wk[4] = {0, 0, 0, 0};          // giorni in target; split FENRIR fatti questa settimana
  char plan_d[8] = "", plan_g[16] = ""; int nplan = 0; PlanEx plan[MAX_PLAN];
  int nfav = 0; Fav fav[MAX_FAV];
};
// Feed "Valhalla Deck Extra": scheda atleta, acqua, integrazioni
struct Sup { char id[33], name[23], dose[19]; int mom; bool taken; };    // mom: 0 mattina, 1 pranzo, 2 sera, 3 altro
struct Extra {
  bool valid = false;
  char name[21] = "", fase[14] = "", blk[25] = "", obj[61] = "";
  float h = 0, bf = 0, lbm = 0, visc = 0, pgoal = 0, pt = 0;
  int h2o = 0, h2oGoal = 3000;
  int nsup = 0; Sup sup[MAX_SUP];
};
// Feed "Valhalla Deck Plus": storico esercizi (progressione), ultimo pasto, digiuno, settimana, firmware
struct HistEx { char base[23]; float kg; int reps; bool top, under; };
struct Fast { char id[33]; float h; };
struct Plus {
  bool valid = false;
  int nhist = 0; HistEx hist[MAX_HIST];
  long meal = 0;                          // epoch dell'ultimo pasto di oggi, 0 = nessuno
  int nfast = 0; Fast fast[MAX_SUP];
  bool wkValid = false; char wd0[6] = "", wd1[6] = "";
  int allen[2] = {0, 0}, pAvg = 0, pDays = 0, fDays = 0, h2oDays = 0, npr = 0;
  float h2oAvg = 0, peso0 = 0, peso1 = 0; char pr[3][11];
  bool target = false;
  char fwId[32] = "", fwName[31] = "";
};
// Stato "di sistema" mostrato a schermo
struct Status {
  char hm[6] = "--:--", date[16] = "";
  int bat_pct = -1; float bat_v = 0;      // -1 = sconosciuto
  bool charging = false;                 // stimato dall'andamento della tensione (la scheda non espone lo stato del caricatore)
  bool wifi = false; int rssi = 0; char ssid[33] = "", ip[16] = "";
  char msg[64] = "";                     // ultimo errore/stato rete
  int age_min = -1;                      // minuti dall'ultimo aggiornamento riuscito
  float hour = -1;                       // ora locale decimale, -1 = sconosciuta
  bool imu_ok = false, rtc_ok = false; float imu_z = 0; bool pocket = false;
  long epoch = 0;                        // ora attuale (UTC, s), per i conti alla rovescia
  int queued = 0;                        // log in attesa di rete
  bool fwNew = false;                    // firmware nuovo nella pagina Notion
};
// Ultime notifiche (pannello che scende dall'alto): cosa e' successo e perche' il Deck ha suonato
static const int MAX_NOTIF = 12;
enum { NK_INFO, NK_MEAL, NK_TRAIN, NK_WARN };
struct Notif { char hm[6]; char txt[44]; int kind; };
struct Notifs { Notif n[MAX_NOTIF]; int count = 0, head = 0, unread = 0; };   // head = prossima da scrivere
// Gestione Wi-Fi dal Deck: reti trovate, connessione in corso, password digitata
static const int MAX_NETS_SCAN = 9;
struct WifiNet { char ssid[33]; int rssi; bool open, saved; };
struct WifiUi {
  int n = 0; WifiNet net[MAX_NETS_SCAN];
  bool scanning = false; int conn = 0;   // conn: 0 niente, 1 in corso, 2 ok, 3 fallita
  char msg[40] = "", target[33] = "", pass[65] = "";
  int kb = 0;                            // tastiera: 0 minuscole, 1 maiuscole, 2 simboli
};
// Stato dell'interfaccia (scritto solo dal loop)
struct Ui {
  int lockMode = LOCK_UNLOCK;
  char entry[5] = "";                    // cifre digitate sul tastierino
  char lockMsg[32] = "Inserisci PIN";
  bool pinSet = false, sound = true;
  int setIdx = 0; float setKg = 0; int setReps = 0;   // serie in modifica
  int favArmed = -1, supArmed = -1;      // voce selezionata, in attesa del secondo tocco
  int prcLift = 0; float prcKg = 100; int prcReps = 5; // "E' un PR?"
  char toast[48] = ""; bool toastOk = true; uint32_t toastUntil = 0;
  bool busy = false;                     // invio in corso
  // celebrazione (PR, acqua): titolo, valore grande, unita', riga sotto, delta, tasto "salva"
  uint32_t celStart = 0, celUntil = 0;
  char celTitle[16] = "", celUnit[10] = "", celName[24] = ""; float celE1 = 0, celDelta = 0;
  bool celSave = false; char celLift[10] = ""; float celKg = 0; int celReps = 0;
  bool fwArmed = false;                  // "INSTALLA FW": serve un secondo tocco
  bool wkArmed = false;                  // "PREPARA SETTIMANA": serve un secondo tocco
  int otaPct = -1; char otaMsg[40] = "";  // aggiornamento firmware in corso (-1 = no)
  Notifs notif;                          // scritte anche dal task di rete, sotto dataLock
  WifiUi wifi;                           // risultati della scansione scritti dal task di rete, sotto dataLock
};

static void cpy(char* dst, size_t n, const char* src) { snprintf(dst, n, "%s", src ? src : ""); }

static bool parseData(const char* json, Data& d) {
  JsonDocument doc;
  if (deserializeJson(doc, json)) return false;
  Data n;
  cpy(n.upd, sizeof n.upd, doc["upd"] | "");
  cpy(n.day, sizeof n.day, doc["day"] | "OFF");
  n.pasti = doc["pasti"] | 0;
  const char* keys[4] = {"kcal", "p", "c", "f"};
  float* dst[4] = {n.kcal, n.p, n.c, n.f};
  for (int i = 0; i < 4; i++) { dst[i][0] = doc[keys[i]][0] | 0.0f; dst[i][1] = doc[keys[i]][1] | 0.0f; }
  n.peso = doc["peso"][0] | 0.0f; n.peso_d7 = doc["peso"][1] | 0.0f; cpy(n.peso_d, sizeof n.peso_d, doc["peso"][2] | "");
  for (JsonVariant v : doc["ps"].as<JsonArray>()) { if (n.nps >= MAX_PS) break; n.ps[n.nps++] = v | 0.0f; }
  n.w_pesi = doc["week"]["pesi"] | 0; n.w_cardio = doc["week"]["cardio"] | 0;
  n.w_pesi30 = doc["week"]["pesi30"] | 0; n.w_cardio30 = doc["week"]["cardio30"] | 0;
  for (JsonArray r : doc["pr"].as<JsonArray>()) {
    if (n.npr >= 4) break;
    cpy(n.pr_name[n.npr], 10, r[0] | ""); n.pr_w[n.npr] = r[1] | 0.0f; n.pr_reps[n.npr] = r[2] | 0.0f; n.pr_e1[n.npr] = r[3] | 0.0f;
    n.npr++;
  }
  JsonObject last = doc["last"];
  if (!last.isNull()) {
    n.has_last = true;
    cpy(n.last_g, sizeof n.last_g, last["g"] | ""); cpy(n.last_d, sizeof n.last_d, last["d"] | "");
    n.last_ago = last["ago"] | 0;
    for (JsonArray e : last["ex"].as<JsonArray>()) {
      if (n.nex >= 4) break;
      Ex& x = n.ex[n.nex++];
      cpy(x.name, sizeof x.name, e[0] | ""); x.kg = e[1] | 0.0f; x.reps = e[2] | 0.0f; cpy(x.delta, sizeof x.delta, e[3] | "");
    }
  }
  for (JsonArray e : doc["next"].as<JsonArray>()) {
    if (n.nnext >= 3) break;
    cpy(n.next_l[n.nnext], 10, e[0] | ""); cpy(n.next_g[n.nnext], 16, e[1] | ""); n.nnext++;
  }
  n.streak = doc["streak"] | 0;
  for (int i = 0; i < 4; i++) n.wk[i] = doc["wk"][i] | 0;
  JsonObject pl = doc["plan"];
  if (!pl.isNull()) {
    cpy(n.plan_d, sizeof n.plan_d, pl["d"] | ""); cpy(n.plan_g, sizeof n.plan_g, pl["g"] | "");
    for (JsonArray e : pl["ex"].as<JsonArray>()) {
      if (n.nplan >= MAX_PLAN) break;
      PlanEx& x = n.plan[n.nplan++];
      cpy(x.id, sizeof x.id, e[0] | ""); cpy(x.name, sizeof x.name, e[1] | "");
      x.rmin = e[2] | 0.0f; x.rmax = e[3] | 0.0f; x.pres = e[4] | 0.0f; x.used = e[5] | -1.0f; x.reps = e[6] | -1.0f;
    }
  }
  for (JsonArray e : doc["fav"].as<JsonArray>()) {
    if (n.nfav >= MAX_FAV) break;
    Fav& f = n.fav[n.nfav++];
    cpy(f.name, sizeof f.name, e[0] | ""); f.kcal = e[1] | 0.0f; f.p = e[2] | 0.0f; f.c = e[3] | 0.0f; f.f = e[4] | 0.0f;
  }
  n.valid = true;
  d = n;
  return true;
}

static bool parseExtra(const char* json, Extra& x) {
  JsonDocument doc;
  if (deserializeJson(doc, json)) return false;
  Extra n;
  JsonObject a = doc["ath"];
  cpy(n.name, sizeof n.name, a["n"] | ""); cpy(n.fase, sizeof n.fase, a["fase"] | "");
  cpy(n.blk, sizeof n.blk, a["blk"] | ""); cpy(n.obj, sizeof n.obj, a["obj"] | "");
  n.h = a["h"] | 0.0f; n.bf = a["bf"] | 0.0f; n.lbm = a["lbm"] | 0.0f; n.visc = a["visc"] | 0.0f;
  n.pgoal = a["pgoal"] | 0.0f; n.pt = a["pt"] | 0.0f;
  n.h2o = doc["h2o"][0] | 0; n.h2oGoal = doc["h2o"][1] | 3000;
  if (n.h2oGoal <= 0) n.h2oGoal = 3000;
  for (JsonArray e : doc["sup"].as<JsonArray>()) {
    if (n.nsup >= MAX_SUP) break;
    Sup& s = n.sup[n.nsup++];
    cpy(s.id, sizeof s.id, e[0] | ""); cpy(s.name, sizeof s.name, e[1] | ""); cpy(s.dose, sizeof s.dose, e[2] | "");
    s.mom = e[3] | 3; s.taken = (e[4] | 0) != 0;
  }
  n.valid = true;
  x = n;
  return true;
}

static bool parsePlus(const char* json, Plus& x) {
  JsonDocument doc;
  if (deserializeJson(doc, json)) return false;
  Plus n;
  for (JsonArray e : doc["hist"].as<JsonArray>()) {
    if (n.nhist >= MAX_HIST) break;
    HistEx& h = n.hist[n.nhist++];
    cpy(h.base, sizeof h.base, e[0] | ""); h.kg = e[1] | 0.0f; h.reps = e[2] | 0; h.top = (e[3] | 0) != 0; h.under = (e[4] | 0) != 0;
  }
  n.meal = doc["meal"] | 0L;
  for (JsonArray e : doc["fast"].as<JsonArray>()) {
    if (n.nfast >= MAX_SUP) break;
    Fast& f = n.fast[n.nfast++];
    cpy(f.id, sizeof f.id, e[0] | ""); f.h = e[1] | 0.0f;
  }
  JsonObject w = doc["wk"];
  if (!w.isNull()) {
    n.wkValid = true;
    cpy(n.wd0, sizeof n.wd0, w["d"][0] | ""); cpy(n.wd1, sizeof n.wd1, w["d"][1] | "");
    n.allen[0] = w["allen"][0] | 0; n.allen[1] = w["allen"][1] | 0;
    n.pAvg = w["p"][0] | 0; n.pDays = w["p"][1] | 0; n.fDays = w["p"][2] | 0;
    n.h2oAvg = w["h2o"][0] | 0.0f; n.h2oDays = w["h2o"][1] | 0;
    n.peso0 = w["peso"][0] | 0.0f; n.peso1 = w["peso"][1] | 0.0f;
    for (JsonVariant v : w["pr"].as<JsonArray>()) { if (n.npr >= 3) break; cpy(n.pr[n.npr++], 11, v | ""); }
    n.target = (w["target"] | 0) != 0;
  }
  cpy(n.fwId, sizeof n.fwId, doc["fw"][0] | ""); cpy(n.fwName, sizeof n.fwName, doc["fw"][1] | "");
  n.valid = true;
  x = n;
  return true;
}

// ---------------- palette "Forgia" e helper ----------------
static constexpr uint16_t C565(uint32_t rgb) {
  return (uint16_t)(((rgb >> 8) & 0xF800) | ((rgb >> 5) & 0x07E0) | ((rgb & 0xFF) >> 3));
}
struct Pal { uint16_t bg, panel, line, white, dim, acc, ice, gold, ok, red, cp, cc, cf; };
static Pal pal(LGFX_Sprite&) {
  return { C565(0x0B0B0D), C565(0x17181C), C565(0x2A2C33), C565(0xF2F2F2), C565(0x8A8F98),
           C565(0xFF4D00), C565(0x7FD3FF), C565(0xFFB627), C565(0xFF8A3D), C565(0xFF3B3B),
           C565(0x7FD3FF), C565(0xFFB627), C565(0xFF8A3D) };   // proteine ghiaccio, carbo oro, grassi arancio chiaro
}
static const lgfx::IFont* const FE16 = &E16;
static const lgfx::IFont* const FE22 = &E22;
static const lgfx::IFont* const FE30 = &E30;
static const lgfx::IFont* const FE64 = &E64;

static const char* fmt(float v, int dec = 0) {
  static char b[4][16]; static int i = 0; i = (i + 1) & 3;
  if (dec == 0) snprintf(b[i], 16, "%d", (int)lroundf(v)); else snprintf(b[i], 16, "%.*f", dec, v);
  return b[i];
}
static const char* kg(float v) { return fmt(v, fabsf(v - roundf(v)) > 0.01f ? 1 : 0); }
static float epley(float w, float r) { return w <= 0 ? 0 : r <= 1 ? w : w * (1 + r / 30); }

static void text(LGFX_Sprite& s, const char* t, int x, int y, const lgfx::IFont* f, uint16_t col, textdatum_t dt = top_left) {
  s.setFont(f); s.setTextColor(col); s.setTextDatum(dt); s.drawString(t, x, y);
}
static int textW(LGFX_Sprite& s, const char* t, const lgfx::IFont* f) { s.setFont(f); return s.textWidth(t); }

// Testo accorciato con "." finche' non sta in maxW pixel (buffer a rotazione come fmt)
static const char* fit(LGFX_Sprite& s, const char* t, const lgfx::IFont* f, int maxW) {
  static char b[4][64]; static int k = 0; k = (k + 1) & 3;
  cpy(b[k], sizeof b[k], t);
  int n = strlen(b[k]);
  while (n > 1 && textW(s, b[k], f) > maxW) { b[k][--n] = 0; b[k][n - 1] = '.'; }
  return b[k];
}

static bool inRect(int x, int y, int rx, int ry, int rw, int rh) { return x >= rx && x < rx + rw && y >= ry && y < ry + rh; }

static uint16_t blend(uint16_t a, uint16_t b, int alpha16) {   // alpha16 0..16: 16 = tutto a
  int ra = a >> 11, ga = (a >> 5) & 63, ba = a & 31, rb = b >> 11, gb = (b >> 5) & 63, bb = b & 31;
  return (uint16_t)(((rb + (ra - rb) * alpha16 / 16) << 11) | ((gb + (ga - gb) * alpha16 / 16) << 5) | (bb + (ba - bb) * alpha16 / 16));
}

static void card(LGFX_Sprite& s, const Pal& P, int x, int y, int w, int h, uint16_t border = 0) {
  s.fillRoundRect(x, y, w, h, 8, P.panel);
  s.drawRoundRect(x, y, w, h, 8, border ? border : P.line);
}

// Logo Valhalla (alfa 4 bit) fuso su un fondo uniforme; level 0..16 = luminosita'
static void drawLogo(LGFX_Sprite& s, int x0, int y0, uint16_t col, uint16_t bg, int level = 16) {
  for (int y = 0; y < LOGO_H; y++)
    for (int x = 0; x < LOGO_W; x++) {
      int i = y * LOGO_W + x;
      int a = (LOGO_A[i >> 1] >> ((i & 1) ? 0 : 4)) & 15;
      if (a) s.drawPixel(x0 + x, y0 + y, blend(col, bg, (a + 1) * level / 16));
    }
}

// Batteria 30x13 con la percentuale dentro; in carica: fulmine oro. Senza batteria (solo USB): "USB".
static const int BAT_W = 30, BAT_H = 13;
static void battery(LGFX_Sprite& s, const Pal& P, int x, int y, int pct, bool charging) {
  uint16_t c = pct < 0 ? P.dim : charging ? P.gold : pct <= 15 ? P.red : pct <= 35 ? P.gold : P.ice;
  if (pct >= 0) s.fillRoundRect(x + 1, y + 1, (BAT_W - 2) * pct / 100, BAT_H - 2, 2, blend(c, P.bg, 5));   // livello, tenue sotto il numero
  s.drawRoundRect(x, y, BAT_W, BAT_H, 3, c); s.fillRect(x + BAT_W, y + 4, 2, 5, c);
  char b[6]; snprintf(b, sizeof b, "%d", pct);
  const char* t = pct < 0 ? "USB" : b;
  if (charging && pct >= 0) {
    s.fillTriangle(x + 7, y + 2, x + 3, y + 7, x + 6, y + 7, P.gold);
    s.fillTriangle(x + 6, y + 6, x + 9, y + 6, x + 4, y + 11, P.gold);
    text(s, t, x + BAT_W - 3, y + BAT_H / 2 + 1, &fonts::Font0, P.white, middle_right);
  } else text(s, t, x + BAT_W / 2, y + BAT_H / 2 + 1, &fonts::Font0, pct < 0 ? P.dim : P.white, middle_center);
}

static void statusIcons(LGFX_Sprite& s, const Pal& P, const Status& st, int y) {
  battery(s, P, UW - 36, y - 1, st.bat_pct, st.charging);
  if (!st.wifi) s.fillCircle(UW - 42, y + 5, 3, P.red);
}

// Area del pulsante "indietro" nelle sotto-pagine
static const int BACK_W = 70, BACK_H = 40;

static void header(LGFX_Sprite& s, const Pal& P, const char* title, const Status& st) {
  s.fillScreen(P.bg);
  s.fillTriangle(10, 18, 19, 10, 19, 26, P.acc);           // freccia indietro
  text(s, title, 28, 8, FE22, P.acc);
  text(s, st.hm, UW - 46, 10, &fonts::DejaVu12, P.dim, top_right);
  statusIcons(s, P, st, 11);
  s.drawFastHLine(10, 36, UW - 20, P.line);
  s.fillRect(28, 35, 36, 2, P.acc);
}

static void bar(LGFX_Sprite& s, const Pal& P, int x, int y, int w, int h, float frac, uint16_t col) {
  s.fillRoundRect(x, y, w, h, h / 2, P.line);
  float f = frac < 0 ? 0 : frac > 1 ? 1 : frac;
  if (f > 0) s.fillRoundRect(x, y, (int)(w * f) < h ? h : (int)(w * f), h, h / 2, col);
  if (frac > 1) s.fillRoundRect(x + w - h, y, h, h, h / 2, P.red);  // target superato
}

static void waiting(LGFX_Sprite& s, const Pal& P, const Status& st, const char* what = "In attesa dei dati") {
  text(s, what, UW / 2, 140, FE22, P.dim, middle_center);
  text(s, st.msg, UW / 2, 168, &fonts::DejaVu12, P.dim, middle_center);
}

static void footer_age(LGFX_Sprite& s, const Pal& P, const Data& d, const Status& st) {
  char b[48];
  if (st.age_min > 15) snprintf(b, sizeof b, "dati di %d min fa", st.age_min);
  else snprintf(b, sizeof b, "agg. %s", d.upd);
  text(s, b, UW / 2, UH - 12, &fonts::Font0, st.age_min > 15 ? P.gold : P.dim, top_center);
}

static void dayChip(LGFX_Sprite& s, const Pal& P, const Data& d, int x, int y) {
  bool on = strcmp(d.day, "ON") == 0;
  s.fillRoundRect(x, y, 44, 20, 6, on ? P.acc : P.panel);
  if (!on) s.drawRoundRect(x, y, 44, 20, 6, P.line);
  text(s, d.day, x + 22, y + 10, FE16, on ? P.bg : P.white, middle_center);
}

// Coach proteine: grammi sotto il ritmo atteso (target distribuito tra le 7 e le 21), 0 se in linea
static int coachDeficit(const Data& d, const Status& st) {
  if (!d.valid || st.hour < 10 || st.hour > 22 || d.p[1] <= 0) return 0;
  float frac = (st.hour - 7) / 14; if (frac > 1) frac = 1;
  int def = (int)lroundf(d.p[1] * frac - d.p[0]);
  return def > 30 ? def : 0;
}
static int currentMoment(const Status& st) { return st.hour < 0 ? -1 : st.hour < 11 ? 0 : st.hour < 16 ? 1 : 2; }
// Progressione a doppio progressivo: tutte le serie al top del range -> sali; qualcuna sotto il minimo -> tieni
static const HistEx* histFor(const Plus& p, const char* planName) {
  char b[64]; cpy(b, sizeof b, planName);
  char* h = strrchr(b, '#');
  if (h && h > b && h[-1] == ' ') { *h = 0; }                     // "Stacco rumeno #2" -> "Stacco rumeno "
  b[22] = 0;
  for (int i = (int)strlen(b) - 1; i >= 0 && b[i] == ' '; i--) b[i] = 0;
  for (char* c = b; *c; c++) *c = (char)tolower((unsigned char)*c);
  for (int i = 0; i < p.nhist; i++) if (!strcmp(p.hist[i].base, b)) return &p.hist[i];
  return nullptr;
}
static float suggestKg(const PlanEx& e, const Plus& p, char* why, size_t n) {
  const HistEx* h = histFor(p, e.name);
  if (!h || h->kg <= 0) { snprintf(why, n, "presunto %s kg", kg(e.pres)); return e.pres; }
  // shortcut: +2.5 kg fisso (+1 sotto i 20 kg); per manubri/macchine con altri salti servirebbe il passo nel Load Tracker
  if (h->top) { float k = h->kg + (h->kg < 20 ? 1.0f : 2.5f); snprintf(why, n, "ultima %sx%d al top", kg(h->kg), h->reps); return k; }
  if (h->under) { snprintf(why, n, "ultima %sx%d sotto range", kg(h->kg), h->reps); return h->kg; }
  snprintf(why, n, "ultima %sx%d: +1 rep", kg(h->kg), h->reps);
  return h->kg;
}

// Digiuno: secondi che mancano perche' l'integratore sia "a stomaco vuoto" (0 = ok, -1 = nessun vincolo)
static long fastLeft(const Plus& p, const char* supId, long now) {
  for (int i = 0; i < p.nfast; i++)
    if (p.fast[i].h > 0 && !strcmp(p.fast[i].id, supId)) {
      if (!p.meal || now <= 0) return 0;
      long left = p.meal + (long)(p.fast[i].h * 3600) - now;
      return left > 0 ? left : 0;
    }
  return -1;
}

// Pasto preferito che chiude meglio i macro che restano (proteine prima di tutto), -1 se non serve nulla
static int bestFav(const Data& d) {
  float rp = d.p[1] - d.p[0], rc = d.c[1] - d.c[0], rf = d.f[1] - d.f[0], rk = d.kcal[1] - d.kcal[0];
  if (!d.valid || d.nfav == 0 || rk < 150) return -1;
  int best = -1; float bs = 1e9f;
  for (int i = 0; i < d.nfav; i++) {
    const Fav& f = d.fav[i];
    float sc = 2 * fabsf(rp - f.p) / fmaxf(rp, 20) + fabsf(rc - f.c) / fmaxf(rc, 30) + fabsf(rf - f.f) / fmaxf(rf, 10)
             + (f.kcal > rk + 100 ? 3 : 0);
    if (sc < bs) { bs = sc; best = i; }
  }
  return best;
}

static int supTaken(const Extra& x) { int n = 0; for (int i = 0; i < x.nsup; i++) n += x.sup[i].taken; return n; }

static void flame(LGFX_Sprite& s, const Pal& P, int cx, int cy) {
  s.fillCircle(cx, cy + 3, 6, P.acc);
  s.fillTriangle(cx - 6, cy + 2, cx + 6, cy + 2, cx + 1, cy - 10, P.acc);
  s.fillCircle(cx, cy + 4, 3, P.gold);
}
static void drop(LGFX_Sprite& s, int cx, int cy, int r, uint16_t c) {
  s.fillCircle(cx, cy, r, c);
  s.fillTriangle(cx - r + 1, cy - 1, cx + r - 1, cy - 1, cx, cy - r * 2 - 1, c);
}

// ---------------- icone (primitive, niente bitmap) ----------------
static void iconMacro(LGFX_Sprite& s, const Pal& P, int cx, int cy) {
  s.fillArc(cx, cy, 12, 7, 272, 388, P.cp);   // angoli > 360 come nell'anello kcal
  s.fillArc(cx, cy, 12, 7, 392, 508, P.cc);
  s.fillArc(cx, cy, 12, 7, 512, 628, P.cf);
}
static void iconTrain(LGFX_Sprite& s, const Pal& P, int cx, int cy) {
  s.fillRect(cx - 11, cy - 2, 22, 4, P.white);
  s.fillRoundRect(cx - 16, cy - 9, 5, 18, 1, P.acc); s.fillRoundRect(cx + 11, cy - 9, 5, 18, 1, P.acc);
  s.fillRoundRect(cx - 11, cy - 6, 3, 12, 1, P.acc); s.fillRoundRect(cx + 8, cy - 6, 3, 12, 1, P.acc);
}
static void iconPR(LGFX_Sprite& s, const Pal& P, int cx, int cy) {
  s.drawCircle(cx - 8, cy - 5, 4, P.gold); s.drawCircle(cx + 8, cy - 5, 4, P.gold);
  s.fillRoundRect(cx - 7, cy - 11, 14, 12, 4, P.gold);
  s.fillRect(cx - 2, cy + 1, 4, 4, P.gold);
  s.fillRoundRect(cx - 6, cy + 5, 12, 4, 1, P.gold);
}
static void iconAth(LGFX_Sprite& s, const Pal& P, int cx, int cy) {    // busto: testa + spalle
  s.fillCircle(cx, cy - 6, 5, P.white);
  s.fillRoundRect(cx - 11, cy + 1, 22, 10, 5, P.acc);
}
static void iconH2O(LGFX_Sprite& s, const Pal& P, int cx, int cy) { drop(s, cx, cy + 4, 7, P.ice); s.fillCircle(cx - 2, cy + 3, 2, P.white); }
static void iconSup(LGFX_Sprite& s, const Pal& P, int cx, int cy) {    // capsula
  s.fillRoundRect(cx - 12, cy - 5, 24, 11, 5, P.white);
  s.fillRoundRect(cx, cy - 5, 12, 11, 5, P.acc); s.fillRect(cx, cy - 5, 4, 11, P.acc);
}

// Griglia di icone in fondo alla home: 2 righe x 3
static const int TILE_Y = 212, TILE_H = 50, TILE_W = 72, TILE_X0 = 6, TILE_STEP = 78, TILE_DY = 54;
static const int TILE_PAGE[6] = {PAGE_MACRO, PAGE_TRAIN, PAGE_PR, PAGE_ATH, PAGE_H2O, PAGE_SUP};
// Zone toccabili della home
static const int HOME_SECRET_H = 100;            // orologio: 5 tap = pagina STATO
static const int NEXT_Y = 106, NEXT_H = 44;      // card prossimo allenamento -> TRAINING
static const int MACRO_Y = 156, MACRO_H = 52;    // riepilogo macro -> MACRO
static const int LOCK_BX = 172, LOCK_BW = 32, LOCK_BH = 30;   // lucchetto in alto: blocca subito
static const int STREAK_BX = 56, STREAK_BW = 44;                // fiamma dello streak: riepilogo della settimana

static int homeTile(int x, int y) {
  for (int i = 0; i < 6; i++)
    if (inRect(x, y, TILE_X0 + (i % 3) * TILE_STEP, TILE_Y + (i / 3) * TILE_DY, TILE_W, TILE_H)) return TILE_PAGE[i];
  return -1;
}

// Pallino delle notifiche non lette, in alto al centro (scorri giu' per vederle)
static void notifBadge(LGFX_Sprite& s, const Pal& P, int unread) {
  if (unread <= 0) return;
  char b[6]; snprintf(b, sizeof b, "%d", unread > 9 ? 9 : unread);
  s.fillRoundRect(UW / 2 - 12, 0, 24, 11, 5, P.acc);
  text(s, b, UW / 2, 6, &fonts::Font0, P.bg, middle_center);
}

static void pageHome(LGFX_Sprite& s, const Pal& P, const Data& d, const Extra& x, const Status& st) {
  s.fillScreen(P.bg);
  if (d.valid) {
    dayChip(s, P, d, 10, 6);
    if (d.streak > 0) { flame(s, P, 66, 15); text(s, fmt((float)d.streak), 76, 9, &fonts::DejaVu12, P.gold); }
    int def = coachDeficit(d, st);
    if (def) {
      char cb[16]; snprintf(cb, sizeof cb, "P -%dg", def);
      s.drawRoundRect(102, 6, 68, 20, 6, P.acc);
      text(s, cb, 136, 16, &fonts::DejaVu12, P.acc, middle_center);
    }
  }
  statusIcons(s, P, st, 10);
  s.fillArc(LOCK_BX + 14, 13, 5, 3, 180, 360, P.dim);           // lucchetto
  s.fillRect(LOCK_BX + 9, 12, 3, 4, P.dim); s.fillRect(LOCK_BX + 17, 12, 3, 4, P.dim);
  s.fillRoundRect(LOCK_BX + 7, 15, 15, 11, 2, P.dim);
  s.fillRect(LOCK_BX + 14, 18, 2, 4, P.bg);
  text(s, st.hm, UW / 2, 22, FE64, P.white, top_center);
  char b[48];
  if (d.valid) snprintf(b, sizeof b, "%s  -  agg. %s", st.date, d.upd); else snprintf(b, sizeof b, "%s", st.date);
  text(s, b, UW / 2, 87, &fonts::DejaVu12, st.age_min > 15 ? P.gold : P.dim, top_center);

  // prossimo allenamento
  card(s, P, 10, NEXT_Y, UW - 20, NEXT_H);
  text(s, "PROSSIMO", 20, NEXT_Y + 6, &fonts::Font0, P.dim);
  if (d.valid && d.nnext > 0) {
    text(s, fit(s, d.next_g[0], FE22, UW - 100), 20, NEXT_Y + 18, FE22, P.white);
    text(s, d.next_l[0], UW - 20, NEXT_Y + 8, &fonts::DejaVu12, P.acc, top_right);
  } else {
    text(s, d.valid ? "Riposo" : "--", 20, NEXT_Y + 18, FE22, P.dim);
  }
  if (d.valid)                                       // split FENRIR fatti questa settimana: LA PS LB PL
    for (int i = 0; i < 4; i++) {
      int cx = UW - 54 + i * 10, cy = NEXT_Y + 32;
      if (d.wk[i]) s.fillCircle(cx, cy, 3, P.ice); else s.drawCircle(cx, cy, 3, P.dim);
    }

  // macro del giorno
  if (d.valid) {
    float ke = d.kcal[0], kt = d.kcal[1], res = kt - ke;
    snprintf(b, sizeof b, "%s / %s kcal", fmt(ke), fmt(kt));
    text(s, b, 12, MACRO_Y, &fonts::DejaVu18, P.white);
    snprintf(b, sizeof b, res >= 0 ? "-%s" : "+%s", fmt(fabsf(res)));
    text(s, b, UW - 12, MACRO_Y + 2, &fonts::DejaVu18, res >= 0 ? P.gold : P.red, top_right);
    bar(s, P, 12, MACRO_Y + 22, UW - 24, 8, kt > 0 ? ke / kt : 0, P.acc);
    const char* lab[3] = {"P", "C", "G"};
    const float* v[3] = {d.p, d.c, d.f};
    uint16_t col[3] = {P.cp, P.cc, P.cf};
    for (int i = 0; i < 3; i++) {
      int xx = 12 + i * 74;
      snprintf(b, sizeof b, "%s %s/%s", lab[i], fmt(v[i][0]), fmt(v[i][1]));
      text(s, b, xx, MACRO_Y + 34, &fonts::Font0, col[i]);
      bar(s, P, xx, MACRO_Y + 44, 68, 4, v[i][1] > 0 ? v[i][0] / v[i][1] : 0, col[i]);
    }
  } else {
    text(s, st.msg[0] ? st.msg : "In attesa dei dati", UW / 2, MACRO_Y + 20, &fonts::DejaVu12, P.dim, top_center);
  }

  // icone
  static const char* L[6] = {"MACRO", "TRAINING", "PR", "ATLETA", "ACQUA", "INTEGRA"};
  for (int i = 0; i < 6; i++) {
    int tx = TILE_X0 + (i % 3) * TILE_STEP, ty = TILE_Y + (i / 3) * TILE_DY, cx = tx + TILE_W / 2, cy = ty + 16;
    card(s, P, tx, ty, TILE_W, TILE_H);
    switch (i) {
      case 0: iconMacro(s, P, cx, cy); break; case 1: iconTrain(s, P, cx, cy); break; case 2: iconPR(s, P, cx, cy); break;
      case 3: iconAth(s, P, cx, cy); break; case 4: iconH2O(s, P, cx, cy); break; default: iconSup(s, P, cx, cy);
    }
    text(s, L[i], cx, ty + 29, FE16, P.white, top_center);
    if (x.valid && i == 4) {                         // badge: litri di oggi
      snprintf(b, sizeof b, "%s", fmt(x.h2o / 1000.0f, 1));
      text(s, b, tx + TILE_W - 5, ty + 4, &fonts::Font0, x.h2o >= x.h2oGoal ? P.gold : P.ice, top_right);
    }
    if (x.valid && i == 5 && x.nsup > 0) {           // badge: integrazioni prese / totali
      snprintf(b, sizeof b, "%d/%d", supTaken(x), x.nsup);
      text(s, b, tx + TILE_W - 5, ty + 4, &fonts::Font0, supTaken(x) == x.nsup ? P.gold : P.ice, top_right);
    }
  }
}

static const int MB_X = 12, MB_Y = 274, MB_W = UW - 24, MB_H = 28;     // "+ Pasto preferito"
static const int TB_X = 12, TB_Y = 260, TB_W = UW - 24, TB_H = 36;     // "Registra serie"

static void actionButton(LGFX_Sprite& s, const Pal& P, int x, int y, int w, int h, const char* label, bool filled = false) {
  s.fillRoundRect(x, y, w, h, 10, filled ? P.acc : P.panel);
  s.drawRoundRect(x, y, w, h, 10, P.acc);
  text(s, label, x + w / 2, y + h / 2, FE22, filled ? P.bg : P.acc, middle_center);
}

static void pageMacro(LGFX_Sprite& s, const Pal& P, const Data& d, const Status& st) {
  header(s, P, "MACRO", st);
  if (!d.valid) return waiting(s, P, st);
  dayChip(s, P, d, 98, 8);

  float ke = d.kcal[0], kt = d.kcal[1], frac = kt > 0 ? ke / kt : 0;
  int cx = 64, cy = 100, r = 50;
  s.fillArc(cx, cy, r, r - 10, 0, 360, P.line);
  float a = (frac > 1 ? 1 : frac) * 360;
  if (a > 0) s.fillArc(cx, cy, r, r - 10, 270, 270 + a, frac > 1 ? P.red : P.acc);
  text(s, fmt(frac * 100), cx, cy - 6, &fonts::DejaVu24, P.white, middle_center);
  text(s, "%", cx, cy + 16, &fonts::DejaVu12, P.dim, middle_center);
  text(s, "ASSUNTE", 130, 52, FE16, P.dim);
  text(s, fmt(ke), 130, 68, &fonts::DejaVu24, P.white);
  float res = kt - ke;
  text(s, res >= 0 ? "RESTANO" : "SFORATO", 130, 100, FE16, P.dim);
  text(s, fmt(fabsf(res)), 130, 116, &fonts::DejaVu24, res >= 0 ? P.gold : P.red);
  char tb[24]; snprintf(tb, sizeof tb, "su %s kcal", fmt(kt));
  text(s, tb, 130, 144, &fonts::Font0, P.dim);

  const char* lab[3] = {"PROTEINE", "CARBO", "GRASSI"};
  const float* v[3] = {d.p, d.c, d.f};
  uint16_t col[3] = {P.cp, P.cc, P.cf};
  int y = 166;
  for (int i = 0; i < 3; i++) {
    text(s, lab[i], 12, y, FE16, col[i]);
    char b[32]; snprintf(b, sizeof b, "%s / %s g", fmt(v[i][0]), fmt(v[i][1]));
    text(s, b, UW - 12, y, &fonts::DejaVu12, P.white, top_right);
    bar(s, P, 12, y + 17, UW - 24, 9, v[i][1] > 0 ? v[i][0] / v[i][1] : 0, col[i]);
    y += 36;
  }
  char fb[48];
  if (d.nfav > 0) {
    snprintf(fb, sizeof fb, "+ PASTO PREFERITO (%d)", d.pasti);
    actionButton(s, P, MB_X, MB_Y, MB_W, MB_H, fb);
  } else {
    snprintf(fb, sizeof fb, "%d pasti registrati", d.pasti);
    text(s, fb, UW / 2, 284, &fonts::DejaVu12, P.dim, top_center);
  }
}

static void pageTrain(LGFX_Sprite& s, const Pal& P, const Data& d, const Status& st) {
  header(s, P, "TRAINING", st);
  if (!d.valid) return waiting(s, P, st);
  text(s, "ULTIMO", 12, 44, FE16, P.dim);
  if (d.has_last) {
    char ago[24];
    if (d.last_ago == 0) snprintf(ago, sizeof ago, "%s  oggi", d.last_d);
    else snprintf(ago, sizeof ago, "%s  %d gg fa", d.last_d, d.last_ago);
    text(s, ago, UW - 12, 44, &fonts::DejaVu12, d.last_ago > 4 ? P.gold : P.dim, top_right);
    text(s, fit(s, d.last_g, FE30, UW - 24), 12, 62, FE30, P.acc);
    int y = 98;
    for (int i = 0; i < d.nex; i++) {
      char b[24]; snprintf(b, sizeof b, "%sx%s %s", kg(d.ex[i].kg), kg(d.ex[i].reps), d.ex[i].delta);
      text(s, fit(s, d.ex[i].name, &fonts::DejaVu12, UW - 32 - textW(s, b, &fonts::DejaVu12)), 12, y, &fonts::DejaVu12, P.white);
      text(s, b, UW - 12, y, &fonts::DejaVu12, d.ex[i].delta[0] == '+' ? P.ice : P.dim, top_right);
      y += 18;
    }
  } else {
    text(s, "nessuna sessione recente", 12, 66, &fonts::DejaVu12, P.dim);
  }
  s.drawFastHLine(12, 176, UW - 24, P.line);
  text(s, "PROSSIMI", 12, 182, FE16, P.dim);
  if (d.nnext == 0) text(s, "niente in programma", 12, 204, &fonts::DejaVu12, P.dim);
  for (int i = 0; i < d.nnext && i < (d.nplan ? 2 : 3); i++) {
    int y = 202 + i * 26;
    text(s, fit(s, d.next_g[i], FE22, UW - 32 - textW(s, d.next_l[i], &fonts::DejaVu12)), 12, y, FE22, i == 0 ? P.white : P.dim);
    text(s, d.next_l[i], UW - 12, y + 4, &fonts::DejaVu12, i == 0 ? P.acc : P.dim, top_right);
  }
  if (d.nplan > 0) {
    int done = 0; for (int i = 0; i < d.nplan; i++) done += d.plan[i].used >= 0;
    char tb[48]; snprintf(tb, sizeof tb, "REGISTRA SERIE  %d/%d", done, d.nplan);
    actionButton(s, P, TB_X, TB_Y, TB_W, TB_H, tb, true);
  } else {
    char wb[56]; snprintf(wb, sizeof wb, "Sett. %d pesi + %d cardio  |  30gg %d/%d", d.w_pesi, d.w_cardio, d.w_pesi30, d.w_cardio30);
    text(s, wb, UW / 2, 284, &fonts::Font0, P.dim, top_center);
  }
  footer_age(s, P, d, st);
}

// ---------------- PR + "E' un PR?" ----------------
static const int PR_Y0 = 44, PR_DY = 54, PR_H = 48, PRB_Y = 268, PRB_H = 40;
static const char* const DEF_LIFTS[4] = {"Squat", "Bench", "Deadlift", "OHP"};
static int prLiftCount(const Data& d) { return d.npr > 0 ? d.npr : 4; }
static const char* prLiftName(const Data& d, int i) { return d.npr > 0 ? d.pr_name[i] : DEF_LIFTS[i & 3]; }
static float prLiftBest(const Data& d, int i) { return d.npr > 0 ? d.pr_e1[i] : 0; }

static void pagePR(LGFX_Sprite& s, const Pal& P, const Data& d, const Status& st) {
  header(s, P, "PR  E1RM", st);
  if (!d.valid) return waiting(s, P, st);
  if (d.npr == 0) text(s, "Nessun PR registrato", UW / 2, 140, FE22, P.dim, middle_center);
  for (int i = 0; i < d.npr; i++) {
    int y = PR_Y0 + i * PR_DY;
    card(s, P, 10, y, UW - 20, PR_H);
    s.fillRect(10, y + 10, 3, PR_H - 20, P.gold);
    text(s, d.pr_name[i], 22, y + 7, FE22, P.white);
    char b[24]; snprintf(b, sizeof b, "top %sx%s", kg(d.pr_w[i]), kg(d.pr_reps[i]));
    text(s, b, 22, y + 33, &fonts::Font0, P.dim);
    text(s, kg(d.pr_e1[i]), UW - 44, y + PR_H / 2, &fonts::DejaVu24, P.gold, middle_right);
    text(s, "kg", UW - 20, y + PR_H / 2 + 4, &fonts::DejaVu12, P.dim, middle_right);
  }
  actionButton(s, P, 12, PRB_Y, UW - 24, PRB_H, "E' UN PR?", true);
}

static const int PRC_CY = 44, PRC_CH = 30, PRC_CW = 52, PRC_CX0 = 10, PRC_CSTEP = 56;
static int prcChip(int x, int y) {
  if (!inRect(x, y, 0, PRC_CY, UW, PRC_CH)) return -1;
  for (int i = 0; i < 4; i++) if (inRect(x, y, PRC_CX0 + i * PRC_CSTEP, PRC_CY, PRC_CW, PRC_CH)) return i;
  return -1;
}

// ---------------- scheda atleta ----------------
static void statTile(LGFX_Sprite& s, const Pal& P, int x, int y, const char* label, const char* val, const char* unit, uint16_t col) {
  card(s, P, x, y, 70, 46);
  text(s, label, x + 8, y + 5, FE16, P.dim);
  text(s, val, x + 8, y + 22, &fonts::DejaVu18, col);
  int ux = x + 10 + textW(s, val, &fonts::DejaVu18);             // unita' subito dopo il numero, se ci sta
  if (unit[0] && ux + textW(s, unit, &fonts::Font0) <= x + 67) text(s, unit, ux, y + 29, &fonts::Font0, P.dim);
}

static void sparkline(LGFX_Sprite& s, const Pal& P, const Data& d, float goal, int gx, int gy, int gw, int gh) {
  card(s, P, gx, gy, gw, gh);
  if (d.nps < 2) { text(s, "storico peso non disponibile", gx + gw / 2, gy + gh / 2, &fonts::DejaVu12, P.dim, middle_center); return; }
  float lo = d.ps[0], hi = d.ps[0];
  for (int i = 1; i < d.nps; i++) { if (d.ps[i] < lo) lo = d.ps[i]; if (d.ps[i] > hi) hi = d.ps[i]; }
  bool showGoal = goal > 0 && goal > lo - 4 && goal < hi + 4;
  if (showGoal) { if (goal < lo) lo = goal; if (goal > hi) hi = goal; }
  if (hi - lo < 1) { lo -= 0.5f; hi += 0.5f; }
  int px = gx + 34, pw = gw - 44, py = gy + 10, ph = gh - 20;
  text(s, fmt(hi, 1), gx + 6, py - 3, &fonts::Font0, P.dim);
  text(s, fmt(lo, 1), gx + 6, py + ph - 5, &fonts::Font0, P.dim);
  if (showGoal) {
    int yg = py + (int)((hi - goal) / (hi - lo) * ph);
    for (int x = px; x < px + pw; x += 6) s.drawFastHLine(x, yg, 3, P.gold);
  }
  int ox = 0, oy = 0;
  for (int i = 0; i < d.nps; i++) {
    int x = px + i * pw / (d.nps - 1), y = py + (int)((hi - d.ps[i]) / (hi - lo) * ph);
    if (i) { s.drawLine(ox, oy, x, y, P.ice); s.drawLine(ox, oy + 1, x, y + 1, P.ice); }
    ox = x; oy = y;
  }
  s.fillCircle(ox, oy, 4, P.acc);
}

static void pageAth(LGFX_Sprite& s, const Pal& P, const Data& d, const Extra& x, const Status& st) {
  header(s, P, "ATLETA", st);
  if (!x.valid && !d.valid) return waiting(s, P, st);
  int chip = 0;
  if (x.fase[0]) {
    chip = textW(s, x.fase, FE16) + 16;
    s.fillRoundRect(UW - 12 - chip, 46, chip, 20, 6, P.acc);
    text(s, x.fase, UW - 12 - chip / 2, 56, FE16, P.bg, middle_center);
  }
  const lgfx::IFont* nf = textW(s, x.name, FE30) <= UW - 32 - chip ? FE30 : FE22;   // nome lungo: font piu' piccolo
  text(s, fit(s, x.name, nf, UW - 32 - chip), 12, nf == FE30 ? 44 : 48, nf, P.white);
  if (x.blk[0]) text(s, fit(s, x.blk, FE16, UW - 24), 12, 76, FE16, P.acc);
  if (x.obj[0]) text(s, fit(s, x.obj, &fonts::Font0, UW - 24), 12, 92, &fonts::Font0, P.dim);
  char v[16], v2[16];
  int y0 = 104;
  if (d.peso > 0) snprintf(v, sizeof v, "%s", fmt(d.peso, 1)); else snprintf(v, sizeof v, "--");
  statTile(s, P, 8, y0, "PESO", v, "kg", P.white);
  if (d.peso > 0 && d.peso_d7 != 0) {
    snprintf(v2, sizeof v2, "%+.1f", d.peso_d7);
    text(s, v2, 8 + 64, y0 + 6, &fonts::Font0, d.peso_d7 <= 0 ? P.ice : P.gold, top_right);
  }
  snprintf(v, sizeof v, "%s", x.h > 0 ? fmt(x.h) : "--");
  statTile(s, P, 85, y0, "ALTEZZA", v, "cm", P.white);
  float bmi = (x.h > 0 && d.peso > 0) ? d.peso / ((x.h / 100) * (x.h / 100)) : 0;
  snprintf(v, sizeof v, "%s", bmi > 0 ? fmt(bmi, 1) : "--");
  statTile(s, P, 162, y0, "BMI", v, "", P.white);
  snprintf(v, sizeof v, "%s", x.bf > 0 ? fmt(x.bf) : "--");
  statTile(s, P, 8, y0 + 52, "BF STIM.", v, "%", P.ice);
  snprintf(v, sizeof v, "%s", x.lbm > 0 ? fmt(x.lbm, 1) : "--");
  statTile(s, P, 85, y0 + 52, "M. MAGRA", v, "kg", P.ice);
  snprintf(v, sizeof v, "%s", x.visc > 0 ? fmt(x.visc) : "--");
  statTile(s, P, 162, y0 + 52, "VISCERALE", v, "", x.visc >= 12 ? P.gold : P.ice);
  sparkline(s, P, d, x.pgoal, 8, 210, UW - 16, 82);
  char f[56];
  if (x.pgoal > 0 && d.peso > 0) snprintf(f, sizeof f, "obiettivo %s kg  |  mancano %s kg", kg(x.pgoal), fmt(fabsf(d.peso - x.pgoal), 1));
  else snprintf(f, sizeof f, "streak %d gg  |  proteine %s g", d.streak, x.pt > 0 ? fmt(x.pt) : "--");
  text(s, f, UW / 2, UH - 22, &fonts::Font0, P.dim, top_center);
}

// ---------------- idratazione ----------------
static const int H2O_BY = 236, H2O_BH = 56, H2O_BW = 70, H2O_BX0 = 8, H2O_BSTEP = 78;
static const int H2O_ML[3] = {200, 500, 1000};
static int h2oButton(int x, int y) {
  for (int i = 0; i < 3; i++) if (inRect(x, y, H2O_BX0 + i * H2O_BSTEP, H2O_BY, H2O_BW, H2O_BH)) return H2O_ML[i];
  return 0;
}

static void pageH2O(LGFX_Sprite& s, const Pal& P, const Extra& x, const Status& st, const Ui& ui) {
  header(s, P, "ACQUA", st);
  if (!x.valid) return waiting(s, P, st, "Deck Extra non attivo");
  float frac = (float)x.h2o / x.h2oGoal;
  bool done = x.h2o >= x.h2oGoal;
  int cx = UW / 2, cy = 134, r = 82;
  s.fillArc(cx, cy, r, r - 14, 0, 360, P.line);
  float a = (frac > 1 ? 1 : frac) * 360;
  if (a > 0) s.fillArc(cx, cy, r, r - 14, 270, 270 + a, done ? P.gold : P.ice);
  drop(s, cx, cy - 34, 6, done ? P.gold : P.ice);
  text(s, fmt(x.h2o / 1000.0f, 2), cx, cy + 2, &fonts::DejaVu40, P.white, middle_center);
  char b[32]; snprintf(b, sizeof b, "di %s L", fmt(x.h2oGoal / 1000.0f, 1));
  text(s, b, cx, cy + 30, &fonts::DejaVu12, P.dim, top_center);
  if (done) text(s, "OBIETTIVO!", cx, 222, FE16, P.gold, middle_center);
  else { snprintf(b, sizeof b, "mancano %s ml", fmt((float)(x.h2oGoal - x.h2o))); text(s, b, cx, 222, &fonts::Font0, P.dim, middle_center); }
  static const char* L[3] = {"+200", "+500", "+1 L"};
  for (int i = 0; i < 3; i++) {
    int bx = H2O_BX0 + i * H2O_BSTEP;
    card(s, P, bx, H2O_BY, H2O_BW, H2O_BH, ui.busy ? P.line : P.ice);
    drop(s, bx + H2O_BW / 2, H2O_BY + 20, 4 + i, ui.busy ? P.dim : P.ice);
    text(s, L[i], bx + H2O_BW / 2, H2O_BY + 32, FE22, ui.busy ? P.dim : P.white, top_center);
  }
  text(s, ui.busy ? "invio..." : "sincronizzato su Notion", UW / 2, UH - 20, &fonts::Font0, P.dim, top_center);
}

// ---------------- integrazione ----------------
static const char* const MOM_L[4] = {"MATTINA", "PRANZO", "SERA", "ALTRO"};
static const int SUP_Y0 = 42, SUP_LH = 18, SUP_RH = 24, SUP_DY = 27;
static int supRowY[MAX_SUP];   // y di ogni riga all'ultimo disegno (-1 = non visibile): lo usa il tocco

static void layoutSup(const Extra& x) {
  int y = SUP_Y0, last = -1;
  for (int i = 0; i < MAX_SUP; i++) supRowY[i] = -1;
  for (int i = 0; i < x.nsup; i++) {
    if (x.sup[i].mom != last) { y += SUP_LH; last = x.sup[i].mom; }
    if (y + SUP_RH > UH - 16) break;
    supRowY[i] = y; y += SUP_DY;
  }
}
static int supRow(const Extra& x, int y) {
  layoutSup(x);
  for (int i = 0; i < x.nsup; i++) if (supRowY[i] >= 0 && y >= supRowY[i] && y < supRowY[i] + SUP_RH) return i;
  return -1;
}

static void pageSup(LGFX_Sprite& s, const Pal& P, const Extra& x, const Plus& pl, const Status& st, const Ui& ui) {
  header(s, P, "INTEGRAZIONE", st);
  if (!x.valid) return waiting(s, P, st, "Deck Extra non attivo");
  if (x.nsup == 0) return waiting(s, P, st, "Nessuna integrazione");
  layoutSup(x);
  int cur = currentMoment(st), last = -1;
  for (int i = 0; i < x.nsup; i++) {
    const Sup& u = x.sup[i];
    if (supRowY[i] < 0) break;
    if (u.mom != last) {
      last = u.mom;
      text(s, MOM_L[u.mom & 3], 12, supRowY[i] - SUP_LH + 3, FE16, u.mom == cur ? P.acc : P.dim);
    }
    int y = supRowY[i];
    bool armed = ui.supArmed == i;
    card(s, P, 8, y, UW - 16, SUP_RH, armed ? P.gold : 0);
    if (u.taken) { s.fillCircle(22, y + 12, 7, P.ice); s.drawLine(18, y + 12, 21, y + 15, P.bg); s.drawLine(21, y + 15, 26, y + 8, P.bg); }
    else s.drawCircle(22, y + 12, 7, armed ? P.gold : P.dim);
    char fb[24]; const char* rt = armed ? "tocca ancora" : u.dose; uint16_t rc = armed ? P.gold : P.dim;
    long left = u.taken ? -1 : fastLeft(pl, u.id, st.epoch);
    if (!armed && left > 0) { snprintf(fb, sizeof fb, "vuoto tra %ldh%02ld", left / 3600, left / 60 % 60); rt = fb; rc = P.gold; }
    else if (!armed && left == 0) { snprintf(fb, sizeof fb, "%s - a digiuno ok", u.dose); rt = fb; rc = P.ice; }
    text(s, fit(s, u.name, &fonts::DejaVu12, UW - 60 - textW(s, rt, &fonts::Font0)), 36, y + 6, &fonts::DejaVu12, u.taken ? P.dim : P.white);
    text(s, rt, UW - 16, y + 8, &fonts::Font0, rc, top_right);
  }
  char b[24]; snprintf(b, sizeof b, "%d/%d assunte oggi", supTaken(x), x.nsup);
  text(s, b, UW / 2, UH - 12, &fonts::Font0, supTaken(x) == x.nsup ? P.gold : P.dim, top_center);
}

// ---------------- pagina STATO (nascosta: 5 tap sull'orologio della home) ----------------
static const int IB_Y0 = 210, IB_Y1 = 260, IB_W = 104, IB_H = 42, IB_X0 = 12, IB_X1 = 124;
enum { IB_NONE = -1, IB_REFRESH, IB_SOUND, IB_PIN, IB_WIFI };
static int infoButton(int x, int y) {
  if (inRect(x, y, IB_X0, IB_Y0, IB_W, IB_H)) return IB_REFRESH;
  if (inRect(x, y, IB_X1, IB_Y0, IB_W, IB_H)) return IB_SOUND;
  if (inRect(x, y, IB_X0, IB_Y1, IB_W, IB_H)) return IB_PIN;
  if (inRect(x, y, IB_X1, IB_Y1, IB_W, IB_H)) return IB_WIFI;
  return IB_NONE;
}
static void ibutton(LGFX_Sprite& s, const Pal& P, int x, int y, const char* l, bool on = false) {
  s.fillRoundRect(x, y, IB_W, IB_H, 10, on ? P.acc : P.panel);
  s.drawRoundRect(x, y, IB_W, IB_H, 10, P.acc);
  text(s, l, x + IB_W / 2, y + IB_H / 2, FE16, on ? P.bg : P.acc, middle_center);
}

static void pageInfo(LGFX_Sprite& s, const Pal& P, const Data& d, const Plus& pl, const Status& st, const Ui& ui) {
  header(s, P, "STATO", st);
  char b[64];
  text(s, "BATTERIA", 12, 44, FE16, P.dim);
  if (st.bat_pct >= 0) snprintf(b, sizeof b, "%d%%  %.2f V%s", st.bat_pct, st.bat_v, st.charging ? "  in carica" : ""); else snprintf(b, sizeof b, "non collegata (USB)");
  text(s, b, 12, 58, &fonts::DejaVu18, P.white);
  text(s, "WI-FI", 12, 84, FE16, P.dim);
  if (st.wifi) snprintf(b, sizeof b, "%s  (%d dBm)", st.ssid, st.rssi); else snprintf(b, sizeof b, "non connesso");
  text(s, b, 12, 98, &fonts::DejaVu18, st.wifi ? P.white : P.red);
  if (st.wifi) text(s, st.ip, 12, 120, &fonts::DejaVu12, P.dim);
  text(s, "DATI", 12, 138, FE16, P.dim);
  if (d.valid) snprintf(b, sizeof b, "Notion %s  |  %s", d.upd, st.age_min >= 0 ? (st.age_min == 0 ? "ora" : fmt((float)st.age_min)) : "-");
  else snprintf(b, sizeof b, "nessun dato");
  if (d.valid && st.age_min > 0) strncat(b, " min fa", sizeof b - strlen(b) - 1);
  text(s, b, 12, 154, &fonts::DejaVu12, P.white);
  text(s, st.msg, 12, 172, &fonts::DejaVu12, P.gold);
  snprintf(b, sizeof b, "fw %s  PIN %s  IMU %s%s  RTC %s", FW_VERSION, ui.pinSet ? "on" : "off",
           st.imu_ok ? "ok" : "--", st.pocket ? " tasca" : "", st.rtc_ok ? "ok" : "--");
  text(s, b, 12, 192, &fonts::Font0, P.dim);

  if (st.queued > 0) { snprintf(b, sizeof b, "%d dati in attesa di rete", st.queued); text(s, b, UW - 12, 142, &fonts::Font0, P.gold, top_right); }
  if (st.fwNew) {
    ibutton(s, P, IB_X0, IB_Y0, ui.fwArmed ? "CONFERMA" : "INSTALLA FW", true);
    text(s, fit(s, pl.fwName, &fonts::Font0, UW - 24), 12, 202, &fonts::Font0, P.gold);
  } else ibutton(s, P, IB_X0, IB_Y0, "AGGIORNA");
  ibutton(s, P, IB_X1, IB_Y0, ui.sound ? "SUONI ON" : "SUONI OFF", ui.sound);
  ibutton(s, P, IB_X0, IB_Y1, ui.pinSet ? "CAMBIA PIN" : "IMPOSTA PIN");
  ibutton(s, P, IB_X1, IB_Y1, "WI-FI");
}

// ---------------- blocco schermo: tastierino ----------------
static const int KP_X0 = 20, KP_Y0 = 136, KP_W = 60, KP_H = 38, KP_DX = 70, KP_DY = 44;
// Ritorna '0'..'9', 'C' (cancella tutto), '<' (backspace) oppure 0
static char keypadKey(int x, int y) {
  static const char K[4][3] = {{'1', '2', '3'}, {'4', '5', '6'}, {'7', '8', '9'}, {'C', '0', '<'}};
  for (int r = 0; r < 4; r++)
    for (int c = 0; c < 3; c++)
      if (inRect(x, y, KP_X0 + c * KP_DX, KP_Y0 + r * KP_DY, KP_W, KP_H)) return K[r][c];
  return 0;
}

static void pageLock(LGFX_Sprite& s, const Pal& P, const Status& st, const Ui& ui) {
  s.fillScreen(P.bg);
  statusIcons(s, P, st, 10);
  text(s, ui.lockMode == LOCK_UNLOCK ? "VALHALLA DECK" : "NUOVO PIN", UW / 2, 8, FE22, P.acc, top_center);
  text(s, st.hm, UW / 2, 32, FE64, P.white, top_center);
  int n = strlen(ui.entry);
  for (int i = 0; i < 4; i++) {
    int cx = UW / 2 - 39 + i * 26;
    if (i < n) s.fillCircle(cx, 106, 6, P.acc); else s.drawCircle(cx, 106, 6, P.dim);
  }
  bool bad = strstr(ui.lockMsg, "errat") || strstr(ui.lockMsg, "coincid") || strstr(ui.lockMsg, "Attendi");
  text(s, ui.lockMsg, UW / 2, 118, &fonts::DejaVu12, bad ? P.red : P.dim, top_center);
  static const char* K[4][3] = {{"1", "2", "3"}, {"4", "5", "6"}, {"7", "8", "9"}, {"C", "0", "<"}};
  for (int r = 0; r < 4; r++)
    for (int c = 0; c < 3; c++) {
      int x = KP_X0 + c * KP_DX, y = KP_Y0 + r * KP_DY;
      card(s, P, x, y, KP_W, KP_H);
      text(s, K[r][c], x + KP_W / 2, y + KP_H / 2, &fonts::DejaVu18, r == 3 && c != 1 ? P.acc : P.white, middle_center);
    }
}

// ---------------- registrazione serie ----------------
static const int LOG_Y0 = 52, LOG_DY = 26, LOG_H = 24;
static int logRow(const Data& d, int y) {
  if (y < LOG_Y0) return -1;
  int i = (y - LOG_Y0) / LOG_DY;
  return i < d.nplan ? i : -1;
}

static void pageLog(LGFX_Sprite& s, const Pal& P, const Data& d, const Status& st) {
  header(s, P, "SERIE", st);
  char b[40]; snprintf(b, sizeof b, "%s  -  %s", d.plan_g, d.plan_d);
  text(s, b, 14, 41, &fonts::Font0, P.dim);
  if (d.nplan == 0) { text(s, "Nessuna serie in programma", UW / 2, 150, &fonts::DejaVu12, P.dim, middle_center); return; }
  for (int i = 0; i < d.nplan; i++) {
    const PlanEx& x = d.plan[i];
    int y = LOG_Y0 + i * LOG_DY;
    bool done = x.used >= 0;
    card(s, P, 8, y, UW - 16, LOG_H);
    if (done) s.fillRect(8, y + 5, 3, LOG_H - 10, P.ice);
    if (done) snprintf(b, sizeof b, "%sx%s", kg(x.used), kg(x.reps < 0 ? 0 : x.reps));
    else snprintf(b, sizeof b, "~%s  %s-%s", kg(x.pres), kg(x.rmin), kg(x.rmax));
    text(s, fit(s, x.name, &fonts::DejaVu12, UW - 38 - textW(s, b, &fonts::Font0)), 16, y + 6, &fonts::DejaVu12, done ? P.dim : P.white);
    text(s, b, UW - 14, y + 8, &fonts::Font0, done ? P.ice : P.acc, top_right);
  }
}

enum { SB_NONE = -1, SB_KG_M, SB_KG_P, SB_REP_M, SB_REP_P, SB_SEND };
static const int SB_X0 = 12, SB_X1 = 172, SB_W = 56, SB_H = 50, SB_KY = 104, SB_RY = 178, SEND_Y = 246, SEND_H = 46;
static int setButton(int x, int y) {
  if (inRect(x, y, SB_X0, SB_KY, SB_W, SB_H)) return SB_KG_M;
  if (inRect(x, y, SB_X1, SB_KY, SB_W, SB_H)) return SB_KG_P;
  if (inRect(x, y, SB_X0, SB_RY, SB_W, SB_H)) return SB_REP_M;
  if (inRect(x, y, SB_X1, SB_RY, SB_W, SB_H)) return SB_REP_P;
  if (inRect(x, y, 12, SEND_Y, UW - 24, SEND_H)) return SB_SEND;
  return SB_NONE;
}
static void pmButton(LGFX_Sprite& s, const Pal& P, int x, int y, bool plus) {
  card(s, P, x, y, SB_W, SB_H);
  s.fillRect(x + SB_W / 2 - 10, y + SB_H / 2 - 2, 20, 4, P.acc);
  if (plus) s.fillRect(x + SB_W / 2 - 2, y + SB_H / 2 - 10, 4, 20, P.acc);
}
// kg e ripetizioni con i +/- (serie e "E' un PR?")
static void kgRepsEditor(LGFX_Sprite& s, const Pal& P, float kgv, int reps, bool repsOk) {
  text(s, "KG", UW / 2, 90, FE16, P.dim, top_center);
  pmButton(s, P, SB_X0, SB_KY, false); pmButton(s, P, SB_X1, SB_KY, true);
  const lgfx::IFont* vf = textW(s, kg(kgv), &fonts::DejaVu40) <= 100 ? (const lgfx::IFont*)&fonts::DejaVu40 : &fonts::DejaVu24;
  text(s, kg(kgv), UW / 2, SB_KY + SB_H / 2, vf, P.white, middle_center);
  text(s, "RIPETIZIONI", UW / 2, 164, FE16, P.dim, top_center);
  pmButton(s, P, SB_X0, SB_RY, false); pmButton(s, P, SB_X1, SB_RY, true);
  text(s, fmt((float)reps), UW / 2, SB_RY + SB_H / 2, &fonts::DejaVu40, repsOk ? P.white : P.gold, middle_center);
}

static void pageSet(LGFX_Sprite& s, const Pal& P, const Data& d, const Plus& pl, const Status& st, const Ui& ui) {
  header(s, P, "SERIE", st);
  if (ui.setIdx < 0 || ui.setIdx >= d.nplan) return;
  const PlanEx& x = d.plan[ui.setIdx];
  text(s, fit(s, x.name, &fonts::DejaVu18, UW - 24), 12, 42, &fonts::DejaVu18, P.white);
  char why[40], b[64];
  float sug = suggestKg(x, pl, why, sizeof why);
  snprintf(b, sizeof b, "obiettivo %s-%s rep", kg(x.rmin), kg(x.rmax));
  text(s, b, 12, 64, &fonts::Font0, P.dim);
  snprintf(b, sizeof b, "%s -> %s kg", why, kg(sug));
  text(s, fit(s, b, &fonts::Font0, UW - 24), 12, 76, &fonts::Font0, fabsf(sug - ui.setKg) > 0.01f ? P.gold : P.ice);
  kgRepsEditor(s, P, ui.setKg, ui.setReps, ui.setReps >= x.rmin && (x.rmax <= 0 || ui.setReps <= x.rmax));
  actionButton(s, P, 12, SEND_Y, UW - 24, SEND_H, ui.busy ? "INVIO..." : "REGISTRA", !ui.busy);
}

static void pagePRC(LGFX_Sprite& s, const Pal& P, const Data& d, const Status& st, const Ui& ui) {
  header(s, P, "E' UN PR?", st);
  int n = prLiftCount(d);
  for (int i = 0; i < n && i < 4; i++) {
    int x = PRC_CX0 + i * PRC_CSTEP;
    bool sel = ui.prcLift == i;
    s.fillRoundRect(x, PRC_CY, PRC_CW, PRC_CH, 8, sel ? P.acc : P.panel);
    if (!sel) s.drawRoundRect(x, PRC_CY, PRC_CW, PRC_CH, 8, P.line);
    const char* nm = fit(s, prLiftName(d, i), FE16, PRC_CW - 6);
    text(s, nm, x + PRC_CW / 2, PRC_CY + PRC_CH / 2, FE16, sel ? P.bg : P.white, middle_center);
  }
  kgRepsEditor(s, P, ui.prcKg, ui.prcReps, ui.prcReps <= 12);
  float e1 = epley(ui.prcKg, ui.prcReps), best = prLiftBest(d, ui.prcLift);
  char b[48];
  if (best > 0) snprintf(b, sizeof b, "e1RM %s kg  |  record %s kg", fmt(e1, 1), fmt(best, 1));
  else snprintf(b, sizeof b, "e1RM %s kg  |  nessun record", fmt(e1, 1));
  text(s, b, UW / 2, 232, &fonts::Font0, e1 > best + 0.05f ? P.gold : P.dim, top_center);
  actionButton(s, P, 12, SEND_Y, UW - 24, SEND_H, "VERIFICA", true);
}

// ---------------- pasti preferiti ----------------
static const int FAV_Y0 = 44, FAV_DY = 44, FAV_H = 40;
static int favRow(const Data& d, int y) {
  if (y < FAV_Y0) return -1;
  int i = (y - FAV_Y0) / FAV_DY;
  return i < d.nfav ? i : -1;
}
static void pageFav(LGFX_Sprite& s, const Pal& P, const Data& d, const Status& st, const Ui& ui) {
  header(s, P, "PASTI", st);
  if (d.nfav == 0) { text(s, "Nessun pasto ricorrente", UW / 2, 150, &fonts::DejaVu12, P.dim, middle_center); return; }
  for (int i = 0; i < d.nfav; i++) {
    const Fav& f = d.fav[i];
    int y = FAV_Y0 + i * FAV_DY;
    bool armed = ui.favArmed == i, best = i == bestFav(d);
    card(s, P, 8, y, UW - 16, FAV_H, armed ? P.gold : best ? P.acc : 0);
    if (best) { s.fillRoundRect(UW - 70, y + 5, 54, 14, 4, P.acc); text(s, "CONSIGLIO", UW - 43, y + 12, &fonts::Font0, P.bg, middle_center); }
    text(s, fit(s, f.name, &fonts::DejaVu12, best ? UW - 92 : UW - 32), 16, y + 6, &fonts::DejaVu12, P.white);
    char b[48];
    if (armed) snprintf(b, sizeof b, "tocca ancora per registrare");
    else snprintf(b, sizeof b, "%s kcal  P%s  C%s  G%s", fmt(f.kcal), fmt(f.p), fmt(f.c), fmt(f.f));
    text(s, b, 16, y + 25, &fonts::Font0, armed ? P.gold : P.dim);
  }
}

// ---------------- celebrazione (5-8 s, un tocco la chiude) ----------------
static const int CEL_SAVE_X = 20, CEL_SAVE_Y = 270, CEL_SAVE_W = UW - 40, CEL_SAVE_H = 34;
static bool celFlash(const Ui& ui, uint32_t now) { return ((now - ui.celStart) % 700) < 90; }

static void drawCelebration(LGFX_Sprite& s, const Ui& ui, uint32_t now) {
  Pal P = pal(s);
  uint32_t t = now - ui.celStart;
  bool flash = celFlash(ui, now);
  uint16_t bg = flash ? C565(0x2A1204) : P.bg;
  s.fillScreen(bg);
  int cx = UW / 2, cy = 146;
  for (int i = 0; i < 16; i++) {                     // raggi che ruotano
    float a = (i * 22.5f + t / 15.0f) * (float)M_PI / 180;
    int r0 = 72, r1 = 72 + (int)(40 + 30 * sinf(t / 200.0f + i));
    s.drawLine(cx + cosf(a) * r0, cy + sinf(a) * r0, cx + cosf(a) * r1, cy + sinf(a) * r1, i & 1 ? P.gold : P.acc);
  }
  for (int i = 0; i < 40; i++) {                     // scintille che esplodono dal centro
    float a = i * 2.39996f, sp = 2 + i % 5;          // angolo aureo: distribuzione uniforme
    int dist = (int)(sp * (t % 1400) / 12.0f);
    if (dist > 170) continue;
    uint16_t c = i % 3 == 0 ? P.white : i % 3 == 1 ? P.gold : P.ice;
    s.fillCircle(cx + cosf(a) * dist, cy + sinf(a) * dist, 2, c);
  }
  text(s, ui.celTitle, cx, 16, FE30, P.gold, top_center);
  text(s, ui.celUnit[0] == 'L' ? fmt(ui.celE1, 1) : kg(ui.celE1), cx, cy, &fonts::DejaVu56, P.white, middle_center);
  text(s, ui.celUnit, cx, cy + 32, &fonts::DejaVu12, P.dim, top_center);
  text(s, fit(s, ui.celName, FE22, UW - 20), cx, 222, FE22, P.white, top_center);
  if (ui.celDelta > 0.05f) { char b[24]; snprintf(b, sizeof b, "+%s kg", kg(ui.celDelta)); text(s, b, cx, 248, &fonts::DejaVu18, P.ok, top_center); }
  if (ui.celSave) {
    actionButton(s, P, CEL_SAVE_X, CEL_SAVE_Y, CEL_SAVE_W, CEL_SAVE_H, ui.busy ? "SALVO..." : "SALVA NEL PR TRACKER", true);
    text(s, "tocca fuori per chiudere", cx, UH - 12, &fonts::Font0, P.dim, top_center);
  } else {
    text(s, "tocca per chiudere", cx, UH - 14, &fonts::Font0, P.dim, top_center);
  }
}

static void drawToast(LGFX_Sprite& s, const Ui& ui) {
  Pal P = pal(s);
  s.fillRoundRect(10, UH - 32, UW - 20, 26, 8, ui.toastOk ? C565(0x13232C) : C565(0x3A1010));
  s.drawRoundRect(10, UH - 32, UW - 20, 26, 8, ui.toastOk ? P.ice : P.red);
  const lgfx::IFont* f = textW(s, ui.toast, &fonts::DejaVu12) <= UW - 32 ? (const lgfx::IFont*)&fonts::DejaVu12 : &fonts::Font0;
  text(s, ui.toast, UW / 2, UH - 19, f, P.white, middle_center);
}

// ---------------- riepilogo della settimana ----------------
static void weekRow(LGFX_Sprite& s, const Pal& P, int y, const char* label, const char* val, const char* sub, float frac, uint16_t col) {
  card(s, P, 8, y, UW - 16, 46);
  text(s, label, 18, y + 5, FE16, P.dim);
  text(s, val, UW - 18, y + 4, &fonts::DejaVu18, col, top_right);
  text(s, fit(s, sub, &fonts::Font0, UW - 36), 18, y + 26, &fonts::Font0, P.dim);
  if (frac >= 0) bar(s, P, 18, y + 37, UW - 36, 4, frac, col);
}
// tasto in fondo alla pagina SETTIMANA: prepara nel Load Tracker la settimana successiva
static const int WKB_Y = 280, WKB_H = 34;
static void pageWeek(LGFX_Sprite& s, const Pal& P, const Plus& p, const Status& st, const Ui& ui) {
  header(s, P, "SETTIMANA", st);
  if (!p.wkValid) return waiting(s, P, st, "Deck Plus non attivo");
  char v[24], sub[48];
  snprintf(sub, sizeof sub, "%s - %s", p.wd0, p.wd1);
  text(s, sub, UW - 12, 40, &fonts::Font0, P.dim, top_right);
  snprintf(v, sizeof v, "%d / %d", p.allen[0], p.allen[1]);
  weekRow(s, P, 52, "ALLENAMENTI", v, "fatte / in programma", p.allen[1] ? (float)p.allen[0] / p.allen[1] : 0, p.allen[0] >= p.allen[1] && p.allen[1] ? P.gold : P.ice);
  snprintf(v, sizeof v, "%d g", p.pAvg); snprintf(sub, sizeof sub, "media/giorno  |  %d/%d gg in target", p.pDays, p.fDays);
  weekRow(s, P, 102, "PROTEINE", v, sub, p.fDays ? (float)p.pDays / p.fDays : 0, P.cp);
  snprintf(v, sizeof v, "%s L", fmt(p.h2oAvg, 1)); snprintf(sub, sizeof sub, "media/giorno  |  %d gg a 3 L", p.h2oDays);
  weekRow(s, P, 152, "ACQUA", v, sub, p.h2oAvg / 3.0f, P.ice);
  if (p.peso0 > 0) { snprintf(v, sizeof v, "%+.1f kg", p.peso1 - p.peso0); snprintf(sub, sizeof sub, "%s -> %s kg", fmt(p.peso0, 1), fmt(p.peso1, 1)); }
  else { snprintf(v, sizeof v, "--"); snprintf(sub, sizeof sub, "nessuna pesata questa settimana"); }
  weekRow(s, P, 202, "PESO", v, sub, -1, p.peso1 <= p.peso0 ? P.ice : P.gold);
  if (p.npr) { snprintf(sub, sizeof sub, "PR: %s%s%s%s%s", p.pr[0], p.npr > 1 ? ", " : "", p.npr > 1 ? p.pr[1] : "", p.npr > 2 ? ", " : "", p.npr > 2 ? p.pr[2] : ""); }
  else snprintf(sub, sizeof sub, "nessun PR questa settimana");
  text(s, fit(s, sub, &fonts::DejaVu12, UW - 24), UW / 2, 246, &fonts::DejaVu12, p.npr ? P.gold : P.dim, top_center);
  if (p.target) text(s, "SETTIMANA DA VALHALLA", UW / 2, 262, FE16, P.acc, top_center);
  else text(s, "ogni settimana conta: avanti cosi'", UW / 2, 265, &fonts::Font0, P.dim, top_center);
  bool on = ui.wkArmed || ui.busy;
  s.fillRoundRect(12, WKB_Y, UW - 24, WKB_H, 10, on ? P.acc : P.panel);
  s.drawRoundRect(12, WKB_Y, UW - 24, WKB_H, 10, P.acc);
  text(s, ui.busy ? "PREPARO..." : ui.wkArmed ? "TOCCA ANCORA: CONFERMA" : "PREPARA PROSSIMA SETTIMANA", UW / 2, WKB_Y + WKB_H / 2, FE16, on ? P.bg : P.acc, middle_center);
}

// ---------------- aggiornamento firmware in corso ----------------
static void drawOta(LGFX_Sprite& s, const Ui& ui) {
  Pal P = pal(s);
  s.fillScreen(P.bg);
  drawLogo(s, (UW - LOGO_W) / 2, 40, P.acc, P.bg, 10);
  text(s, "AGGIORNAMENTO", UW / 2, 182, FE30, P.acc, top_center);
  bar(s, P, 20, 232, UW - 40, 12, ui.otaPct / 100.0f, P.ice);
  char b[16]; snprintf(b, sizeof b, "%d%%", ui.otaPct);
  text(s, b, UW / 2, 254, &fonts::DejaVu18, P.white, top_center);
  text(s, ui.otaMsg, UW / 2, 284, &fonts::DejaVu12, P.dim, top_center);
  text(s, "non spegnere il Deck", UW / 2, UH - 14, &fonts::Font0, P.gold, top_center);
}

// ---------------- pannello notifiche ----------------
static void pageNotif(LGFX_Sprite& s, const Pal& P, const Status& st, const Ui& ui) {
  header(s, P, "NOTIFICHE", st);
  const Notifs& N = ui.notif;
  if (N.count == 0) { text(s, "Nessuna notifica", UW / 2, 150, FE22, P.dim, middle_center); }
  uint16_t col[4] = {P.ice, P.ok, P.gold, P.red};
  for (int i = 0; i < N.count && i < 6; i++) {
    const Notif& n = N.n[(N.head - 1 - i + MAX_NOTIF * 2) % MAX_NOTIF];
    int y = 44 + i * 43;
    card(s, P, 8, y, UW - 16, 40);
    s.fillRect(8, y + 8, 3, 24, col[n.kind & 3]);
    text(s, n.hm, UW - 16, y + 6, &fonts::Font0, P.dim, top_right);
    // testo su due righe: si va a capo all'ultimo spazio che sta nella prima
    char l1[44]; cpy(l1, sizeof l1, n.txt);
    const char* l2 = "";
    if (textW(s, l1, &fonts::DejaVu12) > UW - 72) {
      for (int k = strlen(l1) - 1; k > 0; k--)
        if (l1[k] == ' ') { l1[k] = 0; if (textW(s, l1, &fonts::DejaVu12) <= UW - 72) { l2 = n.txt + k + 1; break; } }
      if (!l2[0]) cpy(l1, sizeof l1, fit(s, n.txt, &fonts::DejaVu12, UW - 72));
    }
    text(s, l1, 18, y + 5, &fonts::DejaVu12, P.white);
    if (l2[0]) text(s, fit(s, l2, &fonts::DejaVu12, UW - 34), 18, y + 21, &fonts::DejaVu12, P.white);
  }
  text(s, "scorri in su per chiudere", UW / 2, UH - 12, &fonts::Font0, P.dim, top_center);
}

// ---------------- Wi-Fi ----------------
static const int WF_Y0 = 64, WF_DY = 26, WF_H = 24, WF_BY = 284, WF_BH = 30;
static int wifiRow(const WifiUi& w, int y) {
  if (y < WF_Y0) return -1;
  int i = (y - WF_Y0) / WF_DY;
  return i < w.n && i < MAX_NETS_SCAN && y < WF_BY - 4 ? i : -1;
}
enum { WB_NONE = -1, WB_SCAN, WB_SETUP };
static int wifiButton(int x, int y) {
  if (inRect(x, y, 12, WF_BY, 104, WF_BH)) return WB_SCAN;
  if (inRect(x, y, 124, WF_BY, 104, WF_BH)) return WB_SETUP;
  return WB_NONE;
}
static void wifiBars(LGFX_Sprite& s, const Pal& P, int x, int y, int rssi) {
  int lv = rssi > -55 ? 4 : rssi > -67 ? 3 : rssi > -78 ? 2 : 1;
  for (int i = 0; i < 4; i++) s.fillRect(x + i * 4, y + 9 - i * 3, 3, 3 + i * 3, i < lv ? P.ice : P.line);
}
static void pageWifi(LGFX_Sprite& s, const Pal& P, const Status& st, const Ui& ui) {
  header(s, P, "WI-FI", st);
  const WifiUi& w = ui.wifi;
  char b[48];
  if (w.conn == 1) snprintf(b, sizeof b, "connessione a %s...", w.target);
  else if (w.msg[0]) snprintf(b, sizeof b, "%s", w.msg);
  else if (st.wifi) snprintf(b, sizeof b, "connesso: %s", st.ssid);
  else snprintf(b, sizeof b, "non connesso");
  text(s, fit(s, b, &fonts::DejaVu12, UW - 24), 12, 42, &fonts::DejaVu12, w.conn == 3 ? P.red : st.wifi ? P.ice : P.gold);
  if (w.scanning && w.n == 0) text(s, "ricerca reti...", UW / 2, 150, FE22, P.dim, middle_center);
  for (int i = 0; i < w.n && i < MAX_NETS_SCAN; i++) {
    const WifiNet& n = w.net[i];
    int y = WF_Y0 + i * WF_DY;
    if (y + WF_H > WF_BY - 4) break;
    bool cur = st.wifi && !strcmp(n.ssid, st.ssid);
    card(s, P, 8, y, UW - 16, WF_H, cur ? P.ice : 0);
    wifiBars(s, P, 14, y + 6, n.rssi);
    if (!n.open) { s.fillRoundRect(UW - 26, y + 11, 9, 7, 1, P.dim); s.drawRoundRect(UW - 24, y + 6, 5, 7, 2, P.dim); }   // lucchetto
    const char* tag = cur ? "connessa" : n.saved ? "salvata" : "";
    if (tag[0]) text(s, tag, UW - 32, y + 8, &fonts::Font0, cur ? P.ice : P.dim, top_right);
    text(s, fit(s, n.ssid, &fonts::DejaVu12, UW - 100), 36, y + 6, &fonts::DejaVu12, P.white);
  }
  actionButton(s, P, 12, WF_BY, 104, WF_BH, w.scanning ? "CERCO..." : "CERCA");
  actionButton(s, P, 124, WF_BY, 104, WF_BH, "SETUP");
}

// Tastiera per la password: 3 righe di tasti + riga comandi
static const char* const KB_ROWS[3][3] = {
  {"qwertyuiop", "asdfghjkl", "zxcvbnm"},
  {"QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"},
  {"1234567890", "-_@#.!?$%&", "*+=/:;,()"}};
static const int KB_Y0 = 124, KB_DY = 40, KB_H = 36, KB_KW = 22, KB_STEP = 24;
enum { KB_NONE = 0, KB_DEL = 1, KB_OK = 2, KB_SHIFT = 3, KB_MODE = 4, KB_SPACE = ' ' };
static int kbRowX0(int len) { return (UW - len * KB_STEP) / 2 + 1; }
// ritorna il carattere del tasto oppure KB_DEL / KB_OK / KB_SHIFT / KB_MODE / KB_NONE
static int kbKey(int mode, int x, int y) {
  for (int r = 0; r < 3; r++) {
    const char* row = KB_ROWS[mode][r];
    int len = strlen(row), x0 = kbRowX0(len), ky = KB_Y0 + r * KB_DY;
    if (y < ky || y >= ky + KB_H) continue;
    if (r == 2 && x < x0 - 2) return mode == 2 ? KB_NONE : KB_SHIFT;
    if (r == 2 && x >= x0 + len * KB_STEP + 2) return KB_DEL;
    int i = (x - x0) / KB_STEP;
    return x >= x0 && i < len ? row[i] : KB_NONE;
  }
  int ky = KB_Y0 + 3 * KB_DY;
  if (y >= ky && y < ky + KB_H) { if (x < 60) return KB_MODE; if (x >= UW - 64) return KB_OK; return KB_SPACE; }
  return KB_NONE;
}
static void kbCap(LGFX_Sprite& s, const Pal& P, int x, int y, int w, const char* l, bool hl = false) {
  s.fillRoundRect(x, y, w, KB_H, 5, hl ? P.acc : P.panel);
  text(s, l, x + w / 2, y + KB_H / 2, &fonts::DejaVu12, hl ? P.bg : P.white, middle_center);
}
static void pageKbd(LGFX_Sprite& s, const Pal& P, const Status& st, const Ui& ui) {
  header(s, P, "PASSWORD", st);
  const WifiUi& w = ui.wifi;
  text(s, fit(s, w.target, &fonts::DejaVu12, UW - 24), 12, 42, &fonts::DejaVu12, P.ice);
  card(s, P, 8, 62, UW - 16, 30, P.acc);
  int len = strlen(w.pass);
  const char* shown = w.pass;
  while (shown[0] && textW(s, shown, &fonts::DejaVu18) > UW - 34) shown++;   // mostra la coda se e' lunga
  text(s, shown, 16, 77, &fonts::DejaVu18, P.white, middle_left);
  s.fillRect(16 + textW(s, shown, &fonts::DejaVu18) + 1, 68, 2, 18, P.acc);   // cursore
  char b[24]; snprintf(b, sizeof b, "%d caratteri", len);
  text(s, b, UW - 12, 98, &fonts::Font0, P.dim, top_right);
  int m = w.kb;
  for (int r = 0; r < 3; r++) {
    const char* row = KB_ROWS[m][r];
    int n = strlen(row), x0 = kbRowX0(n), ky = KB_Y0 + r * KB_DY;
    for (int i = 0; i < n; i++) { char c[2] = {row[i], 0}; kbCap(s, P, x0 + i * KB_STEP, ky, KB_KW, c); }
    if (r == 2) {
      if (m != 2) kbCap(s, P, 2, ky, x0 - 4, "^", m == 1);
      kbCap(s, P, x0 + n * KB_STEP + 2, ky, UW - (x0 + n * KB_STEP + 2) - 2, "<");
    }
  }
  int ky = KB_Y0 + 3 * KB_DY;
  kbCap(s, P, 2, ky, 56, m == 2 ? "abc" : "123");
  kbCap(s, P, 62, ky, UW - 128, "spazio");
  kbCap(s, P, UW - 62, ky, 60, "OK", true);
}

static void drawPage(LGFX_Sprite& s, int page, const Data& d, const Extra& x, const Plus& pl, const Status& st, const Ui& ui) {
  Pal P = pal(s);
  switch (page) {
    case PAGE_LOG: pageLog(s, P, d, st); break;
    case PAGE_SET: pageSet(s, P, d, pl, st, ui); break;
    case PAGE_FAV: pageFav(s, P, d, st, ui); break;
    case PAGE_PRC: pagePRC(s, P, d, st, ui); break;
    case PAGE_HOME: pageHome(s, P, d, x, st); notifBadge(s, P, ui.notif.unread); break;
    case PAGE_MACRO: pageMacro(s, P, d, st); break;
    case PAGE_TRAIN: pageTrain(s, P, d, st); break;
    case PAGE_PR: pagePR(s, P, d, st); break;
    case PAGE_ATH: pageAth(s, P, d, x, st); break;
    case PAGE_H2O: pageH2O(s, P, x, st, ui); break;
    case PAGE_SUP: pageSup(s, P, x, pl, st, ui); break;
    case PAGE_WEEK: pageWeek(s, P, pl, st, ui); break;
    case PAGE_NOTIF: pageNotif(s, P, st, ui); break;
    case PAGE_WIFI: pageWifi(s, P, st, ui); break;
    case PAGE_KBD: pageKbd(s, P, st, ui); break;
    case PAGE_INFO: pageInfo(s, P, d, pl, st, ui); break;
    default: pageLock(s, P, st, ui); break;
  }
}

// ---------------- avvio: logo ----------------
static void drawSplash(LGFX_Sprite& s, int level) {
  Pal P = pal(s);
  s.fillScreen(P.bg);
  drawLogo(s, (UW - LOGO_W) / 2, 70, P.acc, P.bg, level);
  text(s, "VALHALLA DECK", UW / 2, 206, FE30, blend(P.acc, P.bg, level), top_center);
  text(s, "fw " FW_VERSION, UW / 2, UH - 20, &fonts::Font0, P.dim, top_center);
}

// ---------------- standby: logo che pulsa + lampi ----------------
static void drawStandby(LGFX_Sprite& s, uint32_t ms, bool flash, const Data& d, const Extra& x, const Status& st, bool locked) {
  Pal P = pal(s);
  float pulse = (sinf(ms / 700.0f) + 1) / 2;
  uint16_t bg = flash ? C565(0x24100A) : P.bg;
  s.fillScreen(bg);
  text(s, "VALHALLA DECK", UW / 2, 10, FE22, P.acc, top_center);
  uint16_t lc = flash ? C565(0xFFD2B0) : P.acc;
  drawLogo(s, (UW - LOGO_W) / 2, 40, lc, bg, flash ? 16 : 9 + (int)(7 * pulse));
  if (x.name[0]) text(s, fit(s, x.name, FE30, UW - 20), UW / 2, 172, FE30, P.white, top_center);   // dal Profilo Atleta (Notion), salvato in cache
  text(s, st.hm, UW / 2, 208, FE64, P.white, top_center);
  char b[48];
  float rk = d.kcal[1] - d.kcal[0];
  if (d.valid && !locked) snprintf(b, sizeof b, "%s  |  %s %s kcal", st.date, rk >= 0 ? "restano" : "sforato", fmt(fabsf(rk)));
  else snprintf(b, sizeof b, "%s%s", st.date, locked ? "  |  bloccato" : "");
  text(s, b, UW / 2, 290, &fonts::DejaVu12, P.dim, top_center);
  battery(s, P, UW - 38, UH - 18, st.bat_pct, st.charging);
}
