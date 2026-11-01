// n8n Code node "Componi Deck": feed HA + Load Tracker + FoodTracker -> JSON compatto per il Deck ESP32.
// Base = stessa logica di "Valhalla Display Feed" (toDisplay), piu' plan / fav / streak / wk / ps.
const SPLITS = { lower_a: 'Lower A', lower_b: 'Lower B', upper_push: 'Upper Push', upper_pull: 'Upper Pull' };
const CODES = { LA: 'Lower A', LB: 'Lower B', PS: 'Upper Push', PL: 'Upper Pull', PB: 'Pump S+B' };
const BIG = ['Squat', 'Bench', 'Deadlift', 'OHP'];
const GIORNI = ['Lun', 'Mar', 'Mer', 'Gio', 'Ven', 'Sab', 'Dom'];
// Giornata "in target" per lo streak (non sappiamo a posteriori se era ON o OFF):
// proteine >= 90% di 200 g e kcal tra 1500 (giornata davvero registrata) e ON + 5%.
const STREAK_P_MIN = 180, STREAK_KCAL_MIN = 1500, STREAK_KCAL_MAX = 2520;

const asc = s => String(s == null ? '' : s).normalize('NFKD').replace(/[^\x20-\x7e]/g, '');
const ddmm = iso => iso ? iso.slice(8, 10) + '/' + iso.slice(5, 7) : '';
const days = (a, b) => Math.round((Date.parse(a + 'T00:00:00Z') - Date.parse(b + 'T00:00:00Z')) / 86400000);
const r1 = v => Math.round((v || 0) * 10) / 10;
const romeDate = s => {            // data Notion (anche datetime UTC) -> YYYY-MM-DD in Europe/Rome
  if (!s) return null;
  if (s.length <= 10) return s;
  return new Date(s).toLocaleDateString('sv-SE', { timeZone: 'Europe/Rome' });
};
const num = (pr, k) => (pr[k] && typeof pr[k].number === 'number') ? pr[k].number : null;
const txt = (pr, k) => { const v = pr[k]; return v ? (v.title || v.rich_text || []).map(t => t.plain_text || '').join('').trim() : ''; };
const sel = (pr, k) => (pr[k] && pr[k].select) ? pr[k].select.name : null;
const dt = (pr, k) => romeDate(pr[k] && pr[k].date && pr[k].date.start);

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

// Serie da registrare: la seduta di oggi se esiste, altrimenti la prossima ancora da fare.
// Ogni riga del Load Tracker e' una serie ("Calf in piedi", "Calf in piedi (1)" = 2a serie).
function plan(pages, today) {
  const rows = pages.map(pg => {
    const pr = pg.properties || {};
    const ex = txt(pr, 'Esercizio');
    return { id: pg.id, ex, g: sel(pr, 'Giorno'), d: dt(pr, 'Data'), used: num(pr, 'Carico Usato'),
             pres: num(pr, 'Carico Presunto'), rmin: num(pr, 'Rep Min'), rmax: num(pr, 'Rep Max'), reps: num(pr, 'Rep Fatte') };
  }).filter(r => r.ex && r.d && r.d >= today);
  let date = rows.some(r => r.d === today) ? today : null;
  if (!date) date = rows.filter(r => r.used == null).map(r => r.d).sort()[0] || null;
  if (!date) return null;
  const sel_ = rows.filter(r => r.d === date).sort((a, b) => a.ex.localeCompare(b.ex));
  const label = ex => {
    const m = ex.match(/^(.*?)\s*\((\d+)\)$/);
    return m ? asc(m[1]).slice(0, 22) + ' #' + (Number(m[2]) + 1) : asc(ex).slice(0, 25);
  };
  return {
    d: date === today ? 'Oggi' : ddmm(date),
    g: asc(sel_[0].g || ''),
    ex: sel_.slice(0, 10).map(r => [r.id.replace(/-/g, ''), label(r.ex), r.rmin || 0, r.rmax || 0,
                                     r.pres || 0, r.used == null ? -1 : r.used, r.reps == null ? -1 : r.reps]),
  };
}

const mealKey = s => asc(s).toLowerCase().replace(/^\s*(colazione|pranzo|cena|spuntino|snack|merenda|out|fuori pasto)\s*:\s*/, '').replace(/\s+/g, ' ').trim();

