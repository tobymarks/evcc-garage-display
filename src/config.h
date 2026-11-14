#pragma once

// Standardwerte – lassen sich im WLAN-Einrichtungsportal ändern
#define DEFAULT_EVCC_HOST "192.168.178.209"
#define DEFAULT_EVCC_PORT 7070
#define DEFAULT_LOADPOINT 1        // 1 = erster Ladepunkt in evcc ("Garage")
#define MAX_LOADPOINTS 6           // mehr Ladepunkte lassen sich nicht durchwischen
#define DEFAULT_LATITUDE 51.0f     // für Tag/Nacht-Umschaltung (Sonnenstand)
#define DEFAULT_LONGITUDE 10.0f
#define DEFAULT_ROTATION 0         // 0–3, jeweils 90° im Uhrzeigersinn
#define DEFAULT_THEME 0            // 0 = automatisch nach Sonnenstand, 1 = immer hell, 2 = immer dunkel
#define WEB_USER "admin"           // Benutzername, wenn in den Einstellungen ein Passwort gesetzt ist
#define MAX_PASSWORD_LEN 32
#define DEFAULT_AUTO_UPDATE 1      // neue Firmware von der Projektseite selbst installieren

#define HOSTNAME "evcc-garage"
#define PORTAL_AP_NAME "evcc-Garage"
#define PORTAL_TIMEOUT_S 300

#define TIMEZONE "CET-1CEST,M3.5.0,M10.5.0/3"
#define NTP_SERVER_1 "fritz.box"
#define NTP_SERVER_2 "pool.ntp.org"

// evcc-Abfrage und WLAN
#define POLL_INTERVAL_MS 3000
#define STALE_AFTER_MS 15000              // danach gilt evcc als nicht erreichbar
#define MODE_CONFIRM_TIMEOUT_MS 10000
#define WIFI_RECONNECT_MS 30000           // ohne WLAN regelmäßig neu verbinden (AutoReconnect deckt nicht alle Gründe ab)
#define WIFI_STALE_RECONNECT_MS 120000    // WLAN auch neu verbinden, wenn trotz WL_CONNECTED 2 min keine Daten fließen
#define WIFI_STALE_RETRY_MS 600000        // … und danach nur noch alle 10 min, solange weiter nichts kommt
#define VEHICLE_TAP_DELAY_MS 800          // Fahrzeugwechsel erst abschicken, wenn so lange nicht weitergetippt wird
#define SOLAR_INTERVAL_MS 300000          // Stundenwerte für die Solarseite alle 5 min
#define SOLAR_RETRY_MS 30000              // … nach einem Fehlschlag (Uhrzeit fehlt noch, evcc nicht erreichbar) früher
#define CHARGING_STALE_MS 600000          // ohne Daten von evcc gilt "lädt" so lange weiter (Bildschirm an, keine Updates)

// Projektseite (Anleitung, Browser-Installer) und Quellcode – verlinkt unter /settings
#define PROJECT_URL "https://tobymarks.github.io/evcc-garage-display/"
#define REPO_URL "https://github.com/tobymarks/evcc-garage-display"

// Automatische Updates (siehe src/updater.cpp): update.json wird bei jedem Push auf main neu gebaut
#define UPDATE_BASE_URL PROJECT_URL "firmware/"
#define UPDATE_FIRST_CHECK_MS (2 * 60000UL)     // erste Prüfung nach dem Start
#define UPDATE_CHECK_INTERVAL_MS (6 * 3600000UL)
#define UPDATE_IDLE_MS (10 * 60000UL)           // installiert erst, wenn so lange niemand bedient und nicht geladen wird
#define UPDATE_CONFIRM_MS 60000                 // läuft die neue Firmware so lange und hat WLAN, gilt sie als gut (sonst zurück zur alten)
#define UPDATE_FAST_ATTEMPTS 3                  // so oft wird eine Version direkt nacheinander installiert, wenn sie danach nicht läuft
#define UPDATE_RETRY_AFTER_MS (24 * 3600000UL)  // … danach erst wieder nach so langer Laufzeit

// Display
#define DEFAULT_BRIGHTNESS_PCT 100        // fest eingestellt, keine Nachtdimmung (die Garage ist hell, wenn jemand drin ist)
#define MIN_BRIGHTNESS_PCT 10
#define MIN_BACKLIGHT_HZ 100   // einstellbarer Bereich für die Taktung der Hintergrundbeleuchtung (/backlight)
#define MAX_BACKLIGHT_HZ 40000
#define DEFAULT_SCREEN_TIMEOUT_MIN 2              // aus, wenn nicht geladen und nicht bedient wird; 0 = immer an
#define NIGHT_SUN_ELEVATION -3.0                 // Sonne tiefer als -3° → dunkles Design (bei Einstellung automatisch)
#define PREVIEW_DURATION_MS 60000                // Dauer einer Vorschau über /preview
#define SETTINGS_LONG_PRESS_MS 3000              // Titel lange drücken → Einstellungen
#define AUTO_SWITCH_AFTER_TOUCH_MS 60000         // so lange nach der letzten Berührung nicht von selbst zu einem anderen Ladepunkt springen
