#include <windows.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rikky_module_internal.h"

typedef void *(__cdecl *MsvcrtMallocFn)(size_t);
typedef void *(__cdecl *MsvcrtCallocFn)(size_t, size_t);
typedef void *(__cdecl *MsvcrtReallocFn)(void *, size_t);
typedef void(__cdecl *MsvcrtFreeFn)(void *);
typedef BOOL(__cdecl *Yc2RgbFn)(PIXEL *dst, const PIXEL_YC *src, int w);

static MsvcrtMallocFn g_msvcrt_malloc = NULL;
static MsvcrtCallocFn g_msvcrt_calloc = NULL;
static MsvcrtReallocFn g_msvcrt_realloc = NULL;
static MsvcrtFreeFn g_msvcrt_free = NULL;

static double g_euler_cos_x = 1.0;
static double g_euler_sin_x = 0.0;
static double g_euler_cos_y = 1.0;
static double g_euler_sin_y = 0.0;
static double g_euler_cos_z = 1.0;
static double g_euler_sin_z = 0.0;

static double g_axis_m00 = 1.0;
static double g_axis_m01 = 0.0;
static double g_axis_m02 = 0.0;
static double g_axis_m10 = 0.0;
static double g_axis_m11 = 1.0;
static double g_axis_m12 = 0.0;
static double g_axis_m20 = 0.0;
static double g_axis_m21 = 0.0;
static double g_axis_m22 = 1.0;

// error dialog caption and version mismatch messages (shift-jis)
static const char k_err_caption[] = "\x83\x47\x83\x89\x81\x5b";
static const char k_err_exedit_ver[] =
    "rikky_module.dll\x82\xcd\x8a\x67\x92\xa3\x95\xd2\x8f\x57\x82\xcc\x83\x6f"
    "\x81\x5b\x83\x57\x83\x87\x83\x93\x82\xc9\x91\xce\x89\x9e\x82\xb5\x82\xc4"
    "\x82\xa2\x82\xdc\x82\xb9\x82\xf1";
static const char k_err_aviutl_ver[] =
    "rikky_module.dll\x82\xcd"
    "AviUtl\x82\xcc\x83\x6f\x81\x5b\x83\x57\x83\x87"
    "\x83\x93\x82\xc9\x91\xce\x89\x9e\x82\xb5\x82\xc4\x82\xa2\x82\xdc\x82\xb9\x82\xf1";

static void ensure_msvcrt_heap(void)
{
    HMODULE hm;
    if (g_msvcrt_malloc)
    {
        return;
    }
    hm = GetModuleHandleA("msvcrt.dll");
    if (!hm)
    {
        hm = LoadLibraryA("msvcrt.dll");
    }
    if (hm)
    {
        g_msvcrt_malloc = (MsvcrtMallocFn)GetProcAddress(hm, "malloc");
        g_msvcrt_calloc = (MsvcrtCallocFn)GetProcAddress(hm, "calloc");
        g_msvcrt_realloc = (MsvcrtReallocFn)GetProcAddress(hm, "realloc");
        g_msvcrt_free = (MsvcrtFreeFn)GetProcAddress(hm, "free");
    }
}

void *rm_malloc(size_t size)
{
    ensure_msvcrt_heap();
    return g_msvcrt_malloc ? g_msvcrt_malloc(size) : malloc(size);
}

void *rm_calloc(size_t count, size_t size)
{
    ensure_msvcrt_heap();
    return g_msvcrt_calloc ? g_msvcrt_calloc(count, size) : calloc(count, size);
}

void *rm_realloc(void *ptr, size_t size)
{
    ensure_msvcrt_heap();
    return g_msvcrt_realloc ? g_msvcrt_realloc(ptr, size) : realloc(ptr, size);
}

void rm_free(void *ptr)
{
    if (!ptr)
    {
        return;
    }
    ensure_msvcrt_heap();
    if (g_msvcrt_free)
    {
        g_msvcrt_free(ptr);
    }
    else
    {
        free(ptr);
    }
}

int rm_check_aviutl_ver(void)
{
    uintptr_t ver = (uintptr_t)RikkyGetMemoryData(1);
    if (ver <= 0x2a)
    {
        MessageBoxA(NULL, k_err_aviutl_ver, k_err_caption, MB_OK);
        return 0;
    }
    return 1;
}

