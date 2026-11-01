import { workflow, node, trigger, sticky, newCredential } from '@n8n/workflow-sdk';

const hook = trigger({
  type: 'n8n-nodes-base.webhook',
  version: 2.1,
  config: {
    name: 'Display Webhook',
    parameters: { httpMethod: 'GET', path: 'valhalla-display', authentication: 'headerAuth', responseMode: 'responseNode' },
    credentials: { httpHeaderAuth: newCredential('Valhalla Token') },
    position: [0, 300]
  },
  output: [{ headers: {} }]
});

const feed = node({
  type: 'n8n-nodes-base.httpRequest',
  version: 4.4,
  config: {
    name: 'Valhalla HA Feed',
    parameters: {
      method: 'GET',
      url: 'https://n8n.example.com/webhook/valhalla-ha',
      authentication: 'genericCredentialType',
      genericAuthType: 'httpHeaderAuth',
      options: { timeout: 60000 }
    },
    credentials: { httpHeaderAuth: newCredential('Valhalla Token') },
    retryOnFail: true,
    position: [260, 300]
  },
  output: [{ today: '2026-10-27', macro: {} }]
});

const compact = node({
  type: 'n8n-nodes-base.code',
  version: 2,
  config: {
    name: 'Compatta per display',
    parameters: { mode: 'runOnceForAllItems', language: 'javaScript', jsCode: "// n8n Code node (\"Run Once for All Items\"): payload \"Valhalla HA Feed\" -> JSON compatto per il display ESP32.\n// Compatta il feed per il display. Testabile fuori da n8n: node test_transform.js\nconst SPLITS = { lower_a: 'Lower A', lower_b: 'Lower B', upper_push: 'Upper Push', upper_pull: 'Upper Pull' };\nconst CODES = { LA: 'Lower A', LB: 'Lower B', PS: 'Upper Push', PL: 'Upper Pull', PB: 'Pump S+B' };\nconst BIG = ['Squat', 'Bench', 'Deadlift', 'OHP'];\nconst GIORNI = ['Lun', 'Mar', 'Mer', 'Gio', 'Ven', 'Sab', 'Dom'];\n\nconst asc = s => String(s == null ? '' : s).normalize('NFKD').replace(/[^\\x20-\\x7e]/g, '');\nconst ddmm = iso => iso ? iso.slice(8, 10) + '/' + iso.slice(5, 7) : '';\nconst days = (a, b) => Math.round((Date.parse(a + 'T00:00:00Z') - Date.parse(b + 'T00:00:00Z')) / 86400000);\nconst r1 = v => Math.round((v || 0) * 10) / 10;\n\nfunction lastSession(feed, today) {\n  const w = feed.workouts || {};\n  let best = null;\n  for (const [k, name] of Object.entries(SPLITS)) {\n    const d = w['ultimo_' + k];\n    if (d && (!best || d > best[0])) best = [d, name];\n  }\n  if (!best) return null;\n  const [date, split] = best;\n  // shortcut: legge la tabella markdown del feed HA; se ne cambia il formato, esporre campi strutturati nel feed\n  const md = ((feed.lifts || {}).md) || '';\n  const esc = split.replace(/[.*+?^${}()|[\\]\\\\]/g, '\\\\$&');\n  const m = md.match(new RegExp('#### ' + esc + '\\\\n([\\\\s\\\\S]*?)(?:\\\\n\\\\n####|$)'));\n  const ex = [];\n  for (const row of (m ? m[1].split('\\n').slice(2) : [])) {\n    const c = row.replace(/^\\||\\|$/g, '').split('|').map(s => s.trim());\n    if (c.length < 4 || !c[1].includes('\u00d7')) continue;\n    const [kg, reps] = c[1].split('\u00d7').map(Number);\n    const delta = c[2].replace('\u25b2', '+').replace('\u25bc', '-').replace('\uff1d', '=').replace('\u2014', '');\n    if (isNaN(kg) || isNaN(reps)) continue;\n    ex.push([asc(c[0]), kg, reps, delta, Number(c[3]) || 0]);\n  }\n  ex.sort((a, b) => (b[3].startsWith('+') - a[3].startsWith('+')) || (b[4] - a[4]));\n  return { g: split, d: ddmm(date), ago: days(today, date), ex: ex.slice(0, 4).map(e => e.slice(0, 4)) };\n}\n\nfunction nextSessions(feed, today) {\n  const out = [];\n  const macro = feed.macro || {};\n  if (macro.pianificato_oggi && !macro.allenato_oggi) out.push(['Oggi', asc(macro.pianificato_oggi)]);\n  // shortcut: il calendario del feed copre solo il mese corrente\n  const md = ((feed.calendar || {}).md) || '';\n  for (const cell of md.split('|')) {\n    const m = cell.match(/^\\s*\\**\\[?(\\d{1,2})\\]?\\**<br>(.*)/);\n    if (!m) continue;\n    const d = today.slice(0, 8) + String(m[1]).padStart(2, '0');\n    if (d <= today) continue;\n    const wd = (new Date(d + 'T00:00:00Z').getUTCDay() + 6) % 7;\n    for (const code of (m[2].match(/\u26aa(\\w+)/g) || []))\n      out.push([GIORNI[wd] + ' ' + Number(m[1]), CODES[code.slice(1)] || code.slice(1)]);\n  }\n  return out.slice(0, 3);\n}\n\nfunction toDisplay(feed) {\n  const today = feed.today;\n  const m = feed.macro || {};\n  const lt = m.day_type === 'ON' ? 'on' : 'off';\n  const w = feed.weight || {};\n  const wk = feed.workouts || {};\n  const big = {};\n  for (const b of ((feed.lifts || {}).big || [])) big[b.lift] = b;\n  return {\n    upd: String(feed.updated || '').slice(11, 16),\n    day: m.day_type || 'OFF',\n    pasti: m.pasti || 0,\n    kcal: [r1(m.kcal), m[lt + '_target_kcal'] || 0],\n    p: [r1(m.p), m[lt + '_target_p'] || 0],\n    c: [r1(m.c), m[lt + '_target_c'] || 0],\n    f: [r1(m.f), m[lt + '_target_f'] || 0],\n    peso: [w.peso || 0, w.delta7 || 0, ddmm(w.data)],\n    week: { pesi: wk.settimana_pesi || 0, cardio: wk.settimana_cardio || 0, pesi30: wk.pesi_30gg || 0, cardio30: wk.cardio_30gg || 0 },\n    pr: BIG.filter(k => big[k]).map(k => [k, big[k].w, big[k].reps || 1, big[k].e1rm]),\n    last: lastSession(feed, today),\n    next: nextSessions(feed, today),\n  };\n}\n\nif (typeof module !== 'undefined' && typeof $input === 'undefined') {\n  module.exports = { toDisplay };\n} else {\n  return [{ json: toDisplay($input.first().json) }];\n}\n" },
    position: [520, 300]
  },
  output: [{ day: 'ON' }]
});

const respond = node({
  type: 'n8n-nodes-base.respondToWebhook',
  version: 1.5,
  config: {
    name: 'Rispondi al display',
    parameters: {
      respondWith: 'json',
      responseBody: '={{ JSON.stringify($json) }}',
      options: { responseHeaders: { entries: [{ name: 'Cache-Control', value: 'no-store' }] } }
    },
    position: [780, 300]
  },
  output: [{}]
});

const note = sticky('## Valhalla Display\nGET `/webhook/valhalla-display` + header `X-Valhalla-Token`.\nRiusa il feed HA e lo compatta (~550 byte) per l\'ESP32-S3.\nLogica: esp32/n8n/display_transform.js (test: node test_transform.js).', [], { position: [-40, 80], width: 600, height: 180 });

export default workflow('valhalla-display', 'Valhalla Display Feed')
  .add(hook)
  .to(feed)
  .to(compact)
  .to(respond)
  .add(note);
