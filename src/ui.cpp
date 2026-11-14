#include "ui.h"

#include <lvgl.h>
#include <stdarg.h>

#include "config.h"
#include "fw_version.h"
#include "icons.h"

LV_FONT_DECLARE(font_16);
LV_FONT_DECLARE(font_18);
LV_FONT_DECLARE(font_22);
LV_FONT_DECLARE(font_24);
LV_FONT_DECLARE(font_26);
LV_FONT_DECLARE(font_32);
LV_FONT_DECLARE(font_big);
LV_FONT_DECLARE(font_icon_big);

// Farben angelehnt an die evcc-Weboberfläche
struct Theme {
  lv_color_t bg, box, text, muted, activeBg, activeFg, pressed, solar,
      solarText, grid, warn, error, border, battery, feedIn;
};

static const Theme LIGHT = {
    // Grau und Grün dunkler als in evcc: Kontrast ≥ 4,5:1, damit es aus 2–3 m
    // lesbar bleibt
    lv_color_hex(0xF3F3F7),
    lv_color_hex(0xFFFFFF),
    lv_color_hex(0x28293E),
    lv_color_hex(0x6E6F7C),
    lv_color_hex(0x28293E),
    lv_color_hex(0xFFFFFF),
    lv_color_hex(0xE4E4EC),
    lv_color_hex(0x0FDE41),
    lv_color_hex(0x078A2C),
    lv_color_hex(0x28293E),
    lv_color_hex(0xC26A00),
    lv_color_hex(0xD9380A),
    lv_color_hex(0xDCDCE4),
    // Akku und Einspeisung wie im Energiefluss von evcc (Einspeisung auf Hell
    // im dunkleren Gelb)
    lv_color_hex(0x0BA631),
    lv_color_hex(0xF6BB0F),
};
// Dunkles Navy wie evcc ab 0.317
static const Theme DARK = {
    lv_color_hex(0x010322), lv_color_hex(0x151630), lv_color_hex(0xFFFFFF),
    lv_color_hex(0xB5B6C3), lv_color_hex(0xFFFFFF), lv_color_hex(0x010322),
    lv_color_hex(0x2A2C4C), lv_color_hex(0x0FDE41), lv_color_hex(0x0FDE41),
    lv_color_hex(0x93949E), lv_color_hex(0xFF9000), lv_color_hex(0xFC440F),
    lv_color_hex(0x33354F), lv_color_hex(0x0BA631), lv_color_hex(0xFAF000),
};

static const Theme *theme = &LIGHT;
static bool night = false;
static UiView lastView;
static bool hasLastView = false;

static ui::ModeCallback modeCb;
static ui::Callback settingsCb;
static ui::Callback messageTapCb;
static ui::SwipeCallback swipeCb;
static ui::Callback boostCb;
static ui::Callback vehicleCb;
static ui::LimitCallback limitCb;

// Raster für 480x480
static constexpr int MARGIN = 24;
static constexpr int HEADER_H = 64;
static constexpr int SEG_H = 84;
static constexpr int SEG_Y = 480 - MARGIN - SEG_H; // Modusumschalter unten
static constexpr int CONTENT_W = 480 - 2 * MARGIN;
static constexpr int BAR_H =
    40; // Energiebalken wie in evcc, Werte stehen im Balken
static constexpr int BAR_PAD =
    6; // Mindestabstand des Werts zum Rand seines Abschnitts
static constexpr int TILE_GAP = 12;
static constexpr int TILE_H = 64;
static constexpr int DOT = 10;
static constexpr int DOTS_SHIFT =
    12; // so weit rückt der Modusumschalter für die Punkte nach oben
static constexpr int BOOST_PAD_H =
    12; // Innenabstand der Akku-Anzeige als Boost-Knopf
static constexpr int BOOST_PAD_V = 4;
static constexpr int LIMIT_GAP = 14; // Abstand zwischen Fahrzeug und Ladelimit
static constexpr int LIMIT_PAD = 14;
static constexpr int SLIDE_PX =
    48; // Weg der Inhalte beim Wechsel des Ladepunkts
static constexpr int MAX_PAGES =
    MAX_LOADPOINTS + 1; // Ladepunkte und die Solarseite
// Solarseite: Balken je Stunde, mindestens 6–20 Uhr, im Sommer so weit wie die
// Sonne reicht
static constexpr int CHART_H = 84;
static constexpr int CHART_MAX = 24;
static constexpr int CHART_AXIS = 8; // Beschriftung alle drei Stunden
static constexpr int CHART_MIN = 4;  // Stummel für Stunden ohne Erzeugung
static constexpr float CHART_NONE =
    0.05f; // kWh, darunter gilt eine Stunde als leer
static constexpr int PLANT_TRACK_H = 8;
static constexpr int PLANT_GAP = 10;
static int plantRowH() {
  return font_24.line_height + 5 + PLANT_TRACK_H;
} // Name und Wert, darunter der Balken

// Hauptbildschirm
static lv_obj_t *scrMain;
static lv_obj_t *lblTitle;
static lv_obj_t *mid;
static lv_obj_t *dots;
static lv_obj_t *dot[MAX_PAGES];
static lv_obj_t *lblIcons;
static lv_obj_t
    *btnBattery; // Hülle um die Akku-Anzeige; mit Battery Boost ein Knopf
static lv_obj_t *lblBattery;
static lv_obj_t *lblClock;
static lv_obj_t *contPower;
static lv_obj_t *lblPower;
static lv_obj_t *lblUnit;
static lv_obj_t *lblBolt;
static lv_obj_t *contIdle;
static lv_obj_t *lblIdleIcon;
static lv_obj_t *lblIdleText;
static lv_obj_t *rowVehicle; // Fahrzeug und Ladelimit nebeneinander
static lv_obj_t
    *btnVehicle; // Hülle um die Fahrzeugzeile; antippen wechselt das Fahrzeug
static lv_obj_t *lblVehicle;
static lv_obj_t *btnLimit; // Ladelimit; antippen öffnet die Auswahl
static lv_obj_t *lblLimit;
static lv_obj_t *lblStatus;
static lv_obj_t *barBlock;
static lv_obj_t *barBg;
// Abschnitte wie im Energiefluss von evcc: Netzbezug, Sonne, Akku entlädt,
// Einspeisung
enum FlowPart { FLOW_GRID, FLOW_SUN, FLOW_BATTERY, FLOW_FEEDIN, FLOW_COUNT };
static lv_obj_t *flowPart[FLOW_COUNT];
static lv_obj_t *flowLbl[FLOW_COUNT];
// Solarseite: Stundenbalken und Anlagenzeilen sind je ein Objekt, das sich
// selbst zeichnet. Einzelne Objekte je Balken kosteten rund 19 KB internen RAM
// – auch bei ausgeschalteter Seite.
static lv_obj_t *chartBlock;
struct ChartBar {
  int16_t x, height, filled; // filled: bisher Erzeugtes der laufenden Stunde
                             // über ihrer Prognose, 0 = keins
  bool solid;                // vergangene Stunde mit Erzeugung
};
static struct {
  int count, width;
  ChartBar bar[CHART_MAX];
  int axisCount;
  int16_t axisX[CHART_AXIS];
  char axisText[CHART_AXIS][4];
} chartModel;
static lv_obj_t *plantsBlock;
static struct {
  int count;
  char name[MAX_PV_PLANTS][32]; // gekürzt, falls zu lang
  char value[MAX_PV_PLANTS][16];
  int16_t filled[MAX_PV_PLANTS];
} plantModel;
static lv_obj_t *tiles;
static lv_obj_t *tile[3];
static lv_obj_t *tileLabel[3];
static lv_obj_t *tileValue[3];
// Beschriftungen wie in evcc: klein, grau, in Großbuchstaben
static const char *TILE_TEXT[3] = {"GELADEN", "DAUER", "Ø SONNE"};
static const char *HEATING_TILE_TEXT[3] = {"ENERGIE", "DAUER", "Ø SONNE"};
static lv_obj_t *segBg;
static lv_obj_t *seg[3];
static lv_obj_t *segLbl[3];
static lv_obj_t *segSub[3];
static const ChargeMode SEG_MODES[3] = {ChargeMode::Smart, ChargeMode::Slow,
                                        ChargeMode::Fast};
// Nur Text wie in evcc, dazu eine Zeile, die sagt, was der Modus tut.
// "Smart" heißt wie in evcc; "Langsam" ist Smart mit "Immer laden".
static const char *SEG_TEXT[3] = {"Smart", "Langsam", "Schnell"};
static const char *SEG_SUB[3] = {"nur Sonne", "lädt immer", "volle Leistung"};

// Hinweisbildschirm
// Auswahl des Ladelimits: liegt über dem Hauptbildschirm
static constexpr int PICK_MAX = 9;
static constexpr int PICK_GAP = 12;
static constexpr int PICK_TOP = 112;
static constexpr int PICK_CANCEL_H = 64;
static constexpr uint32_t PICK_TIMEOUT_MS =
    30000; // ohne Auswahl schließt sie sich wieder
static const float PICK_SOC[] = {50, 60, 70, 80, 90, 100};
static const float PICK_ENERGY[] = {5,  10, 15, 20, 30,
                                    40, 50, 60, 0}; // kWh, 0 = kein Limit
static lv_obj_t *picker;
static lv_obj_t *lblPickTitle;
static lv_obj_t *lblPickSub;
static lv_obj_t *pickBtn[PICK_MAX];
static lv_obj_t *pickLbl[PICK_MAX];
static lv_obj_t *btnPickCancel;
static lv_obj_t *lblPickCancel;
static lv_timer_t *pickTimer;
static bool pickerOpen = false;
static bool pickerEnergy = false; // Auswahl in kWh statt in Prozent

static lv_obj_t *scrMsg;
static lv_obj_t *lblMsgIcon;
static lv_obj_t *lblMsgTitle;
static lv_obj_t *lblMsgText;
static lv_obj_t *lblMsgVersion;

static lv_obj_t *makeLabel(lv_obj_t *parent, const lv_font_t *font) {
  lv_obj_t *l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font, 0);
  lv_label_set_text(l, "");
  return l;
}