int rm_check_exedit_ver(void)
{
    uintptr_t ver = (uintptr_t)RikkyGetMemoryData(100);
    if (ver <= 0x1a)
    {
        MessageBoxA(NULL, k_err_exedit_ver, k_err_caption, MB_OK);
        return 0;
    }
    return 1;
}

void rm_str_tolower(char *s)
{
    size_t i;
    if (!s)
    {
        return;
    }
    i = strlen(s);
    while (i > 0)
    {
        i--;
        s[i] = (char)tolower((unsigned char)s[i]);
    }
}

wchar_t *rm_sjis_html_to_wide(int *out_len, const char *s)
{
    size_t slen;
    wchar_t *out;
    size_t i = 0;
    int wlen = 0;

    if (!s)
    {
        return NULL;
    }
    slen = strlen(s);
    out = (wchar_t *)rm_malloc(slen * sizeof(wchar_t) + sizeof(wchar_t));
    if (!out)
    {
        return NULL;
    }

    while (i < slen)
    {
        unsigned char b = (unsigned char)s[i];
        wchar_t wc = 0;

        if ((b >= 0x81 && b <= 0x9f) || (b >= 0xe0 && b <= 0xfc))
        {
            char mb[3];
            mb[0] = (char)b;
            mb[1] = (i + 1 < slen) ? s[i + 1] : '\0';
            mb[2] = '\0';
            if (mb[1] != '\0')
            {
                i++;
            }
            MultiByteToWideChar(932, 0, mb, -1, &wc, 1);
            i++;
        }
        else if (b == '&' && i + 2 < slen && s[i + 1] == '#' &&
                 (unsigned char)(s[i + 2] - '0') < 10u)
        {
            size_t j = i + 2;
            while (j < slen && (unsigned char)(s[j] - '0') < 10u)
            {
                j++;
            }
            if (j < slen && s[j] == ';')
            {
                unsigned int code = 0;
                size_t k;
                for (k = i + 2; k < j; k++)
                {
                    code = code * 10u + (unsigned int)(s[k] - '0');
                }
                wc = (wchar_t)code;
                i = j + 1;
            }
            else
            {
                wc = L'&';
                i++;
            }
        }
        else
        {
            char mb[2];
            mb[0] = (char)b;
            mb[1] = '\0';
            MultiByteToWideChar(932, 0, mb, -1, &wc, 1);
            i++;
        }

        out[wlen++] = wc;
    }

    out[wlen] = L'\0';
    if (out_len)
    {
        *out_len = wlen;
    }
    return out;
}

char *rm_wide_to_sjis_html(int *out_len, const wchar_t *ws)
{
    size_t wlen;
    size_t total = 0;
    size_t i;
    char *out;
    size_t pos = 0;

    if (!ws)
    {
        return NULL;
    }
    wlen = wcslen(ws);
    if (wlen == 0)
    {
        out = (char *)rm_calloc(1, 1);
        if (out && out_len)
        {
            *out_len = 0;
        }
        return out;
    }

    for (i = 0; i < wlen; i++)
    {
        char mb[8];
        BOOL used_def = FALSE;
        int n = WideCharToMultiByte(932, WC_NO_BEST_FIT_CHARS, &ws[i], 1, mb, 4, NULL, &used_def);
        if (n <= 0 || used_def || (mb[0] == '?' && ws[i] != L'?'))
        {
            char ent[16];
            total += (size_t)sprintf(ent, "&#%u;", (unsigned int)(unsigned short)ws[i]);
        }
        else
        {
            total += (size_t)n;
        }
    }

    out = (char *)rm_calloc(total + 1, 1);
    if (!out)
    {
        return NULL;
    }

    for (i = 0; i < wlen; i++)
    {
        char mb[8];
        BOOL used_def = FALSE;
        int n = WideCharToMultiByte(932, WC_NO_BEST_FIT_CHARS, &ws[i], 1, mb, 4, NULL, &used_def);
        if (n <= 0 || used_def || (mb[0] == '?' && ws[i] != L'?'))
        {
            pos += (size_t)sprintf(out + pos, "&#%u;", (unsigned int)(unsigned short)ws[i]);
        }
        else
        {
            memcpy(out + pos, mb, (size_t)n);
            pos += (size_t)n;
        }
    }

    out[pos] = '\0';
    if (out_len)
    {
        *out_len = (int)pos;
    }
    return out;
}

