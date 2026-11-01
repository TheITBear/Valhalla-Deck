const a = require('assert'), X = require('./plus_nodes.js');
const T = s => ({ title: [{ plain_text: s }] }), N = n => ({ number: n }), D = d => ({ date: { start: d } });
const L = (ex, d, used, reps, rmin, rmax) => ({ properties: { Esercizio: T(ex), Data: D(d), 'Carico Usato': N(used), 'Rep Fatte': N(reps), 'Rep Min': N(rmin), 'Rep Max': N(rmax) } });
const load = [
  L('Hack squat', '2026-10-20', 120, 12, 10, 12), L('Hack squat (1)', '2026-10-20', 120, 12, 10, 12),
  L('Hack squat', '2026-10-13', 115, 12, 10, 12),
  L('Stacco rumeno', '2026-10-21', 100, 9, 8, 10), L('Stacco rumeno (1)', '2026-10-21', 100, 7, 8, 10),
  L('Leg curl', '2026-10-21', null, null, 12, 15)];
const h = X.history(load);
const hs = Object.fromEntries(h.map(r => [r[0], r]));
a.deepStrictEqual(hs['hack squat'], ['hack squat', 120, 12, 1, 0]);    // tutte al top -> salire
a.deepStrictEqual(hs['stacco rumeno'], ['stacco rumeno', 100, 7, 0, 1]); // una sotto il minimo
a.ok(!hs['leg curl']);
a.strictEqual(X.baseName('Stacco rumeno manubri pesanti (2)'), 'stacco rumeno manubri');
const food = [{ created_time: '2026-10-30T11:40:00.000Z', properties: { Date: D('2026-10-30'), 'Proteins (g)': N(60), Calories: N(700) } },
              { created_time: '2026-10-30T06:10:00.000Z', properties: { Date: D('2026-10-30'), 'Proteins (g)': N(40), Calories: N(400) } },
              { created_time: '2026-10-29T19:00:00.000Z', properties: { Date: D('2026-10-29'), 'Proteins (g)': N(200), Calories: N(2000) } },
              { created_time: '2026-10-27T19:00:00.000Z', properties: { Date: D('2026-10-27'), 'Proteins (g)': N(190), Calories: N(2300) } }];
a.strictEqual(X.lastMeal(food, '2026-10-30'), Date.parse('2026-10-30T11:40:00Z') / 1000);
a.strictEqual(X.lastMeal(food, '2026-10-31'), 0);
a.deepStrictEqual(X.fasting([{ id: '11111111-2222-4333-8444-555555555555', properties: { 'Digiuno (h)': N(2) } }, { id: 'x', properties: {} }]), [['11111111222243338444555555555555', 2]]);
a.deepStrictEqual(X.weekRange('2026-10-30'), { mon: '2026-10-26', sun: '2026-11-01', days: 5 });
a.deepStrictEqual(X.weekRange('2026-11-01').days, 7);
const wl = [L('A', '2026-10-27', 100, 8, 8, 10), L('B', '2026-10-28', 50, 8, 8, 10), L('C', '2026-10-30', null, null, 8, 10), L('D', '2026-10-31', null, null, 8, 10), L('E', '2026-10-20', 1, 1, 1, 1)];
const water = [{ properties: { ml: N(3000), Data: D('2026-10-28T10:00:00+01:00') } }, { properties: { ml: N(500), Data: D('2026-10-30T10:00:00+01:00') } }];
const weights = [{ properties: { Peso: N(91.8), Date: D('2026-10-26') } }, { properties: { Peso: N(91.2), Date: D('2026-10-30') } }, { properties: { Peso: N(95), Date: D('2026-10-10') } }];
const prs = [{ properties: { Lift: T('Squat'), 'Workout Date': D('2026-10-29') } }, { properties: { Lift: T('Squat'), 'Workout Date': D('2026-10-29') } }, { properties: { Lift: T('Bench'), 'Workout Date': D('2026-10-01') } }];
const r = X.recap('2026-10-30', wl, food, water, weights, prs);
a.deepStrictEqual(r.allen, [2, 4]); a.deepStrictEqual(r.p, [Math.round((100 + 200 + 190) / 3), 2, 3]);
a.deepStrictEqual(r.h2o, [0.7, 1]); a.deepStrictEqual(r.peso, [91.8, 91.2]); a.deepStrictEqual(r.pr, ['Squat']); a.strictEqual(r.target, 0);
a.deepStrictEqual(r.d, ['26/10', '01/11']);
const blocks = [{ type: 'paragraph' }, { id: 'aaaa-bbbb', type: 'file', last_edited_time: '2026-10-30T10:00:00.000Z', file: { type: 'file', name: 'old.bin', file: { url: 'https://s3/old' } } },
                { id: 'cccc-dddd-eeee-ffff', type: 'file', last_edited_time: '2026-10-30T11:22:00.000Z', file: { type: 'file', name: 'valhalla_display.ino.bin', file: { url: 'https://s3/new' } } }];
const f = X.firmware(blocks); a.strictEqual(f.url, 'https://s3/new'); a.strictEqual(f.id, 'ccccddddeeee-202610301122'); a.strictEqual(X.firmware([]), null);
const p = X.toPlus('2026-10-30', load, wl, food, water, weights, prs, [], blocks); a.strictEqual(p.fw[1], 'valhalla_display.ino.bin');
console.log('plus ok');
