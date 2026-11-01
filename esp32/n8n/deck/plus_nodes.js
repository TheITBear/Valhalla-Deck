// Logica dei Code node di "Valhalla Deck Plus" (test: test_plus.js).
// GET /valhalla-deck-p : storico ultimo allenamento per esercizio (progressione), ultimo pasto, digiuno integratori,
//                        riepilogo della settimana, firmware disponibile.
// GET /valhalla-fw     : il .bin dell'ultima versione caricata nella pagina Notion "Firmware Deck".
const asc = s => String(s == null ? '' : s).replace(/[→➜➔]/g, '>').normalize('NFKD').replace(/[^\x20-\x7e]/g, '');
const num = (pr, k) => (pr[k] && typeof pr[k].number === 'number') ? pr[k].number : null;
const txt = (pr, k) => { const v = pr[k]; return v ? (v.title || v.rich_text || []).map(t => t.plain_text || '').join('').trim() : ''; };
const nid = id => String(id || '').replace(/-/g, '').toLowerCase();
const romeDay = d => new Date(d).toLocaleDateString('sv-SE', { timeZone: 'Europe/Rome' });
const dateOf = (pr, k) => { const s = pr[k] && pr[k].date && pr[k].date.start; return !s ? null : s.length <= 10 ? s : romeDay(s); };
const r1 = v => Math.round((v || 0) * 10) / 10;
// stessa base dei nomi del piano nel Deck: senza "(n)" / "#n", max 22 caratteri
const baseName = s => asc(String(s || '').replace(/\s*\(\d+\)\s*$/, '')).slice(0, 22).trim().toLowerCase();
const STREAK_P_MIN = 180, STREAK_KCAL_MIN = 1500, STREAK_KCAL_MAX = 2520, H2O_GOAL = 3000;

// Ultima seduta per esercizio: carico massimo, ripetizioni minime a quel carico, tutte al top del range? qualcuna sotto?
function history(loadPages) {
  const byEx = {};
  for (const pg of loadPages) {
    const pr = pg.properties || {};
    const used = num(pr, 'Carico Usato'), d = dateOf(pr, 'Data'), b = baseName(txt(pr, 'Esercizio'));
    if (used == null || !d || !b) continue;
    const e = byEx[b] = byEx[b] || { d: '', sets: [] };
    if (d > e.d) { e.d = d; e.sets = []; }
    if (d === e.d) e.sets.push({ kg: used, reps: num(pr, 'Rep Fatte'), rmin: num(pr, 'Rep Min') || 0, rmax: num(pr, 'Rep Max') || 0 });
  }
  return Object.entries(byEx).sort((a, b) => b[1].d.localeCompare(a[1].d)).slice(0, 40).map(([b, e]) => {
    const kg = Math.max(...e.sets.map(s => s.kg));
    const atKg = e.sets.filter(s => s.kg === kg && s.reps != null);
    const reps = atKg.length ? Math.min(...atKg.map(s => s.reps)) : 0;
    const known = e.sets.filter(s => s.reps != null);
    const top = known.length > 0 && known.every(s => s.rmax > 0 && s.reps >= s.rmax) ? 1 : 0;
    const under = known.some(s => s.rmin > 0 && s.reps < s.rmin) ? 1 : 0;
    return [b, kg, reps, top, under];
  });
}

// Ultimo pasto di oggi: Notion non ha l'ora del pasto, si usa quando e' stato registrato (created_time)
function lastMeal(foodPages, today) {
  let t = 0;
  for (const pg of foodPages) if (dateOf(pg.properties || {}, 'Date') === today) t = Math.max(t, Date.parse(pg.created_time) || 0);
  return t ? Math.floor(t / 1000) : 0;
}

function fasting(suppPages) {
  return suppPages.map(pg => [nid(pg.id), num(pg.properties || {}, 'Digiuno (h)') || 0]).filter(f => f[1] > 0);
}