void rm_update_obj_size(lua_State *L, int new_w, int new_h, int obj_idx)
{
    int *p_put_mode = (int *)RikkyGetMemoryData(0xad);
    uintptr_t *p_ctx = (uintptr_t *)RikkyGetMemoryData(0x72);
    uintptr_t render_info;
    float zoom = 1.0f;
    float aspect = 0.0f;
    double out_h;

    if (p_put_mode && *p_put_mode == 3)
    {
        lua_getfield(L, obj_idx, "pixeloption");
        lua_pushstring(L, "put");
        lua_pushstring(L, "obj");
        lua_call(L, 2, 0);

        if (p_ctx && *p_ctx)
        {
            *(int *)(*p_ctx + 0xb4) = new_w;
            *(int *)(*p_ctx + 0xb8) = new_h;
        }

        lua_getfield(L, obj_idx, "pixeloption");
        lua_pushstring(L, "put");
        lua_pushstring(L, "frm");
        lua_call(L, 2, 0);
    }
    else if (p_ctx && *p_ctx)
    {
        *(int *)(*p_ctx + 0xb4) = new_w;
        *(int *)(*p_ctx + 0xb8) = new_h;
    }

    render_info = (uintptr_t)RikkyGetMemoryData(0xbc);
    if (render_info)
    {
        zoom = (float)(*(int *)(render_info + 0x24)) * (1.0f / 65536.0f);
        aspect = (float)(*(int *)(render_info + 0x28)) * (1.0f / 65536.0f);
    }

    if (aspect < 0.0f)
    {
        lua_pushnumber(L, (double)zoom);
        lua_setfield(L, obj_idx, "w");
        out_h = (double)zoom * ((double)aspect + 1.0);
    }
    else
    {
        out_h = (double)zoom;
        lua_pushnumber(L, (double)((1.0f - aspect) * zoom));
        lua_setfield(L, obj_idx, "w");
    }

    lua_pushnumber(L, out_h);
    lua_setfield(L, obj_idx, "h");
}

