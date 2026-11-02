#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "rikky_module_internal.h"

typedef int(__cdecl *GetPixelFilteredExFn)(void *editp, int n, void *pixelp, int *w, int *h, uint32_t format);

// shift-jis figure names: haikei, en, hoshigata, sankakukei, shikakukei, gokakukei, rokkakukei
static const char k_fig_bg[] = "\x94\x77\x8c\x69";
static const char k_fig_circle[] = "\x89\x7e";
static const char k_fig_star[] = "\x90\xaf\x8c\x5e";
static const char k_fig_tri[] = "\x8e\x4f\x8a\x70\x8c\x60";
static const char k_fig_quad[] = "\x8e\x6c\x8a\x70\x8c\x60";
static const char k_fig_pent[] = "\x8c\xdc\x8a\x70\x8c\x60";
static const char k_fig_hex[] = "\x98\x5a\x8a\x70\x8c\x60";

static int g_sceneload_depth = 0;
static uint8_t g_sceneload_mask[16] = {0};

static int g_reload_depth = 0;
static int g_reload_root_h = 0;
static int g_reload_root_w = 0;
static void *g_reload_buf = NULL;
static uint8_t g_reload_mask[16] = {0};

static const char *oof_figure_str_from_id(int id)
{
    switch (id)
    {
    case 0:
        return k_fig_bg;
    case 2:
        return k_fig_star;
    case 3:
        return k_fig_tri;
    case 4:
        return k_fig_quad;
    case 5:
        return k_fig_pent;
    case 6:
        return k_fig_hex;
    default:
        return k_fig_circle;
    }
}

static int __cdecl oof_set_color(lua_State *L)
{
    lua_setfield(L, 1, "color");
    return 0;
}

static int __cdecl oof_get_color(lua_State *L)
{
    lua_getfield(L, 1, "color");
    return 1;
}

static int __cdecl oof_set_size(lua_State *L)
{
    lua_setfield(L, 1, "size");
    return 0;
}

static int __cdecl oof_get_size(lua_State *L)
{
    lua_getfield(L, 1, "size");
    return 1;
}

static int __cdecl oof_get_figure_str(lua_State *L)
{
    lua_getfield(L, 1, "figure");
    return 1;
}

static int __cdecl oof_set_figure(lua_State *L)
{
    int id;
    if (lua_type(L, 2) != LUA_TNUMBER)
    {
        return 0;
    }
    id = (int)lua_tointeger(L, 2);
    lua_pushstring(L, oof_figure_str_from_id(id));
    lua_setfield(L, 1, "figure");
    return 0;
}

static int __cdecl oof_get_figure(lua_State *L)
{
    const char *s;
    lua_getfield(L, 1, "figure");
    s = lua_tolstring(L, -1, NULL);
    if (s)
    {
        if (strcmp(s, k_fig_bg) == 0)
        {
            lua_pushinteger(L, 0);
        }
        else if (strcmp(s, k_fig_circle) == 0)
        {
            lua_pushinteger(L, 1);
        }
        else if (strcmp(s, k_fig_star) == 0)
        {
            lua_pushinteger(L, 2);
        }
        else if (strcmp(s, k_fig_tri) == 0)
        {
            lua_pushinteger(L, 3);
        }
        else if (strcmp(s, k_fig_quad) == 0)
        {
            lua_pushinteger(L, 4);
        }
        else if (strcmp(s, k_fig_pent) == 0)
        {
            lua_pushinteger(L, 5);
        }
        else if (strcmp(s, k_fig_hex) == 0)
        {
            lua_pushinteger(L, 6);
        }
    }
    return 1;
}

static int __cdecl oof_set_figure_str(lua_State *L)
{
    const char *s;
    if (lua_type(L, 2) != LUA_TSTRING)
    {
        return 0;
    }
    s = lua_tolstring(L, 2, NULL);
    if (!s)
    {
        return 0;
    }
    if (strcmp(s, k_fig_bg) != 0 &&
        strcmp(s, k_fig_circle) != 0 &&
        strcmp(s, k_fig_star) != 0 &&
        strcmp(s, k_fig_tri) != 0 &&
        strcmp(s, k_fig_quad) != 0 &&
        strcmp(s, k_fig_pent) != 0 &&
        strcmp(s, k_fig_hex) != 0)
    {
        return 0;
    }
    lua_setfield(L, 1, "figure");
    return 0;
}

static int __cdecl oof_get_rgb(lua_State *L)
{
    uint32_t col;
    lua_getfield(L, 1, "color");
    col = (uint32_t)lua_tointeger(L, -1);
    lua_pushinteger(L, (lua_Integer)(((int32_t)col >> 16) & 0xff));
    lua_pushinteger(L, (lua_Integer)((col >> 8) & 0xffu));
    lua_pushinteger(L, (lua_Integer)(col & 0xffu));
    return 3;
}

static int __cdecl oof_set_rgb(lua_State *L)
{
    int r = (int)lua_tointeger(L, 2);
    int g = (int)lua_tointeger(L, 3);
    int b = (int)lua_tointeger(L, 4);
    lua_pushinteger(L, (lua_Integer)((r * 256 + g) * 256 + b));
    lua_setfield(L, 1, "color");
    return 0;
}

static int __cdecl oof_set_line(lua_State *L)
{
    double v = lua_tonumber(L, 2);
    if (v < 0.0)
    {
        v = 0.0;
    }
    lua_pushnumber(L, v);
    lua_setfield(L, 1, "line");
    return 0;
}

static int __cdecl oof_get_line(lua_State *L)
{
    double v;
    lua_getfield(L, 1, "line");
    v = lua_tonumber(L, -1);
    if (v == -1.0)
    {
        lua_getfield(L, 1, "size");
    }
    return 1;
}

static int __cdecl oof_set_zoom(lua_State *L)
{
    double v = lua_tonumber(L, 2);
    lua_pushnumber(L, v * 0.01);
    lua_setfield(L, 1, "zoom");
    return 0;
}

static int __cdecl oof_get_zoom(lua_State *L)
{
    double v;
    lua_getfield(L, 1, "zoom");
    v = lua_tonumber(L, 2);
    lua_pushnumber(L, v * 100.0);
    return 1;
}

static int __cdecl oof_set_alpha(lua_State *L)
{
    double v = lua_tonumber(L, 2);
    lua_pushnumber(L, v * 0.01);
    lua_setfield(L, 1, "alpha");
    return 0;
}

static int __cdecl oof_get_alpha(lua_State *L)
{
    double v;
    lua_getfield(L, 1, "alpha");
    v = lua_tonumber(L, 2);
    lua_pushnumber(L, v * 100.0);
    return 1;
}

static int __cdecl oof_set_pos(lua_State *L)
{
    lua_getfield(L, 1, "pos");
    lua_insert(L, 2);
    lua_setfield(L, 2, "z");
    lua_setfield(L, 2, "y");
    lua_setfield(L, 2, "x");
    lua_setfield(L, 1, "pos");
    return 0;
}

static int __cdecl oof_get_pos(lua_State *L)
{
    lua_getfield(L, 1, "pos");
    lua_getfield(L, 2, "x");
    lua_getfield(L, 2, "y");
    lua_getfield(L, 2, "z");
    return 3;
}

static int __cdecl oof_shift_pos(lua_State *L)
{
    double dx, dy, dz;
    double cx, cy, cz;

    lua_getfield(L, 1, "pos");
    lua_getfield(L, -1, "x");
    lua_getfield(L, -2, "y");
    lua_getfield(L, -3, "z");

    dx = lua_tonumber(L, 2);
    cx = lua_tonumber(L, -3);
    lua_pushnumber(L, cx + dx);

    dy = lua_tonumber(L, 3);
    cy = lua_tonumber(L, -3);
    lua_pushnumber(L, cy + dy);

    dz = lua_tonumber(L, 4);
    cz = lua_tonumber(L, -3);
    lua_pushnumber(L, cz + dz);

    lua_setfield(L, 5, "z");
    lua_setfield(L, 5, "y");
    lua_setfield(L, 5, "x");
    lua_settop(L, -4);
    lua_setfield(L, 1, "pos");
    return 0;
}

static int __cdecl oof_set_angle(lua_State *L)
{
    lua_getfield(L, 1, "angle");
    lua_insert(L, 2);
    lua_setfield(L, 2, "z");
    lua_setfield(L, 2, "y");
    lua_setfield(L, 2, "x");
    lua_setfield(L, 1, "angle");
    return 0;
}

static int __cdecl oof_get_angle(lua_State *L)
{
    lua_getfield(L, 1, "angle");
    lua_getfield(L, 2, "x");
    lua_getfield(L, 2, "y");
    lua_getfield(L, 2, "z");
    return 3;
}

