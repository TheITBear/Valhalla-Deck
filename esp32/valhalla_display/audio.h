// Suoni d'avviso sullo speaker della scheda (codec I2S PCM5101: BCK 48, LRCK 38, DIN 47).
// Le melodie girano in un task dedicato: il loop della UI non si blocca mai.
#pragma once
#include <Arduino.h>
#include <ESP_I2S.h>
#include <math.h>
#include <time.h>

#define PIN_I2S_BCK  48
#define PIN_I2S_LRCK 38
#define PIN_I2S_DOUT 47

enum Sound : uint8_t { SND_TICK, SND_MEAL, SND_TRAIN, SND_PR, SND_WARN, SND_UNLOCK, SND_ERROR, SND_COACH, SND_WATER, SND_FANFARE };

struct Note { uint16_t hz, ms; };   // hz = 0 -> pausa
static const Note MEL_TICK[]   = {{1760, 40}, {0, 0}};
static const Note MEL_MEAL[]   = {{988, 90}, {1319, 140}, {0, 0}};                       // ding-dong ascendente
static const Note MEL_TRAIN[]  = {{784, 80}, {988, 80}, {1175, 160}, {0, 0}};
static const Note MEL_PR[]     = {{784, 110}, {988, 110}, {1175, 110}, {1568, 320}, {0, 0}};  // fanfara
static const Note MEL_WARN[]   = {{440, 160}, {1, 80}, {440, 160}, {0, 0}};             // due toni bassi
static const Note MEL_UNLOCK[] = {{1319, 50}, {1760, 70}, {0, 0}};
static const Note MEL_ERROR[]  = {{220, 180}, {0, 0}};
static const Note MEL_COACH[]  = {{659, 120}, {1, 60}, {784, 120}, {1, 60}, {659, 160}, {0, 0}};   // promemoria proteine
static const Note MEL_WATER[]  = {{1568, 40}, {1, 30}, {2093, 60}, {0, 0}};                 // "plin" della goccia
// fanfara delle celebrazioni (~2 s): ta-ta-ta-TAA ta-TAAA
static const Note MEL_FANFARE[] = {{523, 120}, {1, 30}, {523, 120}, {1, 30}, {523, 120}, {1, 30}, {698, 420}, {1, 60},
                                   {622, 160}, {1, 20}, {698, 160}, {1, 20}, {784, 160}, {1, 20}, {1047, 600}, {0, 0}};
static const Note* const MELODIES[] = {MEL_TICK, MEL_MEAL, MEL_TRAIN, MEL_PR, MEL_WARN, MEL_UNLOCK, MEL_ERROR, MEL_COACH, MEL_WATER, MEL_FANFARE};

static const uint32_t SR = 44100;           // frequenza standard: il PLL del PCM5101 la aggancia sicuro
static const float VOLUME = 0.25f;        // 0..1, lo speaker da 2 W e' forte gia' cosi'
static I2SClass i2sOut;
static QueueHandle_t soundQ = nullptr;
static volatile bool soundEnabled = true;

// Silenzio notturno 22:30-07:00 (solo per gli avvisi automatici, non per i suoni di interazione)
static bool quietHours() {
  struct tm t;
  if (!getLocalTime(&t, 0)) return false;
  int m = t.tm_hour * 60 + t.tm_min;
  return m >= 22 * 60 + 30 || m < 7 * 60;
}

static void playTone(uint16_t hz, uint16_t ms) {
  static int16_t buf[256 * 2];              // stereo interleaved
  uint32_t total = (uint32_t)SR * ms / 1000, done = 0;
  const uint32_t fade = SR / 200;           // 5 ms di rampa: niente "click"
  float ph = 0, step = hz > 1 ? 2.0f * (float)M_PI * hz / SR : 0;
  while (done < total) {
    uint32_t n = total - done > 256 ? 256 : total - done;
    for (uint32_t i = 0; i < n; i++) {
      uint32_t k = done + i;
      float env = 1.0f;
      if (k < fade) env = (float)k / fade; else if (total - k < fade) env = (float)(total - k) / fade;
      int16_t v = step ? (int16_t)(sinf(ph) * 32767 * VOLUME * env) : 0;
      ph += step; if (ph > 2 * (float)M_PI) ph -= 2 * (float)M_PI;
      buf[2 * i] = buf[2 * i + 1] = v;
    }
    i2sOut.write((uint8_t*)buf, n * 4);
    done += n;
  }
}

static void soundTask(void*) {
  uint8_t id;
  for (;;) {
    if (xQueueReceive(soundQ, &id, portMAX_DELAY) != pdTRUE || id >= sizeof MELODIES / sizeof MELODIES[0]) continue;
    for (const Note* n = MELODIES[id]; n->ms; n++) playTone(n->hz, n->ms);
    playTone(0, 30);                        // svuota il DMA con silenzio
  }
}

static void audioBegin() {
  i2sOut.setPins(PIN_I2S_BCK, PIN_I2S_LRCK, PIN_I2S_DOUT);
  if (!i2sOut.begin(I2S_MODE_STD, SR, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) {
    Serial.println("I2S non avviato: suoni disattivati");
    return;
  }
  soundQ = xQueueCreate(4, 1);
  xTaskCreatePinnedToCore(soundTask, "snd", 4096, nullptr, 1, nullptr, 0);
}

// alert = avviso automatico (rispetta il silenzio notturno); false = feedback di un tocco
static void playSound(Sound s, bool alert = true) {
  if (!soundQ || !soundEnabled || (alert && quietHours())) return;
  uint8_t id = s;
  xQueueSend(soundQ, &id, 0);               // coda piena: il suono si perde, la UI no
}
