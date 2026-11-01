# Personalizzare il progetto

Guida per adattare Valhalla Deck ai tuoi database, al tuo n8n e al tuo stile, **senza mai committare dati personali o segreti**.

## 0. Flusso Git consigliato
```bash
# 1) fork su GitHub (o "Use this template"), poi:
git clone https://github.com/<tuo-utente>/Valhalla-Deck.git
cd Valhalla-Deck
git remote add upstream https://github.com/TheITBear/Valhalla-Deck.git   # per ricevere gli aggiornamenti

# 2) lavora su un branch
git switch -c mio-setup

# 3) aggiornamenti dall'originale
git fetch upstream && git merge upstream/main
```
Regola d'oro: **i valori reali vivono solo in file ignorati** (`secrets.h`). Prima di ogni push:
```bash
git status --ignored          # secrets.h deve comparire tra gli "Ignored"
gitleaks detect -v            # oppure: pip install detect-secrets && detect-secrets scan
```
Su GitHub attiva *Settings → Code security → Secret scanning* e *Push protection*.
Se un segreto finisce in un commit pubblicato: **ruotalo subito** (riscrivere la storia non basta, potrebbe essere già stato copiato).

## 1. Segreti del firmware
| File | Su Git | Contenuto |
|---|---|---|
| `esp32/valhalla_display/secrets.example.h` | sì | modello con valori finti |
| `esp32/valhalla_display/secrets.h` | **no** | `SECRET_DEFAULT_URL` (webhook n8n), `SECRET_AP_PASS` (rete `Valhalla-Setup`) |
| token `X-Valhalla-Token`, reti Wi-Fi, PIN | **no** | inseriti dal portale / dal Deck, salvati nella NVS della scheda |

```bash
cp esp32/valhalla_display/secrets.example.h esp32/valhalla_display/secrets.h   # poi modificalo
```

## 2. Notion: i tuoi database
Il codice n8n usa segnaposto `YOUR_*`. Duplica/crea questi database (i nomi delle proprietà devono coincidere):

| Segnaposto | Database | Proprietà usate |
|---|---|---|
| `YOUR_LOAD_TRACKER_DB_ID` | Load Tracker | Esercizio (title), Data (date), Giorno (select), Settimana, Carico Presunto, Carico Usato, Rep Min, Rep Max, Rep Fatte (number) |
| `YOUR_FOOD_TRACKER_DB_ID` | FoodTracker | Meal 🍴 (title), Date (date), Calories, Proteins (g), Carbs (g), Fats (g) (number), Tag (select) |
| `YOUR_PR_TRACKER_DB_ID` | PR Tracker | Lift (title), Weight, Reps, Set (number), Workout Date (date) |
| `YOUR_PESO_DB_ID` | Peso Corporeo | Date (date), Peso (number) |
| `YOUR_IDRATAZIONE_LOG_DB_ID` | Idratazione Log | Voce (title), ml (number), Data (date), Fonte (select) |
| `YOUR_INTEGRAZIONE_DB_ID` | Integrazione Giornaliera | Composto (title), Dose (text), Momento (select: Mattina/Pranzo/Sera…), Digiuno (h) (number) |
| `YOUR_INTEGRAZIONE_LOG_DB_ID` | Integrazione Log | Composto (title), Dose (text), Data (date), Integratore (relation), Momento (select) |
| `YOUR_PROFILO_ATLETA_DB_ID` | Profilo Atleta | Nome (title), Fase (select), Blocco/Obiettivo (text), Altezza (cm), BF stimato (%), Massa magra (kg), Peso obiettivo (kg), Proteine target (g), Acqua obiettivo (ml) |
| `YOUR_FIRMWARE_PAGE_ID` | pagina "Firmware Deck" | blocchi file `.bin` (aggiornamento OTA) |