static int __cdecl oof_shift_angle(lua_State *L)
{
    double dx, dy, dz;
    double cx, cy, cz;

    lua_getfield(L, 1, "angle");
    lua_getfield(L, -1, "x");
    lua_getfield(L, -2, "y");
    lua_getfield(L, -3, "z");

    dx = lua_tonumber(L, 2);
    cx = lua_tonumber(L, -3);
    lua_pushnumber(L, cx + dx);

    dy = lua_tonumber(L, 3);
    cy = lua_tonumber(L, -3);
    lua_pushnumber(L, cy + dy);

    dz = lua_tonumber(L, 4);
    cz = lua_tonumber(L, -3);
    lua_pushnumber(L, cz + dz);

    lua_setfield(L, 5, "z");
    lua_setfield(L, 5, "y");
    lua_setfield(L, 5, "x");
    lua_settop(L, -4);
    lua_setfield(L, 1, "angle");
    return 0;
}

static int __cdecl oof_set_effect(lua_State *L)
{
    int nargs = lua_gettop(L) - 1;
    int len;

    lua_createtable(L, 0, 0);
    lua_insert(L, 2);
    while (nargs > 0)
    {
        lua_rawseti(L, 2, nargs);
        nargs--;
    }
    lua_getfield(L, 1, "effect");
    len = (int)lua_objlen(L, -1);
    lua_insert(L, 2);
    lua_rawseti(L, 2, len + 1);
    return 0;
}

static int __cdecl oof_get_effect(lua_State *L)
{
    int eff_n;
    int i;

    lua_createtable(L, 0, 0);
    lua_getfield(L, 1, "effect");
    eff_n = (int)lua_objlen(L, -1);
    for (i = 1; i <= eff_n; i++)
    {
        int arg_n;
        int j;
        lua_rawgeti(L, -1, i);
        arg_n = (int)lua_objlen(L, -1);
        lua_createtable(L, 0, 0);
        for (j = 1; j <= arg_n; j++)
        {
            lua_rawgeti(L, -2, j);
            lua_rawseti(L, -2, j);
        }
        lua_rawseti(L, 2, i);
        lua_settop(L, -2);
    }
    lua_settop(L, -2);
    return 1;
}

static int __cdecl oof_clear_effect(lua_State *L)
{
    lua_createtable(L, 0, 0);
    lua_setfield(L, 1, "effect");
    return 0;
}

static int __cdecl oof_remove_effect(lua_State *L)
{
    int target = (int)lua_tointeger(L, 2);
    int total;
    int out_idx;
    int i;

    lua_getfield(L, 1, "effect");
    total = (int)lua_objlen(L, -1);
    lua_createtable(L, 0, 0);

    if (target < 2)
    {
        out_idx = 0;
    }
    else
    {
        for (i = 1; i != target; i++)
        {
            int arg_n;
            int j;
            lua_rawgeti(L, -2, i);
            arg_n = (int)lua_objlen(L, -1);
            lua_createtable(L, 0, 0);
            for (j = 1; j <= arg_n; j++)
            {
                lua_rawgeti(L, -2, j);
                lua_rawseti(L, -2, j);
            }
            lua_rawseti(L, -3, i);
            lua_settop(L, -2);
        }
        out_idx = target - 1;
    }

    while (1)
    {
        int arg_n;
        int j;
        target++;
        if (target > total)
        {
            break;
        }
        out_idx++;
        lua_rawgeti(L, -2, target);
        arg_n = (int)lua_objlen(L, -1);
        lua_createtable(L, 0, 0);
        for (j = 1; j <= arg_n; j++)
        {
            lua_rawgeti(L, -2, j);
            lua_rawseti(L, -2, j);
        }
        lua_rawseti(L, -3, out_idx);
        lua_settop(L, -2);
    }

    lua_setfield(L, 1, "effect");
    return 0;
}

static int __cdecl oof_draw(lua_State *L)
{
    double px, py, pz;
    double ax, ay, az;
    double color_num;
    int line_w, size_v, zoom_i, alpha_i;
    const char *fig_str;
    void *src_px;
    int orig_w, orig_h;
    size_t byte_size;
    void *saved_px;
    double s_aspect, s_alpha, s_zoom;
    double s_rz, s_ry, s_rx;
    double s_cz, s_cy, s_cx;
    double s_oz, s_oy, s_ox;
    int eff_n, i;
    int max_dim;
    float fig_aspect;

    lua_getfield(L, 1, "pos");
    lua_getfield(L, -1, "x");
    lua_getfield(L, -2, "y");
    lua_getfield(L, -3, "z");
    px = lua_tonumber(L, -3);
    py = lua_tonumber(L, -2);
    pz = lua_tonumber(L, -1);

    lua_getfield(L, 1, "angle");
    lua_getfield(L, -1, "x");
    lua_getfield(L, -2, "y");
    lua_getfield(L, -3, "z");
    ax = lua_tonumber(L, -3);
    ay = lua_tonumber(L, -2);
    az = lua_tonumber(L, -1);

    lua_getfield(L, 1, "color");
    color_num = lua_tonumber(L, -1);

    lua_getfield(L, 1, "line");
    line_w = (int)lua_tointeger(L, -1);

    lua_getfield(L, 1, "size");
    size_v = (int)lua_tointeger(L, -1);

    lua_getfield(L, 1, "zoom");
    zoom_i = (int)lua_tointeger(L, -1);

    lua_getfield(L, 1, "alpha");
    alpha_i = (int)lua_tointeger(L, -1);

    lua_getfield(L, 1, "figure");
    fig_str = lua_tolstring(L, -1, NULL);

    lua_settop(L, -15);
    if (line_w == -1)
    {
        line_w = size_v;
    }

    lua_getglobal(L, "obj");
    lua_getfield(L, -1, "getpixeldata");
    lua_call(L, 0, 3);
    src_px = lua_touserdata(L, -3);
    orig_w = (int)lua_tointeger(L, -2);
    orig_h = (int)lua_tointeger(L, -1);
    byte_size = (size_t)orig_w * (size_t)orig_h * 4u;
    saved_px = rm_malloc(byte_size);
    if (!saved_px)
    {
        byte_size = 0;
        saved_px = src_px;
    }
    else if (src_px && byte_size > 0)
    {
        memcpy(saved_px, src_px, byte_size);
    }

    lua_getfield(L, -4, "ox");
    lua_getfield(L, -5, "oy");
    lua_getfield(L, -6, "oz");
    lua_getfield(L, -7, "cx");
    lua_getfield(L, -8, "cy");
    lua_getfield(L, -9, "cz");
    lua_getfield(L, -10, "rx");
    lua_getfield(L, -11, "ry");
    lua_getfield(L, -12, "rz");
    lua_getfield(L, -13, "zoom");
    lua_getfield(L, -14, "alpha");
    lua_getfield(L, -15, "aspect");

    s_aspect = lua_tonumber(L, -1);
    s_alpha = lua_tonumber(L, -2);
    s_zoom = lua_tonumber(L, -3);
    s_rz = lua_tonumber(L, -4);
    s_ry = lua_tonumber(L, -5);
    s_rx = lua_tonumber(L, -6);
    s_cz = lua_tonumber(L, -7);
    s_cy = lua_tonumber(L, -8);
    s_cx = lua_tonumber(L, -9);
    s_oz = lua_tonumber(L, -10);
    s_oy = lua_tonumber(L, -11);
    s_ox = lua_tonumber(L, -12);
    lua_settop(L, -16);

    lua_getfield(L, -1, "load");
    lua_pushstring(L, "figure");
    lua_pushstring(L, fig_str ? fig_str : k_fig_circle);
    lua_pushinteger(L, (int)color_num);
    lua_pushinteger(L, size_v);
    lua_pushinteger(L, line_w);
    lua_call(L, 5, 0);

    lua_getfield(L, 1, "effect");
    eff_n = (int)lua_objlen(L, -1);
    for (i = 1; i <= eff_n; i++)
    {
        int arg_n;
        int j;
        lua_rawgeti(L, -1, i);
        arg_n = (int)lua_objlen(L, -1);
        lua_getfield(L, -3, "effect");
        for (j = 1; j <= arg_n; j++)
        {
            lua_rawgeti(L, -(j + 1), j);
        }
        lua_call(L, arg_n, 0);
        lua_settop(L, -2);
    }
    lua_settop(L, -2);

    lua_getfield(L, -1, "draw");
    lua_pushnumber(L, px);
    lua_pushnumber(L, py);
    lua_pushnumber(L, pz);
    lua_pushnumber(L, (double)zoom_i);
    lua_pushnumber(L, (double)alpha_i);
    lua_pushnumber(L, ax);
    lua_pushnumber(L, ay);
    lua_pushnumber(L, az);
    lua_call(L, 8, 0);

    lua_getfield(L, -1, "setoption");
    lua_pushstring(L, "draw_state");
    lua_pushboolean(L, 0);
    lua_call(L, 2, 0);

    lua_getfield(L, -1, "load");
    lua_pushstring(L, "figure");
    lua_pushstring(L, k_fig_quad);
    lua_pushinteger(L, 0);
    max_dim = (orig_w <= orig_h) ? orig_h : orig_w;
    lua_pushinteger(L, max_dim);
    lua_pushinteger(L, 100);
    if (orig_w < orig_h)
    {
        fig_aspect = 1.0f - (float)orig_w / (float)orig_h;
    }
    else
    {
        fig_aspect = (float)orig_h / (float)orig_w - 1.0f;
    }
    lua_pushnumber(L, (double)fig_aspect);
    lua_call(L, 6, 0);

    lua_getfield(L, -1, "putpixeldata");
    lua_pushlightuserdata(L, saved_px);
    lua_call(L, 1, 0);

    lua_pushnumber(L, s_aspect);
    lua_pushnumber(L, s_alpha);
    lua_pushnumber(L, s_zoom);
    lua_pushnumber(L, s_rz);
    lua_pushnumber(L, s_ry);
    lua_pushnumber(L, s_rx);
    lua_pushnumber(L, s_cz);
    lua_pushnumber(L, s_cy);
    lua_pushnumber(L, s_cx);
    lua_pushnumber(L, s_oz);
    lua_pushnumber(L, s_oy);
    lua_pushnumber(L, s_ox);
    lua_setfield(L, -13, "ox");
    lua_setfield(L, -12, "oy");
    lua_setfield(L, -11, "oz");
    lua_setfield(L, -10, "cx");
    lua_setfield(L, -9, "cy");
    lua_setfield(L, -8, "cz");
    lua_setfield(L, -7, "rx");
    lua_setfield(L, -6, "ry");
    lua_setfield(L, -5, "rz");
    lua_setfield(L, -4, "zoom");
    lua_setfield(L, -3, "alpha");
    lua_setfield(L, -2, "aspect");
    lua_settop(L, -2);

    if (byte_size != 0)
    {
        rm_free(saved_px);
    }
    return 0;
}

