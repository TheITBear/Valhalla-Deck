// n8n Code node ("Run Once for All Items"): payload "Valhalla HA Feed" -> JSON compatto per il display ESP32.
// Compatta il feed per il display. Testabile fuori da n8n: node test_transform.js
const SPLITS = { lower_a: 'Lower A', lower_b: 'Lower B', upper_push: 'Upper Push', upper_pull: 'Upper Pull' };
const CODES = { LA: 'Lower A', LB: 'Lower B', PS: 'Upper Push', PL: 'Upper Pull', PB: 'Pump S+B' };
const BIG = ['Squat', 'Bench', 'Deadlift', 'OHP'];
const GIORNI = ['Lun', 'Mar', 'Mer', 'Gio', 'Ven', 'Sab', 'Dom'];

const asc = s => String(s == null ? '' : s).normalize('NFKD').replace(/[^\x20-\x7e]/g, '');
const ddmm = iso => iso ? iso.slice(8, 10) + '/' + iso.slice(5, 7) : '';
const days = (a, b) => Math.round((Date.parse(a + 'T00:00:00Z') - Date.parse(b + 'T00:00:00Z')) / 86400000);
const r1 = v => Math.round((v || 0) * 10) / 10;

function lastSession(feed, today) {
  const w = feed.workouts || {};
  let best = null;
  for (const [k, name] of Object.entries(SPLITS)) {
    const d = w['ultimo_' + k];
    if (d && (!best || d > best[0])) best = [d, name];
  }
  if (!best) return null;
  const [date, split] = best;
  // shortcut: legge la tabella markdown del feed HA; se ne cambia il formato, esporre campi strutturati nel feed
  const md = ((feed.lifts || {}).md) || '';
  const esc = split.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
  const m = md.match(new RegExp('#### ' + esc + '\\n([\\s\\S]*?)(?:\\n\\n####|$)'));
  const ex = [];
  for (const row of (m ? m[1].split('\n').slice(2) : [])) {
    const c = row.replace(/^\||\|$/g, '').split('|').map(s => s.trim());
    if (c.length < 4 || !c[1].includes('×')) continue;
    const [kg, reps] = c[1].split('×').map(Number);
    const delta = c[2].replace('▲', '+').replace('▼', '-').replace('＝', '=').replace('—', '');
    if (isNaN(kg) || isNaN(reps)) continue;
    ex.push([asc(c[0]), kg, reps, delta, Number(c[3]) || 0]);
  }
  ex.sort((a, b) => (b[3].startsWith('+') - a[3].startsWith('+')) || (b[4] - a[4]));
  return { g: split, d: ddmm(date), ago: days(today, date), ex: ex.slice(0, 4).map(e => e.slice(0, 4)) };
}

function nextSessions(feed, today) {
  const out = [];
  const macro = feed.macro || {};
  if (macro.pianificato_oggi && !macro.allenato_oggi) out.push(['Oggi', asc(macro.pianificato_oggi)]);
  // shortcut: il calendario del feed copre solo il mese corrente
  const md = ((feed.calendar || {}).md) || '';
  for (const cell of md.split('|')) {
    const m = cell.match(/^\s*\**\[?(\d{1,2})\]?\**<br>(.*)/);
    if (!m) continue;
    const d = today.slice(0, 8) + String(m[1]).padStart(2, '0');
    if (d <= today) continue;
    const wd = (new Date(d + 'T00:00:00Z').getUTCDay() + 6) % 7;
    for (const code of (m[2].match(/⚪(\w+)/g) || []))
      out.push([GIORNI[wd] + ' ' + Number(m[1]), CODES[code.slice(1)] || code.slice(1)]);
  }
  return out.slice(0, 3);
}

function toDisplay(feed) {
  const today = feed.today;
  const m = feed.macro || {};
  const lt = m.day_type === 'ON' ? 'on' : 'off';
  const w = feed.weight || {};
  const wk = feed.workouts || {};
  const big = {};
  for (const b of ((feed.lifts || {}).big || [])) big[b.lift] = b;
  return {
    upd: String(feed.updated || '').slice(11, 16),
    day: m.day_type || 'OFF',
    pasti: m.pasti || 0,
    kcal: [r1(m.kcal), m[lt + '_target_kcal'] || 0],
    p: [r1(m.p), m[lt + '_target_p'] || 0],
    c: [r1(m.c), m[lt + '_target_c'] || 0],
    f: [r1(m.f), m[lt + '_target_f'] || 0],
    peso: [w.peso || 0, w.delta7 || 0, ddmm(w.data)],
    week: { pesi: wk.settimana_pesi || 0, cardio: wk.settimana_cardio || 0, pesi30: wk.pesi_30gg || 0, cardio30: wk.cardio_30gg || 0 },
    pr: BIG.filter(k => big[k]).map(k => [k, big[k].w, big[k].reps || 1, big[k].e1rm]),
    last: lastSession(feed, today),
    next: nextSessions(feed, today),
  };
}

if (typeof module !== 'undefined' && typeof $input === 'undefined') {
  module.exports = { toDisplay };
} else {
  return [{ json: toDisplay($input.first().json) }];
}
