#pragma once

// Standardwerte – lassen sich im WLAN-Einrichtungsportal ändern
#define DEFAULT_EVCC_HOST "192.168.178.209"
#define DEFAULT_EVCC_PORT 7070
#define DEFAULT_LOADPOINT 1        // 1 = erster Ladepunkt in EVCC ("Garage")
#define DEFAULT_LATITUDE 51.0f     // für Tag/Nacht-Umschaltung (Sonnenstand)
#define DEFAULT_LONGITUDE 10.0f
#define DEFAULT_ROTATION 0         // 0–3, jeweils 90° im Uhrzeigersinn
#define DEFAULT_THEME 0            // 0 = automatisch nach Sonnenstand, 1 = immer hell, 2 = immer dunkel

#define HOSTNAME "evcc-garage"
#define PORTAL_AP_NAME "EVCC-Garage"
#define PORTAL_TIMEOUT_S 300

#define TIMEZONE "CET-1CEST,M3.5.0,M10.5.0/3"
#define NTP_SERVER_1 "fritz.box"
#define NTP_SERVER_2 "pool.ntp.org"

// EVCC-Abfrage
#define POLL_INTERVAL_MS 3000
#define STALE_AFTER_MS 15000       // danach gilt EVCC als nicht erreichbar
#define MODE_CONFIRM_TIMEOUT_MS 10000

// Display
#define BRIGHTNESS 255                    // die Garage ist hell, wenn jemand drin ist → nicht dimmen
#define SCREEN_TIMEOUT_MS (2UL * 60UL * 1000UL)  // aus, wenn nicht geladen und nicht bedient wird
#define NIGHT_SUN_ELEVATION -3.0                 // Sonne tiefer als -3° → dunkles Design (bei Einstellung automatisch)
#define PREVIEW_DURATION_MS 60000                // Dauer einer Vorschau über /preview
#define SETTINGS_LONG_PRESS_MS 3000              // Titel lange drücken → Einstellungen
