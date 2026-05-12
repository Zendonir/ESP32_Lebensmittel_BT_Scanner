/**
 * LVGL 8.3 configuration for Waveshare ESP32-S3-Touch-LCD-3.5
 * ST7796 SPI, 480x320 landscape, FT6336 I2C touch
 */

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/*====================
   COLOR SETTINGS
 *====================*/
#define LV_COLOR_DEPTH     16
#define LV_COLOR_16_SWAP   0   /* TFT_eSPI pushColors handles byte order */
#define LV_COLOR_SCREEN_TRANSP 0

/*=========================
   MEMORY SETTINGS
 *=========================*/
#define LV_MEM_CUSTOM      0
#define LV_MEM_SIZE        (64U * 1024U)  /* 64 KB LVGL heap (in SRAM) */
#define LV_MEM_CUSTOM_INCLUDE <stdlib.h>
#define LV_MEM_CUSTOM_ALLOC   malloc
#define LV_MEM_CUSTOM_FREE    free
#define LV_MEM_CUSTOM_REALLOC realloc
#define LV_MEM_BUF_MAX_NUM    16
#define LV_MEMCPY_MEMSET_STD  0

/*====================
   HAL SETTINGS
 *====================*/
#define LV_DISP_DEF_REFR_PERIOD   30   /* ms */
#define LV_INDEV_DEF_READ_PERIOD  30   /* ms */

/* Use a custom tick source - lv_tick_inc() called manually */
#define LV_TICK_CUSTOM     0

#define LV_DPI_DEF        130

/*=======================
   DRAW CONFIGURATION
 *=======================*/
#define LV_DRAW_COMPLEX    1
#define LV_SHADOW_CACHE_SIZE 0
#define LV_CIRCLE_CACHE_SIZE 4
#define LV_IMG_CACHE_DEF_SIZE 0
#define LV_GRADIENT_MAX_STOPS 2
#define LV_GRAD_CACHE_DEF_SIZE 0
#define LV_DITHER_GRADIENT 0
#define LV_DISP_ROT_MAX_BUF (10*1024)

/*======================
   GPU
 *======================*/
#define LV_USE_GPU_STM32_DMA2D  0
#define LV_USE_GPU_SWM341_DMA   0
#define LV_USE_GPU_NXP_PXP      0
#define LV_USE_GPU_NXP_VG_LITE  0
#define LV_USE_GPU_SDL          0

/*==================
   LOGGING
 *==================*/
#define LV_USE_LOG         0
#define LV_LOG_LEVEL       LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF      0
#define LV_LOG_TRACE_MEM   0
#define LV_LOG_TRACE_TIMER 0
#define LV_LOG_TRACE_INDEV 0
#define LV_LOG_TRACE_DISP_REFR 0
#define LV_LOG_TRACE_EVENT 0
#define LV_LOG_TRACE_OBJ_CREATE 0
#define LV_LOG_TRACE_LAYOUT 0
#define LV_LOG_TRACE_ANIM  0

/*=============
   ASSERTS
 *=============*/
#define LV_USE_ASSERT_NULL         0
#define LV_USE_ASSERT_MALLOC       0
#define LV_USE_ASSERT_STYLE        0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ          0
#define LV_ASSERT_HANDLER_INCLUDE  <stdint.h>
#define LV_ASSERT_HANDLER          while(1);

/*=====================
   OTHERS
 *=====================*/
#define LV_USE_PERF_MONITOR    0
#define LV_USE_PERF_MONITOR_POS LV_ALIGN_BOTTOM_RIGHT
#define LV_USE_MEM_MONITOR     0
#define LV_USE_MEM_MONITOR_POS LV_ALIGN_BOTTOM_LEFT
#define LV_USE_REFR_DEBUG      0
#define LV_SPRINTF_CUSTOM      0
#define LV_SPRINTF_USE_FLOAT   0
#define LV_USE_USER_DATA       1
#define LV_ENABLE_GC           0
#define LV_BIN_DECODEr         0

/*==================
   COMPILER SETTINGS
 *==================*/
#define LV_ATTRIBUTE_TICK_INC
#define LV_ATTRIBUTE_TIMER_HANDLER
#define LV_ATTRIBUTE_FLUSH_READY
#define LV_ATTRIBUTE_MEM_ALIGN_SIZE 4
#define LV_ATTRIBUTE_MEM_ALIGN      __attribute__((aligned(4)))
#define LV_ATTRIBUTE_LARGE_CONST
#define LV_ATTRIBUTE_LARGE_RAM_ARRAY
#define LV_ATTRIBUTE_FAST_MEM
#define LV_ATTRIBUTE_DMA
#define LV_EXPORT_CONST_INT(int_value) struct _silence_gcc_warning
#define LV_USE_LARGE_COORD 0

/*==================
   FONTS
 *==================*/