static int __cdecl oof_copy(lua_State *L);

static void oof_register_methods(lua_State *L, int tbl_idx)
{
    lua_pushcclosure(L, oof_draw, 0);
    lua_setfield(L, tbl_idx, "Draw");
    lua_pushcclosure(L, oof_set_figure, 0);
    lua_setfield(L, tbl_idx, "SetFigure");
    lua_pushcclosure(L, oof_get_figure, 0);
    lua_setfield(L, tbl_idx, "GetFigure");
    lua_pushcclosure(L, oof_set_figure_str, 0);
    lua_setfield(L, tbl_idx, "SetFigureStr");
    lua_pushcclosure(L, oof_get_figure_str, 0);
    lua_setfield(L, tbl_idx, "GetFigureStr");
    lua_pushcclosure(L, oof_set_size, 0);
    lua_setfield(L, tbl_idx, "SetSize");
    lua_pushcclosure(L, oof_get_size, 0);
    lua_setfield(L, tbl_idx, "GetSize");
    lua_pushcclosure(L, oof_set_color, 0);
    lua_setfield(L, tbl_idx, "SetColor");
    lua_pushcclosure(L, oof_get_color, 0);
    lua_setfield(L, tbl_idx, "GetColor");
    lua_pushcclosure(L, oof_set_rgb, 0);
    lua_setfield(L, tbl_idx, "SetRGB");
    lua_pushcclosure(L, oof_get_rgb, 0);
    lua_setfield(L, tbl_idx, "GetRGB");
    lua_pushcclosure(L, oof_set_line, 0);
    lua_setfield(L, tbl_idx, "SetLine");
    lua_pushcclosure(L, oof_get_line, 0);
    lua_setfield(L, tbl_idx, "GetLine");
    lua_pushcclosure(L, oof_set_zoom, 0);
    lua_setfield(L, tbl_idx, "SetZoom");
    lua_pushcclosure(L, oof_get_zoom, 0);
    lua_setfield(L, tbl_idx, "GetZoom");
    lua_pushcclosure(L, oof_set_alpha, 0);
    lua_setfield(L, tbl_idx, "SetAlpha");
    lua_pushcclosure(L, oof_get_alpha, 0);
    lua_setfield(L, tbl_idx, "GetAlpha");
    lua_pushcclosure(L, oof_set_pos, 0);
    lua_setfield(L, tbl_idx, "SetPos");
    lua_pushcclosure(L, oof_get_pos, 0);
    lua_setfield(L, tbl_idx, "GetPos");
    lua_pushcclosure(L, oof_shift_pos, 0);
    lua_setfield(L, tbl_idx, "ShiftPos");
    lua_pushcclosure(L, oof_set_angle, 0);
    lua_setfield(L, tbl_idx, "SetAngle");
    lua_pushcclosure(L, oof_get_angle, 0);
    lua_setfield(L, tbl_idx, "GetAngle");
    lua_pushcclosure(L, oof_shift_angle, 0);
    lua_setfield(L, tbl_idx, "ShiftAngle");
    lua_pushcclosure(L, oof_set_effect, 0);
    lua_setfield(L, tbl_idx, "SetEffect");
    lua_pushcclosure(L, oof_get_effect, 0);
    lua_setfield(L, tbl_idx, "GetEffect");
    lua_pushcclosure(L, oof_clear_effect, 0);
    lua_setfield(L, tbl_idx, "ClearEffect");
    lua_pushcclosure(L, oof_remove_effect, 0);
    lua_setfield(L, tbl_idx, "RemoveEffect");
    lua_pushcclosure(L, oof_copy, 0);
    lua_setfield(L, tbl_idx, "Copy");
}

static int __cdecl oof_copy(lua_State *L)
{
    int eff_n;
    int i;

    lua_settop(L, 1);
    lua_createtable(L, 0, 0);

    lua_getfield(L, 1, "color");
    lua_setfield(L, 2, "color");
    lua_getfield(L, 1, "figure");
    lua_setfield(L, 2, "figure");
    lua_getfield(L, 1, "zoom");
    lua_setfield(L, 2, "zoom");
    lua_getfield(L, 1, "line");
    lua_setfield(L, 2, "line");
    lua_getfield(L, 1, "alpha");
    lua_setfield(L, 2, "alpha");
    lua_getfield(L, 1, "size");
    lua_setfield(L, 2, "size");

    lua_createtable(L, 0, 0);
    lua_getfield(L, 1, "effect");
    eff_n = (int)lua_objlen(L, -1);
    for (i = 1; i <= eff_n; i++)
    {
        int arg_n;
        int j;
        lua_rawgeti(L, -1, i);
        arg_n = (int)lua_objlen(L, -1);
        lua_createtable(L, 0, 0);
        for (j = 1; j <= arg_n; j++)
        {
            lua_rawgeti(L, -2, j);
            lua_rawseti(L, -2, j);
        }
        lua_rawseti(L, -4, i);
        lua_settop(L, -2);
    }
    lua_settop(L, -2);
    lua_setfield(L, 2, "effect");

    oof_register_methods(L, 2);

    lua_createtable(L, 0, 0);
    lua_getfield(L, 1, "angle");
    lua_getfield(L, -1, "x");
    lua_getfield(L, -2, "y");
    lua_getfield(L, -3, "z");
    lua_setfield(L, -5, "z");
    lua_setfield(L, -4, "y");
    lua_setfield(L, -3, "x");
    lua_settop(L, -2);
    lua_setfield(L, 2, "angle");

    lua_createtable(L, 0, 0);
    lua_getfield(L, 1, "pos");
    lua_getfield(L, -1, "x");
    lua_getfield(L, -2, "y");
    lua_getfield(L, -3, "z");
    lua_setfield(L, -5, "z");
    lua_setfield(L, -4, "y");
    lua_setfield(L, -3, "x");
    lua_settop(L, -2);
    lua_setfield(L, 2, "pos");

    return 1;
}

int __cdecl lua_rikky_ObjectOrientFigure(lua_State *L)
{
    int fig_id = 1;

    if (lua_gettop(L) != 0)
    {
        if (lua_type(L, 1) == LUA_TNUMBER)
        {
            fig_id = (int)lua_tointeger(L, 1);
        }
        else if (lua_type(L, 1) == LUA_TSTRING)
        {
            const char *s = lua_tolstring(L, 1, NULL);
            if (s)
            {
                if (strcmp(s, k_fig_bg) == 0)
                {
                    fig_id = 0;
                }
                else if (strcmp(s, k_fig_quad) == 0)
                {
                    fig_id = 4;
                }
                else if (strcmp(s, k_fig_tri) == 0)
                {
                    fig_id = 3;
                }
                else if (strcmp(s, k_fig_pent) == 0)
                {
                    fig_id = 5;
                }
                else if (strcmp(s, k_fig_hex) == 0)
                {
                    fig_id = 6;
                }
                else if (strcmp(s, k_fig_star) == 0)
                {
                    fig_id = 2;
                }
                else
                {
                    fig_id = 1;
                }
            }
        }
        lua_settop(L, 0);
    }

    lua_createtable(L, 0, 0);

    lua_createtable(L, 0, 0);
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 0.0);
    lua_setfield(L, -4, "x");
    lua_setfield(L, -3, "y");
    lua_setfield(L, -2, "z");
    lua_setfield(L, -2, "pos");

    lua_createtable(L, 0, 0);
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 0.0);
    lua_setfield(L, -4, "x");
    lua_setfield(L, -3, "y");
    lua_setfield(L, -2, "z");
    lua_setfield(L, -2, "angle");

    lua_pushnumber(L, 16777215.0);
    lua_setfield(L, -2, "color");

    lua_pushstring(L, oof_figure_str_from_id(fig_id));
    lua_setfield(L, -2, "figure");

    lua_pushnumber(L, 1.0);
    lua_setfield(L, -2, "zoom");
    lua_pushnumber(L, 1.0);
    lua_setfield(L, -2, "alpha");
    lua_pushnumber(L, 100.0);
    lua_setfield(L, -2, "size");
    lua_pushnumber(L, -1.0);
    lua_setfield(L, -2, "line");

    lua_createtable(L, 0, 0);
    lua_setfield(L, -2, "effect");

    oof_register_methods(L, 1);
    return 1;
}

