# EVCC-Garagendisplay (ESP32 CYD)

Kleines Touch-Display neben dem Ladepunkt: zeigt die aktuelle Ladeleistung aus
[EVCC](https://evcc.io) und schaltet zwischen **PV**, **Smart** und **Schnell** um.
Hardware: ESP32-2432S028R („Cheap Yellow Display", 2,8", 320×240), Strom per USB.

## Anzeige

| Bereich | Inhalt |
| --- | --- |
| Kopfzeile | Name des Ladepunkts, Uhrzeit, Warnsymbol bei WLAN-/EVCC-Problemen |
| Mitte | Ladeleistung in kW – oder „Kein Fahrzeug", wenn nichts eingesteckt ist |
| Statuszeile | z. B. „lädt · 3-phasig", „Wartet auf Sonnenüberschuss", „Genug Sonne – Start in 0:45" |
| Balken | Sonnenanteil der Ladeleistung, dazu geladene Energie und Dauer der Sitzung |
| Unten | Modusumschalter PV · Smart · Schnell |

**Tag/Nacht:** Das Display berechnet den Sonnenstand für den eingestellten Ort und wechselt
automatisch zwischen hellem und dunklem Design (Farben wie in der EVCC-Weboberfläche). Nachts
wird die Helligkeit reduziert.

**Bildschirm aus:** Wird nicht geladen und 2 Minuten nichts bedient, geht das Display aus.
Antippen weckt es auf – die erste Berührung löst dabei nichts aus. Beim Einstecken des Autos
und beim Start/Ende des Ladens geht es von selbst an.

## Modi

EVCC ab Version 0.316 kennt nur noch `off`, `smart` und `now`. Das Display bildet die drei
Tasten so ab:

| Taste | EVCC | Verhalten |
| --- | --- | --- |
| PV | `smart` + „Immer laden" aus | lädt nur mit Sonnenüberschuss |
| Smart | `smart` + „Immer laden" an | lädt immer mit mindestens Mindestleistung, mehr bei Sonne; berücksichtigt günstige Tarife und Ladepläne |
| Schnell | `now` | volle Leistung |

„Aus" gibt es am Display bewusst nicht. Steht der Ladepunkt in EVCC auf Aus, zeigt das
Display „Ladepunkt ist aus – Modus wählen".

Ältere EVCC-Versionen werden automatisch erkannt (dann `pv` / `minpv`).

## Technik

- PlatformIO / Arduino, [LVGL 9](https://lvgl.io) für die Oberfläche, LovyanGFX als Display-/Touch-Treiber
- Schriften: Inter (UI-Texte), Montserrat (große Leistungszahl), Tabler-Icons – erzeugt mit `tools/gen_fonts.sh`
- Daten: alle 3 s `GET /api/state?jq=…` – der jq-Filter liefert nur die benötigten Felder (~300 Byte statt ~70 KB)
- Umschalten: `POST /api/loadpoints/<n>/alwayscharge/{on|off}` und `POST /api/loadpoints/<n>/mode/{smart|now}`
- Netzwerk läuft in einem eigenen Task auf Kern 0, die Oberfläche bleibt dadurch flüssig
- Eine Taste zeigt den neuen Modus erst voll an, wenn EVCC ihn bestätigt hat (vorher halbtransparent)

## Flashen

```bash
pio run -e cyd -t upload
```

Hat das CYD **zwei** USB-Anschlüsse (Micro-USB + USB-C), ist meist ein ST7789-Display verbaut:
`pio run -e cyd2usb -t upload`. Erkennbar daran, dass die Farben bei `cyd` falsch aussehen.

Spätere Updates übers WLAN:

```bash
pio run -e cyd_ota -t upload
```

## Erster Start

1. **Touch kalibrieren:** Pfeilspitzen in den vier Ecken nacheinander antippen (am besten mit einem Stift).
2. **WLAN einrichten:** Mit dem Handy ins WLAN `EVCC-Garage` wechseln – die Einrichtungsseite öffnet sich
   (sonst `192.168.4.1`). WLAN auswählen, Passwort eingeben. Unter den WLAN-Feldern lassen sich
   EVCC-Adresse (Standard `192.168.178.209:7070`), Ladepunkt-Nr. (Standard `1` = Garage), Standort
   für Tag/Nacht und Drehung des Displays einstellen.

## Später ändern

- **Einstellungen:** Den Namen des Ladepunkts oben links **3 Sekunden gedrückt halten** → das Portal
  `EVCC-Garage` startet erneut. Antippen des Hinweises bricht ab.
- **Touch neu kalibrieren:** Beim Einstecken des USB-Kabels den Bildschirm berühren.
- Nach einem Stromausfall wartet das Display auf den Router und startet nach 5 Minuten ohne WLAN
  selbst neu.

## Anpassen

Helligkeit, Zeiten und Standardwerte stehen in [src/config.h](src/config.h). Für ein OTA-Passwort
in `platformio.ini` unter `build_flags` `-DOTA_PASSWORD=\"…\"` ergänzen.
