# evcc-Garagendisplay (ESP32-4848S040C)

Touch-Display neben dem Ladepunkt: zeigt die aktuelle Ladeleistung aus [evcc](https://evcc.io)
und schaltet zwischen **Smart**, **Langsam** und **Schnell** um.
Hardware: Guition ESP32-4848S040C – ESP32-S3, 4" IPS 480×480 (ST7701S), kapazitiver Touch (GT911),
Strom per USB-C. Bezugsquelle, die ich genutzt habe: [AliExpress](https://de.aliexpress.com/item/1005012911942193.html).

Der frühere Stand für das 2,8"-„Cheap Yellow Display" liegt im Git-Tag `cyd-final`.

## Anzeige

| Bereich | Inhalt |
| --- | --- |
| Kopfzeile | Name des Ladepunkts, Uhrzeit, Ladestand des Hausakkus (sofern vorhanden), Warnsymbol bei WLAN-/evcc-Problemen |
| Mitte | Angestecktes Fahrzeug mit Ladestand (antippen wechselt durch die in evcc angelegten Fahrzeuge und „Gastfahrzeug"), darunter die Ladeleistung in kW mit grünem Blitz beim Laden. Ohne Fahrzeug der aktuelle Sonnenüberschuss (was gerade in den Akku und ins Netz geht) mit Kacheln für Haus, Akku und Solarprognose (Rest heute, abends für morgen) – und ob der Überschuss für Smart reicht. Ohne PV-Anlage nur „Kein Fahrzeug" |
| Statuszeile | z. B. „Lädt · 3-phasig", „Wartet auf Sonnenüberschuss", „Genug Sonne – Start in 0:45" |
| Balken | Energiefluss wie in evcc, die Werte stehen im Balken: Netzbezug (grau), Sonne (grün), Hausakku entlädt (dunkelgrün), Einspeisung (gelb). Beim Laden steht der Sonnenanteil der Ladung in der Statuszeile. Ohne Netzleistung in evcc nur Sonnen- und Netzanteil der Ladeleistung |
| Kacheln | Geladene Energie, Dauer und durchschnittlicher Sonnenanteil der Sitzung |
| Unten | Modustasten Smart · Langsam · Schnell, jeweils mit kurzer Beschreibung |

**Mehrere Ladepunkte:** In den Einstellungen mehrere Nummern mit Komma eintragen (z. B. `1,2,3`, höchstens 6).
Dann wechselt man durch Wischen nach links und rechts, Punkte unter den Modustasten zeigen, welcher Ladepunkt
gerade zu sehen ist. Wird an einem anderen Ladepunkt ein Auto angesteckt oder beginnt/endet dort das Laden,
springt das Display dorthin – außer jemand hat es in der letzten Minute bedient. Alle Ladepunkte kommen mit
einer einzigen Abfrage.

**Solarseite (Schalter unter `/settings`, ab Werk aus):** eine zusätzliche Seite hinter den Ladepunkten, erreichbar
durch Wischen. Sie zeigt die aktuelle Erzeugung, darunter den Tag je Stunde – grün, was erzeugt wurde, grau die
Prognose für die kommenden Stunden – und in Kacheln den Ertrag von heute, was heute noch kommt und die Prognose
für morgen. Kommt heute nichts mehr, steht statt des Rests die Prognose für den ganzen Tag zum Vergleich. Kennt
evcc mehrere PV-Anlagen, stehen statt der Stundenbalken die Leistungen je Anlage (höchstens drei Zeilen, weitere
zusammengefasst). Tasten gibt es dort keine; wird ein Auto angesteckt oder beginnt das Laden, springt das Display
wie sonst zum Ladepunkt. Tagesverlauf und Tagesertrag kommen aus dem Verlauf von evcc
(`/api/history/energy`, getestet mit 0.319); ältere Versionen zeigen nur Erzeugung und Prognose.

**Design:** angelehnt an die evcc-Weboberfläche (ab 0.317): gleiche Farben, Schrift (Montserrat) und Icons –
hell, dunkel oder automatisch nach Sonnenstand. Die Helligkeit ist fest (ab Werk 100 %, unter
`/settings` in 10-%-Schritten einstellbar) – es gibt keine Nachtdimmung, in der Garage ist Licht, wenn jemand
davorsteht.

**Wärmepumpe und Heizstab:** Bei Heizgeräten in evcc zeigt das Display statt Ladestand die Temperatur
(z. B. „43,3 °C") und „Zieltemperatur erreicht" statt „Ladelimit erreicht".

**Bildschirm aus:** Wird nicht geladen und nichts bedient, geht das Display nach 2 Minuten aus
(einstellbar, 0 = immer an). Antippen weckt es, ohne eine Taste auszulösen. Beim Einstecken des Autos
und beim Start/Ende des Ladens geht es von selbst an.

**Bildschirm von außen schalten:** etwa per Bewegungs- oder Präsenzmelder aus Home Assistant oder ioBroker.
`http://evcc-garage.local:8080/wake` wirkt wie eine Berührung: Der Bildschirm geht an und nach der
eingestellten Zeit wieder aus. `/sleep` schaltet ihn sofort aus, auch während einer Ladung und bei
„immer an" – bis zum nächsten Antippen, `/wake` oder bis ein Auto angesteckt wird bzw. das Laden beginnt
oder endet. Ist ein Passwort gesetzt, gilt es auch hier (Benutzer `admin`). Beispiel für Home Assistant:

```yaml
rest_command:
  garagendisplay_an:
    url: "http://evcc-garage.local:8080/wake"
  garagendisplay_aus:
    url: "http://evcc-garage.local:8080/sleep"
```

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
  und Material Symbols (beide Apache-2.0), SVGs in `tools/icons` (das Akku-Symbol ist selbst gezeichnet). Erzeugt mit `tools/gen_fonts.sh` (braucht fontforge)
- Daten: alle 3 s `GET /api/state?jq=…` – der jq-Filter liefert nur die benötigten Felder (unter 1 KB statt ~70 KB),
  darunter `pvPower`, `homePower`, Ladestand und Leistung des Hausakkus und die Netzleistung
- Solarseite: alle 5 min `GET /api/history/energy?from=…&to=…&aggregate=hour` für den laufenden Tag (Erzeugung und
  Prognose je Stunde) und die Prognose der kommenden Stunden aus `forecast.solar.timeseries`, von evcc per jq
  je Stunde zusammengefasst – nur, wenn die Seite eingeschaltet ist
- Umschalten: `POST /api/loadpoints/<n>/alwayscharge/{on|off}`, `POST /api/loadpoints/<n>/mode/{smart|now}` sowie `POST /api/loadpoints/<n>/vehicle/<name>` bzw. `DELETE /api/loadpoints/<n>/vehicle`; Ladelimit: `POST /api/loadpoints/<n>/limitsoc/<Prozent>` bzw. `POST /api/loadpoints/<n>/limitenergy/<kWh>`
- Netzwerk läuft in einem eigenen Task auf Kern 0, die Oberfläche bleibt dadurch flüssig
- Eine Taste zeigt den neuen Modus erst voll an, wenn evcc ihn bestätigt hat (vorher halbtransparent)

## Flashen

Am einfachsten im Browser (Chrome/Edge): **[tobymarks.github.io/evcc-garage-display](https://tobymarks.github.io/evcc-garage-display/)** –
die Seite wird bei jedem Push auf `main` von GitHub Actions mit frisch gebauter Firmware neu erstellt (`site/`, `.github/workflows/pages.yml`).
Beim ersten Mal „Erase device“ anhaken (löscht die Demo-Software ab Werk); am zuverlässigsten klappt es mit einem
Kabel von USB-A auf USB-C. Eine BOOT-Taste ist nicht nötig.

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
nach einer Minute Laufzeit als gut. Läuft nach der Installation weiter die alte Firmware, versucht das Display
dieselbe Version noch zweimal und danach nur noch einmal je 24 Stunden Laufzeit – `/update` zeigt das an,
`/update?check=1&install=1` installiert trotzdem sofort.

Bis zur Version vom 13.11.2026 konnte genau das passieren: Nach einem automatischen Update lief weiter die alte
Firmware, auf manchen Displays jedes Mal. Solche Displays kommen so auf den aktuellen Stand: antippen oder neu
starten und innerhalb von 10 Minuten `/update?check=1&install=1` aufrufen (oder einmal per USB neu aufspielen).

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

- **Einstellungen im Heimnetz:** im Browser `http://evcc-garage.local:8080` aufrufen (leitet auf `/settings`).
  Unter „Anzeige" lässt sich ein- und ausschalten, was das Display zeigt und was sich antippen lässt
  (Fahrzeug wechseln, Ladelimit, Battery Boost, Solarseite); die Seite kann dazu die aktuelle Anzeige des Displays laden.
  Diese Schalter sowie Design, Helligkeit und Bildschirm-Timeout wirken sofort. Nur Änderungen an
  evcc-Adresse, Port, Ladepunkten, Drehung oder Passwort starten das Display neu. Das WLAN bleibt unverändert.
- **Passwort (optional):** unter „System" setzen. Danach verlangen alle Seiten des Displays die Anmeldung als
  `admin`, und Firmware-Uploads im Heimnetz brauchen dasselbe Passwort (`--auth=…` bei `espota`,
  in PlatformIO `upload_flags = --auth=…`). Es wird unverschlüsselt im Heimnetz übertragen – also kein Passwort
  verwenden, das woanders gilt. Vergessen? Am Display das Portal öffnen (siehe nächster Punkt) und dort
  speichern: das löscht das Passwort.
- **Einstellungen am Display** (auch für ein anderes WLAN): „Garage" oben links **3 Sekunden gedrückt halten** →
  das Portal `evcc-Garage` startet erneut. Antippen des Hinweises bricht ab.
- Nach einem Stromausfall wartet das Display auf den Router und startet nach 5 Minuten ohne WLAN selbst neu.

## Diagnose

Im Heimnetz unter `http://evcc-garage.local:8080`:

| Pfad | Zweck |
| --- | --- |
| `/settings` | Einstellungen ändern (wie im Portal, ohne WLAN) |
| `/screenshot.bmp` | aktueller Bildschirminhalt |
| `/preview?s=charging\|solar\|fast\|nocar\|nocarlow\|nocarfull\|night\|waiting\|start\|off\|stale&dark=0\|1` | zeigt 60 s lang einen Beispielzustand ([src/preview.cpp](src/preview.cpp)), die Solarseite mit `pv`, `pvmulti`, `pvnight`, `pvsummer` oder mit den echten Daten `pvlive`; `&title=…` setzt dazu einen anderen Namen in die Kopfzeile; `/preview` ohne Parameter beendet |
| `/update` | Firmware-Version und Stand der automatischen Updates, auch Installationen, nach denen die neue Version nicht lief; `?check=1` prüft sofort, `?install=1` installiert eine gefundene neue Version sofort |
| `/diag` | Laufzeit, Neustartgrund, Zustand des Bildschirms, Flash-Baustein, freier Speicher und Stack des evcc-Tasks, Zustand beider Firmware-Plätze (und ob der Inhalt des anderen startfähig ist), die letzten acht Starts (Firmware, Platz, Grund), zurückgerollte und nicht gestartete Updates und der letzte gespeicherte Absturz |
| `/wake`, `/sleep` | Bildschirm an- bzw. ausschalten, siehe „Bildschirm von außen schalten" |
| `/backlight` | Taktung der Hintergrundbeleuchtung anzeigen; `?hz=1000` stellt sie bis zum nächsten Neustart um (200 bis 40000 Hz, ab Werk 20000). Zum Ausprobieren, falls das Display unterhalb von 90 % Helligkeit dunkel bleibt |
| `/wifiscan` | löst eine WLAN-Suche aus (Lasttest für die Bildausgabe) |

Ohne Gerät: `./tools/sim/render.sh <Ordner> nocar charging …` rendert dieselben Beispielzustände mit der
echten Oberfläche (LVGL, Schriften) auf dem Rechner als PNG, hell und dunkel. Ohne Argumente erneuert es die
Screenshots der Website in `site/img`. Braucht clang und einmal `pio run`.

## Neuigkeiten

Bestehende Displays zeigen nach einem Update dasselbe wie vorher; neue Funktionen sind Schalter unter
`/settings`.

- **14.11.2026**
  - Automatische Updates: Fehler behoben, durch den nach einem Update wieder die alte Firmware lief. Nach der
    Installation begann das Display bis zum Neustart sofort eine zweite; bei schneller Internetverbindung
    überschrieb sie den Anfang der neuen Firmware, und der Bootloader startete die alte. Displays, die
    deshalb auf einer alten Version festhängen, holen die Korrektur so: Display antippen oder neu starten
    und innerhalb von 10 Minuten `http://<IP des Displays>:8080/update?check=1&install=1` aufrufen.
  - Diagnose: `/diag` zeigt den Zustand der Firmware-Plätze nach so einem Fall richtig an.
- **13.11.2026**
  - Neuer Schalter „Solarseite" (ab Werk aus): zusätzliche Seite hinter den Ladepunkten mit der aktuellen
    Erzeugung, dem Tag je Stunde bzw. der Leistung je Anlage, dem Ertrag von heute und der Prognose.
  - Bildschirm von außen schalten: `/wake` und `/sleep`, etwa für Bewegungsmelder über Home Assistant
    oder ioBroker.
  - Updates: Lief nach einer Installation weiter die alte Firmware, lud das Display dieselbe Version alle
    paar Minuten erneut und startete jedes Mal neu. Jetzt nach drei Versuchen nur noch einmal je
    24 Stunden; `/update` und `/diag` zeigen, wie oft das passiert ist. Die alte Firmware bestätigt eine
    nicht gestartete neue nicht mehr als gut.
- **12.11.2026**
  - Neuer Schalter „Ladelimit" (ab Werk aus): Neben dem Fahrzeug steht das Ladelimit, antippen öffnet die
    Auswahl. Kennt evcc den Ladestand, gilt es in Prozent (50–100 %). Ohne Ladestand – etwa bei Fahrzeugen
    ohne funktionierende Hersteller-Schnittstelle oder beim Gastfahrzeug – begrenzt es wie in evcc die
    Energie dieser Ladung in kWh. evcc setzt das Limit beim Abstecken zurück.
  - Einstellungsseite mit den Abschnitten Verbindung, Anzeige und System sowie einer Vorschau der Anzeige.
    Die meisten Änderungen wirken sofort, ohne Neustart.
  - Neuer Schalter „Fahrzeug wechseln" (ab Werk an, wie bisher) und ein optionales Passwort für
    Weboberfläche und Firmware-Uploads (ab Werk keines).
  - Updates: Neustarts laufen als vollständiger System-Reset. Vorher konnte das Display nach einem Update
    auf die alte Firmware zurückfallen. `/diag` zeigt jetzt die letzten Starts.
  - Fahrzeugzeile zeigte teils nur „…"; schnelles Durchtippen der Fahrzeuge sendet nur noch das zuletzt
    gewählte an evcc; rund 48 KB mehr freier Arbeitsspeicher.
- **11.11.2026** – Fahrzeug per Antippen der Fahrzeugzeile wechseln.
- **09.11.2026** – WLAN-Verbindung wird aktiv wiederhergestellt; Akkuanteil, Batteriepriorität und
  Energiebalken korrigiert.
- **06.11.2026** – Helligkeit einstellbar, Wärmepumpe und Heizstab, Energiebalken wie in evcc, Kachel mit
  Solarprognose.

## Anpassen

Zeiten und Standardwerte stehen in [src/config.h](src/config.h), Pins und Display-Timings in
[src/board.h](src/board.h). Ein Passwort für Weboberfläche und OTA
lässt sich unter `/settings` setzen; fest einkompilieren geht mit `-DOTA_PASSWORD=\"…\"` unter `build_flags`
in `platformio.ini` (gilt, solange in den Einstellungen keines gesetzt ist).

## Lizenz

MIT – siehe [LICENSE](LICENSE). Mitgelieferte Fremdinhalte:

- [Montserrat](https://github.com/JulietaUla/Montserrat) – SIL Open Font License 1.1 ([tools/fonts/OFL-Montserrat.txt](tools/fonts/OFL-Montserrat.txt))
- [Shopicons](https://github.com/H2D2-Design/h2d2-shopicons) und [Material Symbols](https://github.com/google/material-design-icons) – Apache-2.0 ([tools/icons/LICENSE-shopicons](tools/icons/LICENSE-shopicons))
- Die Icons entsprechen denen der [evcc](https://github.com/evcc-io/evcc)-Weboberfläche; dieses Projekt ist nicht offiziell mit evcc verbunden.
