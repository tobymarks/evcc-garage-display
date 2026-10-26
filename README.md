# evcc-Garagendisplay (ESP32-4848S040C)

Touch-Display neben dem Ladepunkt: zeigt die aktuelle Ladeleistung aus [evcc](https://evcc.io)
und schaltet zwischen **Smart**, **Langsam** und **Schnell** um.
Hardware: Guition ESP32-4848S040C – ESP32-S3, 4" IPS 480×480 (ST7701S), kapazitiver Touch (GT911),
Strom per USB-C. Bezugsquelle, die ich genutzt habe: [AliExpress](https://de.aliexpress.com/item/1005012911942193.html).

Der frühere Stand für das 2,8"-„Cheap Yellow Display" liegt im Git-Tag `cyd-final`.

## Anzeige

| Bereich | Inhalt |
| --- | --- |
| Kopfzeile | Name des Ladepunkts, Uhrzeit, Warnsymbol bei WLAN-/evcc-Problemen |
| Mitte | Ladeleistung in kW mit grünem Blitz beim Laden. Ohne Fahrzeug der aktuelle Sonnenüberschuss (PV minus Hausverbrauch) mit Kacheln für Sonne, Haus und Akku – und ob der Überschuss für Smart reicht. Ohne PV-Anlage nur „Kein Fahrzeug“ |
| Statuszeile | z. B. „Lädt · 3-phasig", „Wartet auf Sonnenüberschuss", „Genug Sonne – Start in 0:45" |
| Balken | Sonnen- und Netzanteil der aktuellen Ladeleistung |
| Kacheln | Geladene Energie, Dauer und durchschnittlicher Sonnenanteil der Sitzung |
| Unten | Modustasten Smart · Langsam · Schnell, jeweils mit kurzer Beschreibung |

**Mehrere Ladepunkte:** In den Einstellungen mehrere Nummern mit Komma eintragen (z. B. `1,2,3`, höchstens 6).
Dann wechselt man durch Wischen nach links und rechts, Punkte unter den Modustasten zeigen, welcher Ladepunkt
gerade zu sehen ist. Wird an einem anderen Ladepunkt ein Auto angesteckt oder beginnt/endet dort das Laden,
springt das Display dorthin – außer jemand hat es in der letzten Minute bedient. Alle Ladepunkte kommen mit
einer einzigen Abfrage.

**Design:** angelehnt an die evcc-Weboberfläche (ab 0.317): gleiche Farben, Schrift (Montserrat) und Icons –
hell, dunkel oder automatisch nach Sonnenstand. Das Display
läuft immer mit voller Helligkeit – in der Garage ist Licht, wenn jemand davorsteht.

**Bildschirm aus:** Wird nicht geladen und nichts bedient, geht das Display nach 2 Minuten aus
(einstellbar, 0 = immer an). Antippen weckt es, ohne eine Taste auszulösen. Beim Einstecken des Autos
und beim Start/Ende des Ladens geht es von selbst an.

## Modi

evcc ab Version 0.316 kennt nur noch `off`, `smart` und `now`. Das Display bildet die drei Tasten so ab:

| Taste | evcc | Verhalten |
| --- | --- | --- |
| Smart | `smart` + „Immer laden" aus | lädt nur mit Sonnenüberschuss (bzw. günstigem Tarif oder Ladeplan) – z. B. wenn ein Auto länger steht |
| Langsam | `smart` + „Immer laden" an (∞) | lädt sofort und ohne Pause mit Mindestleistung, mehr bei Sonne |
| Schnell | `now` | volle Leistung |

„Aus" gibt es am Display bewusst nicht. Steht der Ladepunkt in evcc auf Aus, zeigt das Display
„Ladepunkt ist aus – Modus wählen". Ältere evcc-Versionen werden erkannt (dann `pv` / `minpv`).

## Technik

- PlatformIO mit [pioarduino](https://github.com/pioarduino/platform-espressif32) (Arduino 3.x auf ESP-IDF 5.5)
- Bild über den ESP-IDF-Treiber `esp_lcd` (RGB-Schnittstelle), Oberfläche mit [LVGL 9](https://lvgl.io)
- **Stabil bei WLAN-Last** (getestet mit WLAN-Suchen und OTA-Updates):
  - Bounce-Buffer im internen RAM – die Bildausgabe liest nicht direkt aus dem PSRAM
  - `CONFIG_SPIRAM_XIP_FROM_PSRAM` und `CONFIG_LCD_RGB_ISR_IRAM_SAFE` – die Ausgabe läuft auch bei Flash-Zugriffen weiter
  - `CONFIG_LCD_RGB_RESTART_IN_VSYNC` – falls doch etwas verrutscht, fängt sich das Bild im nächsten Frame
  - Diese Einstellungen stehen unter `custom_sdkconfig` in `platformio.ini`; das Framework wird deshalb beim ersten Build neu kompiliert (einige Minuten)
- Schriften: Montserrat (SIL OFL) und die Icons der evcc-Oberfläche – [Shopicons](https://github.com/H2D2-Design/h2d2-shopicons)
  und Material Symbols (beide Apache-2.0), SVGs in `tools/icons`. Erzeugt mit `tools/gen_fonts.sh` (braucht fontforge)
- Daten: alle 3 s `GET /api/state?jq=…` – der jq-Filter liefert nur die benötigten Felder (~400 Byte statt ~70 KB),
  darunter `pvPower`, `homePower` und den Akku-Ladestand für die Anzeige ohne Fahrzeug
- Umschalten: `POST /api/loadpoints/<n>/alwayscharge/{on|off}` und `POST /api/loadpoints/<n>/mode/{smart|now}`
- Netzwerk läuft in einem eigenen Task auf Kern 0, die Oberfläche bleibt dadurch flüssig
- Eine Taste zeigt den neuen Modus erst voll an, wenn evcc ihn bestätigt hat (vorher halbtransparent)

## Flashen

Am einfachsten im Browser (Chrome/Edge): **[tobymarks.github.io/evcc-garage-display](https://tobymarks.github.io/evcc-garage-display/)** –
die Seite wird bei jedem Push auf `main` von GitHub Actions mit frisch gebauter Firmware neu erstellt (`site/`, `.github/workflows/pages.yml`).

Mit PlatformIO, erstes Mal per USB-C:

```bash
pio run -e panel -t upload
```

Danach übers WLAN:

```bash
pio run -e panel_ota -t upload
```

### Automatische Updates

Die Displays halten sich selbst aktuell: Alle 6 Stunden (erstmals 2 Minuten nach dem Start) lesen sie
`firmware/update.json` von der Projektseite. Gibt es eine andere Version, laden sie die Firmware über HTTPS
(Zertifikat geprüft, MD5-Prüfsumme) – aber erst, wenn nicht geladen wird und 10 Minuten niemand das Display bedient hat.
Startet die neue Firmware nicht sauber, kehrt der Bootloader beim nächsten Neustart zur alten zurück; sie gilt erst
nach einer Minute Laufzeit als gut.

- Abschalten in den Einstellungen: „Automatische Updates“ auf `0`
- Version: [scripts/version.py](scripts/version.py) – Commit-Datum und Kurz-Hash, lokal und in GitHub Actions gleich.
  Ein lokal gebauter, sauberer Stand von `main` wird deshalb nicht erneut installiert; Builds mit ungespeicherten
  Änderungen (`-dirty`) ersetzt das Display beim nächsten Update durch den veröffentlichten Stand

## Erster Start

Mit dem Handy ins WLAN `evcc-Garage` wechseln – die Einrichtungsseite öffnet sich (sonst `192.168.4.1`).
WLAN auswählen und Passwort eingeben. Darunter lassen sich einstellen:

| Feld | Standard |
| --- | --- |
| evcc-Adresse / Port | `192.168.178.209` / `7070` |
| Ladepunkt-Nr. | `1` (Garage); mehrere mit Komma, z. B. `1,2,3` – dann wischen zum Wechseln |
| Breiten-/Längengrad | Mitte Deutschlands – für die automatische Tag/Nacht-Umschaltung |
| Drehung | `0` (je 90° im Uhrzeigersinn: 1, 2, 3) |
| Design | `0` automatisch, `1` hell, `2` dunkel |
| Bildschirm aus nach Minuten | `2` (`0` = immer an) |
| Automatische Updates | `1` an (`0` = aus) |

## Später ändern

- **Einstellungen:** „Garage" oben links **3 Sekunden gedrückt halten** → das Portal `evcc-Garage` startet
  erneut. Antippen des Hinweises bricht ab.
- Nach einem Stromausfall wartet das Display auf den Router und startet nach 5 Minuten ohne WLAN selbst neu.

## Diagnose

Im Heimnetz unter `http://evcc-garage.local:8080`:

| Pfad | Zweck |
| --- | --- |
| `/screenshot.bmp` | aktueller Bildschirminhalt |
| `/preview?s=charging\|solar\|fast\|nocar\|nocarlow\|night\|waiting\|start\|off\|stale&dark=0\|1` | zeigt 60 s lang einen Beispielzustand ([src/preview.cpp](src/preview.cpp)); `/preview` ohne Parameter beendet |
| `/update` | Firmware-Version und Stand der automatischen Updates; `?check=1` prüft sofort, `?install=1` installiert eine gefundene neue Version sofort |
| `/diag` | Laufzeit, Neustartgrund, freier Speicher, zurückgerollte Updates und der letzte gespeicherte Absturz |
| `/wifiscan` | löst eine WLAN-Suche aus (Lasttest für die Bildausgabe) |

Ohne Gerät: `./tools/sim/render.sh <Ordner> nocar charging …` rendert dieselben Beispielzustände mit der
echten Oberfläche (LVGL, Schriften) auf dem Rechner als PNG, hell und dunkel. Ohne Argumente erneuert es die
Screenshots der Website in `site/img`. Braucht clang und einmal `pio run`.

## Anpassen

Zeiten und Standardwerte stehen in [src/config.h](src/config.h), Pins und Display-Timings in
[src/board.h](src/board.h). Für ein OTA-Passwort in `platformio.ini` unter `build_flags`
`-DOTA_PASSWORD=\"…\"` ergänzen.

## Lizenz

MIT – siehe [LICENSE](LICENSE). Mitgelieferte Fremdinhalte:

- [Montserrat](https://github.com/JulietaUla/Montserrat) – SIL Open Font License 1.1 ([tools/fonts/OFL-Montserrat.txt](tools/fonts/OFL-Montserrat.txt))
- [Shopicons](https://github.com/H2D2-Design/h2d2-shopicons) und [Material Symbols](https://github.com/google/material-design-icons) – Apache-2.0 ([tools/icons/LICENSE-shopicons](tools/icons/LICENSE-shopicons))
- Die Icons entsprechen denen der [evcc](https://github.com/evcc-io/evcc)-Weboberfläche; dieses Projekt ist nicht offiziell mit evcc verbunden.