int __cdecl lua_rikky_effect(lua_State *L)
{
    int nargs = lua_gettop(L);
    int idx, num;
    uintptr_t *pp_aa;
    uintptr_t *p_ctx;
    uintptr_t off_flag;
    uint8_t *p_flag;
    uint8_t old_flag;
    uint32_t *p_99;
    uint32_t saved_99;

    if (nargs < 2)
    {
        lua_pushboolean(L, 0);
        return 1;
    }

    idx = (int)lua_tointeger(L, 1);
    num = (int)lua_tointeger(L, 2);
    if (num < 1)
    {
        num = 1;
    }
    if (idx < 0)
    {
        idx = 0;
    }
    else if (idx >= num)
    {
        idx = num - 1;
    }

    pp_aa = (uintptr_t *)RikkyGetMemoryData(0xaa);
    if (pp_aa && *pp_aa)
    {
        *(int *)(*pp_aa + 0x6c) = idx;
        *(int *)(*pp_aa + 0x70) = num;
    }

    p_ctx = (uintptr_t *)RikkyGetMemoryData(0x72);
    off_flag = (uintptr_t)RikkyGetMemoryData(0x73);
    p_flag = (p_ctx && *p_ctx) ? (uint8_t *)(*p_ctx + off_flag + 2u) : NULL;
    old_flag = p_flag ? *p_flag : 0;

    p_99 = (uint32_t *)RikkyGetMemoryData(0x99);
    saved_99 = p_99 ? *p_99 : 0;

    if (p_flag && (old_flag & 2u) == 0)
    {
        *p_flag = (uint8_t)(old_flag | 2u);
        lua_getglobal(L, "obj");
        lua_pushinteger(L, idx);
        lua_setfield(L, -2, "index");
        lua_pushinteger(L, num);
        lua_setfield(L, -2, "num");
        lua_getfield(L, -1, "effect");
        lua_insert(L, 3);
        lua_settop(L, -2);
        lua_call(L, nargs - 2, 0);
        *p_flag = old_flag;
    }
    else
    {
        lua_getglobal(L, "obj");
        lua_pushinteger(L, idx);
        lua_setfield(L, -2, "index");
        lua_pushinteger(L, num);
        lua_setfield(L, -2, "num");
        lua_getfield(L, -1, "effect");
        lua_insert(L, 3);
        lua_settop(L, -2);
        lua_call(L, nargs - 2, 0);
    }

    if (p_99)
    {
        *p_99 = saved_99;
    }
    return 0;
}