uint8_t *rm_read_exedit_bgra(lua_State *L, int src_mode, int *out_w, int *out_h)
{
    int *p_max_w = (int *)RikkyGetMemoryData(0x13);
    int max_w = p_max_w ? *p_max_w : 1920;
    const char *put_names[2] = {"obj", "frm"};
    const uint8_t *src_ptr = NULL;
    int w = 0;
    int h = 0;
    uint8_t *bgra;
    Yc2RgbFn yc2rgb;

    if (src_mode == 0 || src_mode == 1)
    {
        int *p_put_mode = (int *)RikkyGetMemoryData(0xad);
        int cur_mode = p_put_mode ? *p_put_mode : 0;

        if (cur_mode == src_mode + 3)
        {
            lua_getglobal(L, "obj");
            lua_getfield(L, -1, "pixeloption");
            lua_pushstring(L, "put");
            lua_pushstring(L, put_names[src_mode]);
            lua_call(L, 2, 0);
        }

        {
            void ***pp_buf = (void ***)RikkyGetMemoryData(0x9f);
            int **pp_w = (int **)RikkyGetMemoryData(0xaa);
            int **pp_h = (int **)RikkyGetMemoryData(0xab);
            if (pp_buf && *pp_buf)
            {
                src_ptr = (const uint8_t *)(**pp_buf);
            }
            if (pp_w && *pp_w)
            {
                w = **pp_w;
            }
            if (pp_h && *pp_h)
            {
                h = **pp_h;
            }
        }

        if (w * h <= 0 || !src_ptr)
        {
            if (cur_mode == src_mode + 3)
            {
                lua_pop(L, 1);
            }
            return NULL;
        }

        if (cur_mode == src_mode + 3)
        {
            lua_getfield(L, -1, "pixeloption");
            lua_pushstring(L, "put");
            lua_pushstring(L, put_names[1 - src_mode]);
            lua_call(L, 2, 0);
            lua_pop(L, 1);
        }
    }
    else if (src_mode == 2)
    {
        int *p_scene_dim = (int *)RikkyGetMemoryData(0x92);
        uintptr_t cache_tbl = (uintptr_t)RikkyGetMemoryData(0x9d);
        uintptr_t stride = (uintptr_t)RikkyGetMemoryData(0x9e);

        if (!p_scene_dim || !cache_tbl || !stride)
        {
            return NULL;
        }
        w = p_scene_dim[0];
        h = p_scene_dim[2];
        if (w * h <= 0)
        {
            return NULL;
        }

        for (uintptr_t entry = cache_tbl; *(const char *)(entry + 4) != '\0'; entry += stride)
        {
            if (strncmp((const char *)(entry + 4), "*cache_lua", 10) == 0)
            {
                uintptr_t base = *(uintptr_t *)entry;
                src_ptr = (const uint8_t *)(base + 0x120u + (size_t)max_w * 0x20u);
                break;
            }
        }
        if (!src_ptr)
        {
            return NULL;
        }
    }
    else
    {
        return NULL;
    }

    bgra = (uint8_t *)rm_malloc((size_t)w * (size_t)h * 4u);
    if (!bgra)
    {
        return NULL;
    }

    yc2rgb = (Yc2RgbFn)RikkyGetMemoryData(0x16);
    if (!yc2rgb)
    {
        rm_free(bgra);
        return NULL;
    }

    if (src_mode == 1)
    {
        PIXEL *row_bgr = (PIXEL *)rm_malloc((size_t)w * sizeof(PIXEL));
        size_t yc_row_bytes = (size_t)(max_w + 8) * sizeof(PIXEL_YC);
        int y;
        if (!row_bgr)
        {
            rm_free(bgra);
            return NULL;
        }
        for (y = 0; y < h; y++)
        {
            const PIXEL_YC *yc_row = (const PIXEL_YC *)(src_ptr + (size_t)y * yc_row_bytes);
            uint32_t *dst_row = (uint32_t *)(bgra + (size_t)y * (size_t)w * 4u);
            int x;
            yc2rgb(row_bgr, yc_row, w);
            for (x = 0; x < w; x++)
            {
                dst_row[x] = (uint32_t)row_bgr[x].b |
                             ((uint32_t)row_bgr[x].g << 8) |
                             ((uint32_t)row_bgr[x].r << 16) |
                             0xff000000u;
            }
        }
        rm_free(row_bgr);
    }
    else
    {
        size_t yca_row_bytes = (size_t)(max_w + 8) * 8u;
        int y;
        for (y = 0; y < h; y++)
        {
            const uint8_t *yca_row = src_ptr + (size_t)y * yca_row_bytes;
            uint32_t *dst_row = (uint32_t *)(bgra + (size_t)y * (size_t)w * 4u);
            int x;
            for (x = 0; x < w; x++)
            {
                PIXEL px = {0, 0, 0};
                int alpha_raw;
                uint32_t a8;
                yc2rgb(&px, (const PIXEL_YC *)(yca_row + (size_t)x * 8u), 1);
                alpha_raw = (int)(*(const int16_t *)(yca_row + (size_t)x * 8u + 6u)) * 255;
                if (alpha_raw < 0)
                {
                    alpha_raw += 0xfff;
                }
                a8 = (uint32_t)((alpha_raw >> 12) & 0xff);
                dst_row[x] = (uint32_t)px.b |
                             ((uint32_t)px.g << 8) |
                             ((uint32_t)px.r << 16) |
                             (a8 << 24);
            }
        }
    }

    if (out_w)
    {
        *out_w = w;
    }
    if (out_h)
    {
        *out_h = h;
    }
    return bgra;
}

void rm_vec3_normalize_or_default(double *v, const double *fallback, double skip_len_sq)
{
    double len_sq = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
    double len;
    if (len_sq == 0.0)
    {
        v[0] = fallback[0];
        v[1] = fallback[1];
        v[2] = fallback[2];
        return;
    }
    if (len_sq == skip_len_sq)
    {
        return;
    }
    len = sqrt(len_sq);
    v[0] /= len;
    v[1] /= len;
    v[2] /= len;
}

