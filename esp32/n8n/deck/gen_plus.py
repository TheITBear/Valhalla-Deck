import json, re
J = json.dumps
src = open('plus_nodes.js').read().split("if (typeof module")[0]
src = "\n".join(l for l in src.split("\n")[3:])   # senza commenti di testa
NOTION_H = "sendHeaders: true, headerParameters: { parameters: [{ name: 'Notion-Version', value: '2022-06-28' }] }"
CRED_N = "credentials: { notionApi: newCredential('Notion account') }"
PAG = "pagination: { pagination: { paginationMode: 'updateAParameterInEachRequest', parameters: { parameters: [{ type: 'body', name: 'start_cursor', value: expr('{{ $response.body.next_cursor }}') }] }, paginationCompleteWhen: 'other', completeExpression: expr('{{ $response.body.has_more !== true }}'), limitPagesFetched: true, maxRequests: %d, requestInterval: 350 } }"
WEEK = "$now.setZone('Europe/Rome').startOf('week')"
def q(var, name, pos, db, body, pages=1):
    pg = (", " + PAG % pages) if pages > 1 else ""
    return f"""const {var} = node({{ type: 'n8n-nodes-base.httpRequest', version: 4.2, config: {{ name: {J(name)}, position: {pos}, executeOnce: true, retryOnFail: true, maxTries: 3, waitBetweenTries: 3000, alwaysOutputData: true, parameters: {{ method: 'POST', url: 'https://api.notion.com/v1/databases/{db}/query', authentication: 'predefinedCredentialType', nodeCredentialType: 'notionApi', {NOTION_H}, sendBody: true, contentType: 'json', specifyBody: 'json', jsonBody: expr({J(body)}), options: {{ timeout: 30000{pg} }} }}, {CRED_N} }}, output: [{{ object: 'list', results: [] }}] }});"""
FW_PAGE = 'YOUR_FIRMWARE_PAGE_ID'
def fwq(var, name, pos):
    return f"""const {var} = node({{ type: 'n8n-nodes-base.httpRequest', version: 4.2, config: {{ name: {J(name)}, position: {pos}, executeOnce: true, retryOnFail: true, maxTries: 3, waitBetweenTries: 3000, alwaysOutputData: true, parameters: {{ method: 'GET', url: 'https://api.notion.com/v1/blocks/{FW_PAGE}/children?page_size=100', authentication: 'predefinedCredentialType', nodeCredentialType: 'notionApi', {NOTION_H}, options: {{ timeout: 30000 }} }}, {CRED_N} }}, output: [{{ object: 'list', results: [] }}] }});"""
