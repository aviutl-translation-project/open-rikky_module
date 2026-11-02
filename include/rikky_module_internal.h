#pragma once
#ifndef RIKKY_MODULE_INTERNAL_H
#define RIKKY_MODULE_INTERNAL_H

#include <stdint.h>
#include <stddef.h>
#include "aviutl_sdk.h"
#include "lua51.h"
#include "rikky_memory.h"

// msvcrt heap wrappers so cross-dll frees with rikky_memory.auf share the exact heap
void *rm_malloc(size_t size);
void *rm_calloc(size_t count, size_t size);
void *rm_realloc(void *ptr, size_t size);
void rm_free(void *ptr);

// version checks and shared utilities
int rm_check_aviutl_ver(void);
int rm_check_exedit_ver(void);
void rm_str_tolower(char *s);
wchar_t *rm_sjis_html_to_wide(int *out_len, const char *s);
char *rm_wide_to_sjis_html(int *out_len, const wchar_t *ws);
void rm_update_obj_size(lua_State *L, int new_w, int new_h, int obj_idx);
uint8_t *rm_read_exedit_bgra(lua_State *L, int src_mode, int *out_w, int *out_h);
void rm_ensure_datasave_registered(lua_State *exedit_L);

// shared 3d vector and rotation helpers
void rm_vec3_normalize_or_default(double *v, const double *fallback, double skip_len_sq);
void rm_euler_set_trig(double rx, double ry, double rz);
void rm_euler_rotate_vec3(double *out_v, const double *in_v);
double *rm_axis_angle_rotate_vec3(double *out_v, const double *in_v, const double *axis, double angle);

// shadow / light state queried from exedit
typedef struct
{
    uint8_t enabled;
    uint8_t pad[7];
    double scale;
    double offset_y;
    double offset_x;
    double offset_z;
    double mat[9];
} RmShadowInfo;

RmShadowInfo *rm_query_shadow_info(RmShadowInfo *out, lua_State *L);

// info_sys.c (10 functions)
int __cdecl lua_rikky_getinfo(lua_State *L);
int __cdecl lua_rikky_dir(lua_State *L);
int __cdecl lua_rikky_ext(lua_State *L);
int __cdecl lua_rikky_find(lua_State *L);
int __cdecl lua_rikky_type(lua_State *L);
int __cdecl lua_rikky_key(lua_State *L);
int __cdecl lua_rikky_mouse(lua_State *L);
int __cdecl lua_rikky_desk(lua_State *L);
int __cdecl lua_rikky_settext(lua_State *L);
int __cdecl lua_rikky_progressbar(lua_State *L);

// data_text.c (7 functions)
int __cdecl lua_rikky_assign(lua_State *L);
int __cdecl lua_rikky_convert(lua_State *L);
int __cdecl lua_rikky_datasave(lua_State *L);
int __cdecl lua_rikky_textload(lua_State *L);
int __cdecl lua_rikky_textsplit(lua_State *L);
int __cdecl lua_rikky_string2table(lua_State *L);
int __cdecl lua_rikky_clipboard(lua_State *L);

// color_math.c (4 functions)
int __cdecl lua_rikky_colorconvert(lua_State *L);
int __cdecl lua_rikky_rotation(lua_State *L);
int __cdecl lua_rikky_axisconvert(lua_State *L);
int __cdecl lua_rikky_axisconvertEx(lua_State *L);

// dialog_ui.c (17 functions)
int __cdecl lua_rikky_file(lua_State *L);
int __cdecl lua_rikky_fold(lua_State *L);
int __cdecl lua_rikky_font(lua_State *L);
int __cdecl lua_rikky_list(lua_State *L);
int __cdecl lua_rikky_colordialog(lua_State *L);
int __cdecl lua_rikky_checkbox(lua_State *L);
int __cdecl lua_rikky_parameter(lua_State *L);
int __cdecl lua_rikky_setdialogparam(lua_State *L);
int __cdecl lua_rikky_fileCS(lua_State *L);
int __cdecl lua_rikky_foldCS(lua_State *L);
int __cdecl lua_rikky_fontCS(lua_State *L);
int __cdecl lua_rikky_listCS(lua_State *L);
int __cdecl lua_rikky_colordialogCS(lua_State *L);
int __cdecl lua_rikky_checkboxCS(lua_State *L);
int __cdecl lua_rikky_parameterCS(lua_State *L);
int __cdecl lua_rikky_setdialogparamCS(lua_State *L);
int __cdecl lua_rikky_trackbarwrite(lua_State *L);

// image_io.c (4 functions)
int __cdecl lua_rikky_png(lua_State *L);
int __cdecl lua_rikky_jpg(lua_State *L);
int __cdecl lua_rikky_bmp(lua_State *L);
int __cdecl lua_rikky_image(lua_State *L);

// pixel_fx.c (4 functions)
int __cdecl lua_rikky_bordering(lua_State *L);
int __cdecl lua_rikky_pixelfunction(lua_State *L);
int __cdecl lua_rikky_fillarea(lua_State *L);
int __cdecl lua_rikky_linedetection(lua_State *L);

// exedit_obj.c (4 functions)
int __cdecl lua_rikky_ObjectOrientFigure(lua_State *L);
int __cdecl lua_rikky_reloadbuffer(lua_State *L);
int __cdecl lua_rikky_effect(lua_State *L);
int __cdecl lua_rikky_sceneload(lua_State *L);

// camera_draw3d.c (6 functions)
int __cdecl lua_rikky_camerainfo(lua_State *L);
int __cdecl lua_rikky_glassdraw_init(lua_State *L);
int __cdecl lua_rikky_glassdraw(lua_State *L);
int __cdecl lua_rikky_materialdraw_init(lua_State *L);
int __cdecl lua_rikky_materialdraw(lua_State *L);
int __cdecl lua_rikky_materialdrawEx(lua_State *L);

// audio_midi.c (5 functions)
int __cdecl lua_rikky_audiobuffer(lua_State *L);
int __cdecl lua_rikky_midiconverter(lua_State *L);
int __cdecl lua_rikky_midiconverterEx(lua_State *L);
int __cdecl lua_rikky_soundregister(lua_State *L);
int __cdecl lua_rikky_wave(lua_State *L);

#endif // RIKKY_MODULE_INTERNAL_H