static lv_obj_t *makeBox(lv_obj_t *parent) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  return o;
}

static void formatDecimal(char *buf, size_t len, float value) {
  snprintf(buf, len, "%.1f", value);
  for (char *p = buf; *p; p++)
    if (*p == '.')
      *p = ',';
}

static void formatDuration(char *buf, size_t len, uint32_t seconds) {
  snprintf(buf, len, "%u:%02u h", (unsigned)(seconds / 3600),
           (unsigned)((seconds / 60) % 60));
}

static void formatCountdown(char *buf, size_t len, uint32_t seconds) {
  snprintf(buf, len, "%u:%02u", (unsigned)(seconds / 60),
           (unsigned)(seconds % 60));
}

static void onSegment(lv_event_t *e) {
  const intptr_t i = (intptr_t)lv_event_get_user_data(e);
  if (modeCb)
    modeCb(SEG_MODES[i]);
}

static void onHeaderLongPress(lv_event_t *) {
  if (settingsCb)
    settingsCb();
}

static void setTranslateX(void *o, int32_t v) {
  lv_obj_set_style_translate_x((lv_obj_t *)o, v, 0);
}

static void setOpa(void *o, int32_t v) {
  lv_obj_set_style_opa((lv_obj_t *)o, v, 0);
}

// Neuer Ladepunkt: Titel und Mitte gleiten aus der Wischrichtung herein
static void slideIn(int dir) {
  for (lv_obj_t *o : {lblTitle, mid}) {
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, o);
    lv_anim_set_duration(&a, 220);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a, setTranslateX);
    lv_anim_set_values(&a, dir * SLIDE_PX, 0);
    lv_anim_start(&a);
    lv_anim_set_exec_cb(&a, setOpa);
    lv_anim_set_values(&a, LV_OPA_0, LV_OPA_COVER);
    lv_anim_start(&a);
  }
}

// Am ersten bzw. letzten Ladepunkt kurz anstoßen, damit man sieht, dass es
// nicht weitergeht
static void bounce(int dir) {
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, mid);
  lv_anim_set_exec_cb(&a, setTranslateX);
  lv_anim_set_duration(&a, 120);
  lv_anim_set_playback_duration(&a, 160);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
  lv_anim_set_values(&a, 0, -dir * 20);
  lv_anim_start(&a);
}

static void onGesture(lv_event_t *) {
  if (!hasLastView || lastView.pageCount < 2)
    return;
  lv_indev_t *indev = lv_indev_active();
  const lv_dir_t dir = lv_indev_get_gesture_dir(indev);
  if (dir != LV_DIR_LEFT && dir != LV_DIR_RIGHT)
    return;
  lv_indev_wait_release(
      indev); // kein Klick auf eine Modustaste, wenn das Wischen dort begann
  const int delta = dir == LV_DIR_LEFT ? 1 : -1;
  const int next = lastView.page + delta;
  if (next < 0 || next >= lastView.pageCount)
    bounce(delta);
  else if (swipeCb)
    swipeCb(delta);
}

// Icon neben der großen Zahl: Blitz beim Laden, Sonne beim Überschuss ohne
// Fahrzeug. Die Glyphen haben breite Ränder: näher an "kW" rücken und optisch
// auf die Grundlinie stellen.
static void setBigIcon(const char *icon) {
  const bool sun = !strcmp(icon, ICON_SUN);
  const int lh = font_icon_big.line_height;
  lv_label_set_text(lblBolt, icon);
  lv_obj_set_style_margin_left(lblBolt, sun ? -lh / 8 : -lh / 5, 0);
  lv_obj_set_style_pad_bottom(lblBolt,
                              font_big.base_line - font_icon_big.base_line +
                                  (sun ? lh / 12 : lh / 9),
                              0);
}

static void onBatteryClicked(lv_event_t *) {
  if (boostCb)
    boostCb();
}

static void onVehicleClicked(lv_event_t *) {
  if (vehicleCb)
    vehicleCb();
}

static void renderPicker();

static void closePicker() {
  if (!pickerOpen)
    return;
  pickerOpen = false;
  lv_obj_add_flag(picker, LV_OBJ_FLAG_HIDDEN);
  lv_timer_pause(pickTimer);
}

static void openPicker() {
  if (!hasLastView)
    return;
  pickerOpen = true;
  pickerEnergy = !lastView.state.socBased;
  renderPicker();
  lv_obj_remove_flag(picker, LV_OBJ_FLAG_HIDDEN);
  lv_timer_reset(pickTimer);
  lv_timer_resume(pickTimer);
}

static void onLimitClicked(lv_event_t *) { openPicker(); }

static void onPickCancel(lv_event_t *) { closePicker(); }

static void onPick(lv_event_t *e) {
  const int i = (int)(intptr_t)lv_event_get_user_data(e);
  const bool energy = pickerEnergy;
  closePicker();
  if (limitCb)
    limitCb(energy, (energy ? PICK_ENERGY : PICK_SOC)[i]);
}

// Fahrzeugzeile: so breit wie ihr Text, damit die Hülle beim Antippen genau ihn
// umschließt. Nur ein zu langer Text bekommt die volle Breite und wird mit "…"
// gekürzt – mit fester Kürzung und Breite nach Inhalt zusammen bliebe vom Text
// nur "…" übrig.
static void setVehicleText(const char *text, int reserved = 0) {
  const int MAX_W = CONTENT_W - 24 - reserved;
  lv_point_t size;
  lv_text_get_size(&size, text, &font_22, 0, 0, LV_COORD_MAX,
                   LV_TEXT_FLAG_NONE);
  const bool tooLong = size.x > MAX_W;
  lv_label_set_long_mode(lblVehicle,
                         tooLong ? LV_LABEL_LONG_DOT : LV_LABEL_LONG_CLIP);
  lv_obj_set_width(lblVehicle, tooLong ? MAX_W : LV_SIZE_CONTENT);
  lv_label_set_text(lblVehicle, text);
}

static void drawRect(lv_layer_t *layer, int x, int y, int w, int h, int radius,
                     lv_color_t color) {
  lv_draw_rect_dsc_t dsc;
  lv_draw_rect_dsc_init(&dsc);
  dsc.radius = radius;
  dsc.bg_color = color;
  dsc.bg_opa = LV_OPA_COVER;
  const lv_area_t area = {x, y, x + w - 1, y + h - 1};
  lv_draw_rect(layer, &dsc, &area);
}

// text muss bis zum Ende des Zeichnens bestehen bleiben (steht in chartModel
// bzw. plantModel)
static void drawText(lv_layer_t *layer, const char *text, const lv_font_t *font,
                     lv_color_t color, int x, int y, int w,
                     lv_text_align_t align) {
  lv_draw_label_dsc_t dsc;
  lv_draw_label_dsc_init(&dsc);
  dsc.font = font;
  dsc.color = color;
  dsc.text = text;
  dsc.align = align;
  const lv_area_t area = {x, y, x + w - 1, y + font->line_height - 1};
  lv_draw_label(layer, &dsc, &area);
}

static void onChartDraw(lv_event_t *e) {
  const Theme &t = *theme;
  lv_layer_t *layer = lv_event_get_layer(e);
  lv_area_t c;
  lv_obj_get_content_coords((lv_obj_t *)lv_event_get_target(e), &c);
  const int base = c.y1 + CHART_H;
  for (int i = 0; i < chartModel.count; i++) {
    const ChartBar &bar = chartModel.bar[i];
    drawRect(layer, c.x1 + bar.x, base - bar.height, chartModel.width,
             bar.height, 6, bar.solid ? t.solar : t.border);
    if (bar.filled)
      drawRect(layer, c.x1 + bar.x, base - bar.filled, chartModel.width,
               bar.filled, 6, t.solar);
  }
  for (int i = 0; i < chartModel.axisCount; i++)
    drawText(layer, chartModel.axisText[i], &font_16, t.muted,
             c.x1 + chartModel.axisX[i] - 20, base + 5, 40,
             LV_TEXT_ALIGN_CENTER);
}

static void onPlantsDraw(lv_event_t *e) {
  const Theme &t = *theme;
  lv_layer_t *layer = lv_event_get_layer(e);
  lv_area_t c;
  lv_obj_get_content_coords((lv_obj_t *)lv_event_get_target(e), &c);
  // Name und der größere Wert auf gemeinsamer Grundlinie
  const int nameY = (font_24.line_height - font_24.base_line) -
                    (font_22.line_height - font_22.base_line);
  for (int i = 0; i < plantModel.count; i++) {
    const int y = c.y1 + i * (plantRowH() + PLANT_GAP);
    drawText(layer, plantModel.name[i], &font_22, t.text, c.x1, y + nameY,
             CONTENT_W, LV_TEXT_ALIGN_LEFT);
    drawText(layer, plantModel.value[i], &font_24, t.text, c.x1, y, CONTENT_W,
             LV_TEXT_ALIGN_RIGHT);
    const int trackY = y + font_24.line_height + 5;
    drawRect(layer, c.x1, trackY, CONTENT_W, PLANT_TRACK_H, PLANT_TRACK_H / 2,
             t.border);
    if (plantModel.filled[i])
      drawRect(layer, c.x1, trackY, plantModel.filled[i], PLANT_TRACK_H,
               PLANT_TRACK_H / 2, t.solar);
  }
}

