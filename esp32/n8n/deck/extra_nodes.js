// Logica dei Code node di "Valhalla Deck Extra" (testabile in node: test_extra.js).
// GET  /valhalla-deck-x : scheda atleta, acqua di oggi, integrazioni del giorno (con assunte)
// POST /valhalla-log-x  : {t:'water',ml} | {t:'supp',id} | {t:'prs',lift,kg,reps}
const WATER_DB = 'YOUR_IDRATAZIONE_LOG_DB_ID';
const SUPLOG_DB = 'YOUR_INTEGRAZIONE_LOG_DB_ID';
const SUPP_DB = 'YOUR_INTEGRAZIONE_DB_ID';
const PR_DB = 'YOUR_PR_TRACKER_DB_ID';

const asc = s => String(s == null ? '' : s).replace(/[→➜➔]/g, '>').normalize('NFKD').replace(/[^\x20-\x7e]/g, '');
const num = (pr, k) => (pr[k] && typeof pr[k].number === 'number') ? pr[k].number : null;
const txt = (pr, k) => { const v = pr[k]; return v ? (v.title || v.rich_text || []).map(t => t.plain_text || '').join('').trim() : ''; };
const sel = (pr, k) => (pr[k] && pr[k].select) ? pr[k].select.name : '';
const nid = id => String(id || '').replace(/-/g, '').toLowerCase();
const momentKey = m => /^mattina/i.test(m) ? 0 : /^pranzo/i.test(m) ? 1 : /^sera/i.test(m) ? 2 : 3;

// ---- "Componi Extra" ----
function toExtra(profilePages, suppPages, suppLogPages, waterPages) {
  const p = (profilePages[0] || {}).properties || {};
  const goal = num(p, 'Acqua obiettivo (ml)') || 3000;
  const taken = new Set(suppLogPages.flatMap(pg => (((pg.properties || {}).Integratore || {}).relation || []).map(r => nid(r.id))));
  const sup = suppPages.map(pg => {
    const pr = pg.properties || {};
    return [nid(pg.id), asc(txt(pr, 'Composto')).slice(0, 22), asc(txt(pr, 'Dose')).slice(0, 18), momentKey(sel(pr, 'Momento')), taken.has(nid(pg.id)) ? 1 : 0];
  }).filter(s => s[1]).sort((a, b) => a[3] - b[3] || a[1].localeCompare(b[1])).slice(0, 12);
  return {
    ath: {
      n: asc(txt(p, 'Nome')).slice(0, 20) || 'Atleta',
      fase: asc(sel(p, 'Fase')), blk: asc(txt(p, 'Blocco')).slice(0, 24), obj: asc(txt(p, 'Obiettivo')).slice(0, 60),
      h: num(p, 'Altezza (cm)') || 0, bf: num(p, 'BF stimato (%)') || 0, lbm: num(p, 'Massa magra (kg)') || 0,
      visc: num(p, 'Grasso viscerale') || 0, pgoal: num(p, 'Peso obiettivo (kg)') || 0, pt: num(p, 'Proteine target (g)') || 0,
    },
    h2o: [waterPages.reduce((s, pg) => s + (num(pg.properties || {}, 'ml') || 0), 0), goal],
    sup,
  };
}

