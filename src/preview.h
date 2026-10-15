#pragma once

#include "evcc_client.h"

namespace preview {
// Liste der Beispielzustände, getrennt durch "|"
extern const char* const NAMES;
// Füllt einen Beispielzustand zum Prüfen der Oberfläche ohne echtes Auto.
// false, wenn es den Namen nicht gibt. stale: Daten als veraltet anzeigen.
bool scene(const char* name, EvccState& p, bool& stale);
}  // namespace preview
