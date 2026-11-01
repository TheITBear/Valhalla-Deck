const assert = require('assert');
const { planWeek } = require('./week_nodes.js');
const pg = (t, g, d, s, pres, used, rmin = 8, rmax = 10) => ({ properties: {
  Esercizio: { title: [{ plain_text: t }] }, Giorno: { select: g ? { name: g } : null }, Data: { date: d ? { start: d } : null },
  Settimana: { number: s }, 'Carico Presunto': { number: pres }, 'Carico Usato': { number: used },
  'Rep Min': { number: rmin }, 'Rep Max': { number: rmax } } });
const P = r => r.properties;

// settimana 11 come su Notion: Upper Pull di oggi non ancora fatto
const pages = [
  pg('Calf in piedi', 'Lower A', '2026-10-26', 11, 85, 85),
  pg('Chest Press / Dip zavorrata', 'Upper Push', '2026-10-28', 11, 110, 80),
  pg('Stacco rumeno', 'Lower B', '2026-10-30', 11, 90, 80),
  pg('Stacco rumeno (1)', 'Lower B', '2026-10-30', 11, 90, 85),
  pg('Alzate laterali', 'Upper Pull', '2026-11-01', 11, 25, null),
  pg('Curl nuovo', 'Upper Pull', '2026-11-01', 11, null, null),
  pg('Curl nuovo', 'Upper Pull', '2026-10-09', 10, 12, 12.5),
  pg('Calf in piedi', 'Lower A', '2026-10-05', 10, 80, 85),
  pg('Vecchio blocco', 'Lower A', '2026-06-01', 11, 50, 50),   // stesso numero, blocco vecchio
];
const r = planWeek(pages);
assert.ok(r.ok, r.err);
assert.strictEqual(r.week, 12); assert.strictEqual(r.from, 11);
assert.strictEqual(r.n, 6); assert.strictEqual(r.miss, 2);
assert.deepStrictEqual(r.d, ['02/11', '08/11']);
const by = t => r.bodies.find(b => P(b).Esercizio.title[0].text.content === t);
assert.strictEqual(P(by('Chest Press / Dip zavorrata'))['Carico Presunto'].number, 80);   // usato, non presunto
assert.strictEqual(P(by('Chest Press / Dip zavorrata')).Data.date.start, '2026-11-04');
assert.strictEqual(P(by('Stacco rumeno (1)'))['Carico Presunto'].number, 85);             // serie per serie
assert.strictEqual(P(by('Alzate laterali'))['Carico Presunto'].number, 25);               // non fatto: tiene il presunto
assert.strictEqual(P(by('Curl nuovo'))['Carico Presunto'].number, 12.5);                  // nessun presunto: storico
assert.strictEqual(P(by('Calf in piedi')).Giorno.select.name, 'Lower A');
assert.strictEqual(P(by('Calf in piedi')).Settimana.number, 12);
assert.strictEqual(P(by('Calf in piedi'))['Rep Max'].number, 10);
assert.ok(!by('Vecchio blocco'));
assert.strictEqual(r.bodies[0].parent.database_id, 'YOUR_LOAD_TRACKER_DB_ID');

// secondo tocco: l'ultima settimana e' quella appena preparata -> rifiuto
const created = r.bodies.map(b => pg(P(b).Esercizio.title[0].text.content, 'X', P(b).Data.date.start, 12, 1, null));
const r2 = planWeek(pages.concat(created));
assert.ok(!r2.ok); assert.strictEqual(r2.err, "settimana 12 gia' pronta");

// nuovo programma con numerazione da capo: si usa la data, non il numero piu' alto
const mj = [pg('Squat', 'Lower A', '2026-11-09', 1, 100, 105), pg('Calf in piedi', 'Lower A', '2026-10-26', 11, 85, 85)];
const r3 = planWeek(mj);
assert.strictEqual(r3.week, 2); assert.strictEqual(r3.n, 1);
assert.strictEqual(P(r3.bodies[0])['Carico Presunto'].number, 105);

assert.strictEqual(planWeek([]).err, 'Load Tracker vuoto');
console.log('test_week OK');
