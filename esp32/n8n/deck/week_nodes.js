// Logica del Code node di "Valhalla Load Week" (test: test_week.js).
// POST /valhalla-week : prepara nel Load Tracker la settimana dopo l'ultima.
// Sorgente = l'ultima settimana presente (quella con la data piu' recente), qualunque sia il programma:
// stessi esercizi / giorni / range reps, date +7, Carico Presunto = Carico Usato (se non fatto: il presunto di prima).
// Cambio programma (es. FENRIR -> MJOLNIR): basta creare a mano la prima settimana nuova, il tasto poi copia quella.
const LOAD_DB = 'YOUR_LOAD_TRACKER_DB_ID';
const asc = s => String(s == null ? '' : s).normalize('NFKD').replace(/[^\x20-\x7e]/g, '');
const num = (pr, k) => (pr[k] && typeof pr[k].number === 'number') ? pr[k].number : null;
const txt = (pr, k) => { const v = pr[k]; return v ? (v.title || v.rich_text || []).map(t => t.plain_text || '').join('').trim() : ''; };
const sel = (pr, k) => (pr[k] && pr[k].select) ? pr[k].select.name : '';
const day = (pr, k) => { const s = pr[k] && pr[k].date && pr[k].date.start; return s ? s.slice(0, 10) : ''; };
const addDays = (d, n) => new Date(Date.parse(d + 'T12:00:00Z') + n * 86400000).toISOString().slice(0, 10);
const baseName = s => asc(String(s || '').replace(/\s*\(\d+\)\s*$/, '')).toLowerCase().trim();

function planWeek(pages) {
  const rows = pages.map(pg => {
    const pr = pg.properties || {};
    return { title: txt(pr, 'Esercizio'), giorno: sel(pr, 'Giorno'), d: day(pr, 'Data'), sett: num(pr, 'Settimana'),
      pres: num(pr, 'Carico Presunto'), used: num(pr, 'Carico Usato'), rmin: num(pr, 'Rep Min'), rmax: num(pr, 'Rep Max') };
  }).filter(r => r.title && r.d);
  if (!rows.length) return { ok: false, err: 'Load Tracker vuoto' };
  const maxD = rows.reduce((m, r) => r.d > m ? r.d : m, '');
  const N = rows.find(r => r.d === maxD).sett;
  if (N == null) return { ok: false, err: 'ultima settimana senza numero' };
  // stessa "Settimana" e date vicine: un vecchio blocco con lo stesso numero non si mescola
  const src = rows.filter(r => r.sett === N && r.d >= addDays(maxD, -13));
  if (!src.some(r => r.used != null)) return { ok: false, err: `settimana ${N} gia' pronta` };
  const hist = {};   // ultimo carico usato per esercizio: presunto per chi non ne ha
  for (const r of rows) if (r.used != null && (!hist[baseName(r.title)] || r.d > hist[baseName(r.title)].d)) hist[baseName(r.title)] = r;
  src.sort((a, b) => a.d.localeCompare(b.d) || a.title.localeCompare(b.title));
  const bodies = src.map(r => {
    const h = hist[baseName(r.title)];
    const pres = r.used != null ? r.used : r.pres != null ? r.pres : h ? h.used : null;
    const props = {
      'Esercizio': { title: [{ text: { content: r.title } }] },
      'Data': { date: { start: addDays(r.d, 7) } },
      'Settimana': { number: N + 1 },
    };
    if (r.giorno) props['Giorno'] = { select: { name: r.giorno } };
    if (pres != null) props['Carico Presunto'] = { number: pres };
    if (r.rmin != null) props['Rep Min'] = { number: r.rmin };
    if (r.rmax != null) props['Rep Max'] = { number: r.rmax };
    return { parent: { database_id: LOAD_DB }, properties: props };
  });
  const dates = bodies.map(b => b.properties.Data.date.start);
  return { ok: true, week: N + 1, from: N, n: bodies.length, miss: src.filter(r => r.used == null).length,
    d: [dates[0].slice(8, 10) + '/' + dates[0].slice(5, 7), dates[dates.length - 1].slice(8, 10) + '/' + dates[dates.length - 1].slice(5, 7)], bodies };
}

if (typeof module !== 'undefined') module.exports = { planWeek, addDays, LOAD_DB };