void rm_euler_set_trig(double rx, double ry, double rz)
{
    g_euler_cos_x = cos(rx);
    g_euler_sin_x = sin(rx);
    g_euler_cos_y = cos(ry);
    g_euler_sin_y = sin(ry);
    g_euler_cos_z = cos(rz);
    g_euler_sin_z = sin(rz);
}

void rm_euler_rotate_vec3(double *out_v, const double *in_v)
{
    double x0 = in_v[0];
    double y0 = in_v[1];
    double z0 = in_v[2];
    double xz = x0 * g_euler_cos_z - y0 * g_euler_sin_z;
    double yz = x0 * g_euler_sin_z + y0 * g_euler_cos_z;
    double zy = z0 * g_euler_cos_y - xz * g_euler_sin_y;

    out_v[0] = xz * g_euler_cos_y + g_euler_sin_y * z0;
    out_v[1] = yz * g_euler_cos_x - zy * g_euler_sin_x;
    out_v[2] = g_euler_cos_x * zy + yz * g_euler_sin_x;
}

double *rm_axis_angle_rotate_vec3(double *out_v, const double *in_v, const double *axis, double angle)
{
    double x, y, z;
    if (axis != NULL)
    {
        double ax = axis[0];
        double ay = axis[1];
        double az = axis[2];
        double s = sin(angle);
        double c = cos(angle);
        double omc = 1.0 - c;
        double xy = omc * ax * ay;
        double xz = omc * az * ax;
        double yz = omc * ay * az;

        g_axis_m00 = c + ax * ax * omc;
        g_axis_m01 = xy - az * s;
        g_axis_m02 = xz + ay * s;
        g_axis_m10 = xy + az * s;
        g_axis_m11 = ay * ay * omc + c;
        g_axis_m12 = yz - ax * s;
        g_axis_m20 = xz - ay * s;
        g_axis_m21 = yz + ax * s;
        g_axis_m22 = az * az * omc + c;
    }

    x = in_v[0];
    y = in_v[1];
    z = in_v[2];
    out_v[0] = g_axis_m00 * x + g_axis_m01 * y + g_axis_m02 * z;
    out_v[1] = g_axis_m10 * x + g_axis_m11 * y + g_axis_m12 * z;
    out_v[2] = g_axis_m20 * x + g_axis_m21 * y + g_axis_m22 * z;
    return out_v;
}

