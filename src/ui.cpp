#include "ui.h"

#include <lvgl.h>

#include "config.h"
#include "icons.h"

LV_FONT_DECLARE(font_18);
LV_FONT_DECLARE(font_22);
LV_FONT_DECLARE(font_24);
LV_FONT_DECLARE(font_26);
LV_FONT_DECLARE(font_32);
LV_FONT_DECLARE(font_big);
LV_FONT_DECLARE(font_icon_big);

// Farben angelehnt an die EVCC-Weboberfläche
struct Theme {
  lv_color_t bg, box, text, muted, activeBg, activeFg, pressed, solar, solarText, grid, warn, error, border;
};

static const Theme LIGHT = {
    // Grau und Grün dunkler als in EVCC: Kontrast ≥ 4,5:1, damit es aus 2–3 m lesbar bleibt
    lv_color_hex(0xF3F3F7), lv_color_hex(0xFFFFFF), lv_color_hex(0x28293E), lv_color_hex(0x6E6F7C),
    lv_color_hex(0x28293E), lv_color_hex(0xFFFFFF), lv_color_hex(0xE4E4EC), lv_color_hex(0x0FDE41),
    lv_color_hex(0x078A2C), lv_color_hex(0x28293E), lv_color_hex(0xC26A00), lv_color_hex(0xD9380A),
    lv_color_hex(0xDCDCE4),
};
static const Theme DARK = {
    lv_color_hex(0x05060F), lv_color_hex(0x1C1D2E), lv_color_hex(0xFFFFFF), lv_color_hex(0x9A9BA8),
    lv_color_hex(0xFFFFFF), lv_color_hex(0x05060F), lv_color_hex(0x33344A), lv_color_hex(0x0FDE41),
    lv_color_hex(0x0FDE41), lv_color_hex(0x93949E), lv_color_hex(0xFF9000), lv_color_hex(0xFC440F),
    lv_color_hex(0x1C1D2E),
};

static const Theme* theme = &LIGHT;
static bool night = false;
static UiView lastView;
static bool hasLastView = false;

static ui::ModeCallback modeCb;
static ui::Callback settingsCb;
static ui::Callback messageTapCb;

// Raster für 480x480
static constexpr int MARGIN = 24;
static constexpr int HEADER_H = 64;
static constexpr int SEG_H = 84;
static constexpr int SEG_Y = 480 - MARGIN - SEG_H;  // Modusumschalter unten
static constexpr int CONTENT_W = 480 - 2 * MARGIN;
static constexpr int BAR_H = 14;
static constexpr int TILE_GAP = 12;
static constexpr int TILE_H = 64;

// Hauptbildschirm
static lv_obj_t* scrMain;
static lv_obj_t* lblTitle;
static lv_obj_t* lblIcons;
static lv_obj_t* lblClock;
static lv_obj_t* contPower;
static lv_obj_t* lblPower;
static lv_obj_t* lblUnit;
static lv_obj_t* contIdle;
static lv_obj_t* lblIdleIcon;
static lv_obj_t* lblIdleText;
static lv_obj_t* lblStatus;
static lv_obj_t* barBlock;
static lv_obj_t* barBg;
static lv_obj_t* barFill;
static lv_obj_t* lblSolar;
static lv_obj_t* lblGrid;
static lv_obj_t* tiles;
static lv_obj_t* tile[3];
static lv_obj_t* tileLabel[3];
static lv_obj_t* tileValue[3];
static const char* TILE_TEXT[3] = {"Geladen", "Dauer", "Ø Sonne"};
static lv_obj_t* segBg;
static lv_obj_t* seg[3];
static lv_obj_t* segLbl[3];
static lv_obj_t* segSub[3];
static const ChargeMode SEG_MODES[3] = {ChargeMode::PV, ChargeMode::Smart, ChargeMode::Fast};
// Nur Text wie in EVCC, dazu eine Zeile, die sagt, was der Modus tut
static const char* SEG_TEXT[3] = {"PV", "Smart", "Schnell"};
static const char* SEG_SUB[3] = {"nur Sonne", "Sonne + Netz", "volle Leistung"};

// Hinweisbildschirm
static lv_obj_t* scrMsg;
static lv_obj_t* lblMsgIcon;
static lv_obj_t* lblMsgTitle;
static lv_obj_t* lblMsgText;

static lv_obj_t* makeLabel(lv_obj_t* parent, const lv_font_t* font) {
  lv_obj_t* l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font, 0);
  lv_label_set_text(l, "");
  return l;
}

static lv_obj_t* makeBox(lv_obj_t* parent) {
  lv_obj_t* o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  return o;
}

