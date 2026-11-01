import json, re
src = open('extra_nodes.js').read()
src = src.split("if (typeof module")[0]
J = json.dumps
head, rest = src.split("// ---- \"Componi Extra\"", 1)
head = head.split("\n", 3)[3]   # via i commenti di testa
rest = "// ---- \"Componi Extra\"" + rest
blocks = {}
for part in re.split(r"\n(?=// )", rest):
    m = re.search(r"function (\w+)", part)
    if m: blocks[m.group(1)] = part.strip()
def code(glue):
    need = [f for f in blocks if re.search(r"\b" + f + r"\(", glue)]
    return J(head.strip() + "\n\n" + "\n\n".join(blocks[f] for f in need) + "\n\n" + glue)
NOTION_H = "sendHeaders: true, headerParameters: { parameters: [{ name: 'Notion-Version', value: '2022-06-28' }] }"
CRED_N = "credentials: { notionApi: newCredential('Notion account') }"
def q(name, pos, db, body_expr):
    return f"""node({{ type: 'n8n-nodes-base.httpRequest', version: 4.2, config: {{ name: {J(name)}, position: {pos}, executeOnce: true, retryOnFail: true, maxTries: 3, waitBetweenTries: 3000, alwaysOutputData: true, parameters: {{ method: 'POST', url: 'https://api.notion.com/v1/databases/{db}/query', authentication: 'predefinedCredentialType', nodeCredentialType: 'notionApi', {NOTION_H}, sendBody: true, contentType: 'json', specifyBody: 'json', jsonBody: expr({J(body_expr)}), options: {{ timeout: 30000 }} }}, {CRED_N} }}, output: [{{ object: 'list', results: [] }}] }})"""
def write(name, pos, method='POST', url="'https://api.notion.com/v1/pages'", body="expr('{{ JSON.stringify($json.notionBody) }}')", extra=""):
    b = f", sendBody: true, contentType: 'json', specifyBody: 'json', jsonBody: {body}" if body else ""
    return f"""node({{ type: 'n8n-nodes-base.httpRequest', version: 4.2, config: {{ name: {J(name)}, position: {pos}, retryOnFail: true, maxTries: 3, waitBetweenTries: 2000, onError: 'continueRegularOutput', parameters: {{ method: '{method}', url: {url}, authentication: 'predefinedCredentialType', nodeCredentialType: 'notionApi', {NOTION_H}{b}, options: {{ timeout: 30000 }} }}, {CRED_N} }}, output: [{{ object: 'page', id: 'x' }}] }})"""
def cnode(name, pos, glue, once=True):
    return f"node({{ type: 'n8n-nodes-base.code', version: 2, config: {{ name: {J(name)}, position: {pos}, {'executeOnce: true, ' if once else ''}parameters: {{ jsCode: {code(glue)} }} }}, output: [{{ ok: true }}] }})"