LOAD, FOOD, WATER, PESO, PR, SUPP = 'YOUR_LOAD_TRACKER_DB_ID', 'YOUR_FOOD_TRACKER_DB_ID', 'YOUR_IDRATAZIONE_LOG_DB_ID', 'YOUR_PESO_DB_ID', 'YOUR_PR_TRACKER_DB_ID', 'YOUR_INTEGRAZIONE_DB_ID'
pages = "const pages = n => { try { return $(n).all().flatMap(i => (i.json && i.json.results) || []); } catch (e) { return []; } };\n"
glue = pages + "const today = new Date().toLocaleDateString('sv-SE', { timeZone: 'Europe/Rome' });\nreturn [{ json: toPlus(today, pages('Notion Storico Esercizi'), pages('Notion Settimana Allenamenti'), pages('Notion FoodTracker Settimana'), pages('Notion Acqua Settimana'), pages('Notion Peso Settimana'), pages('Notion PR Settimana'), pages('Notion Integrazione Digiuno'), pages('Notion Pagina Firmware')) }];"
fwglue = "const fw = firmware($input.all().flatMap(i => (i.json && i.json.results) || []));\nreturn [{ json: fw ? { ok: true, url: fw.url, name: fw.name, id: fw.id } : { ok: false, err: 'nessun firmware nella pagina Notion' } }];"
fwsrc = src[:src.index("function history")] + src[src.index("// Ultimo file"):src.index("function toPlus")]
out = f"""import {{ workflow, node, trigger, ifElse, sticky, newCredential, expr }} from '@n8n/workflow-sdk';

const hookP = trigger({{ type: 'n8n-nodes-base.webhook', version: 2.1, config: {{ name: 'Deck Plus Webhook', position: [0, 200], parameters: {{ httpMethod: 'GET', path: 'valhalla-deck-p', authentication: 'headerAuth', responseMode: 'responseNode', options: {{}} }}, credentials: {{ httpHeaderAuth: newCredential('X-Valhalla-Token') }} }}, output: [{{ headers: {{}}, query: {{}} }}] }});
{q('q1', 'Notion Storico Esercizi', '[220, 200]', LOAD, "{{ JSON.stringify({ page_size: 100, sorts: [{ property: 'Data', direction: 'descending' }], filter: { and: [{ property: 'Carico Usato', number: { is_not_empty: true } }, { property: 'Data', date: { on_or_after: $now.setZone('Europe/Rome').minus({ days: 35 }).toFormat('yyyy-MM-dd') } }] } }) }}", 3)}
{q('q2', 'Notion Settimana Allenamenti', '[440, 200]', LOAD, "{{ JSON.stringify({ page_size: 100, filter: { and: [{ property: 'Data', date: { on_or_after: " + WEEK + ".toFormat('yyyy-MM-dd') } }, { property: 'Data', date: { on_or_before: $now.setZone('Europe/Rome').endOf('week').toFormat('yyyy-MM-dd') } }] } }) }}", 2)}
{q('q3', 'Notion FoodTracker Settimana', '[660, 200]', FOOD, "{{ JSON.stringify({ page_size: 100, filter: { property: 'Date', date: { on_or_after: " + WEEK + ".toFormat('yyyy-MM-dd') } } }) }}", 3)}
{q('q4', 'Notion Acqua Settimana', '[880, 200]', WATER, "{{ JSON.stringify({ page_size: 100, filter: { property: 'Data', date: { on_or_after: " + WEEK + ".toISO() } } }) }}", 2)}
{q('q5', 'Notion Peso Settimana', '[1100, 200]', PESO, "{{ JSON.stringify({ page_size: 100, filter: { property: 'Date', date: { on_or_after: " + WEEK + ".toFormat('yyyy-MM-dd') } } }) }}")}
{q('q6', 'Notion PR Settimana', '[1320, 200]', PR, "{{ JSON.stringify({ page_size: 100, filter: { property: 'Workout Date', date: { on_or_after: " + WEEK + ".toFormat('yyyy-MM-dd') } } }) }}")}
{q('q7', 'Notion Integrazione Digiuno', '[1540, 200]', SUPP, "{{ JSON.stringify({ page_size: 100 }) }}")}
{fwq('q8', 'Notion Pagina Firmware', '[1760, 200]')}
const compose = node({{ type: 'n8n-nodes-base.code', version: 2, config: {{ name: 'Componi Plus', position: [1980, 200], executeOnce: true, parameters: {{ jsCode: {J(src.strip() + chr(10) + chr(10) + glue)} }} }}, output: [{{ hist: [] }}] }});
const respP = node({{ type: 'n8n-nodes-base.respondToWebhook', version: 1.5, config: {{ name: 'Rispondi Plus', position: [2200, 200], parameters: {{ respondWith: 'json', responseBody: expr('{{{{ JSON.stringify($json) }}}}'), options: {{ responseHeaders: {{ entries: [{{ name: 'Cache-Control', value: 'no-store' }}] }} }} }} }}, output: [{{}}] }});

const hookFw = trigger({{ type: 'n8n-nodes-base.webhook', version: 2.1, config: {{ name: 'Firmware Webhook', position: [0, 560], parameters: {{ httpMethod: 'GET', path: 'valhalla-fw', authentication: 'headerAuth', responseMode: 'responseNode', options: {{}} }}, credentials: {{ httpHeaderAuth: newCredential('X-Valhalla-Token') }} }}, output: [{{ headers: {{}} }}] }});
{fwq('fwBlocks', 'Blocchi Firmware', '[220, 560]')}
const fwPick = node({{ type: 'n8n-nodes-base.code', version: 2, config: {{ name: 'Scegli Firmware', position: [440, 560], executeOnce: true, parameters: {{ jsCode: {J(fwsrc.strip() + chr(10) + chr(10) + fwglue)} }} }}, output: [{{ ok: true, url: 'https://s3/x' }}] }});
const fwOk = ifElse({{ version: 2.2, config: {{ name: 'Firmware presente?', position: [660, 560], parameters: {{ conditions: {{ options: {{ caseSensitive: true, leftValue: '', typeValidation: 'loose' }}, conditions: [{{ id: 'f1', leftValue: expr('{{{{ $json.ok }}}}'), rightValue: '', operator: {{ type: 'boolean', operation: 'true', singleValue: true }} }}], combinator: 'and' }}, looseTypeValidation: true, options: {{}} }} }} }});
const fwGet = node({{ type: 'n8n-nodes-base.httpRequest', version: 4.2, config: {{ name: 'Scarica Firmware', position: [880, 520], retryOnFail: true, maxTries: 3, waitBetweenTries: 3000, parameters: {{ method: 'GET', url: expr('{{{{ $json.url }}}}'), authentication: 'none', options: {{ timeout: 60000, response: {{ response: {{ responseFormat: 'file', outputPropertyName: 'data' }} }} }} }} }}, output: [{{}}] }});
const fwResp = node({{ type: 'n8n-nodes-base.respondToWebhook', version: 1.5, config: {{ name: 'Invia Firmware', position: [1100, 520], parameters: {{ respondWith: 'binary', responseDataSource: 'set', inputFieldName: 'data', options: {{ responseHeaders: {{ entries: [{{ name: 'Content-Type', value: 'application/octet-stream' }}, {{ name: 'Cache-Control', value: 'no-store' }}] }} }} }} }}, output: [{{}}] }});
const fwNone = node({{ type: 'n8n-nodes-base.respondToWebhook', version: 1.5, config: {{ name: 'Nessun Firmware', position: [880, 700], parameters: {{ respondWith: 'json', responseBody: expr('{{{{ JSON.stringify($json) }}}}'), options: {{ responseCode: 404 }} }} }}, output: [{{}}] }});

const note = sticky('## Valhalla Deck Plus\\nGET `/webhook/valhalla-deck-p` = progressione, ultimo pasto, digiuno, recap settimana, firmware.\\nGET `/webhook/valhalla-fw` = ultimo .bin della pagina Notion Firmware Deck.\\nHeader `X-Valhalla-Token` su entrambi.', [hookP, hookFw], {{ color: 4 }});

export default workflow('valhalla-deck-plus', 'Valhalla Deck Plus')
  .add(hookP).to(q1).to(q2).to(q3).to(q4).to(q5).to(q6).to(q7).to(q8).to(compose).to(respP)
  .add(hookFw).to(fwBlocks).to(fwPick).to(fwOk.onTrue(fwGet.to(fwResp)).onFalse(fwNone))
  .add(note);
"""
open('deck_plus.js', 'w').write(out)
print(len(out))