L'ID è la parte esadecimale di 32 caratteri nell'URL del database. Sostituzione in un colpo solo **sul tuo branch privato**, non su un fork pubblico:
```bash
# Linux / macOS / Git Bash
grep -rl "YOUR_LOAD_TRACKER_DB_ID" esp32/n8n | xargs sed -i "s/YOUR_LOAD_TRACKER_DB_ID/<id-del-tuo-db>/g"
```
```powershell
# Windows PowerShell
Get-ChildItem esp32\n8n -Recurse -File | ForEach-Object {
  (Get-Content $_.FullName -Raw) -replace 'YOUR_LOAD_TRACKER_DB_ID','<id-del-tuo-db>' | Set-Content $_.FullName -NoNewline }
```
Condividi ogni database con la tua integrazione Notion (menu ••• → *Connections*).

## 3. n8n
- `esp32/n8n/deck/*_nodes.js`, `week_nodes.js`, `deck_transform.js`: logica dei Code node, testabile con `node test_*.js`.
- `deck_api.js`, `deck_extra.js`, `deck_plus.js`: definizioni dei workflow con l'**n8n Workflow SDK** (si creano con il server MCP di n8n o copiando i Code node a mano).
- `valhalla_load_week.json`: importabile direttamente (*Workflows → Import from File*).
- `gen_*.py`: rigenerano i workflow dopo aver modificato la logica (`python3 gen_plus.py`).

Dopo ogni import: credenziale **Header Auth** (`X-Valhalla-Token`) sui webhook, credenziale **Notion** sui nodi HTTP, poi *Publish*.
Il feed principale (`/valhalla-deck`) parte dal workflow `Valhalla HA Feed` (`/valhalla-ha`): cambia l'URL in `deck_api.js` con il tuo.

## 4. Allenamento e obiettivi
| Cosa | Dove |
|---|---|
| Nomi degli split (Lower A, Upper Push…) | `deck_transform.js` → `SPLITS`, `CODES` + select "Giorno" in Notion |
| Big lift dei PR | `deck_transform.js` → `BIG` |
| Soglie streak proteine/kcal | `deck_transform.js` e `plus_nodes.js` → `STREAK_*` |
| Obiettivo acqua | Profilo Atleta → *Acqua obiettivo (ml)* (fallback `H2O_GOAL`) |
| Cambio programma (es. nuovo blocco) | crea a mano la prima settimana nel Load Tracker: il tasto *PREPARA PROSSIMA SETTIMANA* copia sempre l'ultima |

## 5. Aspetto del Deck
| Cosa | Dove |
|---|---|
| Colori | `ui.h` → `pal()` (struttura `Pal`: sfondo, accento, oro, ghiaccio…) |
| Font e logo | `tools/make_assets.py` → genera `assets.h` |
| Testi delle pagine | `ui.h`, funzioni `page*()` |
| Suoni | `audio.h` |
| Fuso orario, refresh, standby | `valhalla_display.ino` → `TZ_ROME`, `REFRESH_MS`, `STANDBY_MS`, `SCREEN_OFF_MS` |

Font: nel repository c'è **Antonio** (SIL OFL 1.1, licenza in `esp32/valhalla_display/LICENSE-font-Antonio.txt`). Per usarne un altro:
```bash
pip install pillow
python3 -I tools/make_assets.py MioFont.ttf - esp32/valhalla_display/assets.h        # tiene il logo attuale
python3 -I tools/make_assets.py MioFont.ttf mio_logo.png esp32/valhalla_display/assets.h  # logo arancio su bianco
```
Scegli un font **stretto** (condensed): le pagine sono pensate per larghezze simili ad Antonio.
Se usi un font senza licenza di ridistribuzione, **non committare** l'`assets.h` generato:
```bash
git update-index --skip-worktree esp32/valhalla_display/assets.h   # Git ignora le tue modifiche locali
```

## 6. Prima di rendere pubblico un fork
- [ ] `secrets.h`, `build/`, `*.bin` assenti da `git ls-files`
- [ ] nessun ID Notion reale (`git grep -nE "[0-9a-f]{32}"`)
- [ ] nessun dominio personale (`git grep -n "https://"`)
- [ ] immagini con dati finti e senza EXIF (`exiftool -all= *.jpg`)
- [ ] scansione segreti pulita (`gitleaks detect`)
