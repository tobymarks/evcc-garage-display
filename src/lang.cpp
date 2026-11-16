#include "lang.h"

#include <string.h>

#include <algorithm>

extern const lang::Table LANG_EN;

static Lang current = Lang::De;
static const lang::Table* table = nullptr;
// Reihenfolge der Einträge nach dem deutschen Text sortiert, für die binäre Suche
static uint16_t* order = nullptr;

void lang::set(Lang l) {
  current = l < Lang::Count ? l : Lang::De;
  table = current == Lang::En ? &LANG_EN : nullptr;
  delete[] order;
  order = nullptr;
  if (!table) return;
  order = new uint16_t[table->count];
  for (uint16_t i = 0; i < table->count; i++) order[i] = i;
  const Pair* pairs = table->pairs;
  std::sort(order, order + table->count,
            [pairs](uint16_t a, uint16_t b) { return strcmp(pairs[a].de, pairs[b].de) < 0; });
}

Lang lang::get() { return current; }

const char* lang::code() { return current == Lang::En ? "en" : "de"; }

const char* lang::locale() { return current == Lang::En ? "en-GB" : "de-DE"; }

char lang::decimal() { return current == Lang::En ? '.' : ','; }

const char* lang::tr(const char* de) {
  if (!table || !de) return de;
  int lo = 0, hi = (int)table->count - 1;
  while (lo <= hi) {
    const int mid = (lo + hi) / 2;
    const Pair& p = table->pairs[order[mid]];
    const int cmp = strcmp(de, p.de);
    if (cmp == 0) return p.text;
    if (cmp < 0)
      hi = mid - 1;
    else
      lo = mid + 1;
  }
  return de;
}
