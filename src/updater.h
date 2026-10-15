#pragma once

#include <Arduino.h>

// Automatische Updates: prüft regelmäßig update.json auf der Projektseite (GitHub Pages)
// und installiert neue Firmware, wenn das Display gerade nicht gebraucht wird.
// Startet die neue Firmware nicht sauber, kehrt der Bootloader zur alten zurück.
namespace updater {
enum class Event : uint8_t { None, Installing, Done, Failed };

void begin();
// Im loop aufrufen. idle: niemand bedient das Display und es wird nicht geladen.
Event loop(bool idle);
void checkNow();
// Installiert eine bekannte neuere Version sofort, auch ohne Leerlauf und ohne automatische Updates
void installNow();
const char* availableVersion();  // "" = aktuell
String status();
}  // namespace updater
