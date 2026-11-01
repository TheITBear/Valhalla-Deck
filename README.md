<p align="center">
  <img src="docs/hero.png" alt="Valhalla Deck" width="100%">
</p>

<p align="center">
  <img alt="ESP32-S3" src="https://img.shields.io/badge/ESP32--S3-touch%202.8%22-E7352C?style=for-the-badge&logo=espressif&logoColor=white">
  <img alt="n8n" src="https://img.shields.io/badge/n8n-workflow-EA4B71?style=for-the-badge&logo=n8n&logoColor=white">
  <img alt="Notion" src="https://img.shields.io/badge/Notion-database-000000?style=for-the-badge&logo=notion&logoColor=white">
</p>

<p align="center">
  Un display touch a batteria che porta in tasca il tuo sistema di <b>allenamento e nutrizione</b> su Notion:<br>
  registri serie, pasti, acqua e integratori con un tocco, vedi progressi e PR, prepari la settimana dopo con un pulsante.
</p>

<p align="center">
  <a href="esp32/">Firmware e installazione</a> ·
  <a href="CUSTOMIZE.md">Personalizzazione</a>
</p>

---

## ✨ Cosa fa

| | |
|---|---|
| 🏋️ **Training** | piano del giorno, registrazione serie kg × reps, suggerimento di progressione, festa a ogni PR |
| 🍽️ **Nutrizione** | macro del giorno, pasti preferiti con doppio tocco, streak proteine, finestra di digiuno |
| 💧 **Acqua e integratori** | +200 / +500 / +1 L, checklist integratori per momento della giornata |
| 📅 **Settimana** | recap allenamenti, proteine, acqua, peso e PR; genera il Load Tracker della settimana dopo |
| 🔔 **Notifiche** | pannello a scorrimento dall'alto, suoni dedicati, coach proteine |
| 📶 **Sempre utilizzabile** | coda offline, riconnessione automatica, gestione Wi-Fi e aggiornamento firmware dal dispositivo |

## 🧩 Architettura

```mermaid
flowchart LR
  N[(Notion<br/>Load Tracker · FoodTracker<br/>PR · Peso · Acqua · Integratori)]
  W[n8n<br/>workflow Valhalla]
  D[Valhalla Deck<br/>ESP32-S3]
  N <-->|API Notion| W
  W <-->|HTTPS + X-Valhalla-Token| D
```

**Hardware:** Waveshare ESP32-S3-Touch-LCD-2.8 (batteria, touch, audio, IMU, RTC), collegato via Wi-Fi e HTTPS al tuo n8n.

## 📸 Tutte le schermate

<p align="center">
  <img src="esp32/preview/anteprima_esp32.png" alt="Schermate del Valhalla Deck" width="100%">
</p>

<sub>Schermate generate con dati dimostrativi.</sub>

## 🚀 Per iniziare

1. Crea i database Notion e importa i workflow n8n → [CUSTOMIZE.md](CUSTOMIZE.md)
2. Copia `esp32/valhalla_display/secrets.example.h` in `secrets.h` e inserisci i tuoi valori
3. Compila e carica il firmware → [esp32/README.md](esp32/README.md)

## 🔐 Sicurezza

- Nessun segreto nel repository: token e password Wi-Fi si inseriscono sul dispositivo (NVS) o in file locali esclusi da Git (`secrets.h`).
- Tutti i webhook n8n richiedono l'header `X-Valhalla-Token`; HTTPS verificato con certificati radice inclusi nel firmware.

## 📄 Licenze di terze parti

- Font **Antonio**: SIL Open Font License 1.1 (`esp32/valhalla_display/LICENSE-font-Antonio.txt`)
- Driver LCD/touch derivati dagli esempi Waveshare

<p align="center"><sub>Made by <a href="https://github.com/TheITBear">The IT Bear</a></sub></p>
