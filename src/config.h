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
#define DEFAULT_AUTO_UPDATE 1      // neue Firmware von der Projektseite selbst installieren

#define HOSTNAME "evcc-garage"
#define PORTAL_AP_NAME "evcc-Garage"
#define PORTAL_TIMEOUT_S 300

#define TIMEZONE "CET-1CEST,M3.5.0,M10.5.0/3"
#define NTP_SERVER_1 "fritz.box"
#define NTP_SERVER_2 "pool.ntp.org"

// evcc-Abfrage
#define POLL_INTERVAL_MS 3000
#define STALE_AFTER_MS 15000       // danach gilt evcc als nicht erreichbar
#define MODE_CONFIRM_TIMEOUT_MS 10000

// Automatische Updates (siehe src/updater.cpp): update.json wird bei jedem Push auf main neu gebaut
#define UPDATE_BASE_URL "https://tobymarks.github.io/evcc-garage-display/firmware/"
#define UPDATE_FIRST_CHECK_MS (2 * 60000UL)     // erste Prüfung nach dem Start
#define UPDATE_CHECK_INTERVAL_MS (6 * 3600000UL)
#define UPDATE_IDLE_MS (10 * 60000UL)           // installiert erst, wenn so lange niemand bedient und nicht geladen wird
#define UPDATE_CONFIRM_MS 60000                 // läuft die neue Firmware so lange, gilt sie als gut (sonst zurück zur alten)

// Display
#define BRIGHTNESS 255                    // die Garage ist hell, wenn jemand drin ist → nicht dimmen
#define DEFAULT_SCREEN_TIMEOUT_MIN 2              // aus, wenn nicht geladen und nicht bedient wird; 0 = immer an
#define NIGHT_SUN_ELEVATION -3.0                 // Sonne tiefer als -3° → dunkles Design (bei Einstellung automatisch)
#define PREVIEW_DURATION_MS 60000                // Dauer einer Vorschau über /preview
#define SETTINGS_LONG_PRESS_MS 3000              // Titel lange drücken → Einstellungen
#define AUTO_SWITCH_AFTER_TOUCH_MS 60000         // so lange nach der letzten Berührung nicht von selbst zu einem anderen Ladepunkt springen