RmShadowInfo *rm_query_shadow_info(RmShadowInfo *out, lua_State *L)
{
    const char *p_mode_str;
    memset(out, 0, sizeof(*out));

    p_mode_str = (const char *)RikkyGetMemoryData(0x8a);
    if (p_mode_str && *p_mode_str != 't')
    {
        uint32_t *p_draw_state = (uint32_t *)RikkyGetMemoryData(0x6e);
        uint32_t saved_state = p_draw_state ? *p_draw_state : 0;
        uintptr_t *p_shadow_obj;

        lua_getglobal(L, "obj");
        lua_getfield(L, -1, "draw");
        lua_pushinteger(L, 0);
        lua_pushinteger(L, 0);
        lua_pushinteger(L, 0);
        lua_pushinteger(L, 0);
        lua_pushinteger(L, 0);
        lua_call(L, 5, 0);
        lua_remove(L, -1);

        if (p_draw_state)
        {
            *p_draw_state = saved_state;
        }

        p_shadow_obj = (uintptr_t *)RikkyGetMemoryData(0x82);
        if (p_shadow_obj && *p_shadow_obj != 0 && (uintptr_t)RikkyGetMemoryData(100) > 0x1c)
        {
            uintptr_t sh_obj = *p_shadow_obj;
            uintptr_t cur_obj = (uintptr_t)GetObjectProcessIndex(0);
            uintptr_t off_layer = (uintptr_t)RikkyGetMemoryData(0x67);
            uintptr_t layer_tbl = (uintptr_t)RikkyGetMemoryData(0x97);

            if (p_draw_state)
            {
                *p_draw_state = saved_state;
            }

            if (cur_obj && layer_tbl)
            {
                int *sh_layer_scene = (int *)(sh_obj + off_layer);
                int *cur_layer_scene = (int *)(cur_obj + off_layer);
                int s_frame = *(int *)(sh_obj + 8);
                int e_frame = *(int *)(sh_obj + 12);

                if (sh_layer_scene[1] == cur_layer_scene[1] &&
                    (*(uint8_t *)(layer_tbl + (size_t)(sh_layer_scene[1] * 100 + sh_layer_scene[0]) * 8u) & 1u) == 0)
                {
                    int cur_frame = 0;
                    if (*(int *)(cur_obj + 4) == -1)
                    {
                        uintptr_t *p_sub = (uintptr_t *)RikkyGetMemoryData(0x79);
                        uintptr_t off_f = (uintptr_t)RikkyGetMemoryData(0x7a);
                        if (p_sub && *p_sub)
                        {
                            cur_frame = *(int *)(*p_sub + off_f);
                        }
                    }
                    else
                    {
                        int *p_subwin = (int *)RikkyGetMemoryData(0x0b);
                        int *p_playing = (int *)RikkyGetMemoryData(0x0a);
                        if (p_subwin && *p_subwin == 1 && p_playing && *p_playing == 1)
                        {
                            int *p_pf = (int *)RikkyGetMemoryData(0x09);
                            if (p_pf)
                            {
                                cur_frame = *p_pf;
                            }
                        }
                        else
                        {
                            uintptr_t *p_editp = (uintptr_t *)RikkyGetMemoryData(3);
                            uintptr_t off_frame = (uintptr_t)RikkyGetMemoryData(6);
                            if (p_editp && *p_editp)
                            {
                                cur_frame = *(int *)(*p_editp + off_frame);
                            }
                        }
                    }

                    if (cur_frame >= s_frame && cur_frame <= e_frame)
                    {
                        uintptr_t off_td = (uintptr_t)RikkyGetMemoryData(0x68);
                        int layer_diff = cur_layer_scene[0] - sh_layer_scene[0];
                        int sh_td0 = *(int *)(sh_obj + off_td);
                        int *p_track_base = (int *)RikkyGetMemoryData(0x7b);
                        int range_layers = p_track_base ? *(int *)((uintptr_t)(*p_track_base) + (uintptr_t)sh_td0 + 4u) : 0;
                        uintptr_t off_grp = (uintptr_t)RikkyGetMemoryData(0x83);

                        if (layer_diff > 0 && (layer_diff <= range_layers || range_layers == 0) &&
                            (off_grp == 0 || *(int *)(sh_obj + off_grp) == 0 ||
                             ((int *)(sh_obj + off_td))[1] == ((int *)(cur_obj + off_td))[1]))
                        {
                            uint32_t *p_pos = (uint32_t *)RikkyGetMemoryData(0x84);
                            uint32_t *p_scl = (uint32_t *)RikkyGetMemoryData(0x85);
                            double *p_mat = (double *)RikkyGetMemoryData(0x86);

                            if (p_pos && p_scl && p_mat)
                            {
                                uint32_t raw_x = p_pos[0];
                                uint32_t raw_y = p_pos[1];
                                uint32_t raw_z = p_pos[2];
                                float scl = (float)(p_scl[0] & 0xffffu) / 65535.0f + (float)(p_scl[0] >> 16);
                                float inv_scl = 1.0f / scl;

                                out->enabled = 1;
                                out->scale = (double)scl;
                                out->offset_y = (double)((float)((int32_t)raw_y >> 12) + (float)(raw_y & 0xfffu) / 4095.0f);
                                out->offset_x = (double)((float)((int32_t)raw_x >> 12) + (float)(raw_x & 0xfffu) / 4095.0f);
                                out->offset_z = (double)((float)((int32_t)raw_z >> 12) + (float)(raw_z & 0xfffu) / 4095.0f);
                                out->mat[0] = (double)((float)p_mat[0] * inv_scl);
                                out->mat[1] = (double)((float)p_mat[3] * inv_scl);
                                out->mat[2] = (double)((float)p_mat[6] * inv_scl);
                                out->mat[3] = (double)((float)p_mat[1] * inv_scl);
                                out->mat[4] = (double)((float)p_mat[4] * inv_scl);
                                out->mat[5] = (double)((float)p_mat[7] * inv_scl);
                                out->mat[6] = (double)((float)p_mat[2] * inv_scl);
                                out->mat[7] = (double)((float)p_mat[5] * inv_scl);
                                out->mat[8] = (double)((float)p_mat[8] * inv_scl);
                                return out;
                            }
                        }
                    }
                }
            }
        }
    }

    out->enabled = 0;
    out->scale = 1.0;
    out->offset_y = 0.0;
    out->offset_x = 0.0;
    out->offset_z = 0.0;
    out->mat[0] = 1.0;
    out->mat[4] = 1.0;
    out->mat[8] = 1.0;
    return out;
}

