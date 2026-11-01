# Genera "Valhalla Load Week" (JSON da importare in n8n: Workflow > Import from File)
import json
src = open('week_nodes.js').read().split("if (typeof module")[0]
code = src + """
const pages = $('Leggi Load Tracker').all().flatMap(i => (i.json && i.json.results) || []);
return [{ json: planWeek(pages) }];
"""
NOTION_H = {"sendHeaders": True, "headerParameters": {"parameters": [{"name": "Notion-Version", "value": "2022-06-28"}]}}
NOTION_AUTH = {"authentication": "predefinedCredentialType", "nodeCredentialType": "notionApi"}
def n(name, typ, ver, pos, params, **extra):
    return {"name": name, "type": typ, "typeVersion": ver, "position": pos, "parameters": params, **extra}
summary = "={{ JSON.stringify({ ok: $json.ok, week: $json.week, from: $json.from, n: $json.n, miss: $json.miss, d: $json.d, err: $json.err }) }}"
nodes = [
    n("Load Week Webhook", "n8n-nodes-base.webhook", 2.1, [0, 300],
      {"httpMethod": "POST", "path": "valhalla-week", "authentication": "headerAuth", "responseMode": "responseNode", "options": {}},
      webhookId="valhalla-week"),
    n("Leggi Load Tracker", "n8n-nodes-base.httpRequest", 4.2, [220, 300],
      {"method": "POST", "url": "https://api.notion.com/v1/databases/YOUR_LOAD_TRACKER_DB_ID/query", **NOTION_AUTH, **NOTION_H,
       "sendBody": True, "contentType": "json", "specifyBody": "json",
       "jsonBody": "={{ JSON.stringify({ page_size: 100, sorts: [{ property: 'Data', direction: 'descending' }] }) }}",
       "options": {"timeout": 30000, "pagination": {"pagination": {"paginationMode": "updateAParameterInEachRequest",
           "parameters": {"parameters": [{"type": "body", "name": "start_cursor", "value": "={{ $response.body.next_cursor }}"}]},
           "paginationCompleteWhen": "other", "completeExpression": "={{ $response.body.has_more !== true }}",
           "limitPagesFetched": True, "maxRequests": 2, "requestInterval": 350}}}},
      executeOnce=True, retryOnFail=True, maxTries=3, waitBetweenTries=3000),
    n("Prepara Settimana", "n8n-nodes-base.code", 2, [440, 300], {"jsCode": code}),
    n("OK?", "n8n-nodes-base.if", 2.2, [660, 300],
      {"conditions": {"options": {"caseSensitive": True, "leftValue": "", "typeValidation": "loose", "version": 2},
                      "conditions": [{"id": "ok", "leftValue": "={{ $json.ok }}", "rightValue": True,
                                      "operator": {"type": "boolean", "operation": "true", "singleValue": True}}], "combinator": "and"},
       "looseTypeValidation": True, "options": {}}),
    n("Rispondi OK", "n8n-nodes-base.respondToWebhook", 1.5, [880, 200],
      {"respondWith": "json", "responseBody": summary, "options": {}}),
    n("Rispondi Errore", "n8n-nodes-base.respondToWebhook", 1.5, [880, 420],
      {"respondWith": "json", "responseBody": summary, "options": {}}),
    # si risponde subito al Deck (timeout 20 s), le ~30 pagine si creano dopo, ~3 al secondo (limite Notion)
    n("Una per Pagina", "n8n-nodes-base.code", 2, [1100, 200],
      {"jsCode": "return $input.first().json.bodies.map(b => ({ json: b }));"}),
    n("Crea Riga Notion", "n8n-nodes-base.httpRequest", 4.2, [1320, 200],
      {"method": "POST", "url": "https://api.notion.com/v1/pages", **NOTION_AUTH, **NOTION_H,
       "sendBody": True, "contentType": "json", "specifyBody": "json", "jsonBody": "={{ JSON.stringify($json) }}",
       "options": {"timeout": 30000, "batching": {"batch": {"batchSize": 1, "batchInterval": 400}}}},
      retryOnFail=True, maxTries=3, waitBetweenTries=3000),
]
conn = lambda *targets: {"main": [[{"node": t, "type": "main", "index": 0} for t in ts] for ts in targets]}
wf = {"name": "Valhalla Load Week", "nodes": nodes, "settings": {"executionOrder": "v1"}, "connections": {
    "Load Week Webhook": conn(["Leggi Load Tracker"]),
    "Leggi Load Tracker": conn(["Prepara Settimana"]),
    "Prepara Settimana": conn(["OK?"]),
    "OK?": conn(["Rispondi OK"], ["Rispondi Errore"]),
    "Rispondi OK": conn(["Una per Pagina"]),
    "Una per Pagina": conn(["Crea Riga Notion"]),
}}
json.dump(wf, open('valhalla_load_week.json', 'w'), indent=1, ensure_ascii=False)
print('ok', len(nodes), 'nodi')
