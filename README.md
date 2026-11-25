# evcc-Garagendisplay (ESP32-4848S040C)

Touch-Display neben dem Ladepunkt: zeigt die aktuelle Ladeleistung aus [evcc](https://evcc.io)
und schaltet zwischen **Smart**, **Langsam** und **Schnell** um.
Hardware: Guition ESP32-4848S040C – ESP32-S3, 4" IPS 480×480 (ST7701S), kapazitiver Touch (GT911),
Strom per USB-C. Bezugsquelle, die ich genutzt habe: [AliExpress](https://de.aliexpress.com/item/1005012911942193.html).

Der frühere Stand für das 2,8"-„Cheap Yellow Display" liegt im Git-Tag `cyd-final`.

**English:** The display and its settings pages are available in English. A newly flashed display starts in
English; choose the language in the Wi-Fi setup portal ("Sprache / Language") or later under `/settings` → System.
This guide and the diagnostics output are German only.

**Anleitungen und häufige Fragen** für alle, die das Display nutzen: [Hilfe](https://tobymarks.github.io/evcc-garage-display/hilfe.html).
Diese README beschreibt Funktionen und Technik.

## Anzeige

| Bereich | Inhalt |
| --- | --- |
| Kopfzeile | Name des Ladepunkts, Uhrzeit, Ladestand des Hausakkus (sofern vorhanden), Warnsymbol bei WLAN-/evcc-Problemen |
| Mitte | Angestecktes Fahrzeug mit Ladestand (antippen wechselt durch die in evcc angelegten Fahrzeuge und „Gastfahrzeug"), darunter die Ladeleistung in kW mit grünem Blitz beim Laden. Ohne Fahrzeug der aktuelle Sonnenüberschuss (was gerade in den Akku und ins Netz geht) mit Kacheln für Haus, Akku und Solarprognose (Rest heute, abends für morgen) – und ob der Überschuss für Smart reicht. Ohne PV-Anlage nur „Kein Fahrzeug" |
| Statuszeile | z. B. „Lädt · 3-phasig", „Wartet auf Sonnenüberschuss", „Genug Sonne – Start in 0:45" |
| Balken | Energiefluss wie in evcc, die Werte stehen im Balken: Netzbezug (grau), Sonne (grün), Hausakku entlädt (dunkelgrün), Einspeisung (gelb). Beim Laden steht der Sonnenanteil der Ladung in der Statuszeile. Ohne Netzleistung in evcc nur Sonnen- und Netzanteil der Ladeleistung |
| Kacheln | Ab Werk geladene Energie, Dauer und durchschnittlicher Sonnenanteil der Sitzung. Unter `/settings` → „Ladepunktseite“ → „Kacheln bei angestecktem Fahrzeug“ lässt sich jede Position belegen oder ausblenden. Die Kachel „Fahrzeug-Ladestand“ zeigt den Ladestand des angesteckten Autos auch bei eingeblendetem Ladelimit und vor Ladebeginn. Bei Heizgeräten zeigt sie die Temperatur, bei fehlendem Wert einen Strich. |
| Unten | Modustasten Smart · Langsam · Schnell, jeweils mit kurzer Beschreibung |

**Mehrere Ladepunkte:** In den Einstellungen mehrere Nummern mit Komma eintragen (z. B. `1,2,3`, höchstens 6).
Die Reihenfolge der Nummern bestimmt die Wischreihenfolge. Unter „Bildschirm“ wählt man die Startseite.
Ab Werk startet das Display mit dem ersten Ladepunkt der Liste, bei `2,1` also mit Ladepunkt 2, und behält beim
Aufwecken die zuletzt gezeigte Seite. Wer einen festen Ladepunkt oder die Solarseite auswählt, kehrt beim Ausschalten
dorthin zurück. Punkte unter den Modustasten zeigen die aktuelle Seite. Wird an einem anderen Ladepunkt ein Auto angesteckt oder beginnt/endet dort das Laden,
springt das Display dorthin – außer jemand hat es in der letzten Minute bedient. Alle Ladepunkte kommen mit
einer einzigen Abfrage.

**Solarseite (Schalter unter `/settings`, ab Werk aus):** eine zusätzliche Seite an wählbarer Position, erreichbar
durch Wischen. Sie zeigt die aktuelle Erzeugung, darunter den Tag je Stunde – grün, was erzeugt wurde, grau die
Prognose für die kommenden Stunden – und in Kacheln den Ertrag von heute, was heute noch kommt und die Prognose
für morgen. Kommt heute nichts mehr, entfällt die mittlere Kachel. Kennt
evcc mehrere PV-Anlagen, stehen statt der Stundenbalken die Leistungen je Anlage (höchstens drei Zeilen, weitere
zusammengefasst). Tasten gibt es dort keine; wird ein Auto angesteckt oder beginnt das Laden, springt das Display
wie sonst zum Ladepunkt. Tagesverlauf und Tagesertrag kommen aus dem Verlauf von evcc
(`/api/history/energy`, getestet mit 0.319); ältere Versionen zeigen nur Erzeugung und Prognose.

Was die Solarseite zeigt, lässt sich unter `/settings` → „Solarseite einrichten“ aus festen Bausteinen wählen; ab
Werk bleibt sie wie beschrieben. Zur Wahl stehen die **große Zahl** (Erzeugung, Überschuss, Verbrauch, Netz,
Hausakku), das **Diagramm** (Tagesverlauf, Energiebalken mit Tagesverlauf, Anlagen einzeln) und die **drei
Kacheln** (heute erzeugt, Rest heute, morgen, Prognose heute, Erzeugung, Überschuss, Verbrauch, Netz, Ladestand
oder Leistung des Hausakkus oder keine Kachel). Die Seite zeigt eine Vorschau, die sofort reagiert, und lädt auf Wunsch das
echte Bild vom Display. Was evcc nicht liefert, ist dort nicht wählbar. Der Überschuss ist gerechnet wie auf der
Ladepunktseite ohne Fahrzeug: Einspeisung und, sobald der Hausakku seinen Vorrang erreicht hat, auch seine
Ladeleistung – für ein Display im Haus: Lohnt es sich gerade, die Spülmaschine einzuschalten?

Mit der **Solarseite als Startseite** startet das Display dort, springt nicht
zum Ladepunkt und behält bei Bildschirm-Timeout „nie“ die gewählte Seite bei. Die Balken unter den einzelnen
Anlagen zeigen standardmäßig deren Leistung im Verhältnis zur gerade stärksten Anlage. Unter „Solarseite einrichten“
→ **PV-Nennleistung (kWp)** lässt sich für jede Anlage ein Bezugswert eintragen. Sind alle Anlagen über ihre evcc-Zählernamen
eindeutig zugeordnet und alle Werte gesetzt, zeigen die Balken den Anteil an der jeweiligen Nennleistung (z. B. rund 14 %
bei 1,3 kW von 9 kWp). Fehlt ein Wert oder ein eindeutiger Zählername, oder werden Anlagen zusammengefasst, bleiben alle
Balken relativ. Die Vorschau im Browser verwendet Beispielbalken; das echte Bild zeigt die gespeicherte Skalierung.

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

Am einfachsten im Browser am Computer (Chrome, Edge oder Firefox ab Version 151): **[tobymarks.github.io/evcc-garage-display](https://tobymarks.github.io/evcc-garage-display/)** –
die Seite wird bei jedem Push auf `main` von GitHub Actions mit frisch gebauter Firmware neu erstellt (`site/`, `.github/workflows/pages.yml`).
Beim ersten Mal „Erase device“ anhaken (löscht die Demo-Software ab Werk); ohne den Haken bleiben WLAN und
Einstellungen bei einer erneuten Installation erhalten. Am zuverlässigsten klappt es mit einem Kabel von USB-A auf
USB-C. Eine BOOT-Taste ist nicht nötig.

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
- Version: [scripts/version.py](scripts/version.py) – Datum und Kurz-Hash des letzten Commits, der die Firmware ändert (`src/`, `include/`, `platformio.ini`, `partitions.csv`, `scripts/version.py`), lokal und in GitHub Actions gleich. Reine Website- oder Doku-Änderungen lösen dadurch kein Update auf den Displays aus.
  Ein lokal gebauter, sauberer Stand von `main` wird deshalb nicht erneut installiert; Builds mit ungespeicherten
  Änderungen (`-dirty`) ersetzt das Display beim nächsten Update durch den veröffentlichten Stand

## Erster Start

Mit dem Handy ins WLAN `evcc-Garage` wechseln – die Einrichtungsseite öffnet sich (sonst `192.168.4.1`).
WLAN auswählen und Passwort eingeben. Schritt für Schritt steht das in der
[Hilfe](https://tobymarks.github.io/evcc-garage-display/hilfe.html#einrichten). Darunter lassen sich einstellen:

| Feld | Standard |
| --- | --- |
| Sprache / Language | `en` bei einem neuen Display, `de` für Deutsch |
| evcc-Adresse / Port | `192.168.178.209` / `7070` |
| Ladepunkt-Nr. | `1` (Garage); mehrere mit Komma, z. B. `1,2,3` – dann wischen zum Wechseln |
| Breiten-/Längengrad | Mitte Deutschlands – für die automatische Tag/Nacht-Umschaltung |
| Drehung | `0` (je 90° im Uhrzeigersinn: 1, 2, 3) |
| Design | `0` automatisch, `1` hell, `2` dunkel |
| Bildschirm aus nach Minuten | `2` (`0` = immer an) |
| Automatische Updates | `1` an (`0` = aus) |
| Name im Heimnetz | `evcc-garage` – bei mehreren Displays je einen eigenen, z. B. `evcc-kueche` |

## Später ändern

- **Einstellungen im Heimnetz:** im Browser `http://evcc-garage.local:8080` aufrufen (leitet auf `/settings`).
  Unter „Ladepunktseite" und „Solarseite" lässt sich ein- und ausschalten, was das Display zeigt und was sich
  antippen lässt (Fahrzeug wechseln, Ladelimit, Battery Boost, Solarseite); unter „Bildschirm" kann die Seite die
  aktuelle Anzeige des Displays laden. Diese Schalter sowie Design, Helligkeit und Bildschirm-Timeout wirken sofort.
  Nur Änderungen an evcc-Adresse, Port, Ladepunkten, Drehung, Passwort, Sprache, Name im Heimnetz oder am Schalter „Dimmen mit langsamer
  Taktung" starten das Display neu – der Knopf heißt dann „Speichern und neu starten". Oben steht, ob das Display
  evcc erreicht. Das WLAN bleibt unverändert.
- **Bildschirm bleibt beim Dimmen dunkel oder flackert:** Schalter „Dimmen mit langsamer Taktung" unter „Bildschirm"
  (150 Hz statt 20 000 Hz, ab Werk aus), siehe [Hilfe](https://tobymarks.github.io/evcc-garage-display/hilfe.html#helligkeit-dunkel).
- **Streifen oder Schatten nach dem Aufwecken:** Solange der Fehler sichtbar ist, ein Foto machen und
  gleichzeitig auf der Einstellungsseite unter „Bildschirm“ die aktuelle Anzeige laden. Foto und
  Bildschirmaufnahme zusammen zeigen, ob die Streifen schon im Bildspeicher zu sehen sind.
- **Bildschirm kurz nach dem Start dunkel:** Während er dunkel ist, auf Port 8080 zuerst
  `/diag?download=1` sichern, dann `/wake` öffnen. Festhalten, ob das Bild zurückkommt und wie lange
  es sichtbar bleibt. Die Diagnose enthält den Startgrund und die von der Firmware eingestellte Helligkeit.
- **Mehrere Displays:** Jedes bekommt unter „System“ → „Name im Heimnetz“ (oder im WLAN-Portal) einen eigenen
  Namen, z. B. `evcc-kueche`. Es ist dann unter `http://evcc-kueche.local:8080` erreichbar und steht so in der
  Geräteliste des Routers; auch die Adressen für `/wake` und `/sleep` und der Upload per PlatformIO nutzen ihn.
  Erlaubt sind a–z, 0–9 und Bindestrich, höchstens 24 Zeichen; Umlaute werden umschrieben.
- **Passwort (optional):** unter „System" setzen. Danach verlangen alle Seiten des Displays die Anmeldung als
  `admin`, und Firmware-Uploads im Heimnetz brauchen dasselbe Passwort (`--auth=…` bei `espota`,
  in PlatformIO `upload_flags = --auth=…`). Es wird unverschlüsselt im Heimnetz übertragen – also kein Passwort
  verwenden, das woanders gilt. Vergessen? Am Display das Portal öffnen (siehe nächster Punkt) und dort
  speichern: das löscht das Passwort.
- **Einstellungen am Display** (auch für ein anderes WLAN): „Garage" oben links **3 Sekunden gedrückt halten** →
  das Portal `evcc-Garage` startet erneut. Antippen des Hinweises bricht ab.
- Nach einem Stromausfall wartet das Display auf den Router und startet nach 5 Minuten ohne WLAN selbst neu.
- Weitere Fragen zu Verbindung, Anzeige und Updates beantwortet die [Hilfe](https://tobymarks.github.io/evcc-garage-display/hilfe.html#fragen).

## Diagnose

Im Heimnetz unter `http://evcc-garage.local:8080`:

| Pfad | Zweck |
| --- | --- |
| `/settings` | Einstellungen ändern (wie im Portal, ohne WLAN) |
| `/screenshot.bmp` | aktueller Bildschirminhalt |
| `/preview?s=charging\|solar\|fast\|nocar\|nocarlow\|nocarfull\|night\|waiting\|start\|off\|stale&dark=0\|1` | zeigt 60 s lang einen Beispielzustand ([src/preview.cpp](src/preview.cpp)), die Solarseite mit `pv`, `pvmulti`, `pvnight`, `pvsummer` oder mit den echten Daten `pvlive` (`&cfg=1,2,0,9,2` zeigt sie mit anderen Bausteinen: große Zahl, Diagramm, drei Kacheln – die Nummern wie in [src/config.h](src/config.h)); `&title=…` setzt dazu einen anderen Namen in die Kopfzeile; `/preview` ohne Parameter beendet |
| `/update` | Firmware-Version und Stand der automatischen Updates, auch Installationen, nach denen die neue Version nicht lief; `?check=1` prüft sofort, `?install=1` installiert eine gefundene neue Version sofort |
| `/diag` | Diagnose, auch als Knopf „Diagnose herunterladen“ auf der Einstellungsseite (`/diag?download=1` liefert sie als Datei zum Anhängen an ein Issue; ohne Passwörter, WLAN-Namen, Adressen, Standort und Namen von Ladepunkten oder Fahrzeugen): Laufzeit, Neustartgrund, Zustand des Bildschirms, Flash-Baustein, freier Speicher und Stack des evcc-Tasks, Zustand beider Firmware-Plätze (und ob der Inhalt des anderen startfähig ist), die letzten acht Starts (Firmware, Platz, Grund), zurückgerollte und nicht gestartete Updates, der letzte gespeicherte Absturz, dazu Chip, Touch-Controller, Einstellungen, evcc-Version und was evcc je Ladepunkt liefert |
| `/wake`, `/sleep` | Bildschirm an- bzw. ausschalten, siehe „Bildschirm von außen schalten" |
| `/backlight` | Taktung der Hintergrundbeleuchtung anzeigen; `?hz=1000` stellt sie bis zum nächsten Neustart um (100 bis 40000 Hz, ab Werk 20000), `?hz=150&save=1` speichert sie und startet das Display neu, sodass sie schon ab dem Start gilt (zurück mit `?hz=20000&save=1`). Zum Ausprobieren anderer Werte; für den üblichen Fall gibt es unter `/settings` den Schalter „Dimmen mit langsamer Taktung" |
| `/wifiscan` | löst eine WLAN-Suche aus (Lasttest für die Bildausgabe) |

Ohne Gerät: `./tools/sim/render.sh <Ordner> nocar charging …` rendert dieselben Beispielzustände mit der
echten Oberfläche (LVGL, Schriften) auf dem Rechner als PNG, hell und dunkel. Ohne Argumente erneuert es die
Screenshots der Website in `site/img`. Braucht clang und einmal `pio run`.

## Neuigkeiten

Bestehende Displays zeigen nach einem Update dasselbe wie vorher; neue Funktionen sind Schalter unter
`/settings`.

- **22.11.2026**
  - Startseite und Wischreihenfolge sind unabhängig wählbar. So können zwei Displays dieselben Ladepunkte
    in derselben Reihenfolge zeigen und jeweils mit einem anderen Ladepunkt starten (#54).
  - Auf der Ladepunktseite lässt sich die Dauer durch den Fahrzeug-Ladestand ersetzen. Alle drei Kacheln
    lassen sich einzeln belegen. Das Ladelimit bleibt separat bedienbar (#55).
  - Die Fahrzeugzeile sitzt mittiger zwischen Titel und Ladeleistung (#51). Uhr und Hausakku haben etwas engere
    Ziffernabstände und behalten beim Wechsel der Zahlen ihre Position.
  - Fehlt zum Prüfzeitpunkt das WLAN, holt das Display die Update-Prüfung nach dem Wiederverbinden nach.
- **18.11.2026**
  - Installation per USB: Ohne Haken bei „Erase device“ bleiben WLAN und Einstellungen jetzt wirklich erhalten. Bisher
    gingen sie bei jeder Installation über die Projektseite verloren.
  - Ist die Solarseite die Startseite, bleibt eine gewählte Ladepunktseite stehen, bis der Bildschirm ausgeht (#53).
  - Das Display beginnt nach dem Start immer auf der ersten Seite, auch wenn an einem anderen Ladepunkt ein Auto
    angesteckt ist (#54).
  - Nach der WLAN-Einrichtung bleibt der Bildschirm an, statt sofort auszugehen.
- **16.11.2026**
  - Name im Heimnetz einstellbar (#47): Unter `/settings` → System oder im WLAN-Portal bekommt jedes Display einen
    eigenen Namen, z. B. `evcc-kueche` → `http://evcc-kueche.local:8080`. Ab Werk bleibt es `evcc-garage`.
  - Neue [Hilfeseite](https://tobymarks.github.io/evcc-garage-display/hilfe.html) mit Anleitungen und häufigen
    Fragen. Die Einstellungsseite verlinkt dorthin, unter „Firmware“ direkt auf das Update von Hand.
  - Englisch: Display, Einstellungsseite und „Solarseite einrichten“ gibt es jetzt auch auf Englisch (#44).
    Umschalten unter `/settings` → System → „Sprache / Language“ oder im WLAN-Portal; danach startet das Display
    neu. Bestehende Displays bleiben deutsch, neu geflashte beginnen englisch. Diagnose (`/diag`, `/update`) und
    diese Anleitung bleiben deutsch.
- **15.11.2026**
  - Einstellungsseite überarbeitet: Schalter statt Auswahllisten für An/Aus, kürzere Erklärungen, neue Abschnitte
    (Verbindung, Ladepunktseite, Solarseite, Bildschirm, System). „Speichern“ bleibt am unteren Rand sichtbar und
    meldet ungespeicherte Änderungen; oben steht, ob das Display evcc erreicht. Die Einstellungen selbst bleiben
    unverändert.
  - Solarseite einrichten: Unter „Solarseite“ führt ein neuer Knopf zu einer Seite, auf der sich große Zahl,
    Diagramm und Kacheln aus festen Bausteinen wählen lassen, mit Vorschau und echtem Bild vom Display (#36).
    Neu wählbar sind Verbrauch, Netz und Hausakku. Ab Werk bleibt die Seite wie bisher; die Einstellung
    „Große Zahl“ ist in der neuen Seite aufgegangen und wird übernommen. Bei drei Kacheln ist die Beschriftung
    etwas kleiner als bisher, damit nichts an den Rand stößt; „ÜBERSCHUSS“ heißt dort „ÜBRIG“.
  - Solarseite: Abends, wenn heute nichts mehr kommt, stehen nur noch „HEUTE“ und „MORGEN“. Die Kachel
    „PROGNOSE“ mit der Vorhersage für den ganzen Tag entfällt, sie las sich wie etwas, das noch kommt (#37).
  - Solarseite einrichten: Als Kachel ist jetzt auch die Leistung des Hausakkus wählbar. Die Beschriftung nennt
    die Richtung: „IN AKKU“ beim Laden, „AUS AKKU“ beim Entladen (#42).
  - Helligkeit: Neuer Schalter „Dimmen mit langsamer Taktung“ unter „Bildschirm“, ab Werk aus. Er hilft bei
    Boards, die unter 90 % Helligkeit dunkel bleiben oder flackern, und ersetzt dort den Umweg über
    `/backlight?hz=150&save=1`. Ein so gespeicherter Wert bleibt erhalten (#21).
- **14.11.2026**
  - Automatische Updates: Fehler behoben, durch den nach einem Update wieder die alte Firmware lief. Nach der
    Installation begann das Display bis zum Neustart sofort eine zweite; bei schneller Internetverbindung
    überschrieb sie den Anfang der neuen Firmware, und der Bootloader startete die alte. Displays, die
    deshalb auf einer alten Version festhängen, holen die Korrektur so: Display antippen oder neu starten
    und innerhalb von 10 Minuten `http://<IP des Displays>:8080/update?check=1&install=1` aufrufen.
  - Diagnose: `/diag` zeigt den Zustand der Firmware-Plätze nach so einem Fall richtig an.
  - Kopfzeile: Zu lange Ladepunktnamen enden mit „…“, statt unter die Akku-Anzeige zu laufen.
  - Solarprognose: Ist in evcc „PV-Vorhersage anhand der realen Erzeugungsdaten anpassen“ eingeschaltet,
    rechnet das Display „REST HEUTE“, „MORGEN“ und die kommenden Stunden im Tagesverlauf jetzt mit demselben
    Faktor wie evcc. Vorher wichen die Werte von evcc ab.
  - Solarseite, zwei neue Schalter (ab Werk aus): „Solarseite: große Zahl“ zeigt auf Wunsch den Überschuss statt
    der Erzeugung, darunter den Energiebalken; „Solarseite zuerst“ macht sie zur Startseite, etwa für ein Display
    in der Küche (#31).
  - Einstellungsseite: Knopf „Jetzt aktualisieren“ sucht sofort nach einer neuen Version und installiert sie,
    ohne auf das automatische Update zu warten (#26).
  - Helligkeit: Die Taktung der Hintergrundbeleuchtung lässt sich zum Ausprobieren jetzt auch speichern
    (`/backlight?hz=150&save=1`, zurück mit `?hz=20000&save=1`). Sie gilt dann schon ab dem Start des Displays;
    der Bereich reicht jetzt bis 100 Hz hinunter (#21).
  - Diagnose als Datei: Auf der Einstellungsseite gibt es unten „Diagnose herunterladen“, zum Anhängen an ein
    Issue. Die Datei enthält keine Passwörter, keinen WLAN-Namen, keine Netzwerkadressen, keinen Standort und
    keine Namen von Ladepunkten oder Fahrzeugen. Neu darin: Chip, Touch-Controller, Einstellungen, evcc-Version
    und was evcc je Ladepunkt liefert.
  - Helligkeit: Bleibt das Display unterhalb von 90 % dunkel, lässt sich mit
    `http://<IP des Displays>:8080/backlight?hz=1000` eine andere Taktung der Hintergrundbeleuchtung
    ausprobieren (200 bis 40000 Hz, gilt bis zum Neustart).
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

Weitere Sprachen: Im Quelltext stehen die deutschen Texte in `tr("…")`; eine Sprache ist eine Tabelle wie
[src/lang_en.cpp](src/lang_en.cpp), eingetragen in [src/lang.h](src/lang.h) und `src/lang.cpp`.
`python3 tools/check_lang.py` prüft, ob sie vollständig ist (läuft auch vor jedem Build), und
`SIM_LANG=en ./tools/sim/render.sh <Ordner> <Zustand …>` zeigt, ob die Texte auf den Bildschirm passen.

Zeiten und Standardwerte stehen in [src/config.h](src/config.h), Pins und Display-Timings in
[src/board.h](src/board.h). Ein Passwort für Weboberfläche und OTA
lässt sich unter `/settings` setzen; fest einkompilieren geht mit `-DOTA_PASSWORD=\"…\"` unter `build_flags`
in `platformio.ini` (gilt, solange in den Einstellungen keines gesetzt ist).

## Lizenz

MIT – siehe [LICENSE](LICENSE). Mitgelieferte Fremdinhalte:

- [Montserrat](https://github.com/JulietaUla/Montserrat) – SIL Open Font License 1.1 ([tools/fonts/OFL-Montserrat.txt](tools/fonts/OFL-Montserrat.txt))
- [Shopicons](https://github.com/H2D2-Design/h2d2-shopicons) und [Material Symbols](https://github.com/google/material-design-icons) – Apache-2.0 ([tools/icons/LICENSE-shopicons](tools/icons/LICENSE-shopicons))
- Die Icons entsprechen denen der [evcc](https://github.com/evcc-io/evcc)-Weboberfläche; dieses Projekt ist nicht offiziell mit evcc verbunden.
