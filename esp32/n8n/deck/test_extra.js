const a = require('assert'), X = require('./extra_nodes.js');
const T = s => ({ title: [{ plain_text: s }] }), R = s => ({ rich_text: [{ plain_text: s }] }), S = s => ({ select: s ? { name: s } : null }), N = n => ({ number: n });
const prof = [{ properties: { Nome: T('Atleta'), Fase: S('Recomp'), Blocco: R('FENRIR → MJÖLNIR'), Obiettivo: R('x'), 'Altezza (cm)': N(178), 'BF stimato (%)': N(18), 'Acqua obiettivo (ml)': N(3000) } }];
const sid = '11111111-2222-4333-8444-555555555555', sid2 = '66666666-7777-4888-8999-aaaaaaaaaaab';
const supp = [
  { id: sid, properties: { Composto: T('Elettroliti'), Dose: R('100 mcg'), Momento: S('Sera (21:30-23:00)') } },
  { id: sid2, properties: { Composto: T('Creatina'), Dose: R('5 g'), Momento: S('Mattina (07:00-09:00)') } },
  { id: 'aaaa', properties: { Composto: T(''), Momento: S('Pranzo') } }];
const log = [{ properties: { Integratore: { relation: [{ id: sid }] } } }];
const water = [{ properties: { ml: N(500) } }, { properties: { ml: N(200) } }];
const e = X.toExtra(prof, supp, log, water);
a.strictEqual(e.ath.n, 'Atleta'); a.strictEqual(e.ath.blk, 'FENRIR > MJOLNIR'); a.strictEqual(e.ath.h, 178);
a.deepStrictEqual(e.h2o, [700, 3000]);
a.strictEqual(e.sup.length, 2); a.strictEqual(e.sup[0][1], 'Creatina'); a.strictEqual(e.sup[0][3], 0); a.strictEqual(e.sup[0][4], 0);
a.strictEqual(e.sup[1][4], 1); a.strictEqual(e.sup[1][0], sid.replace(/-/g, ''));
const empty = X.toExtra([], [], [], []); a.strictEqual(empty.ath.n, 'Atleta'); a.deepStrictEqual(empty.h2o, [0, 3000]);
const now = '2026-10-29T14:30:00+01:00';
a.strictEqual(X.validate({ t: 'water', ml: 500 }, now).notionBody.properties.ml.number, 500);
a.ok(!X.validate({ t: 'water', ml: 5000 }, now).ok); a.ok(!X.validate({ t: 'water', ml: 2.5 }, now).ok);
a.ok(!X.validate({ t: 'supp', id: 'zz' }, now).ok); a.strictEqual(X.validate({ t: 'supp', id: sid }, now).id, sid.replace(/-/g, ''));
const p = X.validate({ t: 'prs', lift: 'Squat', kg: 142.6, reps: 3 }, now);
a.strictEqual(p.kg, 142.5); a.strictEqual(p.notionBody.properties['Workout Date'].date.start, '2026-10-29');
a.ok(!X.validate({ t: 'prs', lift: 'Squat', kg: 0, reps: 3 }, now).ok); a.ok(!X.validate({ t: 'prs', lift: '', kg: 10, reps: 3 }, now).ok);
a.ok(!X.validate({ t: 'boh' }, now).ok); a.ok(!X.validate(null, now).ok);
const page = { object: 'page', id: sid, parent: { database_id: 'YOUR_INTEGRAZIONE_DB_ID' }, properties: supp[0].properties };
const ps = X.prepareSupp({ id: sid }, page, now);
a.ok(ps.ok); a.deepStrictEqual(ps.notionBody.properties.Integratore.relation, [{ id: sid }]); a.strictEqual(ps.notionBody.properties.Momento.select.name, 'Sera (21:30-23:00)');
a.ok(!X.prepareSupp({ id: sid }, { ...page, parent: { database_id: 'YOUR_LOAD_TRACKER_DB_ID' } }, now).ok);   // pagina di un altro DB: rifiutata
a.ok(!X.prepareSupp({ id: sid }, { object: 'error' }, now).ok);
a.deepStrictEqual(X.waterResult({ object: 'page' }, water), { ok: true, t: 'water', tot: 700 }); a.ok(!X.waterResult({ object: 'error' }, []).ok);
a.strictEqual(X.romeIso(new Date('2026-10-29T13:30:05Z')), '2026-10-29T14:30:05+01:00');
a.strictEqual(X.romeIso(new Date('2026-07-01T22:15:00Z')), '2026-07-02T00:15:00+02:00');
console.log('extra ok');