TODAY = "$now.setZone('Europe/Rome').startOf('day').toISO()"
P, S, L, W = 'YOUR_PROFILO_ATLETA_DB_ID', 'YOUR_INTEGRAZIONE_DB_ID', 'YOUR_INTEGRAZIONE_LOG_DB_ID', 'YOUR_IDRATAZIONE_LOG_DB_ID'
pages = "const pages = n => { try { return $(n).all().flatMap(i => (i.json && i.json.results) || []); } catch (e) { return []; } };\n"
out = f"""import {{ workflow, node, trigger, switchCase, ifElse, sticky, newCredential, expr }} from '@n8n/workflow-sdk';

const hookX = trigger({{ type: 'n8n-nodes-base.webhook', version: 2.1, config: {{ name: 'Deck Extra Webhook', position: [0, 200], parameters: {{ httpMethod: 'GET', path: 'valhalla-deck-x', authentication: 'headerAuth', responseMode: 'responseNode', options: {{}} }}, credentials: {{ httpHeaderAuth: newCredential('X-Valhalla-Token') }} }}, output: [{{ headers: {{}}, query: {{}} }}] }});
const qProf = {q('Notion Profilo Atleta', [240, 200], P, "{{ JSON.stringify({ page_size: 1 }) }}")};
const qSupp = {q('Notion Integrazione Giornaliera', [480, 200], S, "{{ JSON.stringify({ page_size: 100 }) }}")};
const qSupLog = {q('Notion Integrazioni Oggi', [720, 200], L, "{{ JSON.stringify({ page_size: 100, filter: { property: 'Data', date: { on_or_after: " + TODAY + " } } }) }}")};
const qWater = {q('Notion Acqua Oggi', [960, 200], W, "{{ JSON.stringify({ page_size: 100, filter: { property: 'Data', date: { on_or_after: " + TODAY + " } } }) }}")};
const compose = {cnode('Componi Extra', [1200, 200], pages + "return [{ json: toExtra(pages('Notion Profilo Atleta'), pages('Notion Integrazione Giornaliera'), pages('Notion Integrazioni Oggi'), pages('Notion Acqua Oggi')) }];")};
const respX = node({{ type: 'n8n-nodes-base.respondToWebhook', version: 1.5, config: {{ name: 'Rispondi Extra', position: [1440, 200], parameters: {{ respondWith: 'json', responseBody: expr('{{{{ JSON.stringify($json) }}}}'), options: {{ responseHeaders: {{ entries: [{{ name: 'Cache-Control', value: 'no-store' }}] }} }} }} }}, output: [{{}}] }});

const logX = trigger({{ type: 'n8n-nodes-base.webhook', version: 2.1, config: {{ name: 'Log Extra Webhook', position: [0, 640], parameters: {{ httpMethod: 'POST', path: 'valhalla-log-x', authentication: 'headerAuth', responseMode: 'responseNode', options: {{}} }}, credentials: {{ httpHeaderAuth: newCredential('X-Valhalla-Token') }} }}, output: [{{ body: {{ t: 'water', ml: 500 }} }}] }});
const valida = {cnode('Valida Extra', [240, 640], "return [{ json: validate($json.body, romeIso(new Date())) }];", once=False)};
const tipo = switchCase({{ version: 3.2, config: {{ name: 'Tipo Extra', position: [480, 640], parameters: {{ mode: 'expression', numberOutputs: 4, output: expr("{{{{ $json.ok ? ({{ water: 0, supp: 1, prs: 2 }}[$json.t] ?? 3) : 3 }}}}") }} }} }});
const scriviAcqua = {write('Scrivi Acqua', [720, 440])};
const qWater2 = {q('Acqua Oggi', [960, 440], W, "{{ JSON.stringify({ page_size: 100, filter: { property: 'Data', date: { on_or_after: " + TODAY + " } } }) }}")};
const esitoAcqua = {cnode('Esito Acqua', [1200, 440], "const p = $input.all().flatMap(i => (i.json && i.json.results) || []);\nreturn [{ json: waterResult($('Scrivi Acqua').first().json, p) }];")};
const leggiInt = {write('Leggi Integratore', [720, 640], method='GET', url="expr('https://api.notion.com/v1/pages/{{ $json.id }}')", body=None)};
const prepInt = {cnode('Prepara Integrazione', [960, 640], "return [{ json: prepareSupp($('Valida Extra').first().json, $json, romeIso(new Date())) }];")};
const intOk = ifElse({{ version: 2.2, config: {{ name: 'Integratore valido?', position: [1200, 640], parameters: {{ conditions: {{ options: {{ caseSensitive: true, leftValue: '', typeValidation: 'loose' }}, conditions: [{{ id: 'i1', leftValue: expr('{{{{ $json.ok }}}}'), rightValue: '', operator: {{ type: 'boolean', operation: 'true', singleValue: true }} }}], combinator: 'and' }}, looseTypeValidation: true, options: {{}} }} }} }});
const scriviInt = {write('Scrivi Integrazione', [1440, 600])};
const esitoInt = {cnode('Esito Integrazione', [1680, 600], "const p = $('Prepara Integrazione').first().json;\nreturn [{ json: $json.object === 'page' ? { ok: true, t: 'supp', id: p.id, name: p.name } : { ok: false, err: 'Notion non ha salvato' } }];")};
const scriviPR = {write('Scrivi PR', [720, 840])};
const esitoPR = {cnode('Esito PR', [960, 840], "const v = $('Valida Extra').first().json;\nreturn [{ json: $json.object === 'page' ? { ok: true, t: 'prs', lift: v.lift, kg: v.kg, reps: v.reps } : { ok: false, err: 'Notion non ha salvato il PR' } }];")};
const respLogX = node({{ type: 'n8n-nodes-base.respondToWebhook', version: 1.5, config: {{ name: 'Rispondi Log Extra', position: [1920, 640], parameters: {{ respondWith: 'json', responseBody: expr('{{{{ JSON.stringify($json) }}}}'), options: {{}} }} }}, output: [{{}}] }});

const note = sticky('## Valhalla Deck Extra (v3)\\nGET `/webhook/valhalla-deck-x` = atleta, acqua oggi, integrazioni del giorno.\\nPOST `/webhook/valhalla-log-x` {{t:water,ml}} | {{t:supp,id}} | {{t:prs,lift,kg,reps}}.\\nHeader `X-Valhalla-Token` su entrambi.', [hookX, logX], {{ color: 4 }});

export default workflow('valhalla-deck-extra', 'Valhalla Deck Extra')
  .add(hookX).to(qProf).to(qSupp).to(qSupLog).to(qWater).to(compose).to(respX)
  .add(logX).to(valida).to(tipo
    .onCase(0, scriviAcqua.to(qWater2.to(esitoAcqua.to(respLogX))))
    .onCase(1, leggiInt.to(prepInt.to(intOk.onTrue(scriviInt.to(esitoInt.to(respLogX))).onFalse(respLogX))))
    .onCase(2, scriviPR.to(esitoPR.to(respLogX)))
    .onCase(3, respLogX))
  .add(note);
"""
open('deck_extra.js','w').write(out)
print(len(out))
