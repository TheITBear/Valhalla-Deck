// node test_transform.js  -> verifica la trasformazione sul feed d'esempio
const assert = require('assert');
const fs = require('fs');
const path = require('path');
const src = fs.readFileSync(path.join(__dirname, 'display_transform.js'), 'utf8');
const { toDisplay } = new Function('module', src + '\nreturn module.exports;')({});
const feed = JSON.parse(fs.readFileSync(path.join(__dirname, 'sample_feed.json'), 'utf8'));

const v = toDisplay(feed);
assert.strictEqual(v.day, 'ON');
assert.deepStrictEqual(v.kcal, [1430.5, 2400]);
assert.deepStrictEqual(v.p, [128, 200]);
assert.strictEqual(v.last.g, 'Upper Pull');
assert.strictEqual(v.last.d, '10/10');
assert.strictEqual(v.last.ago, 14);
assert.deepStrictEqual(v.last.ex[0], ['Rematore chest-supported / macchina', 80, 10, '+5']);
assert.strictEqual(v.last.ex[1][0], 'Alzate laterali');
assert.deepStrictEqual(v.next, [['Oggi', 'Lower A'], ['Lun 26', 'Upper Push'], ['Mer 28', 'Upper Pull']]);
assert.deepStrictEqual(v.pr.map(p => p[0]), ['Squat', 'Bench', 'Deadlift', 'OHP']);
assert.strictEqual(toDisplay({ ...feed, macro: { ...feed.macro, day_type: 'OFF' } }).kcal[1], 1975);
assert.strictEqual(toDisplay({ ...feed, workouts: {} }).last, null);
console.log('ok', JSON.stringify(v).length, 'byte');