static void formatDecimal(char* buf, size_t len, float value) {
  snprintf(buf, len, "%.1f", value);
  for (char* p = buf; *p; p++)
    if (*p == '.') *p = ',';
}

static void formatDuration(char* buf, size_t len, uint32_t seconds) {
  snprintf(buf, len, "%u:%02u h", (unsigned)(seconds / 3600), (unsigned)((seconds / 60) % 60));
}

static void formatCountdown(char* buf, size_t len, uint32_t seconds) {
  snprintf(buf, len, "%u:%02u", (unsigned)(seconds / 60), (unsigned)(seconds % 60));
}

static void onSegment(lv_event_t* e) {
  const intptr_t i = (intptr_t)lv_event_get_user_data(e);
  if (modeCb) modeCb(SEG_MODES[i]);
}

static void onHeaderLongPress(lv_event_t*) {
  if (settingsCb) settingsCb();
}

static void buildMain() {
  scrMain = lv_obj_create(nullptr);
  lv_obj_remove_flag(scrMain, LV_OBJ_FLAG_SCROLLABLE);

  // Kopfzeile (Titel lange drücken → Einstellungen)
  lv_obj_t* header = makeBox(scrMain);
  lv_obj_set_size(header, 300, HEADER_H);
  lv_obj_add_flag(header, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(header, onHeaderLongPress, LV_EVENT_LONG_PRESSED, nullptr);
  lblTitle = makeLabel(header, &font_26);
  lv_obj_align(lblTitle, LV_ALIGN_LEFT_MID, MARGIN + 4, 4);

  lblClock = makeLabel(scrMain, &font_26);
  lv_obj_align(lblClock, LV_ALIGN_TOP_RIGHT, -MARGIN - 4, 0);
  lv_obj_set_style_pad_top(lblClock, (HEADER_H - font_26.line_height) / 2 + 4, 0);
  lblIcons = makeLabel(scrMain, &font_26);

  // Mittelteil: Inhalt vertikal zentriert zwischen Kopfzeile und Modusumschalter,
  // ausgeblendete Elemente nehmen keinen Platz ein
  lv_obj_t* mid = makeBox(scrMain);
  lv_obj_set_pos(mid, 0, HEADER_H);
  lv_obj_set_size(mid, 480, SEG_Y - HEADER_H);
  lv_obj_set_flex_flow(mid, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(mid, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  // Ladeleistung – Zahl und Einheit auf gemeinsamer Grundlinie
  contPower = makeBox(mid);
  lv_obj_set_size(contPower, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(contPower, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(contPower, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
  lv_obj_set_style_pad_column(contPower, 12, 0);
  lblPower = makeLabel(contPower, &font_big);
  lblUnit = makeLabel(contPower, &font_32);
  lv_label_set_text(lblUnit, "kW");
  lv_obj_set_style_pad_bottom(lblUnit, font_big.base_line - font_32.base_line, 0);

  // Anzeige ohne Fahrzeug / ohne Daten
  contIdle = makeBox(mid);
  lv_obj_set_size(contIdle, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(contIdle, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(contIdle, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(contIdle, 20, 0);
  lblIdleIcon = makeLabel(contIdle, &font_icon_big);
  lblIdleText = makeLabel(contIdle, &font_32);

  // Statuszeile
  lblStatus = makeLabel(mid, &font_22);
  lv_obj_set_width(lblStatus, CONTENT_W);
  lv_label_set_long_mode(lblStatus, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_align(lblStatus, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_pad_top(lblStatus, 4, 0);

  // Aktueller Sonnen-/Netzanteil der Ladeleistung
  barBlock = makeBox(mid);
  lv_obj_set_size(barBlock, CONTENT_W, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_top(barBlock, 18, 0);
  barBg = makeBox(barBlock);
  lv_obj_set_size(barBg, CONTENT_W, BAR_H);
  lv_obj_set_style_radius(barBg, BAR_H / 2, 0);
  lv_obj_set_style_bg_opa(barBg, LV_OPA_COVER, 0);
  lv_obj_set_style_clip_corner(barBg, true, 0);
  barFill = makeBox(barBg);
  lv_obj_set_height(barFill, BAR_H);
  lv_obj_set_style_bg_opa(barFill, LV_OPA_COVER, 0);
  lblSolar = makeLabel(barBlock, &font_18);
  lv_obj_set_pos(lblSolar, 0, BAR_H + 8);
  lblGrid = makeLabel(barBlock, &font_18);
  lv_obj_align(lblGrid, LV_ALIGN_TOP_RIGHT, 0, BAR_H + 8);

  // Kacheln mit den Werten der Ladesitzung
  tiles = makeBox(mid);
  lv_obj_set_size(tiles, CONTENT_W, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_top(tiles, 14, 0);
  lv_obj_set_style_pad_column(tiles, TILE_GAP, 0);
  lv_obj_set_flex_flow(tiles, LV_FLEX_FLOW_ROW);
  for (int i = 0; i < 3; i++) {
    tile[i] = makeBox(tiles);
    lv_obj_set_height(tile[i], TILE_H);
    lv_obj_set_flex_grow(tile[i], 1);
    lv_obj_set_style_radius(tile[i], 16, 0);
    lv_obj_set_style_bg_opa(tile[i], LV_OPA_COVER, 0);
    lv_obj_set_style_pad_left(tile[i], 14, 0);
    tileLabel[i] = makeLabel(tile[i], &font_18);
    lv_label_set_text(tileLabel[i], TILE_TEXT[i]);
    lv_obj_set_pos(tileLabel[i], 0, 7);
    tileValue[i] = makeLabel(tile[i], &font_26);
    lv_obj_set_pos(tileValue[i], 0, 29);
  }

  // Modusumschalter wie in EVCC: Pill-Gruppe, aktiver Modus gefüllt
  segBg = makeBox(scrMain);
  lv_obj_set_pos(segBg, MARGIN, SEG_Y);
  lv_obj_set_size(segBg, CONTENT_W, SEG_H);
  lv_obj_set_style_radius(segBg, SEG_H / 2, 0);
  lv_obj_set_style_bg_opa(segBg, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(segBg, 1, 0);
  lv_obj_set_style_pad_all(segBg, 6, 0);
  lv_obj_set_style_pad_column(segBg, 6, 0);
  lv_obj_set_flex_flow(segBg, LV_FLEX_FLOW_ROW);
  for (int i = 0; i < 3; i++) {
    seg[i] = makeBox(segBg);
    lv_obj_add_flag(seg[i], LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_height(seg[i], LV_PCT(100));
    lv_obj_set_flex_grow(seg[i], 1);
    lv_obj_set_style_radius(seg[i], SEG_H / 2 - 6, 0);
    lv_obj_set_style_bg_opa(seg[i], LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_add_event_cb(seg[i], onSegment, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    lv_obj_t* col = makeBox(seg[i]);
    lv_obj_remove_flag(col, LV_OBJ_FLAG_CLICKABLE);  // Klicks gehen an die Taste
    lv_obj_set_size(col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_center(col);
    segLbl[i] = makeLabel(col, &font_24);
    lv_label_set_text(segLbl[i], SEG_TEXT[i]);
    segSub[i] = makeLabel(col, &font_18);
    lv_label_set_text(segSub[i], SEG_SUB[i]);
  }
}

static void onMessageClicked(lv_event_t*) {
  if (messageTapCb) messageTapCb();
}

static void buildMessage() {
  scrMsg = lv_obj_create(nullptr);
  lv_obj_remove_flag(scrMsg, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(scrMsg, onMessageClicked, LV_EVENT_CLICKED, nullptr);

  lv_obj_t* col = makeBox(scrMsg);
  lv_obj_set_size(col, 400, LV_SIZE_CONTENT);
  lv_obj_center(col);
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_remove_flag(col, LV_OBJ_FLAG_CLICKABLE);

  lblMsgIcon = makeLabel(col, &font_icon_big);
  lblMsgTitle = makeLabel(col, &font_32);
  lv_obj_set_style_pad_top(lblMsgTitle, 16, 0);
  lblMsgText = makeLabel(col, &font_22);
  lv_obj_set_width(lblMsgText, 400);
  lv_label_set_long_mode(lblMsgText, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(lblMsgText, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_line_space(lblMsgText, 6, 0);
  lv_obj_set_style_pad_top(lblMsgText, 12, 0);
}

static void applyTheme() {
  const Theme& t = *theme;
  for (lv_obj_t* scr : {scrMain, scrMsg}) {
    lv_obj_set_style_bg_color(scr, t.bg, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  }
  lv_obj_set_style_text_color(lblTitle, t.text, 0);
  lv_obj_set_style_text_color(lblClock, t.muted, 0);
  lv_obj_set_style_text_color(lblUnit, t.muted, 0);
  lv_obj_set_style_text_color(lblIdleIcon, t.muted, 0);
  lv_obj_set_style_text_color(lblIdleText, t.text, 0);
  lv_obj_set_style_text_color(lblGrid, t.muted, 0);
  lv_obj_set_style_bg_color(barFill, t.solar, 0);
  for (int i = 0; i < 3; i++) {
    lv_obj_set_style_bg_color(tile[i], t.box, 0);
    lv_obj_set_style_text_color(tileLabel[i], t.muted, 0);
  }
  lv_obj_set_style_bg_color(segBg, t.box, 0);
  lv_obj_set_style_border_color(segBg, t.border, 0);
  lv_obj_set_style_text_color(lblMsgIcon, t.muted, 0);
  lv_obj_set_style_text_color(lblMsgTitle, t.text, 0);
  lv_obj_set_style_text_color(lblMsgText, t.muted, 0);
}

static void setVisible(lv_obj_t* o, bool visible) {
  if (visible)
    lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
  else
    lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static void renderSegments(ChargeMode shown, bool pending) {
  const Theme& t = *theme;
  for (int i = 0; i < 3; i++) {
    const bool active = SEG_MODES[i] == shown;
    lv_obj_set_style_bg_color(seg[i], active ? t.activeBg : t.box, 0);
    lv_obj_set_style_bg_opa(seg[i], active ? (pending ? LV_OPA_50 : LV_OPA_COVER) : LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(seg[i], active ? t.activeBg : t.pressed, LV_STATE_PRESSED);
    const bool filled = active && !pending;
    lv_obj_set_style_text_color(segLbl[i], filled ? t.activeFg : t.text, 0);
    lv_obj_set_style_text_color(segSub[i], filled ? t.activeFg : t.muted, 0);
    lv_obj_set_style_text_opa(segSub[i], filled ? LV_OPA_80 : LV_OPA_COVER, 0);
  }
}

static void renderStatus(const UiView& v, char* buf, size_t len, lv_color_t& color) {
  const EvccState& s = v.state;
  const Theme& t = *theme;
  color = t.muted;
  char tmp[16];

  if (v.flash) {
    color = t.error;
    snprintf(buf, len, "%s", v.flash);
  } else if (!v.wifiConnected) {
    color = t.error;
    snprintf(buf, len, "Keine WLAN-Verbindung");
  } else if (v.stale) {
    color = t.error;
    if (s.valid && v.lastUpdate[0])
      snprintf(buf, len, "EVCC nicht erreichbar · Stand %s", v.lastUpdate);
    else
      snprintf(buf, len, millis() > STALE_AFTER_MS ? "EVCC nicht erreichbar" : "Verbinde mit EVCC …");
  } else if (v.pendingMode != ChargeMode::None) {
    snprintf(buf, len, "Modus wird gesetzt …");
  } else if (!s.connected) {
    snprintf(buf, len, "Modus für die nächste Ladung wählen");
  } else if (s.modeOff) {
    color = t.warn;
    snprintf(buf, len, "Ladepunkt ist aus – Modus wählen");
  } else if (s.charging) {
    int n = snprintf(buf, len, "Lädt");
    if (s.phasesActive > 0) n += snprintf(buf + n, len - n, " · %d-phasig", s.phasesActive);
    if (s.vehicleSoc > 0) n += snprintf(buf + n, len - n, " · Akku %d %%", (int)lroundf(s.vehicleSoc));
    if (!strcmp(s.pvAction, "disable") && s.pvRemaining > 0) {
      formatCountdown(tmp, sizeof(tmp), s.pvRemaining);
      snprintf(buf + n, len - n, " · stoppt in %s", tmp);
      color = t.warn;
    }
  } else if (!strcmp(s.pvAction, "enable") && s.pvRemaining > 0) {
    formatCountdown(tmp, sizeof(tmp), s.pvRemaining);
    color = t.solarText;
    snprintf(buf, len, "Genug Sonne – Start in %s", tmp);
  } else if (s.mode == ChargeMode::PV) {
    snprintf(buf, len, "Wartet auf Sonnenüberschuss");
  } else if (s.planActive || s.smartCostActive) {
    snprintf(buf, len, "Wartet auf günstigen Zeitpunkt");
  } else {
    snprintf(buf, len, "Verbunden · lädt gerade nicht");
  }
}

void ui::render(const UiView& v) {
  lastView = v;
  hasLastView = true;
  const EvccState& s = v.state;
  const Theme& t = *theme;
  const bool live = s.valid && !v.stale;
  char buf[64];

  lv_label_set_text(lblTitle, s.valid ? s.title : "EVCC");
  lv_label_set_text(lblClock, v.clock);
  lv_label_set_text(lblIcons, !v.wifiConnected ? ICON_WIFI_OFF : v.stale ? ICON_ALERT : "");
  lv_obj_set_style_text_color(lblIcons, t.error, 0);
  lv_obj_align_to(lblIcons, lblClock, LV_ALIGN_OUT_LEFT_MID, -10, 0);

  // Hauptwert
  const bool showPower = s.valid && s.connected;
  setVisible(contPower, showPower);
  setVisible(contIdle, !showPower);
  if (showPower) {
    formatDecimal(buf, sizeof(buf), s.chargePower / 1000.0f);
    lv_label_set_text(lblPower, buf);
    lv_obj_set_style_text_color(lblPower, live && s.charging ? t.text : t.muted, 0);
  } else if (!s.valid) {
    const bool starting = millis() < STALE_AFTER_MS;
    lv_label_set_text(lblIdleIcon, !v.wifiConnected ? ICON_WIFI_OFF : starting ? ICON_PLUG : ICON_ALERT);
    lv_label_set_text(lblIdleText, !v.wifiConnected ? "Kein WLAN" : starting ? "Verbinde …" : "Keine Daten");
  } else {
    lv_label_set_text(lblIdleIcon, ICON_CAR_OFF);
    lv_label_set_text(lblIdleText, "Kein Fahrzeug");
  }

  lv_color_t statusColor;
  renderStatus(v, buf, sizeof(buf), statusColor);
  lv_label_set_text(lblStatus, buf);
  lv_obj_set_style_text_color(lblStatus, statusColor, 0);

  // Balken: aktueller Sonnenanteil der Ladeleistung
  const bool showBar = showPower && s.charging && s.greenShare >= 0;
  setVisible(barBlock, showBar);
  if (showBar) {
    const int pct = constrain((int)lroundf(s.greenShare * 100.0f), 0, 100);
    lv_obj_set_width(barFill, CONTENT_W * pct / 100);
    setVisible(barFill, pct > 0);
    lv_obj_set_style_bg_color(barBg, live ? t.grid : t.muted, 0);
    lv_obj_set_style_bg_opa(barBg, live ? LV_OPA_COVER : LV_OPA_40, 0);
    snprintf(buf, sizeof(buf), ICON_SUN " Jetzt %d %% Sonne", pct);
    lv_label_set_text(lblSolar, buf);
    lv_obj_set_style_text_color(lblSolar, live ? t.solarText : t.muted, 0);
    snprintf(buf, sizeof(buf), "%d %% Netz", 100 - pct);
    lv_label_set_text(lblGrid, buf);
    lv_obj_align(lblGrid, LV_ALIGN_TOP_RIGHT, 0, BAR_H + 8);
  }

  // Kacheln: Werte der laufenden bzw. letzten Sitzung
  const bool showTiles = showPower && (s.charging || s.chargedEnergy > 0);
  setVisible(tiles, showTiles);
  if (showTiles) {
    char num[16];
    formatDecimal(num, sizeof(num), s.chargedEnergy / 1000.0f);
    snprintf(buf, sizeof(buf), "%s kWh", num);
    lv_label_set_text(tileValue[0], buf);
    formatDuration(buf, sizeof(buf), s.chargeDuration);
    lv_label_set_text(tileValue[1], buf);
    snprintf(buf, sizeof(buf), "%d %%", (int)lroundf(s.sessionSolarPct));
    lv_label_set_text(tileValue[2], buf);
    for (int i = 0; i < 3; i++) lv_obj_set_style_text_color(tileValue[i], live ? t.text : t.muted, 0);
  }

  const bool pending = v.pendingMode != ChargeMode::None;
  renderSegments(pending ? v.pendingMode : s.mode, pending);
}

void ui::setNight(bool value) {
  if (value == night) return;
  night = value;
  theme = night ? &DARK : &LIGHT;
  applyTheme();
  if (hasLastView) render(lastView);
}

void ui::begin(ModeCallback onMode, Callback onSettingsRequested) {
  modeCb = onMode;
  settingsCb = onSettingsRequested;
  buildMain();
  buildMessage();
  applyTheme();
  render(UiView());
  lv_screen_load(scrMain);
}

void ui::showMessage(const char* icon, const char* title, const char* text) {
  lv_label_set_text(lblMsgIcon, icon);
  lv_label_set_text(lblMsgTitle, title);
  lv_label_set_text(lblMsgText, text);
  if (lv_screen_active() != scrMsg) lv_screen_load(scrMsg);
}

void ui::onMessageTap(Callback cb) { messageTapCb = cb; }

void ui::showMain() {
  if (lv_screen_active() != scrMain) lv_screen_load(scrMain);
}