#define LV_FONT_MONTSERRAT_8   0
#define LV_FONT_MONTSERRAT_10  0
#define LV_FONT_MONTSERRAT_12  1
#define LV_FONT_MONTSERRAT_14  1
#define LV_FONT_MONTSERRAT_16  1
#define LV_FONT_MONTSERRAT_18  0
#define LV_FONT_MONTSERRAT_20  1
#define LV_FONT_MONTSERRAT_22  0
#define LV_FONT_MONTSERRAT_24  1
#define LV_FONT_MONTSERRAT_26  0
#define LV_FONT_MONTSERRAT_28  0
#define LV_FONT_MONTSERRAT_30  0
#define LV_FONT_MONTSERRAT_32  0
#define LV_FONT_MONTSERRAT_34  0
#define LV_FONT_MONTSERRAT_36  0
#define LV_FONT_MONTSERRAT_38  0
#define LV_FONT_MONTSERRAT_40  0
#define LV_FONT_MONTSERRAT_42  0
#define LV_FONT_MONTSERRAT_44  0
#define LV_FONT_MONTSERRAT_46  0
#define LV_FONT_MONTSERRAT_48  1
#define LV_FONT_MONTSERRAT_12_SUBPX 0
#define LV_FONT_MONTSERRAT_28_COMPRESSED 0
#define LV_FONT_DEJAVU_16_PERSIAN_HEBREW 0
#define LV_FONT_SIMSUN_16_CJK 0
#define LV_FONT_UNSCII_8   0
#define LV_FONT_UNSCII_16  0
#define LV_FONT_CUSTOM_DECLARE
#define LV_FONT_DEFAULT    &lv_font_montserrat_14
#define LV_FONT_FMT_TXT_LARGE 0
#define LV_USE_FONT_SUBPX     0
#define LV_FONT_SUBPX_BGR     0

/*=================
   TEXT SETTINGS
 *=================*/
#define LV_TXT_ENC         LV_TXT_ENC_UTF8
#define LV_TXT_BREAK_CHARS " _.,-;:!?"
#define LV_TXT_LINE_BREAK_LONG_LEN 0
#define LV_TXT_LINE_BREAK_LONG_PRE_MIN_LEN  3
#define LV_TXT_LINE_BREAK_LONG_POST_MIN_LEN 3
#define LV_TXT_COLOR_CMD   "#"
#define LV_USE_BIDI        0
#define LV_BIDI_BASE_DIR_DEF  LV_BASE_DIR_LTR
#define LV_USE_ARABIC_PERSIAN_CHARS 0

/*==================
   WIDGET USAGE
 *==================*/
#define LV_USE_ARC         1
#define LV_USE_BAR         1
#define LV_USE_BTN         1
#define LV_USE_BTNMATRIX   1
#define LV_USE_CANVAS      0
#define LV_USE_CHECKBOX    1
#define LV_USE_DROPDOWN    1
#define LV_USE_IMG         1
#define LV_USE_LABEL       1
#define LV_LABEL_TEXT_SELECTION 0
#define LV_LABEL_LONG_TXT_HINT  0
#define LV_USE_LINE        1
#define LV_USE_ROLLER      0
#define LV_ROLLER_INF_PAGES 7
#define LV_USE_SLIDER      1
#define LV_USE_SWITCH      1
#define LV_USE_TEXTAREA    1
#define LV_TEXTAREA_DEF_PWD_SHOW_TIME 1500
#define LV_USE_TABLE       0

/*==================
   EXTRA COMPONENTS
 *==================*/
#define LV_USE_FLEX        1
#define LV_USE_GRID        1
#define LV_USE_ANIMIMG     0
#define LV_USE_CALENDAR    0
#define LV_USE_CHART       0
#define LV_USE_COLORWHEEL  0
#define LV_USE_IMGBTN      0
#define LV_USE_KEYBOARD    1
#define LV_USE_LED         0
#define LV_USE_LIST        1
#define LV_USE_MENU        0
#define LV_USE_METER       0
#define LV_USE_MSGBOX      1
#define LV_USE_SPAN        0
#define LV_USE_SPINBOX     0
#define LV_USE_SPINNER     1
#define LV_USE_TABVIEW     1
#define LV_USE_TILEVIEW    0
#define LV_USE_WIN         0

/*==================
   THEMES
 *==================*/
#define LV_USE_THEME_DEFAULT  1
#define LV_THEME_DEFAULT_DARK 1
#define LV_THEME_DEFAULT_GROW 1
#define LV_THEME_DEFAULT_TRANSITION_TIME 80
#define LV_USE_THEME_SIMPLE   0
#define LV_USE_THEME_MONO     0

/*==================
   LAYOUTS
 *==================*/
#define LV_USE_FLEX 1
#define LV_USE_GRID 1

/*=======================
   3RD PARTY LIBRARIES
 *=======================*/
#define LV_USE_FS_STDIO    0
#define LV_USE_FS_POSIX    0
#define LV_USE_FS_WIN32    0
#define LV_USE_FS_FATFS    0
#define LV_USE_PNG         0
#define LV_USE_BMP         0
#define LV_USE_SJPG        0
#define LV_USE_GIF         0
#define LV_USE_QRCODE      0
#define LV_USE_FFMPEG      0
#define LV_USE_RLOTTIE     0

/*==================
   OTHERS
 *==================*/
#define LV_USE_SNAPSHOT    0
#define LV_USE_MONKEY      0
#define LV_USE_GRIDNAV     0
#define LV_USE_FRAGMENT    0
#define LV_USE_IMGFONT     0
#define LV_USE_MSG         0
#define LV_USE_IME_PINYIN  0

#endif /* LV_CONF_H */
