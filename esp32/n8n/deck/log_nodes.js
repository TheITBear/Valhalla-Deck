// Logica dei Code node del ramo POST /valhalla-log (testabile in node: test_log.js).
const LOAD_DB = 'YOUR_LOAD_TRACKER_DB_ID';
const FOOD_DB = 'YOUR_FOOD_TRACKER_DB_ID';

// ---- "Valida Log": confine di fiducia, il body arriva dal Deck ----
function validate(body) {
  const b = body || {};
  const isNum = (v, lo, hi) => typeof v === 'number' && isFinite(v) && v >= lo && v <= hi;
  if (b.t === 'set') {
    const id = String(b.id || '').replace(/-/g, '').toLowerCase();
    if (!/^[0-9a-f]{32}$/.test(id)) return { ok: false, err: 'id non valido' };
    if (!isNum(b.kg, 0, 500)) return { ok: false, err: 'kg non validi' };
    if (!isNum(b.reps, 1, 50) || !Number.isInteger(b.reps)) return { ok: false, err: 'reps non valide' };
    const name = String(b.name || '').replace(/\s*#\d+$/, '').slice(0, 40).trim();
    if (!name) return { ok: false, err: 'esercizio mancante' };
    return { ok: true, t: 'set', id, kg: Math.round(b.kg * 4) / 4, reps: b.reps, name,
             notionBody: { properties: { 'Carico Usato': { number: Math.round(b.kg * 4) / 4 }, 'Rep Fatte': { number: b.reps } } },
             historyBody: { page_size: 100, filter: { and: [
               { property: 'Esercizio', title: { contains: name } },
               { property: 'Carico Usato', number: { is_not_empty: true } }] } } };
  }
  if (b.t === 'meal') {
    const label = String(b.label || '').slice(0, 30);
    if (!label || !Number.isInteger(b.i) || b.i < 0 || b.i > 5) return { ok: false, err: 'pasto non valido' };
    return { ok: true, t: 'meal', label, i: b.i };
  }
  return { ok: false, err: 'tipo sconosciuto' };
}

// ---- "Calcola PR" ----
const epley = (w, r) => (!w ? 0 : (!r || r <= 1) ? w : w * (1 + r / 30));
const r1 = v => Math.round((v || 0) * 10) / 10;
function computePR(v, writeResp, history) {
  if (!writeResp || writeResp.object !== 'page') return { ok: false, err: 'Notion non ha salvato la serie' };
  const num = (pr, k) => (pr[k] && typeof pr[k].number === 'number') ? pr[k].number : null;
  const rows = (history || []).filter(p => p.id.replace(/-/g, '') !== v.id).map(p => {
    const pr = p.properties || {};
    return { d: (pr.Data && pr.Data.date && pr.Data.date.start) || '', w: num(pr, 'Carico Usato'), reps: num(pr, 'Rep Fatte') || num(pr, 'Rep Min') || 1 };
  }).filter(r => r.w > 0);
  const best = rows.reduce((m, r) => Math.max(m, epley(r.w, r.reps)), 0);
  const prev = rows.sort((a, b) => b.d.localeCompare(a.d))[0];
  const e1 = epley(v.kg, v.reps);
  return { ok: true, t: 'set', ex: v.name, kg: v.kg, reps: v.reps, e1rm: r1(e1), best: r1(best),
           pr: best > 0 && e1 > best + 0.05, delta: prev ? r1(v.kg - prev.w) : null };
}

// ---- "Prepara Pasto": ricalcola i preferiti (stessa logica del feed) e sceglie quello toccato ----
const asc = s => String(s == null ? '' : s).normalize('NFKD').replace(/[^\x20-\x7e]/g, '');
const mealKey = s => asc(s).toLowerCase().replace(/^\s*(colazione|pranzo|cena|spuntino|snack|merenda|out|fuori pasto)\s*:\s*/, '').replace(/\s+/g, ' ').trim();
function favorites(pages) {
  const num = (pr, k) => (pr[k] && typeof pr[k].number === 'number') ? pr[k].number : null;
  const txt = (pr, k) => { const x = pr[k]; return x ? (x.title || x.rich_text || []).map(t => t.plain_text || '').join('').trim() : ''; };
  const g = {};
  for (const pg of pages) {
    const pr = pg.properties || {};
    const name = txt(pr, 'Meal 🍴'), k = mealKey(name);
    if (!k) continue;
    const rec = { name, d: (pr.Date && pr.Date.date && pr.Date.date.start) || '', kcal: num(pr, 'Calories') || 0, p: num(pr, 'Proteins (g)') || 0,
                  c: num(pr, 'Carbs (g)') || 0, f: num(pr, 'Fats (g)') || 0 };
    const e = g[k] = g[k] || { n: 0, last: rec };
    e.n++;
    if (rec.d >= e.last.d) e.last = rec;
  }
  return Object.entries(g).filter(([, e]) => e.n >= 2).sort((a, b) => b[1].n - a[1].n).slice(0, 6).map(([k, e]) => {
    const short = k.charAt(0).toUpperCase() + k.slice(1);
    return { label: short.length > 24 ? short.slice(0, 23) + '.' : short, ...e.last };
  });
}
function prepareMeal(v, pages, now) {
  const favs = favorites(pages);
  const f = favs.find(x => x.label === v.label) || null;   // per etichetta: l'ordine puo' essere cambiato
  if (!f) return { ok: false, err: 'preferito non trovato' };
  const h = Number(now.toLocaleString('en-GB', { timeZone: 'Europe/Rome', hour: '2-digit', hour12: false }));
  const tag = h < 11 ? 'Colazione' : h < 15 ? 'Pranzo' : h < 18 ? 'Snack' : h < 22 ? 'Cena' : 'Snack';
  const today = now.toLocaleDateString('sv-SE', { timeZone: 'Europe/Rome' });
  const title = f.name.replace(/^\s*[^:]{3,15}:\s*/, tag + ': ');
  return { ok: true, t: 'meal', label: f.label, kcal: Math.round(f.kcal), notionBody: {
    parent: { database_id: FOOD_DB },
    properties: {
      'Meal 🍴': { title: [{ text: { content: title.slice(0, 1900) } }] },
      'Calories': { number: Math.round(f.kcal) }, 'Proteins (g)': { number: Math.round(f.p) },
      'Carbs (g)': { number: Math.round(f.c) }, 'Fats (g)': { number: Math.round(f.f) },
      'Tag': { select: { name: tag } }, 'Date': { date: { start: today } } } } };
}

if (typeof module !== 'undefined') module.exports = { validate, computePR, favorites, prepareMeal, LOAD_DB, FOOD_DB };