static void buildMain() {
  scrMain = lv_obj_create(nullptr);
  lv_obj_remove_flag(scrMain, LV_OBJ_FLAG_SCROLLABLE);

  // Kopfzeile (Titel lange drücken → Einstellungen)
  lv_obj_t *header = makeBox(scrMain);
  lv_obj_set_size(header, 300, HEADER_H);
  lv_obj_add_flag(header, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(header, onHeaderLongPress, LV_EVENT_LONG_PRESSED,
                      nullptr);
  lblTitle = makeLabel(header, &font_26);
  lv_obj_align(lblTitle, LV_ALIGN_LEFT_MID, MARGIN + 4, 4);

  lblClock = makeLabel(scrMain, &font_26);
  lv_obj_align(lblClock, LV_ALIGN_TOP_RIGHT, -MARGIN - 4, 0);
  lv_obj_set_style_pad_top(lblClock, (HEADER_H - font_26.line_height) / 2 + 4,
                           0);
  lblIcons = makeLabel(scrMain, &font_26);
  // Ladestand des Hausakkus – gehört zum Haus, bleibt beim Wischen stehen.
  // Mit Battery Boost (Einstellung) wird daraus im Modus Smart ein Knopf.
  btnBattery = makeBox(scrMain);
  lv_obj_set_size(btnBattery, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_radius(btnBattery, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_ext_click_area(btnBattery, 16);
  lv_obj_add_event_cb(btnBattery, onBatteryClicked, LV_EVENT_CLICKED, nullptr);
  lblBattery = makeLabel(btnBattery, &font_26);

  // Mittelteil: Inhalt vertikal zentriert zwischen Kopfzeile und
  // Modusumschalter, ausgeblendete Elemente nehmen keinen Platz ein
  lv_obj_add_event_cb(scrMain, onGesture, LV_EVENT_GESTURE, nullptr);

  mid = makeBox(scrMain);
  lv_obj_set_pos(mid, 0, HEADER_H);
  lv_obj_set_size(mid, 480, SEG_Y - HEADER_H);
  lv_obj_set_flex_flow(mid, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(mid, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  // Kleine Zeile über der großen Zahl: Fahrzeug mit Ladestand bzw. "Kein
  // Fahrzeug" über dem Überschuss. Sind in evcc Fahrzeuge angelegt, lässt sich
  // das Fahrzeug hier per Antippen wechseln.
  rowVehicle = makeBox(mid);
  lv_obj_set_size(rowVehicle, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(rowVehicle, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(rowVehicle, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(rowVehicle, LIMIT_GAP, 0);
  lv_obj_set_style_margin_bottom(rowVehicle, 2, 0);
  btnVehicle = makeBox(rowVehicle);
  lv_obj_set_size(btnVehicle, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_radius(btnVehicle, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_pad_hor(btnVehicle, 12, 0);
  lv_obj_set_style_pad_ver(btnVehicle, 2, 0);
  lv_obj_set_ext_click_area(btnVehicle, 12);
  lv_obj_add_event_cb(btnVehicle, onVehicleClicked, LV_EVENT_CLICKED, nullptr);
  lblVehicle = makeLabel(btnVehicle, &font_22);
  lv_obj_set_style_text_align(lblVehicle, LV_TEXT_ALIGN_CENTER, 0);
  // Ladelimit als eigene Fläche daneben (Einstellung): mit Rahmen, damit sie
  // als Taste erkennbar ist
  btnLimit = makeBox(rowVehicle);
  lv_obj_set_size(btnLimit, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_radius(btnLimit, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_pad_hor(btnLimit, LIMIT_PAD, 0);
  lv_obj_set_style_pad_ver(btnLimit, 2, 0);
  lv_obj_set_style_border_width(btnLimit, 1, 0);
  lv_obj_set_style_bg_opa(btnLimit, LV_OPA_COVER, 0);
  lv_obj_set_ext_click_area(btnLimit, 12);
  lv_obj_add_flag(btnLimit, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(btnLimit, onLimitClicked, LV_EVENT_CLICKED, nullptr);
  lblLimit = makeLabel(btnLimit, &font_22);

  // Ladeleistung – Zahl und Einheit auf gemeinsamer Grundlinie
  contPower = makeBox(mid);
  lv_obj_set_size(contPower, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(contPower, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(contPower, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END,
                        LV_FLEX_ALIGN_END);
  lv_obj_set_style_pad_column(contPower, 12, 0);
  lblPower = makeLabel(contPower, &font_big);
  lblUnit = makeLabel(contPower, &font_32);
  lv_label_set_text(lblUnit, "kW");
  lv_obj_set_style_pad_bottom(lblUnit, font_big.base_line - font_32.base_line,
                              0);
  // Grüner Blitz wie in evcc, solange geladen wird; unsichtbar statt
  // ausgeblendet, damit die Zahl nicht springt
  lblBolt = makeLabel(contPower, &font_icon_big);
  setBigIcon(ICON_BOLT);

  // Anzeige ohne Fahrzeug / ohne Daten
  contIdle = makeBox(mid);
  lv_obj_set_size(contIdle, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(contIdle, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(contIdle, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(contIdle, 20, 0);
  lblIdleIcon = makeLabel(contIdle, &font_icon_big);
  lblIdleText = makeLabel(contIdle, &font_32);

  // Statuszeile
  lblStatus = makeLabel(mid, &font_22);
  lv_obj_set_width(lblStatus, CONTENT_W);
  lv_label_set_long_mode(lblStatus, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_align(lblStatus, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_pad_top(lblStatus, 4, 0);

  // Energiebalken wie in evcc: woher der Strom gerade kommt und was ins Netz
  // geht
  barBlock = makeBox(mid);
  lv_obj_set_size(barBlock, CONTENT_W, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_top(barBlock, 12, 0);
  barBg = makeBox(barBlock);
  lv_obj_set_size(barBg, CONTENT_W, BAR_H);
  lv_obj_set_style_radius(barBg, BAR_H / 2, 0);
  lv_obj_set_style_clip_corner(barBg, true, 0);
  for (int i = 0; i < FLOW_COUNT; i++) {
    flowPart[i] = makeBox(barBg);
    lv_obj_set_height(flowPart[i], BAR_H);
    lv_obj_set_style_bg_opa(flowPart[i], LV_OPA_COVER, 0);
    flowLbl[i] = makeLabel(flowPart[i], &font_22);
    lv_obj_center(flowLbl[i]);
  }

  // Solarseite: Erzeugung je Stunde – erzeugt (grün) und Prognose (grau)
  chartBlock = makeBox(mid);
  lv_obj_set_size(chartBlock, CONTENT_W,
                  14 + CHART_H + 5 + font_16.line_height);
  lv_obj_set_style_pad_top(chartBlock, 14, 0);
  lv_obj_add_event_cb(chartBlock, onChartDraw, LV_EVENT_DRAW_MAIN, nullptr);

  // Solarseite bei mehreren Anlagen: Leistung je Anlage mit Balken im
  // Verhältnis zur stärksten
  plantsBlock = makeBox(mid);
  lv_obj_set_width(plantsBlock, CONTENT_W);
  lv_obj_set_style_pad_top(plantsBlock, 16, 0);
  lv_obj_add_event_cb(plantsBlock, onPlantsDraw, LV_EVENT_DRAW_MAIN, nullptr);

  // Kacheln mit den Werten der Ladesitzung bzw. ohne Fahrzeug Sonne, Haus und
  // Akku
  tiles = makeBox(mid);
  lv_obj_set_size(tiles, CONTENT_W, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_top(tiles, 10, 0);
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
    lv_obj_set_pos(tileLabel[i], 0, 7);
    tileValue[i] = makeLabel(tile[i], &font_24);
    lv_obj_set_pos(tileValue[i], 0, 29);
  }

  // Modusumschalter wie in evcc: Pill-Gruppe, aktiver Modus gefüllt
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
    lv_obj_set_style_radius(seg[i], SEG_H / 2 - 6, 0);
    lv_obj_set_style_bg_opa(seg[i], LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_add_event_cb(seg[i], onSegment, LV_EVENT_CLICKED,
                        (void *)(intptr_t)i);
    lv_obj_t *col = makeBox(seg[i]);
    lv_obj_remove_flag(col, LV_OBJ_FLAG_CLICKABLE); // Klicks gehen an die Taste
    lv_obj_set_size(col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_center(col);
    segLbl[i] = makeLabel(col, &font_24);
    lv_label_set_text(segLbl[i], SEG_TEXT[i]);
    segSub[i] = makeLabel(col, &font_16);
    lv_label_set_text(segSub[i], SEG_SUB[i]);
  }

  // Breite nach Textlänge statt gleicher Drittel: jede Taste bekommt ihren Text
  // plus gleich viel Rand, damit auch "Langsam" und "volle Leistung" in der
  // gefüllten Taste nicht an den Rand stoßen
  int textW[3], sum = 0;
  for (int i = 0; i < 3; i++) {
    lv_point_t a, b;
    lv_text_get_size(&a, SEG_TEXT[i], &font_24, 0, 0, LV_COORD_MAX,
                     LV_TEXT_FLAG_NONE);
    lv_text_get_size(&b, SEG_SUB[i], &font_16, 0, 0, LV_COORD_MAX,
                     LV_TEXT_FLAG_NONE);
    textW[i] = LV_MAX(a.x, b.x);
    sum += textW[i];
  }
  const int inner =
      CONTENT_W - 2 * 6 - 2 * 6 - 2; // Innenabstand, Lücken, Rahmen
  const int extra = (inner - sum) / 3;
  for (int i = 0; i < 3; i++)
    lv_obj_set_width(seg[i], textW[i] + extra);

  // Punkte unter dem Modusumschalter, nur bei mehreren Ladepunkten
  dots = makeBox(scrMain);
  lv_obj_set_size(dots, LV_SIZE_CONTENT, DOT);
  lv_obj_align(dots, LV_ALIGN_BOTTOM_MID, 0, -(MARGIN + DOTS_SHIFT - DOT) / 2);
  lv_obj_set_flex_flow(dots, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(dots, 12, 0);
  for (int i = 0; i < MAX_PAGES; i++) {
    dot[i] = makeBox(dots);
    lv_obj_set_size(dot[i], DOT, DOT);
    lv_obj_set_style_radius(dot[i], LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(dot[i], LV_OPA_COVER, 0);
  }
}

static void buildPicker() {
  picker = makeBox(scrMain);
  lv_obj_set_size(picker, 480, 480);
  lv_obj_set_style_bg_opa(picker, LV_OPA_COVER, 0);
  lv_obj_add_flag(picker, LV_OBJ_FLAG_CLICKABLE); // fängt Berührungen ab
  lv_obj_remove_flag(
      picker,
      LV_OBJ_FLAG_GESTURE_BUBBLE); // kein Wischen zum nächsten Ladepunkt
  lv_obj_add_flag(picker, LV_OBJ_FLAG_HIDDEN);

  lblPickTitle = makeLabel(picker, &font_26);
  lv_label_set_text(lblPickTitle, "Ladelimit");
  lv_obj_set_pos(lblPickTitle, MARGIN + 4,
                 (HEADER_H - font_26.line_height) / 2 + 4);
  lblPickSub = makeLabel(picker, &font_22);
  lv_obj_set_width(lblPickSub, CONTENT_W - 8);
  lv_label_set_long_mode(lblPickSub, LV_LABEL_LONG_DOT);
  lv_obj_set_pos(lblPickSub, MARGIN + 4, HEADER_H + 4);

  for (int i = 0; i < PICK_MAX; i++) {
    pickBtn[i] = makeBox(picker);
    lv_obj_set_style_radius(pickBtn[i], 20, 0);
    lv_obj_set_style_bg_opa(pickBtn[i], LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(pickBtn[i], 1, 0);
    lv_obj_add_event_cb(pickBtn[i], onPick, LV_EVENT_CLICKED,
                        (void *)(intptr_t)i);
    pickLbl[i] = makeLabel(pickBtn[i], &font_26);
    lv_obj_center(pickLbl[i]);
  }

  btnPickCancel = makeBox(picker);
  lv_obj_set_size(btnPickCancel, CONTENT_W, PICK_CANCEL_H);
  lv_obj_set_pos(btnPickCancel, MARGIN, 480 - MARGIN - PICK_CANCEL_H);
  lv_obj_set_style_radius(btnPickCancel, PICK_CANCEL_H / 2, 0);
  lv_obj_set_style_border_width(btnPickCancel, 1, 0);
  lv_obj_set_style_bg_opa(btnPickCancel, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_add_flag(btnPickCancel, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(btnPickCancel, onPickCancel, LV_EVENT_CLICKED, nullptr);
  lblPickCancel = makeLabel(btnPickCancel, &font_24);
  lv_label_set_text(lblPickCancel, "Abbrechen");
  lv_obj_center(lblPickCancel);

  pickTimer = lv_timer_create([](lv_timer_t *) { closePicker(); },
                              PICK_TIMEOUT_MS, nullptr);
  lv_timer_pause(pickTimer);
}

static void onMessageClicked(lv_event_t *) {
  if (messageTapCb)
    messageTapCb();
}

static void buildMessage() {
  scrMsg = lv_obj_create(nullptr);
  lv_obj_remove_flag(scrMsg, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(scrMsg, onMessageClicked, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *col = makeBox(scrMsg);
  lv_obj_set_size(col, 400, LV_SIZE_CONTENT);
  lv_obj_center(col);
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
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

  // Klein am unteren Rand, damit man ohne Browser sieht, ob ein Update
  // angekommen ist
  lblMsgVersion = makeLabel(scrMsg, &font_16);
  lv_label_set_text(lblMsgVersion, "Firmware " FW_VERSION);
  lv_obj_align(lblMsgVersion, LV_ALIGN_BOTTOM_MID, 0, -20);
}

static void applyTheme() {
  const Theme &t = *theme;
  for (lv_obj_t *scr : {scrMain, scrMsg}) {
    lv_obj_set_style_bg_color(scr, t.bg, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  }
  lv_obj_set_style_text_color(lblTitle, t.text, 0);
  lv_obj_set_style_text_color(lblClock, t.muted, 0);
  lv_obj_set_style_text_color(lblUnit, t.muted, 0);
  lv_obj_set_style_text_color(lblBolt, t.solar, 0);
  lv_obj_set_style_text_color(lblIdleIcon, t.muted, 0);
  lv_obj_set_style_text_color(lblIdleText, t.text, 0);
  lv_obj_set_style_text_color(lblVehicle, t.muted, 0);
  lv_obj_set_style_bg_color(btnVehicle, t.pressed, LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(btnVehicle, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_set_style_text_color(lblBattery, t.muted, 0);
  lv_obj_set_style_text_color(lblLimit, t.text, 0);
  lv_obj_set_style_bg_color(btnLimit, t.box, 0);
  lv_obj_set_style_border_color(btnLimit, t.border, 0);
  lv_obj_set_style_bg_color(btnLimit, t.pressed, LV_STATE_PRESSED);
  lv_obj_set_style_bg_color(picker, t.bg, 0);
  lv_obj_set_style_text_color(lblPickTitle, t.text, 0);
  lv_obj_set_style_text_color(lblPickSub, t.muted, 0);
  lv_obj_set_style_border_color(btnPickCancel, t.border, 0);
  lv_obj_set_style_bg_color(btnPickCancel, t.pressed, LV_STATE_PRESSED);
  lv_obj_set_style_text_color(lblPickCancel, t.text, 0);
  for (int i = 0; i < 3; i++) {
    lv_obj_set_style_bg_color(tile[i], t.box, 0);
    lv_obj_set_style_text_color(tileLabel[i], t.muted, 0);
  }
  lv_obj_set_style_bg_color(segBg, t.box, 0);
  lv_obj_set_style_border_color(segBg, t.border, 0);
  lv_obj_set_style_text_color(lblMsgIcon, t.muted, 0);
  lv_obj_set_style_text_color(lblMsgTitle, t.text, 0);
  lv_obj_set_style_text_color(lblMsgText, t.muted, 0);
  lv_obj_set_style_text_color(lblMsgVersion, t.muted, 0);
}

static void setVisible(lv_obj_t *o, bool visible) {
  if (visible)
    lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
  else
    lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static void renderSegments(ChargeMode shown, bool pending) {
  const Theme &t = *theme;
  for (int i = 0; i < 3; i++) {
    const bool active = SEG_MODES[i] == shown;
    lv_obj_set_style_bg_color(seg[i], active ? t.activeBg : t.box, 0);
    lv_obj_set_style_bg_opa(
        seg[i], active ? (pending ? LV_OPA_50 : LV_OPA_COVER) : LV_OPA_TRANSP,
        0);
    lv_obj_set_style_bg_color(seg[i], active ? t.activeBg : t.pressed,
                              LV_STATE_PRESSED);
    const bool filled = active && !pending;
    lv_obj_set_style_text_color(segLbl[i], filled ? t.activeFg : t.text, 0);
    lv_obj_set_style_text_color(segSub[i], filled ? t.activeFg : t.muted, 0);
    lv_obj_set_style_text_opa(segSub[i], filled ? LV_OPA_80 : LV_OPA_COVER, 0);
  }
}

// Was ein Fahrzeug im Modus Smart bekäme: die Einspeisung und, sobald der
// Hausakku seinen Vorrang (prioritySoc in evcc) erreicht hat, auch das, was
// gerade in den Akku geht. Ohne Netzleistung: PV minus Hausverbrauch
static float surplus(const EvccState &s) {
  const bool batteryYields = s.batterySoc >= 0 && s.batterySoc >= s.prioritySoc;
  const float battery =
      batteryYields ? s.batteryPower : LV_MAX(0.0f, s.batteryPower);
  return LV_MAX(0.0f,
                s.hasGrid ? -(s.gridPower + battery) : s.pvPower - s.homePower);
}

// Energiebalken wie in evcc: Netzbezug, selbst genutzte Sonne, Akku entlädt,
// Einspeisung – zusammen das, was gerade erzeugt und bezogen wird. Werte stehen
// im Abschnitt, sofern sie hineinpassen. false, wenn nichts darzustellen ist.
static bool renderFlow(const EvccState &s, bool live) {
  const Theme &t = *theme;
  float w[FLOW_COUNT];
  if (s.hasGrid) {
    w[FLOW_GRID] = LV_MAX(0.0f, s.gridPower);
    w[FLOW_FEEDIN] = LV_MAX(0.0f, -s.gridPower);
    w[FLOW_SUN] = LV_MAX(0.0f, LV_MAX(0.0f, s.pvPower) - w[FLOW_FEEDIN]);
    w[FLOW_BATTERY] = s.batterySoc >= 0 ? LV_MAX(0.0f, s.batteryPower) : 0;
  } else {
    w[FLOW_SUN] = s.chargePower * s.greenShare;
    w[FLOW_GRID] = s.chargePower - w[FLOW_SUN];
    w[FLOW_BATTERY] = w[FLOW_FEEDIN] = 0;
  }
  float total = 0;
  for (int i = 0; i < FLOW_COUNT; i++) {
    if (w[i] < 50)
      w[i] = 0; // Messrauschen nicht als Splitter zeigen
    total += w[i];
  }
  if (total <= 0)
    return false;

  const lv_color_t bg[FLOW_COUNT] = {t.grid, t.solar, t.battery, t.feedIn};
  // Dunkle Schrift auf Grün und Gelb; auf dem Netzbezug die Gegenfarbe (hell:
  // weiß auf Navy, dunkel: Navy auf Grau)
  const lv_color_t dark = lv_color_hex(0x28293E);
  const lv_color_t fg[FLOW_COUNT] = {night ? t.bg : lv_color_white(), dark,
                                     dark, dark};
  static const char *const ICONS[FLOW_COUNT] = {"", ICON_SUN " ",
                                                ICON_BATTERY " ", ""};
  float acc = 0;
  int x = 0;
  for (int i = 0; i < FLOW_COUNT; i++) {
    acc += w[i];
    const int x2 = (int)lroundf(
        acc / total *
        CONTENT_W); // aufsummiert, damit keine Rundungslücken entstehen
    const int width = x2 - x;
    setVisible(flowPart[i], width > 0);
    if (width <= 0)
      continue;
    lv_obj_set_pos(flowPart[i], x, 0);
    lv_obj_set_width(flowPart[i], width);
    x = x2;
    lv_obj_set_style_bg_color(flowPart[i], live ? bg[i] : t.muted, 0);
    lv_obj_set_style_bg_opa(flowPart[i], live ? LV_OPA_COVER : LV_OPA_40, 0);
    lv_obj_set_style_text_color(flowLbl[i], fg[i], 0);

    char num[16], buf[32];
    formatDecimal(num, sizeof(num), w[i] / 1000.0f);
    snprintf(buf, sizeof(buf), "%s%s kW", ICONS[i], num);
    lv_point_t size;
    lv_text_get_size(&size, buf, &font_22, 0, 0, LV_COORD_MAX,
                     LV_TEXT_FLAG_NONE);
    if (size.x + 2 * BAR_PAD >
        width) { // zu schmal: erst ohne Symbol, dann nur noch die Farbe
      snprintf(buf, sizeof(buf), "%s kW", num);
      lv_text_get_size(&size, buf, &font_22, 0, 0, LV_COORD_MAX,
                       LV_TEXT_FLAG_NONE);
      if (size.x + 2 * BAR_PAD > width)
        buf[0] = 0;
    }
    lv_label_set_text(flowLbl[i], live ? buf : "");
  }
  return true;
}

// Ohne Fahrzeug: Haus, Akku-Leistung und Solarprognose. Die Sonne steht schon
// im Balken und kommt nur in eine Kachel, wenn Akku oder Prognose fehlen.
static void renderHouseTiles(const EvccState &s, bool live) {
  const Theme &t = *theme;
  const bool battery = s.batterySoc >= 0;
  // Nach Sonnenuntergang ist vom heutigen Tag nichts mehr zu erwarten – dann
  // die Prognose für morgen
  const bool today =
      s.solarToday >= 1000 || (s.solarToday >= 0 && s.solarTomorrow < 0);
  const float forecast = today ? s.solarToday : s.solarTomorrow;
  const bool hasForecast = forecast >= 0;
  char num[16];
  int n = 0;
  auto add = [&](const char *label, const char *fmt) {
    lv_label_set_text(tileLabel[n], label);
    lv_label_set_text_fmt(tileValue[n], fmt, num);
    n++;
  };
  if (!battery || !hasForecast) {
    formatDecimal(num, sizeof(num), s.pvPower / 1000.0f);
    add("SONNE", "%s kW");
  }
  formatDecimal(num, sizeof(num), s.homePower / 1000.0f);
  add("HAUS", "%s kW");
  if (battery) { // Ladestand steht oben neben der Uhr, hier die Leistung
    const bool charging = s.batteryPower <= -50,
               discharging = s.batteryPower >= 50;
    formatDecimal(num, sizeof(num),
                  (charging || discharging ? fabsf(s.batteryPower) : 0) /
                      1000.0f);
    add(charging ? "AKKU LÄDT" : discharging ? "AKKU GIBT" : "AKKU", "%s kW");
  }
  if (hasForecast && n < 3) {
    formatDecimal(num, sizeof(num), forecast / 1000.0f);
    add(today ? "REST HEUTE" : "MORGEN", "%s kWh");
  }
  for (int i = 0; i < 3; i++) {
    setVisible(tile[i], i < n);
    lv_obj_set_style_text_color(tileValue[i], live ? t.text : t.muted, 0);
  }
}

// Battery Boost lässt sich schalten: eingeschaltet, Auto angesteckt, Modus
// Smart (oder Boost läuft noch)
static bool boostable(const UiView &v) {
  const EvccState &s = v.state;
  return !v.solar && v.boostButton && s.hasBoost && s.valid && s.connected &&
         s.batterySoc >= 0 &&
         (s.mode == ChargeMode::Smart || s.batteryBoost || v.pendingBoost == 1);
}

static bool boostShown(const UiView &v) {
  return v.pendingBoost >= 0 ? v.pendingBoost : v.state.batteryBoost;
}

// Akku-Anzeige oben neben der Uhr; als Boost-Knopf mit Rahmen, bei aktivem
// Boost grün gefüllt
static void renderBattery(const UiView &v, bool live) {
  const Theme &t = *theme;
  const bool button = boostable(v);
  const bool on = button && boostShown(v);
  const bool pending = v.pendingBoost >= 0;
  char buf[32];
  snprintf(buf, sizeof(buf), ICON_BATTERY " %d %%",
           (int)lroundf(v.state.batterySoc));
  lv_label_set_text(lblBattery, buf);

  if (button)
    lv_obj_add_flag(btnBattery, LV_OBJ_FLAG_CLICKABLE);
  else
    lv_obj_remove_flag(btnBattery, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_pad_hor(btnBattery, button ? BOOST_PAD_H : 0, 0);
  lv_obj_set_style_pad_ver(btnBattery, button ? BOOST_PAD_V : 0, 0);
  lv_obj_set_style_border_width(btnBattery, button && !on ? 2 : 0, 0);
  lv_obj_set_style_border_color(btnBattery, t.muted, 0);
  lv_obj_set_style_border_opa(btnBattery, pending ? LV_OPA_50 : LV_OPA_COVER,
                              0);
  lv_obj_set_style_bg_color(btnBattery, t.battery, 0);
  lv_obj_set_style_bg_opa(
      btnBattery,
      on ? (pending || !live ? LV_OPA_50 : LV_OPA_COVER) : LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_color(btnBattery, on ? t.battery : t.pressed,
                            LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(btnBattery, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_set_style_text_color(lblBattery,
                              on       ? lv_color_white()
                              : button ? t.text
                                       : t.muted,
                              0);
  // Text bleibt auf derselben Höhe wie die Uhr; der Rahmen wächst nach außen
  lv_obj_align_to(btnBattery, lblClock, LV_ALIGN_OUT_LEFT_BOTTOM,
                  button ? -20 + BOOST_PAD_H / 2 : -20,
                  button ? BOOST_PAD_V : 0);
}

// Ohne Fahrzeug Sonnenüberschuss, Haus und Akku zeigen, sofern evcc eine
// PV-Anlage kennt (auch nachts)
static bool showHouse(const EvccState &s) {
  return s.valid && !s.connected && s.pvPower >= 0;
}

// Anteil der Ladung aus eigener Erzeugung. evcc zählt Sonne und Hausakku
// zusammen (greenShare); hier aufgeteilt im Verhältnis von selbst genutzter
// Sonne und Akku-Entladung. Der Balken zeigt das ganze Haus, die Statuszeile
// den Anteil der Ladung.
static int greenShareText(const EvccState &s, char *buf, size_t len) {
  if (s.greenShare < 0)
    return 0;
  const float green = constrain(s.greenShare, 0.0f, 1.0f) * 100.0f;
  const float battery = s.batterySoc >= 0 ? LV_MAX(0.0f, s.batteryPower) : 0;
  const float feedIn = s.hasGrid ? LV_MAX(0.0f, -s.gridPower) : 0;
  const float sun = LV_MAX(0.0f, s.pvPower - feedIn);
  if (s.pvPower < 0 && battery < 50)
    return snprintf(buf, len, " · %d %% Sonne",
                    (int)lroundf(green)); // PV unbekannt
  const float sources = (sun >= 50 ? sun : 0) + (battery >= 50 ? battery : 0);
  if (sources <= 0)
    return 0; // nachts ohne Akku: kein Zusatz
  const int sunPct = (int)lroundf(green * (sun >= 50 ? sun : 0) / sources);
  const int batteryPct = (int)lroundf(green) - sunPct;
  if (sunPct > 0 && batteryPct > 0)
    return snprintf(buf, len, " · " ICON_SUN " %d %% · " ICON_BATTERY " %d %%",
                    sunPct, batteryPct);
  if (batteryPct > 0)
    return snprintf(buf, len, " · %d %% Akku", batteryPct);
  return snprintf(buf, len, " · %d %% Sonne", sunPct);
}

// Fahrzeug hat sein Ziel erreicht und lädt deshalb nicht weiter – egal wie viel
// Sonne da ist. Maßgeblich ist das niedrigere der beiden Limits (Ladepunkt in
// evcc, Fahrzeug selbst).
static bool limitReached(const EvccState &s, char *buf, size_t len) {
  // Ohne Ladestand begrenzt evcc die Energie der Ladung
  if (!s.heating && !s.socBased && s.limitEnergy > 0 &&
      s.chargedEnergy >= s.limitEnergy * 1000.0f - 50.0f) {
    snprintf(buf, len, "Ladelimit erreicht");
    return true;
  }
  if (s.vehicleSoc <= 0)
    return false;
  // Heizgeräte melden Ist- und Zieltemperatur
  if (s.heating) {
    if (s.limitSoc <= 0 || s.vehicleSoc < s.limitSoc)
      return false;
    snprintf(buf, len, "Zieltemperatur erreicht");
    return true;
  }
  const bool lp =
      s.limitSoc > 0 && s.limitSoc < 100 && s.vehicleSoc >= s.limitSoc;
  const bool car = s.vehicleLimitSoc > 0 && s.vehicleLimitSoc < 100 &&
                   s.vehicleSoc >= s.vehicleLimitSoc;
  if (s.vehicleSoc >= 99.5f) {
    snprintf(buf, len, "Voll geladen");
  } else if (lp) {
    snprintf(buf, len, "Ladelimit erreicht");
  } else if (car) {
    snprintf(buf, len, "Limit im Fahrzeug erreicht");
  } else {
    return false;
  }
  return true;
}

// Hängt an buf an und liefert die neue Länge; was nicht mehr passt, fällt weg
static int append(char *buf, size_t len, int n, const char *fmt, ...) {
  if (n < 0 || (size_t)n >= len - 1)
    return n;
  va_list args;
  va_start(args, fmt);
  const int written = vsnprintf(buf + n, len - n, fmt, args);
  va_end(args);
  return written < 0 ? n : LV_MIN(n + written, (int)len - 1);
}

static void renderStatus(const UiView &v, bool house, char *buf, size_t len,
                         lv_color_t &color) {
  const EvccState &s = v.state;
  const Theme &t = *theme;
  color = t.muted;
  char tmp[40];

  if (v.flash) {
    color = t.error;
    snprintf(buf, len, "%s", v.flash);
  } else if (!v.wifiConnected) {
    color = t.error;
    snprintf(buf, len, "Keine WLAN-Verbindung");
  } else if (v.stale) {
    color = t.error;
    if (s.valid && v.lastUpdate[0])
      snprintf(buf, len, "evcc nicht erreichbar · Stand %s", v.lastUpdate);
    else
      snprintf(buf, len,
               millis() > STALE_AFTER_MS ? "evcc nicht erreichbar"
                                         : "Verbinde mit evcc …");
  } else if (s.missing) {
    color = t.warn;
    snprintf(buf, len, "Diese Nummer gibt es in evcc nicht");
  } else if (v.pendingMode != ChargeMode::None) {
    snprintf(buf, len, "Modus wird gesetzt …");
  } else if (v.pendingBoost >= 0) {
    snprintf(buf, len,
             v.pendingBoost ? "Boost wird eingeschaltet …"
                            : "Boost wird ausgeschaltet …");
  } else if (v.pendingVehicle) {
    snprintf(buf, len, "Fahrzeug wird gewechselt …");
  } else if (v.pendingLimit >= 0) {
    snprintf(buf, len, "Ladelimit wird gesetzt …");
  } else if (!s.connected && house && s.minChargePower > 0) {
    // Hilft bei der Entscheidung: reicht die Sonne gerade für Smart?
    if (surplus(s) >= s.minChargePower) {
      color = t.solarText;
      snprintf(buf, len, "Überschuss reicht für Smart");
    } else {
      formatDecimal(tmp, sizeof(tmp), s.minChargePower / 1000.0f);
      snprintf(buf, len, "Smart startet ab %s kW Überschuss", tmp);
    }
  } else if (!s.connected && house) {
    snprintf(buf, len, "Sonnenüberschuss");
  } else if (!s.connected) {
    snprintf(buf, len, "Modus für die nächste Ladung wählen");
  } else if (s.modeOff) {
    color = t.warn;
    snprintf(buf, len, "Ladepunkt ist aus – Modus wählen");
  } else if (s.charging) {
    int n = append(buf, len, 0, s.heating ? "Heizt" : "Lädt");
    if (s.phasesActive > 0)
      n = append(buf, len, n, " · %d-phasig", s.phasesActive);
    if (!strcmp(s.pvAction, "disable") && s.pvRemaining > 0) {
      // Die Warnung verdrängt den Sonnenanteil – beides zusammen passt nicht in
      // die Zeile
      formatCountdown(tmp, sizeof(tmp), s.pvRemaining);
      append(buf, len, n, " · stoppt in %s", tmp);
      color = t.warn;
    } else if (s.batteryBoost) {
      append(buf, len, n, " · Akku hilft mit");
    } else if (greenShareText(s, tmp, sizeof(tmp)) > 0) {
      append(buf, len, n, "%s", tmp);
    }
  } else if (limitReached(s, tmp, sizeof(tmp))) {
    color = t.solarText;
    snprintf(buf, len, "%s", tmp);
  } else if (!strcmp(s.pvAction, "enable") && s.pvRemaining > 0) {
    formatCountdown(tmp, sizeof(tmp), s.pvRemaining);
    color = t.solarText;
    snprintf(buf, len, "Genug Sonne – Start in %s", tmp);
  } else if (s.mode == ChargeMode::Smart) {
    snprintf(buf, len, "Wartet auf Sonnenüberschuss");
  } else if (s.planActive || s.smartCostActive) {
    snprintf(buf, len, "Wartet auf günstigen Zeitpunkt");
  } else {
    snprintf(buf, len,
             s.heating ? "Heizt gerade nicht"
                       : "Verbunden · lädt gerade nicht");
  }
}

// "20 kWh", "12,5 kWh"
static void formatEnergy(char *buf, size_t len, float kwh) {
  if (fabsf(kwh - roundf(kwh)) < 0.05f) {
    snprintf(buf, len, "%d kWh", (int)lroundf(kwh));
  } else {
    char num[16];
    formatDecimal(num, sizeof(num), kwh);
    snprintf(buf, len, "%s kWh", num);
  }
}

// Ladelimit neben der Fahrzeugzeile: Prozent, wenn evcc den Ladestand kennt,
// sonst Energie der Ladung
static bool limitShown(const UiView &v) {
  const EvccState &s = v.state;
  return v.limitButton && s.valid && s.connected && !s.heating;
}

// Angezeigtes Limit: das angefragte, bis evcc es bestätigt. Prozent: 100 =
// voll, Energie: 0 = keins
static float limitValue(const UiView &v) {
  if (v.pendingLimit >= 0)
    return v.pendingLimit;
  if (!v.state.socBased)
    return v.state.limitEnergy;
  return v.state.limitSoc > 0 ? v.state.limitSoc : 100;
}

// Setzt den Text des Ladelimits und liefert die Breite, die es in der Zeile
// belegt
static int renderLimit(const UiView &v) {
  char buf[32];
  const float value = limitValue(v);
  if (v.state.socBased) {
    snprintf(buf, sizeof(buf), "Limit %d %%", (int)lroundf(value));
  } else if (value > 0) {
    char num[20];
    formatEnergy(num, sizeof(num), value);
    snprintf(buf, sizeof(buf), "Limit %s", num);
  } else {
    snprintf(buf, sizeof(buf), "Kein Limit");
  }
  lv_label_set_text(lblLimit, buf);
  lv_obj_set_style_text_opa(lblLimit,
                            v.pendingLimit >= 0 ? LV_OPA_50 : LV_OPA_COVER, 0);
  lv_point_t size;
  lv_text_get_size(&size, buf, &font_22, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
  return size.x + 2 * LIMIT_PAD + 2 + LIMIT_GAP;
}

static void renderPicker() {
  const UiView &v = lastView;
  const EvccState &s = v.state;
  const Theme &t = *theme;
  const int count = pickerEnergy
                        ? (int)(sizeof(PICK_ENERGY) / sizeof(PICK_ENERGY[0]))
                        : (int)(sizeof(PICK_SOC) / sizeof(PICK_SOC[0]));
  const int rows = (count + 2) / 3;
  const int areaH = 480 - MARGIN - PICK_CANCEL_H - PICK_GAP - 4 - PICK_TOP;
  const int w = (CONTENT_W - 2 * PICK_GAP) / 3;
  const int h = (areaH - (rows - 1) * PICK_GAP) / rows;
  const float current = limitValue(v);
  char buf[64], num[20];

  if (pickerEnergy) {
    formatDecimal(num, sizeof(num), s.chargedEnergy / 1000.0f);
    snprintf(buf, sizeof(buf), "Für diese Ladung · bisher %s kWh", num);
  } else if (s.vehicleSoc > 0) {
    snprintf(buf, sizeof(buf), "%s%sjetzt %d %%", s.vehicleTitle,
             s.vehicleTitle[0] ? " · " : "", (int)lroundf(s.vehicleSoc));
  } else {
    snprintf(buf, sizeof(buf), "%s",
             s.vehicleTitle[0] ? s.vehicleTitle : "Laden bis");
  }
  lv_label_set_text(lblPickSub, buf);

  for (int i = 0; i < PICK_MAX; i++) {
    setVisible(pickBtn[i], i < count);
    if (i >= count)
      continue;
    const float value = (pickerEnergy ? PICK_ENERGY : PICK_SOC)[i];
    lv_obj_set_size(pickBtn[i], w, h);
    lv_obj_set_pos(pickBtn[i], MARGIN + (i % 3) * (w + PICK_GAP),
                   PICK_TOP + (i / 3) * (h + PICK_GAP));
    if (!pickerEnergy)
      snprintf(buf, sizeof(buf), "%d %%", (int)value);
    else if (value > 0)
      formatEnergy(buf, sizeof(buf), value);
    else
      snprintf(buf, sizeof(buf), "Kein Limit");
    lv_label_set_text(pickLbl[i], buf);
    lv_obj_set_style_text_font(pickLbl[i],
                               !pickerEnergy ? &font_32
                               : value > 0   ? &font_24
                                             : &font_22,
                               0);
    // Weniger als schon geladen ergibt kein sinnvolles Energielimit
    const bool enabled =
        !pickerEnergy || value == 0 || value * 1000.0f > s.chargedEnergy;
    const bool active = fabsf(value - current) < 0.05f;
    if (enabled)
      lv_obj_add_flag(pickBtn[i], LV_OBJ_FLAG_CLICKABLE);
    else
      lv_obj_remove_flag(pickBtn[i], LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(pickBtn[i], active ? t.activeBg : t.box, 0);
    lv_obj_set_style_bg_color(pickBtn[i], active ? t.activeBg : t.pressed,
                              LV_STATE_PRESSED);
    lv_obj_set_style_border_color(pickBtn[i], active ? t.activeBg : t.border,
                                  0);
    lv_obj_set_style_text_color(pickLbl[i], active ? t.activeFg : t.text, 0);
    lv_obj_set_style_text_opa(pickLbl[i], enabled ? LV_OPA_COVER : LV_OPA_30,
                              0);
  }
}

static void renderDots(const UiView &v) {
  const Theme &t = *theme;
  const bool multi = v.pageCount > 1;
  setVisible(dots, multi);
  const int segY = SEG_Y - (multi ? DOTS_SHIFT : 0);
  lv_obj_set_y(segBg, segY);
  // Die Solarseite hat keine Modustasten: ihre Mitte reicht bis dorthin, wo sie
  // sonst enden
  setVisible(segBg, !v.solar);
  lv_obj_set_height(mid, (v.solar ? segY + SEG_H : segY) - HEADER_H);
  for (int i = 0; i < MAX_PAGES; i++) {
    setVisible(dot[i], i < v.pageCount);
    lv_obj_set_style_bg_color(dot[i], i == v.page ? t.text : t.muted, 0);
    lv_obj_set_style_bg_opa(dot[i], i == v.page ? LV_OPA_COVER : LV_OPA_50, 0);
  }
}

// "18,2 kWh"; ab 100 kWh ohne Nachkommastelle, damit es in die Kachel passt
static void formatTileEnergy(char *buf, size_t len, float kwh) {
  if (kwh >= 99.95f) {
    snprintf(buf, len, "%d kWh", (int)lroundf(kwh));
  } else {
    char num[16];
    formatDecimal(num, sizeof(num), kwh);
    snprintf(buf, len, "%s kWh", num);
  }
}

// Solarseite: Erzeugung je Stunde. Vergangene Stunden zeigen das Erzeugte,
// kommende die Prognose, die laufende beides übereinander.
static void renderChart(const UiView &v) {
  const EvccSolar &d = v.solarData;
  int first = 6, last = 20;
  for (int h = 0; h < 24; h++) {
    if (d.hourEnergy[h] < CHART_NONE && d.hourForecast[h] < CHART_NONE)
      continue;
    first = LV_MIN(first, h);
    last = LV_MAX(last, h);
  }
  const int n = last - first + 1;
  const int gap = n <= 15 ? 7 : n <= 19 ? 5 : 4;
  const int w = (CONTENT_W - (n - 1) * gap) / n;
  float max =
      0.5f; // an trüben Tagen nicht jede Kleinigkeit auf volle Höhe ziehen
  for (int h = first; h <= last; h++)
    max = LV_MAX(max, LV_MAX(d.hourEnergy[h], d.hourForecast[h]));
  auto height = [&](float kwh) {
    return LV_CLAMP(CHART_MIN, (int)lroundf(kwh / max * CHART_H), CHART_H);
  };

  chartModel.count = n;
  chartModel.width = w;
  chartModel.axisCount = 0;
  for (int i = 0; i < n; i++) {
    const int h = first + i;
    const float made = d.hourEnergy[h], forecast = d.hourForecast[h];
    const bool past = v.hour < 0 || h < v.hour, current = h == v.hour;
    ChartBar &bar = chartModel.bar[i];
    bar.x = (int)lroundf((float)i * (CONTENT_W - w) / (n - 1));
    bar.height = height(past      ? made
                        : current ? LV_MAX(made, forecast)
                                  : forecast);
    bar.solid = past && made >= CHART_NONE;
    bar.filled =
        current && made >= CHART_NONE ? LV_MIN(bar.height, height(made)) : 0;
    if (h % 3 == 0 && chartModel.axisCount < CHART_AXIS) {
      chartModel.axisX[chartModel.axisCount] = bar.x + w / 2;
      snprintf(chartModel.axisText[chartModel.axisCount],
               sizeof(chartModel.axisText[0]), "%d", h);
      chartModel.axisCount++;
    }
  }
  lv_obj_invalidate(chartBlock);
}

static void renderPowerSources(const EvccState &s) {
  static constexpr int NAME_W = CONTENT_W - 130; // Platz für "12,4 kW" rechts
  float max = 1;
  max = LV_MAX(max, s.pvPower);
  max = LV_MAX(max, s.gridPower);
  plantModel.count = 2;
  char num[16];
  for (int i = 0; i < 2; i++) {
    float power = LV_MAX(0.0f, i == 0 ? s.pvPower : s.gridPower);
    // Zu lange Namen kürzen, bis sie mit "…" in die Zeile passen (ganze
    // UTF-8-Zeichen)
    char *name = plantModel.name[i];
    const char *title = i == 0 ? "PV Erzeugung" : "Netzbezug    ";
    strlcpy(name, title, 13);
    size_t len = strlen(name);
    lv_point_t size;
    lv_text_get_size(&size, name, &font_22, 0, 0, LV_COORD_MAX,
                     LV_TEXT_FLAG_NONE);
    formatDecimal(num, sizeof(num), power / 1000.0f);
    snprintf(plantModel.value[i], sizeof(plantModel.value[0]), "%s kW", num);
    plantModel.filled[i] = 0;
    //    const int width = (int)lroundf(power / max * CONTENT_W);
    //    plantModel.filled[i] =
    //        width >= PLANT_TRACK_H
    //            ? width
    //            : 0; // schmaler ließe sich die Rundung nicht zeichnen
  }
  lv_obj_set_height(plantsBlock, 16 + 2 * plantRowH() + PLANT_GAP);
  lv_obj_invalidate(plantsBlock);
}

static void renderPlants(const EvccSolar &d) {
  static constexpr int NAME_W = CONTENT_W - 130; // Platz für "12,4 kW" rechts
  float max = 1;
  for (int i = 0; i < d.plantCount; i++)
    max = LV_MAX(max, d.plantPower[i]);
  plantModel.count = d.plantCount;
  char num[16];
  for (int i = 0; i < d.plantCount; i++) {
    const float power = LV_MAX(0.0f, d.plantPower[i]);
    // Zu lange Namen kürzen, bis sie mit "…" in die Zeile passen (ganze
    // UTF-8-Zeichen)
    char *name = plantModel.name[i];
    strlcpy(name, d.plantTitle[i], sizeof(d.plantTitle[0]));
    size_t len = strlen(name);
    lv_point_t size;
    lv_text_get_size(&size, name, &font_22, 0, 0, LV_COORD_MAX,
                     LV_TEXT_FLAG_NONE);
    while (size.x > NAME_W && len > 1) {
      do
        len--;
      while (len > 0 && ((uint8_t)name[len] & 0xC0) == 0x80);
      strcpy(name + len, "…");
      lv_text_get_size(&size, name, &font_22, 0, 0, LV_COORD_MAX,
                       LV_TEXT_FLAG_NONE);
    }
    formatDecimal(num, sizeof(num), power / 1000.0f);
    snprintf(plantModel.value[i], sizeof(plantModel.value[0]), "%s kW", num);
    const int width = (int)lroundf(power / max * CONTENT_W);
    plantModel.filled[i] =
        width >= PLANT_TRACK_H
            ? width
            : 0; // schmaler ließe sich die Rundung nicht zeichnen
  }
  lv_obj_set_height(plantsBlock, 16 + d.plantCount * plantRowH() +
                                     (d.plantCount - 1) * PLANT_GAP);
  lv_obj_invalidate(plantsBlock);
}

// Solarseite hinter den Ladepunkten: was die Sonne gerade bringt, der Tag je
// Stunde bzw. die Leistung je Anlage, darunter heute, Rest und morgen. Keine
// Tasten – hier gibt es nichts zu schalten.
static void renderSolar(const UiView &v, bool live) {
  const EvccState &s = v.state;
  const EvccSolar &d = v.solarData;
  const Theme &t = *theme;
  char buf[64];

  closePicker();
  const bool data = s.valid && s.pvPower >= 0;
  lv_obj_remove_flag(btnVehicle, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_text_opa(lblVehicle, LV_OPA_COVER, 0);
  setVisible(btnLimit, false);
  setVisible(btnVehicle, data);
  setVisible(rowVehicle, data);
  setVisible(contPower, data);
  setVisible(contIdle, !data);
  setVisible(barBlock, false);
  const bool rows = data && live && d.plantCount >= 2;
  const float ueberschuss = s.pvPower - s.homePower;
  if (data) {
    if (rows)
      //      snprintf(buf, sizeof(buf), "Überschuss aktuell · %u Anlagen",
      //               d.plantTotal);
      //    else
      snprintf(buf, sizeof(buf), "Überschuss aktuell");
    setVehicleText(buf);
    const bool producing = s.pvPower >= 50;
    formatDecimal(buf, sizeof(buf), LV_MAX(0.0f, ueberschuss) / 1000.0f);
    lv_label_set_text(lblPower, buf);
    lv_obj_set_style_text_color(lblPower, live && producing ? t.text : t.muted,
                                0);
    setBigIcon(ICON_SUN);
    lv_obj_set_style_text_opa(lblBolt,
                              live && producing ? LV_OPA_COVER : LV_OPA_40, 0);
  } else if (!s.valid) {
    const bool starting = millis() < STALE_AFTER_MS;
    lv_label_set_text(lblIdleIcon,
                      !v.wifiConnected ? ICON_WIFI_OFF : ICON_CLOUD_OFF);
    lv_label_set_text(lblIdleText, !v.wifiConnected ? "Kein WLAN"
                                   : starting       ? "Verbinde …"
                                                    : "Keine Daten");
  } else {
    lv_label_set_text(lblIdleIcon, ICON_SUN);
    lv_label_set_text(lblIdleText, "Keine PV-Anlage");
  }

  // Die Statuszeile meldet hier nur Störungen; dann weichen ihr Verlauf und
  // Anlagen
  const bool trouble = !v.wifiConnected || v.stale;
  setVisible(lblStatus, trouble);
  if (trouble) {
    lv_color_t color;
    renderStatus(v, false, buf, sizeof(buf), color);
    lv_label_set_text(lblStatus, buf);
    lv_obj_set_style_text_color(lblStatus, color, 0);
  }
  setVisible(plantsBlock, rows);
  if (rows)
    renderPowerSources(s);
  const bool hours = data && live && !rows && d.valid;
  setVisible(chartBlock, hours);
  if (hours)
    renderChart(v);

  // Heute bisher, was heute noch kommt, morgen. Kommt heute nichts mehr, steht
  // statt des Rests die Prognose für den ganzen Tag – zum Vergleich mit dem
  // Erzeugten.
  int n = 0;
  // Bei drei Kacheln bleibt wenig Breite: stößt ein Wert an den Rand, fällt
  // seine Nachkommastelle weg
  const int tileRoom = (CONTENT_W - 2 * TILE_GAP) / 3 - 14 - 10;
  auto add = [&](const char *label, float kwh) {
    lv_label_set_text(tileLabel[n], label);
    formatTileEnergy(buf, sizeof(buf), kwh);
    lv_point_t size;
    lv_text_get_size(&size, buf, &font_24, 0, 0, LV_COORD_MAX,
                     LV_TEXT_FLAG_NONE);
    if (size.x > tileRoom)
      snprintf(buf, sizeof(buf), "%d kWh", (int)lroundf(kwh));
    lv_label_set_text(tileValue[n], buf);
    n++;
  };
  if (data && d.valid)
    add("HEUTE", d.today);
  if (data && s.solarToday >= 0) {
    if (s.solarToday >= 100 || !d.valid)
      add("REST HEUTE", s.solarToday / 1000.0f);
    else
      add("PROGNOSE", d.forecastToday);
  }
  if (data && s.solarTomorrow >= 0)
    add("MORGEN", s.solarTomorrow / 1000.0f);
  setVisible(tiles, n > 0);
  for (int i = 0; i < 3; i++) {
    setVisible(tile[i], i < n);
    lv_obj_set_style_text_color(tileValue[i], live ? t.text : t.muted, 0);
  }
}

void ui::render(const UiView &v) {
  const bool pageChanged = hasLastView && v.page != lastView.page;
  if (pageChanged)
    slideIn(v.page > lastView.page ? 1 : -1);
  lastView = v;
  hasLastView = true;
  const EvccState &s = v.state;
  const Theme &t = *theme;
  const bool live = s.valid && !v.stale;
  char buf[64];

  renderDots(v);
  lv_label_set_text(lblTitle, v.solar                ? "Solar"
                              : s.valid || s.missing ? s.title
                                                     : "evcc");
  lv_label_set_text(lblClock, v.clock);
  lv_label_set_text(lblIcons, !v.wifiConnected ? ICON_WIFI_OFF
                              : v.stale        ? ICON_ALERT
                                               : "");
  lv_obj_set_style_text_color(lblIcons, t.error, 0);

  // Hauptwert: Ladeleistung, ohne Fahrzeug der Sonnenüberschuss
  const bool showPower = s.valid && s.connected;
  const bool house = showHouse(s);

  // Hausakku oben neben der Uhr, ohne Fahrzeug steht seine Leistung in der
  // Kachel
  const bool battery = s.valid && s.batterySoc >= 0;
  setVisible(btnBattery, battery);
  if (battery)
    renderBattery(v, live);
  lv_obj_align_to(lblIcons, battery ? btnBattery : lblClock,
                  LV_ALIGN_OUT_LEFT_BOTTOM, -10,
                  battery && boostable(v) ? -BOOST_PAD_V : 0);

  if (v.solar) {
    renderSolar(v, live);
    return;
  }
  setVisible(chartBlock, false);
  setVisible(plantsBlock, false);
  setVisible(lblStatus, true);

  // Fahrzeug mit Ladestand, solange es angesteckt ist; sind Fahrzeuge in evcc
  // angelegt, erscheint auch ohne erkanntes Auto "Gastfahrzeug", damit man es
  // antippen und wechseln kann
  const bool canSelectVehicle = v.vehicleSwitch && showPower && !s.heating &&
                                (v.hasVehicles || s.vehicleName[0]);
  const bool vehicle = showPower && (s.vehicleSoc > 0 || s.vehicleTitle[0] ||
                                     canSelectVehicle || v.pendingVehicle);
  if (canSelectVehicle)
    lv_obj_add_flag(btnVehicle, LV_OBJ_FLAG_CLICKABLE);
  else
    lv_obj_remove_flag(btnVehicle, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_text_opa(lblVehicle,
                            v.pendingVehicle ? LV_OPA_50 : LV_OPA_COVER, 0);
  const bool limit = limitShown(v);
  const int limitW = limit ? renderLimit(v) : 0;
  setVisible(btnLimit, limit);
  // Die Auswahl gehört zu diesem Ladepunkt und dieser Art von Limit
  if (pickerOpen &&
      (!limit || pageChanged || pickerEnergy == s.socBased || v.stale))
    closePicker();
  else if (pickerOpen)
    renderPicker();
  if (house) {
    setVehicleText(ICON_PLUG " Kein Fahrzeug");
  } else if (vehicle && s.heating) {
    // Wärmepumpe oder Heizstab: Temperatur mit einer Nachkommastelle wie in
    // evcc, ohne Auto-Symbol
    char temp[16];
    formatDecimal(temp, sizeof(temp), s.vehicleSoc);
    if (s.vehicleSoc > 0)
      snprintf(buf, sizeof(buf), "%s%s%s °C",
               s.vehicleTitle[0] ? s.vehicleTitle : "Temperatur",
               s.vehicleTitle[0] ? " · " : " ", temp);
    else
      snprintf(buf, sizeof(buf), "%s", s.vehicleTitle);
    setVehicleText(buf);
  } else if (vehicle) {
    const char *title = v.pendingVehicle    ? v.pendingVehicleTitle
                        : s.vehicleTitle[0] ? s.vehicleTitle
                        : canSelectVehicle  ? "Gastfahrzeug"
                                            : "";
    int n =
        snprintf(buf, sizeof(buf), ICON_CAR "%s%s", title[0] ? " " : "", title);
    if (!v.pendingVehicle && s.vehicleSoc > 0)
      snprintf(buf + n, sizeof(buf) - n, "%s%d %%", title[0] ? " · " : " ",
               (int)lroundf(s.vehicleSoc));
    setVehicleText(buf, limitW);
  }
  setVisible(btnVehicle, house || vehicle);
  setVisible(rowVehicle, house || vehicle || limit);
  setVisible(contPower, showPower || house);
  setVisible(contIdle, !showPower && !house);
  if (showPower) {
    formatDecimal(buf, sizeof(buf), s.chargePower / 1000.0f);
    lv_label_set_text(lblPower, buf);
    lv_obj_set_style_text_color(lblPower, live && s.charging ? t.text : t.muted,
                                0);
    setBigIcon(ICON_BOLT);
    lv_obj_set_style_text_opa(
        lblBolt, live && s.charging ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
  } else if (house) {
    formatDecimal(buf, sizeof(buf), surplus(s) / 1000.0f);
    lv_label_set_text(lblPower, buf);
    const bool enough = surplus(s) >= LV_MAX(s.minChargePower, 1.0f);
    lv_obj_set_style_text_color(lblPower, live && enough ? t.text : t.muted, 0);
    setBigIcon(ICON_SUN);
    lv_obj_set_style_text_opa(lblBolt,
                              live && enough ? LV_OPA_COVER : LV_OPA_40, 0);
  } else if (s.missing) {
    lv_label_set_text(lblIdleIcon, ICON_ALERT);
    lv_label_set_text(lblIdleText, "Nicht gefunden");
  } else if (!s.valid) {
    const bool starting = millis() < STALE_AFTER_MS;
    lv_label_set_text(lblIdleIcon,
                      !v.wifiConnected ? ICON_WIFI_OFF : ICON_CLOUD_OFF);
    lv_label_set_text(lblIdleText, !v.wifiConnected ? "Kein WLAN"
                                   : starting       ? "Verbinde …"
                                                    : "Keine Daten");
  } else {
    lv_label_set_text(lblIdleIcon, ICON_PLUG);
    lv_label_set_text(lblIdleText, "Kein Fahrzeug");
  }

  lv_color_t statusColor;
  renderStatus(v, house, buf, sizeof(buf), statusColor);
  lv_label_set_text(lblStatus, buf);
  lv_obj_set_style_text_color(lblStatus, statusColor, 0);

  // Energiebalken wie in evcc, auch angesteckt ohne Laden (Ladelimit, Warten
  // auf Überschuss); ohne Netzleistung nur beim Laden mit dem Sonnenanteil der
  // Ladeleistung
  const bool chargeBar =
      showPower &&
      (s.hasGrid ? s.pvPower >= 0 : s.charging && s.greenShare >= 0);
  setVisible(barBlock,
             ((house && s.hasGrid) || chargeBar) && renderFlow(s, live));

  // Kacheln: Werte der laufenden bzw. letzten Sitzung, ohne Fahrzeug Haus, Akku
  // und Prognose
  char num[16];
  const bool showTiles = showPower && (s.charging || s.chargedEnergy > 0);
  setVisible(tiles, showTiles || house);
  if (house) {
    renderHouseTiles(s, live);
  } else if (showTiles) {
    for (int i = 0; i < 3; i++) {
      lv_label_set_text(tileLabel[i],
                        (s.heating ? HEATING_TILE_TEXT : TILE_TEXT)[i]);
      setVisible(tile[i], true);
    }
    formatDecimal(num, sizeof(num), s.chargedEnergy / 1000.0f);
    snprintf(buf, sizeof(buf), "%s kWh", num);
    lv_label_set_text(tileValue[0], buf);
    formatDuration(buf, sizeof(buf), s.chargeDuration);
    lv_label_set_text(tileValue[1], buf);
    snprintf(buf, sizeof(buf), "%d %%", (int)lroundf(s.sessionSolarPct));
    lv_label_set_text(tileValue[2], buf);
    for (int i = 0; i < 3; i++)
      lv_obj_set_style_text_color(tileValue[i], live ? t.text : t.muted, 0);
  }

  lv_label_set_text(segSub[1], s.heating ? "heizt immer" : SEG_SUB[1]);
  const bool pending = v.pendingMode != ChargeMode::None;
  renderSegments(pending ? v.pendingMode : s.mode, pending);
}

void ui::setNight(bool value) {
  if (value == night)
    return;
  night = value;
  theme = night ? &DARK : &LIGHT;
  applyTheme();
  if (hasLastView)
    render(lastView);
}

void ui::begin(ModeCallback onMode, Callback onSettingsRequested,
               SwipeCallback onSwipe, Callback onBoost, Callback onVehicle,
               LimitCallback onLimit) {
  limitCb = onLimit;
  modeCb = onMode;
  boostCb = onBoost;
  vehicleCb = onVehicle;
  settingsCb = onSettingsRequested;
  swipeCb = onSwipe;
  buildMain();
  buildPicker();
  buildMessage();
  applyTheme();
  render(UiView());
  lv_screen_load(scrMain);
}

void ui::showMessage(const char *icon, const char *title, const char *text) {
  lv_label_set_text(lblMsgIcon, icon);
  lv_label_set_text(lblMsgTitle, title);
  lv_label_set_text(lblMsgText, text);
  if (lv_screen_active() != scrMsg)
    lv_screen_load(scrMsg);
}

void ui::showLimitPicker(bool show) {
  if (show)
    openPicker();
  else
    closePicker();
}

void ui::onMessageTap(Callback cb) { messageTapCb = cb; }

void ui::showMain() {
  if (lv_screen_active() != scrMain)
    lv_screen_load(scrMain);
}