// ---- "Valida Extra": confine di fiducia, il body arriva dal Deck ----
function validate(body, nowIso) {
  const b = body || {};
  const isInt = (v, lo, hi) => Number.isInteger(v) && v >= lo && v <= hi;
  if (b.t === 'water') {
    if (!isInt(b.ml, 50, 2000)) return { ok: false, err: 'quantita non valida' };
    return { ok: true, t: 'water', ml: b.ml, notionBody: { parent: { database_id: WATER_DB }, properties: {
      'Voce': { title: [{ text: { content: '+' + b.ml + ' ml' } }] }, 'ml': { number: b.ml },
      'Data': { date: { start: nowIso } }, 'Fonte': { select: { name: 'Deck' } } } } };
  }
  if (b.t === 'supp') {
    const id = nid(b.id);
    if (!/^[0-9a-f]{32}$/.test(id)) return { ok: false, err: 'id non valido' };
    return { ok: true, t: 'supp', id };
  }
  if (b.t === 'prs') {
    const lift = asc(b.lift).replace(/[^\w +\-.]/g, '').slice(0, 30).trim();
    if (!lift) return { ok: false, err: 'esercizio mancante' };
    if (typeof b.kg !== 'number' || !isFinite(b.kg) || b.kg <= 0 || b.kg > 500) return { ok: false, err: 'kg non validi' };
    if (!isInt(b.reps, 1, 20)) return { ok: false, err: 'reps non valide' };
    const kg = Math.round(b.kg * 4) / 4;
    return { ok: true, t: 'prs', lift, kg, reps: b.reps, notionBody: { parent: { database_id: PR_DB }, properties: {
      'Lift': { title: [{ text: { content: lift } }] }, 'Weight': { number: kg }, 'Reps': { number: b.reps }, 'Set': { number: 1 },
      'Workout Date': { date: { start: nowIso.slice(0, 10) } } } } };
  }
  return { ok: false, err: 'tipo sconosciuto' };
}

// ---- "Prepara Integrazione": il composto si legge da Notion, non ci si fida del Deck ----
function prepareSupp(v, page, nowIso) {
  const parent = nid(page && page.parent && page.parent.database_id);
  if (!page || page.object !== 'page' || parent !== nid(SUPP_DB)) return { ok: false, err: 'integratore non trovato' };
  const pr = page.properties || {};
  const name = txt(pr, 'Composto');
  const mom = sel(pr, 'Momento');
  const props = {
    'Composto': { title: [{ text: { content: name.slice(0, 200) } }] },
    'Dose': { rich_text: [{ text: { content: txt(pr, 'Dose').slice(0, 200) } }] },
    'Data': { date: { start: nowIso } },
    'Integratore': { relation: [{ id: page.id }] },
  };
  if (mom) props['Momento'] = { select: { name: mom } };
  return { ok: true, t: 'supp', id: v.id, name: asc(name).slice(0, 22), notionBody: { parent: { database_id: SUPLOG_DB }, properties: props } };
}

// ---- "Esito Acqua": totale di oggi dopo la scrittura ----
function waterResult(writeResp, todayPages) {
  if (!writeResp || writeResp.object !== 'page') return { ok: false, err: 'Notion non ha salvato l\'acqua' };
  const tot = todayPages.reduce((s, pg) => s + (num(pg.properties || {}, 'ml') || 0), 0);
  return { ok: true, t: 'water', tot };
}

// ISO con offset di Roma (Notion lo mostra all'ora giusta)
function romeIso(d) {
  const p = Object.fromEntries(new Intl.DateTimeFormat('en-GB', { timeZone: 'Europe/Rome', year: 'numeric', month: '2-digit', day: '2-digit',
    hour: '2-digit', minute: '2-digit', second: '2-digit', hour12: false }).formatToParts(d).map(x => [x.type, x.value]));
  const local = Date.UTC(+p.year, +p.month - 1, +p.day, +p.hour % 24, +p.minute, +p.second);
  const off = Math.round((local - Math.floor(d.getTime() / 1000) * 1000) / 60000);
  const sg = off >= 0 ? '+' : '-', a = Math.abs(off);
  return `${p.year}-${p.month}-${p.day}T${String(+p.hour % 24).padStart(2, '0')}:${p.minute}:${p.second}${sg}${String(Math.floor(a / 60)).padStart(2, '0')}:${String(a % 60).padStart(2, '0')}`;
}

if (typeof module !== 'undefined') module.exports = { toExtra, validate, prepareSupp, waterResult, romeIso, WATER_DB, SUPLOG_DB, SUPP_DB, PR_DB };
