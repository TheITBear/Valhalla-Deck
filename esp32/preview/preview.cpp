// Anteprima su PC delle pagine del firmware (stesso ui.h). Uso: ./preview sample.json -> page_*.ppm
#include "../valhalla_display/ui.h"
#include <fstream>
#include <sstream>
#include <cassert>

static void save(LGFX_Sprite& s, const char* name) {
  FILE* f = fopen(name, "wb");
  fprintf(f, "P6\n%d %d\n255\n", UW, UH);
  for (int y = 0; y < UH; y++) for (int x = 0; x < UW; x++) {
    auto c = s.readPixelRGB(x, y);
    unsigned char px[3] = {c.R8(), c.G8(), c.B8()};
    fwrite(px, 1, 3, f);
  }
  fclose(f);
}

int main(int argc, char** argv) {
  std::ifstream in(argc > 1 ? argv[1] : "sample.json");
  std::stringstream ss; ss << in.rdbuf();
  Data d;
  assert(parseData(ss.str().c_str(), d));
  assert(d.valid && d.npr == 4 && d.nex == 4 && d.nnext == 3 && d.has_last && d.last_ago == 14);
  assert(!parseData("{rotto", d) && d.valid);   // JSON rotto: i dati precedenti restano
  Status st; snprintf(st.hm, 6, "10:42"); snprintf(st.date, 16, "Mar 27 Ott");
  st.bat_pct = 72; st.bat_v = 3.98f; st.wifi = true; st.rssi = -58; snprintf(st.ssid, 33, "CasaWiFi"); snprintf(st.ip, 16, "10.0.0.42"); st.age_min = 3;
  LGFX_Sprite s; s.setColorDepth(16); s.createSprite(UW, UH);
  const char* names[] = {"page_macro.ppm", "page_train.ppm", "page_pr.ppm", "page_info.ppm"};
  for (int p = 0; p < PAGE_COUNT; p++) { drawPage(s, p, d, st); save(s, names[p]); }
  drawStandby(s, 1000, false, d, st); save(s, "page_bolt.ppm");
  Data empty; Status off; snprintf(off.msg, 64, "Wi-Fi non configurato"); drawPage(s, PAGE_MACRO, empty, off); save(s, "page_empty.ppm");
  puts("ok");
}