int __cdecl lua_rikky_sceneload(lua_State *L)
{
    uintptr_t exedit_ver = (uintptr_t)RikkyGetMemoryData(100);
    int nargs;
    uint32_t zero_px = 0;
    int obj_idx;

    if (exedit_ver > 0x2d && (nargs = lua_gettop(L)) != 0)
    {
        uint32_t target_scene;
        uintptr_t cur_obj;
        uintptr_t off_layer;
        int cur_scene;

        if (nargs == 1)
        {
            target_scene = (uint32_t)lua_tointeger(L, 1);
            if (target_scene < 50u)
            {
                uintptr_t scene_tbl = (uintptr_t)RikkyGetMemoryData(0xc3);
                lua_pushinteger(L, *(int *)(scene_tbl + 0x58u + target_scene * 0x60u) + 1);
                return 1;
            }
            return 0;
        }

        target_scene = (uint32_t)lua_tointeger(L, 1);
        cur_obj = (uintptr_t)GetObjectProcessIndex(0);
        off_layer = (uintptr_t)RikkyGetMemoryData(0x67);
        cur_scene = cur_obj ? *(int *)(cur_obj + off_layer + 4u) : -1;

        if (target_scene < 50u && cur_scene >= 0 && cur_scene < 128)
        {
            int byte_idx = cur_scene >> 3;
            uint32_t bit_idx = (uint32_t)(cur_scene & 7);

            if (((g_sceneload_mask[byte_idx] >> bit_idx) & 1u) == 0)
            {
                uint32_t *p_total_objs = (uint32_t *)RikkyGetMemoryData(0x93);
                uint32_t total_objs = p_total_objs ? *p_total_objs : 0;
                uintptr_t *obj_ptrs = (uintptr_t *)RikkyGetMemoryData(0xae);
                int *p_scene_obj_cnt = (int *)RikkyGetMemoryData(0xc5);
                int saved_scene_obj_cnt = p_scene_obj_cnt ? *p_scene_obj_cnt : 0;

                if (total_objs != 0 && obj_ptrs && p_scene_obj_cnt)
                {
                    uint32_t start_idx = 0;
                    uint32_t end_idx = 0;

                    if (*(uint32_t *)(obj_ptrs[0] + off_layer + 4u) == target_scene)
                    {
                        start_idx = 0;
                        end_idx = 1;
                    }
                    else
                    {
                        uint32_t k = 0;
                        while (1)
                        {
                            uint32_t prev = k;
                            k = prev + 1u;
                            if (k == total_objs)
                            {
                                start_idx = total_objs;
                                end_idx = 0;
                                break;
                            }
                            if (*(uint32_t *)(obj_ptrs[k] + off_layer + 4u) == target_scene)
                            {
                                start_idx = k;
                                end_idx = prev + 2u;
                                break;
                            }
                        }
                    }

                    if (start_idx != total_objs)
                    {
                        while (end_idx < total_objs &&
                               *(uint32_t *)(obj_ptrs[end_idx] + off_layer + 4u) == target_scene)
                        {
                            end_idx++;
                        }

                        {
                            int req_frame = (int)lua_tointeger(L, 2);
                            uintptr_t *p_editp = (uintptr_t *)RikkyGetMemoryData(3);
                            uintptr_t editp = (p_editp && *p_editp) ? *p_editp : 0;
                            uintptr_t scene_tbl = (uintptr_t)RikkyGetMemoryData(0xc3);
                            uintptr_t scene_entry = scene_tbl + target_scene * 0x60u;
                            int scene_w = *(int *)(scene_entry + 0x38u);
                            int scene_h = *(int *)(scene_entry + 0x3cu);
                            int max_frame = *(int *)(scene_entry + 0x58u);
                            uint32_t *p_c7;
                            uint32_t saved_c7;
                            uint32_t saved_editp_318;
                            uint32_t *p_render_scene;
                            uint32_t saved_render_scene;
                            uint32_t target_cnt;
                            uint8_t *active_flags;
                            uintptr_t *target_list;
                            uint8_t *yc_backup;
                            uint8_t *bgr24_buf;
                            uint32_t *bgra_buf;
                            uint32_t saved_layers[100];

                            if (max_frame == 0)
                            {
                                uintptr_t off_fn = (uintptr_t)RikkyGetMemoryData(7);
                                max_frame = *(int *)(editp + off_fn) - 1;
                            }
                            if (scene_w == 0)
                            {
                                int *def_wh = (int *)RikkyGetMemoryData(0xc4);
                                scene_w = def_wh[0];
                            }
                            if (scene_h == 0)
                            {
                                int *def_wh = (int *)RikkyGetMemoryData(0xc4);
                                scene_h = def_wh[1];
                            }

                            p_c7 = (uint32_t *)RikkyGetMemoryData(199);
                            saved_c7 = *p_c7;
                            saved_editp_318 = *(uint32_t *)(editp + 0x318u);
                            p_render_scene = (uint32_t *)RikkyGetMemoryData(0xbd);
                            saved_render_scene = *p_render_scene;

                            target_cnt = end_idx - start_idx;
                            active_flags = (uint8_t *)rm_malloc(end_idx ? end_idx : 1u);
                            target_list = (uintptr_t *)rm_malloc((target_cnt ? target_cnt : 1u) * sizeof(uintptr_t));
                            yc_backup = (uint8_t *)rm_malloc((size_t)scene_w * (size_t)scene_h * 6u);
                            memset(saved_layers, 0, sizeof(saved_layers));
                            bgr24_buf = (uint8_t *)rm_malloc((size_t)scene_w * (size_t)scene_h * 3u + 4u);
                            bgra_buf = (uint32_t *)rm_malloc((size_t)scene_w * (size_t)scene_h * 4u);

                            if (active_flags && target_list && yc_backup && bgr24_buf && bgra_buf)
                            {
                                uint32_t idx;
                                uintptr_t *p_yc_edit = (uintptr_t *)RikkyGetMemoryData(0x1c);
                                uint8_t *yc_edit = p_yc_edit ? (uint8_t *)(*p_yc_edit) : NULL;
                                size_t row_yc_bytes = (size_t)scene_w * 6u;
                                int *p_max_w = (int *)RikkyGetMemoryData(0x13);
                                size_t yc_stride = (size_t)((p_max_w ? *p_max_w : 1920) + 8) * 6u;
                                int has_yc_backup = 0;
                                uintptr_t layer_tbl = (uintptr_t)RikkyGetMemoryData(0x97);
                                uintptr_t render_layer_base = layer_tbl + saved_render_scene * 800u;
                                uintptr_t target_layer_base = layer_tbl + target_scene * 800u;
                                uint32_t *p_91 = (uint32_t *)RikkyGetMemoryData(0x91);
                                uint32_t saved_91_0 = p_91[0];
                                uint32_t saved_91_1 = p_91[1];
                                uint32_t *p_19 = (uint32_t *)RikkyGetMemoryData(0x19);
                                uint32_t *p_1a = (uint32_t *)RikkyGetMemoryData(0x1a);
                                uint32_t saved_19 = *p_19;
                                uint32_t saved_1a = *p_1a;
                                int *p_b0 = (int *)RikkyGetMemoryData(0xb0);
                                int saved_b0 = *p_b0;
                                GetPixelFilteredExFn get_pixel_ex = (GetPixelFilteredExFn)RikkyGetMemoryData(0x18);
                                uint8_t mask_bit = (uint8_t)(1u << bit_idx);
                                int i, y;

                                for (idx = start_idx; idx < end_idx; idx++)
                                {
                                    uintptr_t obj = obj_ptrs[idx];
                                    uint8_t was_active = (*(int *)(obj + 4u) != -1) ? 1u : 0u;
                                    active_flags[idx] = was_active;
                                    if (!was_active)
                                    {
                                        *(uint32_t *)(obj + 4u) = *(uint32_t *)(obj + off_layer);
                                    }
                                    target_list[idx - start_idx] = obj;
                                }

                                idx = start_idx;
                                while (idx > 0)
                                {
                                    uintptr_t obj;
                                    uint8_t was_active;
                                    idx--;
                                    obj = obj_ptrs[idx];
                                    was_active = (*(int *)(obj + 4u) != -1) ? 1u : 0u;
                                    active_flags[idx] = was_active;
                                    if (was_active)
                                    {
                                        *(int *)(obj + 4u) = -1;
                                    }
                                    obj_ptrs[target_cnt + idx] = obj;
                                }

                                memcpy(obj_ptrs, target_list, target_cnt * sizeof(uintptr_t));

                                if (yc_edit)
                                {
                                    for (y = 0; y < scene_h; y++)
                                    {
                                        memcpy(yc_backup + (size_t)y * row_yc_bytes,
                                               yc_edit + (size_t)y * yc_stride,
                                               row_yc_bytes);
                                    }
                                    has_yc_backup = 1;
                                }

                                for (i = 0; i < 100; i++)
                                {
                                    saved_layers[i] = *(uint32_t *)(render_layer_base + (size_t)i * 8u);
                                    *(uint32_t *)(render_layer_base + (size_t)i * 8u) =
                                        *(uint32_t *)(target_layer_base + (size_t)i * 8u);
                                }

                                *p_scene_obj_cnt = (int)target_cnt;
                                *p_c7 = 100u;
                                *(int *)(editp + 0x318u) = max_frame + 1;
                                *p_b0 = saved_b0 - 1;
                                *p_render_scene = target_scene;

                                g_sceneload_depth++;
                                g_sceneload_mask[byte_idx] |= mask_bit;
                                if (req_frame > max_frame)
                                {
                                    req_frame = max_frame;
                                }
                                get_pixel_ex((void *)editp, req_frame, bgr24_buf, NULL, NULL, 0);
                                g_sceneload_depth--;
                                g_sceneload_mask[byte_idx] &= (uint8_t)(~mask_bit);

                                *p_render_scene = saved_render_scene;
                                *p_b0 = saved_b0;
                                *p_19 = saved_19;
                                *p_1a = saved_1a;

                                for (i = 0; i < 100; i++)
                                {
                                    *(uint32_t *)(render_layer_base + (size_t)i * 8u) = saved_layers[i];
                                }

                                if (has_yc_backup)
                                {
                                    for (y = 0; y < scene_h; y++)
                                    {
                                        memcpy(yc_edit + (size_t)y * yc_stride,
                                               yc_backup + (size_t)y * row_yc_bytes,
                                               row_yc_bytes);
                                    }
                                }

                                *p_scene_obj_cnt = saved_scene_obj_cnt;
                                *p_c7 = saved_c7;
                                *(uint32_t *)(editp + 0x318u) = saved_editp_318;
                                p_91[0] = saved_91_0;
                                p_91[1] = saved_91_1;

                                if (scene_h > 0 && scene_w > 0)
                                {
                                    size_t dst_pos = 0;
                                    for (y = 0; y < scene_h; y++)
                                    {
                                        const uint8_t *src_row =
                                            bgr24_buf + (size_t)(scene_h - 1 - y) * (size_t)scene_w * 3u;
                                        int x;
                                        for (x = 0; x < scene_w; x++)
                                        {
                                            uint32_t b = src_row[0];
                                            uint32_t g = src_row[1];
                                            uint32_t r = src_row[2];
                                            bgra_buf[dst_pos++] = b | (g << 8) | (r << 16) | 0xff000000u;
                                            src_row += 3;
                                        }
                                    }
                                }

                                rm_free(bgr24_buf);

                                for (idx = 0; idx < start_idx; idx++)
                                {
                                    uintptr_t obj = obj_ptrs[idx + target_cnt];
                                    if (active_flags[idx] == 0)
                                    {
                                        *(int *)(obj + 4u) = -1;
                                    }
                                    else
                                    {
                                        *(uint32_t *)(obj + 4u) = *(uint32_t *)(obj + off_layer);
                                    }
                                    obj_ptrs[idx] = obj;
                                }

                                for (idx = start_idx; idx < end_idx; idx++)
                                {
                                    uintptr_t obj = target_list[idx - start_idx];
                                    obj_ptrs[idx] = obj;
                                    if (active_flags[idx] == 0)
                                    {
                                        *(int *)(obj + 4u) = -1;
                                    }
                                    else
                                    {
                                        *(uint32_t *)(obj + 4u) = *(uint32_t *)(obj + off_layer);
                                    }
                                }

                                rm_free(active_flags);
                                rm_free(target_list);
                                rm_free(yc_backup);

                                lua_getglobal(L, "obj");
                                obj_idx = lua_gettop(L);
                                rm_update_obj_size(L, scene_w, scene_h, obj_idx);
                                lua_getfield(L, -1, "putpixeldata");
                                lua_pushlightuserdata(L, bgra_buf);
                                lua_call(L, 1, 0);
                                rm_free(bgra_buf);
                                lua_getfield(L, -1, "getpixeldata");
                                lua_call(L, 0, 0);
                                lua_pushboolean(L, 1);
                                return 1;
                            }

                            rm_free(bgra_buf);
                            rm_free(bgr24_buf);
                            rm_free(active_flags);
                            rm_free(target_list);
                            rm_free(yc_backup);
                        }
                    }
                }
            }
        }
    }

    lua_getglobal(L, "obj");
    obj_idx = lua_gettop(L);
    rm_update_obj_size(L, 1, 1, obj_idx);
    lua_getfield(L, -1, "putpixeldata");
    lua_pushlightuserdata(L, &zero_px);
    lua_call(L, 1, 0);
    lua_pushboolean(L, 0);
    return 1;
}