static const luaL_Reg k_rikky_funcs[] =
    {
        {"getinfo", lua_rikky_getinfo},
        {"png", lua_rikky_png},
        {"jpg", lua_rikky_jpg},
        {"bmp", lua_rikky_bmp},
        {"dir", lua_rikky_dir},
        {"ext", lua_rikky_ext},
        {"key", lua_rikky_key},
        {"find", lua_rikky_find},
        {"type", lua_rikky_type},
        {"desk", lua_rikky_desk},
        {"file", lua_rikky_file},
        {"fold", lua_rikky_fold},
        {"font", lua_rikky_font},
        {"list", lua_rikky_list},
        {"mouse", lua_rikky_mouse},
        {"image", lua_rikky_image},
        {"assign", lua_rikky_assign},
        {"convert", lua_rikky_convert},
        {"datasave", lua_rikky_datasave},
        {"rotation", lua_rikky_rotation},
        {"textload", lua_rikky_textload},
        {"parameter", lua_rikky_parameter},
        {"bordering", lua_rikky_bordering},
        {"camerainfo", lua_rikky_camerainfo},
        {"audiobuffer", lua_rikky_audiobuffer},
        {"colorconvert", lua_rikky_colorconvert},
        {"axisconvert", lua_rikky_axisconvert},
        {"axisconvertEx", lua_rikky_axisconvertEx},
        {"setdialogparam", lua_rikky_setdialogparam},
        {"glassdraw_init", lua_rikky_glassdraw_init},
        {"glassdraw", lua_rikky_glassdraw},
        {"materialdraw_init", lua_rikky_materialdraw_init},
        {"materialdraw", lua_rikky_materialdraw},
        {"materialdrawEx", lua_rikky_materialdrawEx},
        {"ObjectOrientFigure", lua_rikky_ObjectOrientFigure},
        {"textsplit", lua_rikky_textsplit},
        {"reloadbuffer", lua_rikky_reloadbuffer},
        {"string2table", lua_rikky_string2table},
        {"pixelfunction", lua_rikky_pixelfunction},
        {"colordialog", lua_rikky_colordialog},
        {"checkbox", lua_rikky_checkbox},
        {"fileCS", lua_rikky_fileCS},
        {"foldCS", lua_rikky_foldCS},
        {"fontCS", lua_rikky_fontCS},
        {"listCS", lua_rikky_listCS},
        {"colordialogCS", lua_rikky_colordialogCS},
        {"checkboxCS", lua_rikky_checkboxCS},
        {"parameterCS", lua_rikky_parameterCS},
        {"setdialogparamCS", lua_rikky_setdialogparamCS},
        {"trackbarwrite", lua_rikky_trackbarwrite},
        {"midiconverter", lua_rikky_midiconverter},
        {"midiconverterEx", lua_rikky_midiconverterEx},
        {"clipboard", lua_rikky_clipboard},
        {"fillarea", lua_rikky_fillarea},
        {"effect", lua_rikky_effect},
        {"sceneload", lua_rikky_sceneload},
        {"soundregister", lua_rikky_soundregister},
        {"wave", lua_rikky_wave},
        {"linedetection", lua_rikky_linedetection},
        {"settext", lua_rikky_settext},
        {"progressbar", lua_rikky_progressbar},
        {NULL, NULL}};

int __cdecl luaopen_rikky_module(lua_State *L)
{
    luaL_register(L, "rikky_module", k_rikky_funcs);
    return 1;
}
