const assert = require('assert');
const { toDeck, plan, favorites, streak } = require('./deck_transform.js');
const L = require('./log_nodes.js');
const t = s => ({ title: [{ plain_text: s }] }), n = v => ({ number: v }), d = v => ({ date: { start: v } }), sl = v => ({ select: { name: v } });
const today = '2026-10-27';
const lt = (id, ex, g, date, used, extra = {}) => ({ id, properties: { Esercizio: t(ex), Giorno: sl(g), Data: d(date), 'Carico Usato': n(used), 'Carico Presunto': n(80), 'Rep Min': n(8), 'Rep Max': n(12), 'Rep Fatte': n(extra.reps ?? null) } });
const load = [
  lt('aaaa-1', 'Hack Squat', 'Lower A', '2026-10-29', null), lt('aaaa-2', 'Hack Squat (1)', 'Lower A', '2026-10-29', null),
  lt('bbbb-1', 'Leg Curl', 'Lower B', '2026-10-31', null),
  lt('cccc-1', 'Old', 'Lower A', '2026-10-20', 100, { reps: 8 }),
];
let p = plan(load, today);
assert.equal(p.d, '29/10'); assert.equal(p.g, 'Lower A'); assert.equal(p.ex.length, 2);
assert.deepEqual(p.ex[1].slice(1), ['Hack Squat #2', 8, 12, 80, -1, -1]);
// oggi con serie gia' fatte -> resta sulla seduta di oggi
load.push(lt('dddd-1', 'Panca', 'Upper Push', today, 90, { reps: 8 }));
p = plan(load, today); assert.equal(p.d, 'Oggi'); assert.deepEqual(p.ex[0].slice(5), [90, 8]);
assert.equal(plan([], today), null);

const ft = (name, date, kcal, pp, tag) => ({ properties: { 'Meal 🍴': t(name), Date: d(date), Calories: n(kcal), 'Proteins (g)': n(pp), 'Carbs (g)': n(10), 'Fats (g)': n(5), Tag: sl(tag) } });
const food = [
  ft('Colazione: 140g albume + 30g avena', '2026-10-25', 500, 60, 'Colazione'), ft('Colazione: 140g albume + 30g avena', '2026-10-26', 507, 59, 'Colazione'),
  ft('Spuntino: 35g proteine limone', '2026-10-26', 121, 29, 'Snack'), ft('spuntino: 35g proteine limone', '2026-10-24', 120, 29, 'Snack'),
  ft('Pranzo: unico', '2026-10-26', 900, 60, 'Pranzo'),
  ft('Cena: x', '2026-10-26', 600, 60, 'Cena'), ft('Cena: x', '2026-10-25', 1500, 130, 'Cena'), ft('Cena: x', '2026-10-24', 1700, 160, 'Cena'),
];
const fav = favorites(food);
assert.equal(fav.length, 3); assert.equal(fav[0][0], 'X'); assert.ok(fav.find(f => f[0] === '140g albume + 30g avena' && f[1] === 507));
// streak: 26 = 507+121+900+600=2128 kcal P 208 ok; 25 = 500+1500=2000 P190 ok; 24 = 120+1700=1820 P189 ok; 23 none
assert.equal(streak(food, today), 3);
food.push(ft('Pranzo: oggi', today, 1600, 185, 'Pranzo')); assert.equal(streak(food, today), 4);

const feed = { today, updated: '2026-10-27T10:14:00+01:00', macro: { day_type: 'ON', pasti: 2, kcal: 743, p: 78, c: 67, f: 15, on_target_kcal: 2400, on_target_p: 200, on_target_c: 243, on_target_f: 70 },
  weight: { peso: 92.1, delta7: 0.4, data: '2026-10-10', series: [['2026-10-01', 92.5], ['2026-10-10', 92.1]] },
  workouts: { settimana_pesi: 1, weekly: [{ 'Lower A': 1, 'Upper Push': 0, 'Lower B': 1, 'Upper Pull': 0, Cardio: 2 }] }, lifts: { big: [{ lift: 'Bench', w: 115, reps: 3, e1rm: 126.5 }], md: '' }, calendar: { md: '' } };
const out = toDeck(feed, load, food);
assert.deepEqual(out.wk, [1, 0, 1, 0]); assert.deepEqual(out.ps, [92.5, 92.1]); assert.equal(out.fav[0].length, 5);
const js = JSON.stringify(out); console.log('deck bytes', js.length);

// log: validazione
assert.equal(L.validate({ t: 'set', id: 'x', kg: 10, reps: 8, name: 'a' }).ok, false);
assert.equal(L.validate({ t: 'set', id: 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa', kg: 600, reps: 8, name: 'a' }).ok, false);
assert.equal(L.validate({ t: 'set', id: 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa', kg: 100, reps: 8.5, name: 'a' }).ok, false);
const v = L.validate({ t: 'set', id: 'AAAA-aaaaaaaaaaaaaaaaaaaaaaaaaaaa', kg: 102.6, reps: 8, name: 'Hack Squat #2' });
assert.ok(v.ok); assert.equal(v.name, 'Hack Squat'); assert.equal(v.kg, 102.5);
assert.equal(L.validate({ t: 'x' }).ok, false);
// PR
const hist = [{ id: 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa', properties: { Data: d(today), 'Carico Usato': n(102.5), 'Rep Fatte': n(8) } },
              { id: 'old1', properties: { Data: d('2026-10-20'), 'Carico Usato': n(100), 'Rep Fatte': n(8) } }];
let r = L.computePR(v, { object: 'page' }, hist);
assert.ok(r.pr && r.delta === 2.5 && r.best === 126.7);
r = L.computePR({ ...v, kg: 90 }, { object: 'page' }, hist); assert.ok(!r.pr);
assert.equal(L.computePR(v, { object: 'error' }, hist).ok, false);
// pasto
const m = L.prepareMeal(L.validate({ t: 'meal', i: 0, label: '35g proteine limone' }), food, new Date('2026-10-27T15:30:00Z'));
assert.ok(m.ok); assert.equal(m.notionBody.properties.Tag.select.name, 'Snack');
assert.equal(m.notionBody.properties['Meal 🍴'].title[0].text.content, 'Snack: 35g proteine limone');
assert.equal(m.notionBody.properties.Date.date.start, today);
assert.equal(L.prepareMeal({ label: 'nope' }, food, new Date()).ok, false);
console.log('n8n logic ok');
