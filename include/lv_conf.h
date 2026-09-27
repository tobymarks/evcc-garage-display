/**
 * LVGL-Konfiguration für das ESP32-4848S040C.
 * Nicht aufgeführte Optionen nutzen die Standardwerte aus lv_conf_internal.h.
 */
#if 1
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16

/* System-Heap statt fester LVGL-Speicherpool – spart statisches DRAM */
#define LV_USE_STDLIB_MALLOC LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_CLIB

#define LV_DEF_REFR_PERIOD 20
#define LV_DPI_DEF 130
#define LV_USE_OS LV_OS_NONE

#define LV_USE_LOG 0
#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1

/* Eigene Schriften (Montserrat mit Umlauten + Tabler-Icons), siehe tools/gen_fonts.sh */
#define LV_FONT_MONTSERRAT_14 0
#define LV_FONT_CUSTOM_DECLARE LV_FONT_DECLARE(font_22)
#define LV_FONT_DEFAULT &font_22

/* Wir stylen alles selbst */
#define LV_USE_THEME_DEFAULT 0
#define LV_USE_THEME_SIMPLE 0
#define LV_USE_THEME_MONO 0

/* Nicht benötigte Widgets weglassen */
#define LV_USE_ANIMIMG 0
#define LV_USE_ARC 0
#define LV_USE_BAR 0
#define LV_USE_BUTTONMATRIX 0
#define LV_USE_CALENDAR 0
#define LV_USE_CANVAS 0
#define LV_USE_CHART 0
#define LV_USE_CHECKBOX 0
#define LV_USE_DROPDOWN 0
#define LV_USE_IMAGEBUTTON 0
#define LV_USE_KEYBOARD 0
#define LV_USE_LED 0
#define LV_USE_LINE 0
#define LV_USE_LIST 0
#define LV_USE_MENU 0
#define LV_USE_MSGBOX 0
#define LV_USE_ROLLER 0
#define LV_USE_SCALE 0
#define LV_USE_SLIDER 0
#define LV_USE_SPAN 0
#define LV_USE_SPINBOX 0
#define LV_USE_SPINNER 0
#define LV_USE_SWITCH 0
#define LV_USE_TABLE 0
#define LV_USE_TABVIEW 0
#define LV_USE_TEXTAREA 0
#define LV_USE_TILEVIEW 0
#define LV_USE_WIN 0

#endif /* LV_CONF_H */
#endif