int __cdecl lua_rikky_reloadbuffer(lua_State *L)
{
    uintptr_t exedit_ver = (uintptr_t)RikkyGetMemoryData(100);
    int has_scene = 0;
    int cur_layer = 0;
    int scene_idx = 0;
    int start_frame = 0;
    int byte_idx = 0;
    uint32_t bit_idx = 0;
    int nargs;
    int want_buf = 0;
    int has_layer_mask = 0;
    uint8_t hide_layer[100];
    int target_frame;
    int is_relative = 1;
    uintptr_t *p_editp;
    uintptr_t editp;
    GetPixelFilteredExFn get_pixel_ex;
    int orig_w = 0;
    int orig_h = 0;
    uint8_t *orig_bgra;

    if (exedit_ver < 0x1e)
    {
        uintptr_t cur_obj = (uintptr_t)GetObjectProcessIndex(0);
        if (cur_obj)
        {
            cur_layer = *(int *)(cur_obj + 4u);
            start_frame = *(int *)(cur_obj + 8u);
        }
        nargs = lua_gettop(L);
    }
    else
    {
        uintptr_t cur_obj = (uintptr_t)GetObjectProcessIndex(0);
        uintptr_t off_layer = (uintptr_t)RikkyGetMemoryData(0x67);
        if (cur_obj)
        {
            int mid_idx = *(int *)(cur_obj + 0x50u);
            cur_layer = *(int *)(cur_obj + off_layer);
            scene_idx = *(int *)(cur_obj + off_layer + 4u);
            if (mid_idx != -1)
            {
                uintptr_t obj_stride = (uintptr_t)RikkyGetMemoryData(0x6d);
                uintptr_t *p_obj_tbl = (uintptr_t *)RikkyGetMemoryData(0x6c);
                if (p_obj_tbl && *p_obj_tbl)
                {
                    cur_obj = *p_obj_tbl + (size_t)mid_idx * obj_stride;
                }
            }
            start_frame = *(int *)(cur_obj + 8u);
        }
        has_scene = 1;
        if (scene_idx >= 0 && scene_idx < 128)
        {
            byte_idx = scene_idx >> 3;
            bit_idx = (uint32_t)(scene_idx & 7);
        }
        nargs = lua_gettop(L);
    }

    if (nargs == 0)
    {
        lua_pushboolean(L, (int)((g_reload_mask[byte_idx] >> bit_idx) & 1u));
        return 1;
    }

    if (lua_type(L, 3) == LUA_TBOOLEAN && lua_toboolean(L, 3) != 0)
    {
        if (((g_reload_mask[byte_idx] >> bit_idx) & 1u) != 0)
        {
            int w = 0;
            int h = 0;
            if (has_scene)
            {
                uintptr_t scene_tbl = (uintptr_t)RikkyGetMemoryData(0xc3);
                uintptr_t scene_stride = (uintptr_t)RikkyGetMemoryData(0xcb);
                uintptr_t entry = scene_tbl + (size_t)scene_idx * scene_stride;
                w = *(int *)(entry + 0x38u);
                h = *(int *)(entry + 0x3cu);
                if (w == 0)
                {
                    int *def_wh = (int *)RikkyGetMemoryData(0xc4);
                    w = def_wh[0];
                }
                if (h == 0)
                {
                    int *def_wh = (int *)RikkyGetMemoryData(0xc4);
                    h = def_wh[1];
                }
            }
            else
            {
                uintptr_t *pe = (uintptr_t *)RikkyGetMemoryData(3);
                uintptr_t off_wh = (uintptr_t)RikkyGetMemoryData(8);
                int *wh = (int *)(*pe + off_wh);
                w = wh[0];
                h = wh[1];
            }
            if (g_reload_buf)
            {
                rm_free(g_reload_buf);
            }
            g_reload_buf = rm_calloc((size_t)w * (size_t)h, 4u);
            if (g_reload_buf)
            {
                lua_pushlightuserdata(L, g_reload_buf);
                lua_pushinteger(L, w);
                lua_pushinteger(L, h);
                return 3;
            }
            lua_pushnil(L);
            return 1;
        }
        want_buf = 1;
    }
    else
    {
        if (((g_reload_mask[byte_idx] >> bit_idx) & 1u) != 0)
        {
            lua_pushnil(L);
            return 1;
        }
        want_buf = 0;
    }

    memset(hide_layer, 0, sizeof(hide_layer));
    if (lua_type(L, 4) == LUA_TSTRING)
    {
        const char *mode_s = lua_tolstring(L, 4, NULL);
        int invert = (lua_type(L, 5) == LUA_TBOOLEAN && lua_toboolean(L, 5) != 0);
        if (mode_s && _stricmp(mode_s, "SELF") == 0)
        {
            if (invert)
            {
                if ((uint32_t)cur_layer < 100u)
                {
                    hide_layer[cur_layer] = 1;
                }
            }
            else
            {
                if (cur_layer > 0 && cur_layer <= 100)
                {
                    memset(hide_layer, 1, (size_t)cur_layer);
                }
                if (cur_layer + 1 < 100 && cur_layer >= -1)
                {
                    memset(hide_layer + cur_layer + 1, 1, (size_t)(99 - cur_layer));
                }
            }
            has_layer_mask = 1;
        }
        else if (mode_s && _stricmp(mode_s, "OTHER") == 0)
        {
            if (invert)
            {
                if (cur_layer > 0 && cur_layer <= 100)
                {
                    memset(hide_layer, 1, (size_t)cur_layer);
                }
                if (cur_layer + 1 < 100 && cur_layer >= -1)
                {
                    memset(hide_layer + cur_layer + 1, 1, (size_t)(99 - cur_layer));
                }
            }
            else
            {
                if ((uint32_t)cur_layer < 100u)
                {
                    hide_layer[cur_layer] = 1;
                }
            }
            has_layer_mask = 1;
        }
    }
    else if (lua_type(L, 4) == LUA_TTABLE)
    {
        int invert = (lua_type(L, 5) == LUA_TBOOLEAN && lua_toboolean(L, 5) != 0);
        int tbl_len = (int)lua_objlen(L, 4);
        int i;
        if (invert)
        {
            memset(hide_layer, 1, sizeof(hide_layer));
            for (i = 1; i <= tbl_len; i++)
            {
                int lyr;
                lua_rawgeti(L, 4, i);
                lyr = (int)lua_tointeger(L, -1);
                lua_remove(L, -1);
                if ((uint32_t)(lyr - 1) < 100u)
                {
                    hide_layer[lyr - 1] = 0;
                    has_layer_mask = 1;
                }
            }
        }
        else
        {
            for (i = 1; i <= tbl_len; i++)
            {
                int lyr;
                lua_rawgeti(L, 4, i);
                lyr = (int)lua_tointeger(L, -1);
                lua_remove(L, -1);
                if ((uint32_t)(lyr - 1) < 100u)
                {
                    hide_layer[lyr - 1] = 1;
                    has_layer_mask = 1;
                }
            }
        }
    }

    target_frame = (int)lua_tointeger(L, 1);
    if (lua_type(L, 2) == LUA_TSTRING)
    {
        const char *rel_s = lua_tolstring(L, 2, NULL);
        if (rel_s && (_stricmp(rel_s, "absolute") == 0 || _stricmp(rel_s, "abs") == 0))
        {
            is_relative = 0;
        }
    }

    lua_settop(L, 0);
    p_editp = (uintptr_t *)RikkyGetMemoryData(3);
    editp = (p_editp && *p_editp) ? *p_editp : 0;
    get_pixel_ex = (GetPixelFilteredExFn)RikkyGetMemoryData(0x18);

    lua_getglobal(L, "obj");
    orig_bgra = rm_read_exedit_bgra(L, 0, &orig_w, &orig_h);
    if (!orig_bgra)
    {
        lua_pushnil(L);
        return 1;
    }

    {
        double sv_x, sv_y, sv_z;
        double sv_ox, sv_oy, sv_oz;
        double sv_cx, sv_cy, sv_cz;
        double sv_rx, sv_ry, sv_rz;
        double sv_zoom, sv_alpha, sv_aspect;
        double sv_w, sv_h;
        double sv_frame, sv_totalframe, sv_time, sv_totaltime;
        double sv_layer, sv_index, sv_num;
        double sv_track0, sv_track1, sv_track2, sv_track3;
        int sv_check0;

        uint32_t *p_96 = (uint32_t *)RikkyGetMemoryData(0x96);
        uint32_t saved_96_1 = p_96[1];
        uint32_t *p_19 = (uint32_t *)RikkyGetMemoryData(0x19);
        uint32_t *p_1a = (uint32_t *)RikkyGetMemoryData(0x1a);
        uint32_t saved_19 = *p_19;
        uint32_t saved_1a = *p_1a;
        int *p_b0 = (int *)RikkyGetMemoryData(0xb0);
        int saved_b0 = *p_b0;

        int scene_w = 0;
        int scene_h = 0;
        int max_frame = 0;
        uint32_t start_idx = 0;
        uint32_t end_idx = 0;
        uint32_t target_cnt = 0;
        uintptr_t *obj_ptrs = NULL;
        uintptr_t off_layer = 0;
        int *p_scene_obj_cnt = NULL;
        int saved_scene_obj_cnt = 0;
        uint8_t *active_flags = NULL;
        uintptr_t *target_list = NULL;
        uint8_t *yc_backup = NULL;
        uint8_t *yc_edit = NULL;
        size_t row_yc_bytes = 0;
        size_t yc_stride = 0;
        int has_yc_backup = 0;
        uint32_t *p_c7 = NULL;
        uint32_t saved_c7 = 0;
        uint32_t saved_editp_318 = 0;
        int *p_render_scene_wh = NULL;
        int saved_rs_w = 0;
        int saved_rs_h = 0;
        uint32_t *p_91 = NULL;
        uint32_t saved_91_0 = 0;
        uint32_t saved_91_1 = 0;
        uint32_t *layer_base = NULL;
        uint32_t saved_layers[100];
        uint8_t *bgr24_buf = NULL;
        int got_buf = 0;
        uint8_t mask_bit;
        int obj_idx;

        lua_getfield(L, 1, "x");
        sv_x = lua_tonumber(L, -1);
        lua_getfield(L, 1, "y");
        sv_y = lua_tonumber(L, -1);
        lua_getfield(L, 1, "z");
        sv_z = lua_tonumber(L, -1);
        lua_getfield(L, 1, "ox");
        sv_ox = lua_tonumber(L, -1);
        lua_getfield(L, 1, "oy");
        sv_oy = lua_tonumber(L, -1);
        lua_getfield(L, 1, "oz");
        sv_oz = lua_tonumber(L, -1);
        lua_getfield(L, 1, "cx");
        sv_cx = lua_tonumber(L, -1);
        lua_getfield(L, 1, "cy");
        sv_cy = lua_tonumber(L, -1);
        lua_getfield(L, 1, "cz");
        sv_cz = lua_tonumber(L, -1);
        lua_getfield(L, 1, "rx");
        sv_rx = lua_tonumber(L, -1);
        lua_getfield(L, 1, "ry");
        sv_ry = lua_tonumber(L, -1);
        lua_getfield(L, 1, "rz");
        sv_rz = lua_tonumber(L, -1);
        lua_getfield(L, 1, "zoom");
        sv_zoom = lua_tonumber(L, -1);
        lua_getfield(L, 1, "alpha");
        sv_alpha = lua_tonumber(L, -1);
        lua_getfield(L, 1, "aspect");
        sv_aspect = lua_tonumber(L, -1);
        lua_getfield(L, 1, "w");
        sv_w = lua_tonumber(L, -1);
        lua_getfield(L, 1, "h");
        sv_h = lua_tonumber(L, -1);
        lua_getfield(L, 1, "frame");
        sv_frame = lua_tonumber(L, -1);
        if (is_relative)
        {
            target_frame = (int)((double)target_frame + (double)start_frame + sv_frame);
        }
        lua_getfield(L, 1, "totalframe");
        sv_totalframe = lua_tonumber(L, -1);
        lua_getfield(L, 1, "time");
        sv_time = lua_tonumber(L, -1);
        lua_getfield(L, 1, "totaltime");
        sv_totaltime = lua_tonumber(L, -1);
        lua_getfield(L, 1, "layer");
        sv_layer = lua_tonumber(L, -1);
        lua_getfield(L, 1, "index");
        sv_index = lua_tonumber(L, -1);
        lua_getfield(L, 1, "num");
        sv_num = lua_tonumber(L, -1);
        lua_getfield(L, 1, "track0");
        sv_track0 = lua_tonumber(L, -1);
        lua_getfield(L, 1, "track1");
        sv_track1 = lua_tonumber(L, -1);
        lua_getfield(L, 1, "track2");
        sv_track2 = lua_tonumber(L, -1);
        lua_getfield(L, 1, "track3");
        sv_track3 = lua_tonumber(L, -1);
        lua_getfield(L, 1, "check0");
        sv_check0 = lua_toboolean(L, -1);
        lua_getfield(L, 1, "screen_w");
        (void)lua_tonumber(L, -1);
        lua_getfield(L, 1, "screen_h");
        (void)lua_tonumber(L, -1);
        lua_settop(L, 0);

        *p_b0 = saved_b0 - 1;

        if (has_scene)
        {
            uint32_t *p_render_scene = (uint32_t *)RikkyGetMemoryData(0xbd);
            uint32_t render_scene = *p_render_scene;
            uintptr_t scene_stride = (uintptr_t)RikkyGetMemoryData(0xcb);
            uint32_t *p_total_objs = (uint32_t *)RikkyGetMemoryData(0x93);
            uint32_t total_objs = *p_total_objs;
            uintptr_t scene_tbl;
            uintptr_t scene_entry;
            uintptr_t layer_tbl;
            uint32_t idx;
            int i, y;

            obj_ptrs = (uintptr_t *)RikkyGetMemoryData(0xae);
            off_layer = (uintptr_t)RikkyGetMemoryData(0x67);
            p_scene_obj_cnt = (int *)RikkyGetMemoryData(0xc5);
            saved_scene_obj_cnt = *p_scene_obj_cnt;

            if (total_objs != 0)
            {
                if (*(int *)(obj_ptrs[0] + off_layer + 4u) == scene_idx)
                {
                    start_idx = 0;
                    end_idx = 1;
                }
                else
                {
                    uint32_t k = 0;
                    while (1)
                    {
                        uint32_t prev = k;
                        k = prev + 1u;
                        if (k == total_objs)
                        {
                            start_idx = 0;
                            end_idx = 0;
                            break;
                        }
                        if (*(int *)(obj_ptrs[k] + off_layer + 4u) == scene_idx)
                        {
                            start_idx = k;
                            end_idx = prev + 2u;
                            break;
                        }
                    }
                }
                while (end_idx > 0 && end_idx < total_objs &&
                       *(int *)(obj_ptrs[end_idx] + off_layer + 4u) == scene_idx)
                {
                    end_idx++;
                }
            }

            scene_tbl = (uintptr_t)RikkyGetMemoryData(0xc3);
            scene_entry = scene_tbl + (size_t)scene_idx * scene_stride;
            scene_w = *(int *)(scene_entry + 0x38u);
            scene_h = *(int *)(scene_entry + 0x3cu);
            max_frame = *(int *)(scene_entry + 0x58u);
            if (scene_w == 0)
            {
                int *def_wh = (int *)RikkyGetMemoryData(0xc4);
                scene_w = def_wh[0];
            }
            if (scene_h == 0)
            {
                int *def_wh = (int *)RikkyGetMemoryData(0xc4);
                scene_h = def_wh[1];
            }
            if (max_frame == 0)
            {
                uintptr_t off_fn = (uintptr_t)RikkyGetMemoryData(7);
                max_frame = *(int *)(editp + off_fn) - 1;
            }
            if (g_reload_depth != 0 && (uint32_t)scene_idx == render_scene)
            {
                scene_w = g_reload_root_w;
                scene_h = g_reload_root_h;
            }

            target_cnt = end_idx - start_idx;
            active_flags = (uint8_t *)rm_malloc(end_idx ? end_idx : 1u);
            target_list = (uintptr_t *)rm_malloc((target_cnt ? target_cnt : 1u) * sizeof(uintptr_t));
            yc_backup = (uint8_t *)rm_malloc((size_t)scene_w * (size_t)scene_h * 6u);
            if (!active_flags || !target_list || !yc_backup)
            {
                *p_b0 = saved_b0;
                rm_free(active_flags);
                rm_free(target_list);
                rm_free(yc_backup);
                rm_free(orig_bgra);
                lua_pushnil(L);
                return 1;
            }

            p_c7 = (uint32_t *)RikkyGetMemoryData(199);
            saved_c7 = *p_c7;
            saved_editp_318 = *(uint32_t *)(editp + 0x318u);

            for (idx = start_idx; idx < end_idx; idx++)
            {
                uintptr_t obj = obj_ptrs[idx];
                uint8_t was_active = (*(int *)(obj + 4u) != -1) ? 1u : 0u;
                active_flags[idx] = was_active;
                if (!was_active)
                {
                    *(uint32_t *)(obj + 4u) = *(uint32_t *)(obj + off_layer);
                }
                target_list[idx - start_idx] = obj;
            }

            idx = start_idx;
            while (idx > 0)
            {
                uintptr_t obj;
                uint8_t was_active;
                idx--;
                obj = obj_ptrs[idx];
                was_active = (*(int *)(obj + 4u) != -1) ? 1u : 0u;
                active_flags[idx] = was_active;
                if (was_active)
                {
                    *(int *)(obj + 4u) = -1;
                }
                obj_ptrs[target_cnt + idx] = obj;
            }

            if (target_cnt > 0)
            {
                memcpy(obj_ptrs, target_list, target_cnt * sizeof(uintptr_t));
            }

            {
                uintptr_t *p_yc_edit = (uintptr_t *)RikkyGetMemoryData(0x1c);
                int *p_max_w = (int *)RikkyGetMemoryData(0x13);
                yc_edit = p_yc_edit ? (uint8_t *)(*p_yc_edit) : NULL;
                row_yc_bytes = (size_t)scene_w * 6u;
                yc_stride = (size_t)((p_max_w ? *p_max_w : 1920) + 8) * 6u;
                if (yc_edit)
                {
                    for (y = 0; y < scene_h; y++)
                    {
                        memcpy(yc_backup + (size_t)y * row_yc_bytes,
                               yc_edit + (size_t)y * yc_stride,
                               row_yc_bytes);
                    }
                    has_yc_backup = 1;
                }
            }

            layer_tbl = (uintptr_t)RikkyGetMemoryData(0x97);
            layer_base = (uint32_t *)(layer_tbl + render_scene * 800u);
            for (i = 0; i < 100; i++)
            {
                saved_layers[i] = layer_base[i * 2];
                if (hide_layer[i] != 0)
                {
                    layer_base[i * 2] = *(uint32_t *)(layer_tbl + (size_t)scene_idx * 800u + (size_t)i * 8u) | 1u;
                }
            }

            *p_scene_obj_cnt = (int)target_cnt;
            *p_c7 = 100u;
            *(int *)(editp + 0x318u) = max_frame + 1;

            p_render_scene_wh = (int *)(scene_tbl + 0x38u + render_scene * scene_stride);
            p_91 = (uint32_t *)RikkyGetMemoryData(0x91);
            saved_rs_w = p_render_scene_wh[0];
            saved_rs_h = p_render_scene_wh[1];
            saved_91_0 = p_91[0];
            saved_91_1 = p_91[1];
            p_render_scene_wh[0] = scene_w;
            p_render_scene_wh[1] = scene_h;
        }
        else
        {
            uintptr_t off_fn = (uintptr_t)RikkyGetMemoryData(7);
            max_frame = *(int *)(editp + off_fn) - 1;
            if (has_layer_mask)
            {
                uint32_t *ptr;
                int i;
                layer_base = (uint32_t *)RikkyGetMemoryData(0x97);
                ptr = layer_base;
                for (i = 0; i < 100; i++)
                {
                    uint32_t flg = *ptr;
                    saved_layers[i] = flg;
                    if (hide_layer[i] != 0)
                    {
                        *ptr = flg | 1u;
                    }
                    ptr += 0x11;
                }
            }
            if (want_buf)
            {
                uintptr_t off_wh = (uintptr_t)RikkyGetMemoryData(8);
                int *wh = (int *)(editp + off_wh);
                scene_w = wh[0];
                scene_h = wh[1];
            }
        }

        if (target_frame < 0)
        {
            target_frame = 0;
        }
        else if (target_frame > max_frame)
        {
            target_frame = max_frame;
        }

        if (g_reload_buf)
        {
            rm_free(g_reload_buf);
            g_reload_buf = NULL;
        }
        if (want_buf)
        {
            g_reload_buf = rm_malloc((size_t)scene_w * (size_t)scene_h * 4u);
            bgr24_buf = (uint8_t *)rm_malloc((size_t)scene_w * (size_t)scene_h * 3u);
            if (g_reload_buf && bgr24_buf)
            {
                got_buf = 1;
            }
            else
            {
                rm_free(g_reload_buf);
                rm_free(bgr24_buf);
                g_reload_buf = NULL;
                bgr24_buf = NULL;
                got_buf = 0;
            }
        }

        mask_bit = (uint8_t)(1u << bit_idx);
        g_reload_mask[byte_idx] |= mask_bit;
        p_96[1] = p_96[0];
        if (g_reload_depth == 0)
        {
            g_reload_root_w = scene_w;
            g_reload_root_h = scene_h;
        }
        g_reload_depth++;
        get_pixel_ex((void *)editp, target_frame, bgr24_buf, NULL, NULL, 0);
        g_reload_depth--;
        p_96[1] = saved_96_1;
        g_reload_mask[byte_idx] &= (uint8_t)(~mask_bit);

        if (got_buf && g_reload_buf)
        {
            uint8_t *dst = (uint8_t *)g_reload_buf;
            int y;
            for (y = 0; y < scene_h; y++)
            {
                const uint8_t *src = bgr24_buf + (size_t)(scene_h - 1 - y) * (size_t)scene_w * 3u;
                int x;
                for (x = 0; x < scene_w; x++)
                {
                    dst[0] = src[0];
                    dst[1] = src[1];
                    dst[2] = src[2];
                    dst[3] = 0xffu;
                    dst += 4;
                    src += 3;
                }
            }
            rm_free(bgr24_buf);
        }

        if (has_scene)
        {
            uint32_t idx;
            int i, y;

            for (i = 0; i < 100; i++)
            {
                layer_base[i * 2] = saved_layers[i];
            }
            if (has_yc_backup)
            {
                for (y = 0; y < scene_h; y++)
                {
                    memcpy(yc_edit + (size_t)y * yc_stride,
                           yc_backup + (size_t)y * row_yc_bytes,
                           row_yc_bytes);
                }
            }

            *p_scene_obj_cnt = saved_scene_obj_cnt;
            *p_c7 = saved_c7;
            *(uint32_t *)(editp + 0x318u) = saved_editp_318;
            p_render_scene_wh[0] = saved_rs_w;
            p_render_scene_wh[1] = saved_rs_h;
            p_91[0] = saved_91_0;
            p_91[1] = saved_91_1;

            for (idx = 0; idx < start_idx; idx++)
            {
                uintptr_t obj = obj_ptrs[idx + target_cnt];
                if (active_flags[idx] == 0)
                {
                    *(int *)(obj + 4u) = -1;
                }
                else
                {
                    *(uint32_t *)(obj + 4u) = *(uint32_t *)(obj + off_layer);
                }
                obj_ptrs[idx] = obj;
            }
            for (idx = start_idx; idx < end_idx; idx++)
            {
                uintptr_t obj = target_list[idx - start_idx];
                obj_ptrs[idx] = obj;
                if (active_flags[idx] == 0)
                {
                    *(int *)(obj + 4u) = -1;
                }
                else
                {
                    *(uint32_t *)(obj + 4u) = *(uint32_t *)(obj + off_layer);
                }
            }

            rm_free(active_flags);
            rm_free(target_list);
            rm_free(yc_backup);
        }
        else if (has_layer_mask)
        {
            uint32_t *ptr = layer_base;
            int i;
            for (i = 0; i < 100; i++)
            {
                if (hide_layer[i] != 0)
                {
                    *ptr = saved_layers[i];
                }
                ptr += 0x11;
            }
        }

        *p_b0 = saved_b0;
        *p_19 = saved_19;
        *p_1a = saved_1a;

        lua_settop(L, 0);
        lua_getglobal(L, "obj");
        obj_idx = lua_gettop(L);
        rm_update_obj_size(L, orig_w, orig_h, obj_idx);
        lua_getfield(L, -1, "putpixeldata");
        lua_pushlightuserdata(L, orig_bgra);
        lua_call(L, 1, 0);
        rm_free(orig_bgra);

        lua_pushnumber(L, sv_x);
        lua_setfield(L, 1, "x");
        lua_pushnumber(L, sv_y);
        lua_setfield(L, 1, "y");
        lua_pushnumber(L, sv_z);
        lua_setfield(L, 1, "z");
        lua_pushnumber(L, sv_ox);
        lua_setfield(L, 1, "ox");
        lua_pushnumber(L, sv_oy);
        lua_setfield(L, 1, "oy");
        lua_pushnumber(L, sv_oz);
        lua_setfield(L, 1, "oz");
        lua_pushnumber(L, sv_cx);
        lua_setfield(L, 1, "cx");
        lua_pushnumber(L, sv_cy);
        lua_setfield(L, 1, "cy");
        lua_pushnumber(L, sv_cz);
        lua_setfield(L, 1, "cz");
        lua_pushnumber(L, sv_rx);
        lua_setfield(L, 1, "rx");
        lua_pushnumber(L, sv_ry);
        lua_setfield(L, 1, "ry");
        lua_pushnumber(L, sv_rz);
        lua_setfield(L, 1, "rz");
        lua_pushnumber(L, sv_zoom);
        lua_setfield(L, 1, "zoom");
        lua_pushnumber(L, sv_alpha);
        lua_setfield(L, 1, "alpha");
        lua_pushnumber(L, sv_aspect);
        lua_setfield(L, 1, "aspect");
        lua_pushnumber(L, sv_w);
        lua_setfield(L, 1, "w");
        lua_pushnumber(L, sv_h);
        lua_setfield(L, 1, "h");
        lua_pushnumber(L, sv_frame);
        lua_setfield(L, 1, "frame");
        lua_pushnumber(L, sv_totalframe);
        lua_setfield(L, 1, "totalframe");
        lua_pushnumber(L, sv_time);
        lua_setfield(L, 1, "time");
        lua_pushnumber(L, sv_totaltime);
        lua_setfield(L, 1, "totaltime");
        lua_pushnumber(L, (double)(int)sv_layer);
        lua_setfield(L, 1, "layer");
        lua_pushnumber(L, (double)(int)sv_index);
        lua_setfield(L, 1, "index");
        lua_pushnumber(L, (double)(int)sv_num);
        lua_setfield(L, 1, "num");
        lua_pushnumber(L, sv_track0);
        lua_setfield(L, 1, "track0");
        lua_pushnumber(L, sv_track1);
        lua_setfield(L, 1, "track1");
        lua_pushnumber(L, sv_track2);
        lua_setfield(L, 1, "track2");
        lua_pushnumber(L, sv_track3);
        lua_setfield(L, 1, "track3");
        lua_pushboolean(L, sv_check0);
        lua_setfield(L, 1, "check0");

        if (got_buf)
        {
            lua_pushlightuserdata(L, g_reload_buf);
            lua_pushinteger(L, scene_w);
            lua_pushinteger(L, scene_h);
            return 3;
        }
    }

    lua_pushnil(L);
    return 1;
}
