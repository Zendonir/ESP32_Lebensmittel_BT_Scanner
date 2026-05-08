#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

#define LV_HOR_RES_MAX          320
#define LV_VER_RES_MAX          480
#define LV_COLOR_DEPTH          16

#define LV_MEM_CUSTOM           1
#define LV_MEM_CUSTOM_INCLUDE   <stdlib.h>
#define LV_MEM_CUSTOM_ALLOC     malloc
#define LV_MEM_CUSTOM_FREE      free
#define LV_MEM_CUSTOM_REALLOC   realloc
#define LV_MEM_SIZE             (256 * 1024)

#define LV_USE_LOG              1
#define LV_LOG_LEVEL            LV_LOG_LEVEL_WARN

#define LV_USE_ASSERT_NULL      1
#define LV_USE_ASSERT_MALLOC    1

#define LV_USE_THEME_DEFAULT    1
#define LV_THEME_DEFAULT_DARK   1
#define LV_THEME_DEFAULT_FONT_SMALL  &lv_font_montserrat_12
#define LV_THEME_DEFAULT_FONT_NORMAL &lv_font_montserrat_16
#define LV_THEME_DEFAULT_FONT_LARGE  &lv_font_montserrat_24

#define LV_USE_BTN              1
#define LV_USE_LABEL            1
#define LV_USE_TEXTAREA         1
#define LV_USE_KEYBOARD         1
#define LV_USE_SWITCH           1
#define LV_USE_SLIDER           1
#define LV_USE_IMG              1
#define LV_USE_CHART            1

#define LV_INDEV_DEF_READ_PERIOD 30

#define LV_FONT_MONTSERRAT_12   1
#define LV_FONT_MONTSERRAT_14   1
#define LV_FONT_MONTSERRAT_16   1
#define LV_FONT_MONTSERRAT_20   1
#define LV_FONT_MONTSERRAT_24   1
#define LV_FONT_MONTSERRAT_28   1

#endif
