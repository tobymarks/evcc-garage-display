#pragma once

#include <stdint.h>

// Sprache der Oberfläche. Im Quelltext stehen die deutschen Texte; tr() liefert die Übersetzung der
// eingestellten Sprache oder, wenn es keine gibt, den deutschen Text selbst.
// Eine weitere Sprache ist eine weitere Tabelle wie src/lang_en.cpp, hier und in lang.cpp eingetragen.
// tools/check_lang.py prüft, ob jede Tabelle vollständig ist und die Platzhalter (%s, %d …) übereinstimmen.
//
// Deutsch bleiben mit Absicht: /diag, /update und die übrigen reinen Textantworten – sie dienen der
// Fehlersuche und werden an Issues angehängt.
//
// Die Zahlen stehen so im Speicher des Displays: nicht umsortieren.
enum class Lang : uint8_t { De, En, Count };

namespace lang {
struct Pair {
  const char* de;
  const char* text;
};
struct Table {
  const Pair* pairs;
  uint16_t count;
};

void set(Lang l);
Lang get();
const char* code();    // "de", "en" – für <html lang> und die Auswahl in den Einstellungen
const char* locale();  // "de-DE", "en-GB" – Zahlen- und Zeitformat im Browser
char decimal();        // Dezimaltrennzeichen
const char* tr(const char* de);
}  // namespace lang

inline const char* tr(const char* de) { return lang::tr(de); }
// Markiert einen Text, der erst später über tr(variable) übersetzt wird (Tabellen, Funktionsargumente),
// damit tools/check_lang.py ihn findet
#define TR_KEY(text) text
