#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

// ============ DISPLAY SETTINGS ============
#define LV_HOR_RES_MAX          320
#define LV_VER_RES_MAX          480

#define LV_COLOR_DEPTH          16
#define LV_USE_DEMO_WIDGETS     0

// ============ MEMORY CONFIGURATION ============
#define LV_MEM_CUSTOM           1
#define LV_MEM_CUSTOM_INCLUDE   <stdlib.h>
#define LV_MEM_CUSTOM_ALLOC     malloc
#define LV_MEM_CUSTOM_FREE      free
#define LV_MEM_CUSTOM_REALLOC   realloc

#define LV_MEM_SIZE             (256 * 1024)

// ============ RENDERING ============
#define LV_DRAW_COMPLEX         1
#define LV_DRAW_GRADIENT        1

// ============ LOGGING ============
#define LV_USE_LOG              1
#define LV_LOG_LEVEL            LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF           0

// ============ ASSERTS ============
#define LV_USE_ASSERT_NULL      1
#define LV_USE_ASSERT_MALLOC    1
#define LV_USE_ASSERT_STYLE     0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ       0

// ============ THEME & COLORS ============
#define LV_USE_THEME_DEFAULT    1
#define LV_THEME_DEFAULT_DARK   1
#define LV_THEME_DEFAULT_FONT_SMALL  &lv_font_montserrat_12
#define LV_THEME_DEFAULT_FONT_NORMAL &lv_font_montserrat_16
#define LV_THEME_DEFAULT_FONT_LARGE  &lv_font_montserrat_24

// ============ WIDGET SUPPORT ============
#define LV_USE_BUTTON           1
#define LV_USE_LABEL            1
#define LV_USE_TEXTAREA         1
#define LV_USE_KEYBOARD         1
#define LV_USE_SWITCH           1
#define LV_USE_SLIDER           1
#define LV_USE_GAUGE            1
#define LV_USE_SPINNER          1
#define LV_USE_IMAGE            1
#define LV_USE_CHART            1
#define LV_USE_TABLE            1
#define LV_USE_WINDOW           1
#define LV_USE_MSGBOX           1
#define LV_USE_LIST             1
#define LV_USE_DROPDOWN         1
#define LV_USE_ROLLER           1
#define LV_USE_MENU             1

// ============ INPUT DEVICES ============
#define LV_USE_POINTER          1
#define LV_INDEV_DEF_READ_PERIOD 30
#define LV_INDEV_DEF_DRAG_LIMIT 10
#define LV_INDEV_DEF_DRAG_THROW_SLOW_DOWN 20
#define LV_INDEV_DEF_DRAG_THROW_VEL_MAX 500

// ============ ANIMATION ============
#define LV_USE_ANIMATION        1

// ============ FONTS ============
#define LV_FONT_MONTSERRAT_12   1
#define LV_FONT_MONTSERRAT_14   1
#define LV_FONT_MONTSERRAT_16   1
#define LV_FONT_MONTSERRAT_18   1
#define LV_FONT_MONTSERRAT_20   1
#define LV_FONT_MONTSERRAT_24   1
#define LV_FONT_MONTSERRAT_28   1
#define LV_FONT_MONTSERRAT_32   1

// ============ PERFORMANCE ============
#define LV_TICK_CUSTOM          0
#define LV_DISP_DEF_REFR_PERIOD 30

#endif