// Pasti preferiti: i 6 testi piu' ripetuti negli ultimi 35 giorni (almeno 2 volte), con i macro dell'ultima volta.
function favorites(pages) {
  const g = {};
  for (const pg of pages) {
    const pr = pg.properties || {};
    const name = txt(pr, 'Meal 🍴'), k = mealKey(name);
    if (!k) continue;
    const rec = { name, d: dt(pr, 'Date') || '', kcal: num(pr, 'Calories') || 0, p: num(pr, 'Proteins (g)') || 0,
                  c: num(pr, 'Carbs (g)') || 0, f: num(pr, 'Fats (g)') || 0, tag: sel(pr, 'Tag') || '' };
    const e = g[k] = g[k] || { n: 0, last: rec };
    e.n++;
    if (rec.d >= e.last.d) e.last = rec;
  }
  return Object.entries(g).filter(([, e]) => e.n >= 2).sort((a, b) => b[1].n - a[1].n).slice(0, 6).map(([k, e]) => {
    const short = k.charAt(0).toUpperCase() + k.slice(1);
    return [short.length > 24 ? short.slice(0, 23) + '.' : short, Math.round(e.last.kcal), Math.round(e.last.p),
            Math.round(e.last.c), Math.round(e.last.f), e.last.tag, e.last.name];
  });
}

// Giorni consecutivi in target, contando oggi solo se e' gia' in target.
function streak(pages, today) {
  const day = {};
  for (const pg of pages) {
    const pr = pg.properties || {};
    const d = dt(pr, 'Date');
    if (!d) continue;
    const s = day[d] = day[d] || { kcal: 0, p: 0 };
    s.kcal += num(pr, 'Calories') || 0; s.p += num(pr, 'Proteins (g)') || 0;
  }
  const ok = d => { const s = day[d]; return !!s && s.p >= STREAK_P_MIN && s.kcal >= STREAK_KCAL_MIN && s.kcal <= STREAK_KCAL_MAX; };
  const prev = d => new Date(Date.parse(d + 'T12:00:00Z') - 86400000).toISOString().slice(0, 10);
  let n = ok(today) ? 1 : 0, d = prev(today);
  while (ok(d) && n < 99) { n++; d = prev(d); }
  return n;
}

function toDeck(feed, loadPages, foodPages) {
  const today = feed.today;
  const m = feed.macro || {};
  const lt = m.day_type === 'ON' ? 'on' : 'off';
  const w = feed.weight || {};
  const wk = feed.workouts || {};
  const big = {};
  for (const b of ((feed.lifts || {}).big || [])) big[b.lift] = b;
  const week = (wk.weekly || []).slice(-1)[0] || {};
  return {
    upd: String(feed.updated || '').slice(11, 16),
    day: m.day_type || 'OFF',
    pasti: m.pasti || 0,
    kcal: [r1(m.kcal), m[lt + '_target_kcal'] || 0],
    p: [r1(m.p), m[lt + '_target_p'] || 0],
    c: [r1(m.c), m[lt + '_target_c'] || 0],
    f: [r1(m.f), m[lt + '_target_f'] || 0],
    peso: [w.peso || 0, w.delta7 || 0, ddmm(w.data)],
    ps: (w.series || []).slice(-30).map(r => r[1]),
    week: { pesi: wk.settimana_pesi || 0, cardio: wk.settimana_cardio || 0, pesi30: wk.pesi_30gg || 0, cardio30: wk.cardio_30gg || 0 },
    wk: ['Lower A', 'Upper Push', 'Lower B', 'Upper Pull'].map(k => (week[k] || 0) > 0 ? 1 : 0),
    pr: BIG.filter(k => big[k]).map(k => [k, big[k].w, big[k].reps || 1, big[k].e1rm]),
    last: lastSession(feed, today),
    next: nextSessions(feed, today),
    plan: plan(loadPages, today),
    fav: favorites(foodPages).map(f => f.slice(0, 5)),   // al display bastano etichetta e macro
    streak: streak(foodPages, today),
  };
}

if (typeof module !== 'undefined' && typeof $input === 'undefined') {
  module.exports = { toDeck, plan, favorites, streak };
} else {
  const pages = n => { try { return $(n).all().flatMap(i => (i.json && i.json.results) || []); } catch (e) { return []; } };
  return [{ json: toDeck($('Feed HA').first().json, pages('Notion Load Tracker'), pages('Notion FoodTracker 35gg')) }];
}