// Settimana da lunedi' a oggi (Europe/Rome)
function weekRange(today) {
  const d = new Date(today + 'T12:00:00Z'), wd = (d.getUTCDay() + 6) % 7;
  const mon = new Date(d.getTime() - wd * 86400000).toISOString().slice(0, 10);
  const sun = new Date(d.getTime() + (6 - wd) * 86400000).toISOString().slice(0, 10);
  return { mon, sun, days: wd + 1 };
}

function recap(today, weekLoad, food, water, weights, prs) {
  const { mon, sun, days } = weekRange(today);
  const inWeek = d => d && d >= mon && d <= sun;
  const planned = new Set(), done = new Set();
  for (const pg of weekLoad) {
    const pr = pg.properties || {}, d = dateOf(pr, 'Data');
    if (!inWeek(d)) continue;
    planned.add(d);
    if (num(pr, 'Carico Usato') != null) done.add(d);
  }
  const fd = {};
  for (const pg of food) {
    const pr = pg.properties || {}, d = dateOf(pr, 'Date');
    if (!inWeek(d) || d > today) continue;
    const s = fd[d] = fd[d] || { p: 0, k: 0 };
    s.p += num(pr, 'Proteins (g)') || 0; s.k += num(pr, 'Calories') || 0;
  }
  const fdays = Object.values(fd);
  const pAvg = fdays.length ? fdays.reduce((a, s) => a + s.p, 0) / fdays.length : 0;
  const pDays = fdays.filter(s => s.p >= STREAK_P_MIN && s.k >= STREAK_KCAL_MIN && s.k <= STREAK_KCAL_MAX).length;
  const wd = {};
  for (const pg of water) {
    const pr = pg.properties || {}, d = dateOf(pr, 'Data');
    if (inWeek(d)) wd[d] = (wd[d] || 0) + (num(pr, 'ml') || 0);
  }
  const h2oTot = Object.values(wd).reduce((a, b) => a + b, 0);
  const ws = weights.map(pg => ({ d: dateOf(pg.properties || {}, 'Date'), w: num(pg.properties || {}, 'Peso') }))
    .filter(x => inWeek(x.d) && x.w > 0).sort((a, b) => a.d.localeCompare(b.d));
  const prNames = [...new Set(prs.filter(pg => inWeek(dateOf(pg.properties || {}, 'Workout Date'))).map(pg => asc(txt(pg.properties || {}, 'Lift')).slice(0, 10)).filter(Boolean))];
  const allen = [done.size, planned.size];
  const target = allen[1] > 0 && allen[0] >= allen[1] && pDays >= Math.ceil(days * 0.7) ? 1 : 0;
  return {
    d: [mon.slice(8, 10) + '/' + mon.slice(5, 7), sun.slice(8, 10) + '/' + sun.slice(5, 7)],
    allen, p: [Math.round(pAvg), pDays, fdays.length],
    h2o: [r1(h2oTot / days / 1000), Object.values(wd).filter(v => v >= H2O_GOAL).length],
    peso: ws.length >= 2 ? [ws[0].w, ws[ws.length - 1].w] : ws.length === 1 ? [ws[0].w, ws[0].w] : [],
    pr: prNames.slice(0, 3), npr: prNames.length, target,
  };
}

// Ultimo file nella pagina "Firmware Deck"
function firmware(blocks) {
  const files = (blocks || []).filter(b => b.type === 'file' && b.file);
  const f = files[files.length - 1];
  if (!f) return null;
  const url = (f.file.file && f.file.file.url) || (f.file.external && f.file.external.url) || '';
  return { id: nid(f.id).slice(0, 12) + '-' + String(f.last_edited_time || '').replace(/[^0-9]/g, '').slice(0, 12), name: asc(f.file.name || 'firmware.bin').slice(0, 30), url };
}

function toPlus(today, loadHist, weekLoad, food, water, weights, prs, supp, fwBlocks) {
  const fw = firmware(fwBlocks);
  return {
    hist: history(loadHist), meal: lastMeal(food, today), fast: fasting(supp),
    wk: recap(today, weekLoad, food, water, weights, prs),
    fw: fw ? [fw.id, fw.name] : [],
  };
}

if (typeof module !== 'undefined') module.exports = { history, lastMeal, fasting, weekRange, recap, firmware, toPlus, baseName };
