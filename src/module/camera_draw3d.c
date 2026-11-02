#include <windows.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rikky_module_internal.h"

#define RM_DEG2RAD 0.017453292519943295
#define RM_PI 3.141592653589793
#define RM_INV255 0.00392156862
#define RM_DBL_MIN_POS 2.2250738585072014e-308

typedef BOOL (__cdecl *Yc2RgbFn)(PIXEL *dst, const PIXEL_YC *src, int w);

// shift-jis filter and parameter names
static const char k_sjis_blur[] = "\x82\xda\x82\xa9\x82\xb5";
static const char k_sjis_range[] = "\x94\xcd\x88\xcd";
static const char k_sjis_fix_size[] = "\x83\x54\x83\x43\x83\x59\x8c\xc5\x92\xe8";
static const char k_sjis_resize[] = "\x83\x8a\x83\x54\x83\x43\x83\x59";

typedef struct
{
    double ox;
    double oy;
    double oz;
    double cx;
    double cy;
    double cz;
    double rx;
    double ry;
    double rz;
    double zoom;
    double aspect;
    double w;
    double h;
    int pixel_w;
    int pixel_h;
    void *pixel_data;
} RmObjState;

typedef struct
{
    double obj_pos[3];
    double cam_pos[3];
    double u[3];
    double n[3];
    double e[3];
    double zoom;
    double aspect;
    double rz;
    double d;
    double xz_len;
    double bb_xz[2];
    double bb_dir[2];
    double bb_mat[9];
    double rz_mat[9];
    uint8_t camera_mode;
    RmShadowInfo shadow;
} RmCamState;

typedef struct
{
    double eff_scale;
    double rot[3];
    double center[3];
    double normal[3];
    double tangent[3];
    double offset[3];
    double corners[4][3];
    double args[21];
    uint8_t flags;
    int nargs;
} RmDrawState;

typedef struct
{
    int color;
    int16_t blur;
    uint8_t color_r;
    uint8_t color_g;
    uint8_t color_b;
    uint8_t lens;
    uint8_t reverse;
    uint8_t boundary;
    uint8_t culling;
    double half_sw;
    double half_sh;
    double refractive;
    double offset_z;
    double inv_zoom;
    int sw;
    int sh;
    int yc_stride;
    RmObjState obj;
    uint8_t *async_rgb;
} RmGlassState;

typedef struct
{
    double pos[3];
    double col[3];
    int target;
} RmMatLight;

typedef struct
{
    int light_num;
    RmMatLight lights[4];
    double ambient[3];
    double specular[3];
    double shininess;
    int emissive[3];
    size_t buf_bytes;
    RmObjState obj;
} RmMatState;

typedef struct
{
    double dir1[3];
    double dir2[3];
    double cos1;
    double cos2;
    double omc1;
    double omc2;
} RmSpotExtra;

typedef struct
{
    int v[4];
    double col[3];
    uint8_t edge_mask;
} RmTexQuad;

typedef struct
{
    double dir[3];
    double right[3];
    double down[3];
    double cos_deg;
    double omc_deg;
    double *verts;
    int num_quads;
    RmTexQuad *quads;
} RmDirectExtra;

typedef struct
{
    double pos[3];
    double col[3];
    double spec[3];
    double shininess;
    int8_t type;
    void *extra;
} RmExLight;

typedef struct
{
    int num_lights;
    size_t buf_bytes;
    RmExLight *lights;
    double ambient[3];
    double emissive[3];
    double damping_sq;
    RmObjState obj;
    uint8_t drawhq;
    int16_t drawhq_part;
} RmMatExEntry;

static RmCamState g_cam;
static RmGlassState g_glass;
static RmMatState g_mat;
static RmMatExEntry *g_matex_entries = NULL;
static uint32_t g_matex_count = 0;
static int g_matex_frame = 0;

static void vec3_cross(const double *a, const double *b, double *out)
{
    double x = a[1] * b[2] - a[2] * b[1];
    double y = a[2] * b[0] - a[0] * b[2];
    double z = a[0] * b[1] - a[1] * b[0];
    out[0] = x;
    out[1] = y;
    out[2] = z;
}

static double vec3_dot(const double *a, const double *b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static int clamp_int(int v, int lo, int hi)
{
    if (v < lo)
    {
        return lo;
    }
    if (v > hi)
    {
        return hi;
    }
    return v;
}

static double clamp_01(double v)
{
    if (v < 0.0)
    {
        return 0.0;
    }
    if (v > 1.0)
    {
        return 1.0;
    }
    return v;
}

static void mat3_mul_vec3(const double *m, double *v)
{
    double x = v[0];
    double y = v[1];
    double z = v[2];
    v[0] = m[0] * x + m[1] * y + m[2] * z;
    v[1] = m[3] * x + m[4] * y + m[5] * z;
    v[2] = m[6] * x + m[7] * y + m[8] * z;
}

static void shadow_rot_vec3(const RmShadowInfo *sh, double *v)
{
    const double *m = sh->mat;
    double x = v[0];
    double y = v[1];
    double z = v[2];
    v[0] = m[0] * x + m[3] * y + m[6] * z;
    v[1] = m[1] * x + m[4] * y + m[7] * z;
    v[2] = m[2] * x + m[5] * y + m[8] * z;
}

static void shadow_transform_pt(const RmShadowInfo *sh, const double *in_p, double *out_p)
{
    const double *m = sh->mat;
    double s = sh->scale;
    double x = in_p[0];
    double y = in_p[1];
    double z = in_p[2];
    out_p[0] = (m[0] * x + m[3] * y + m[6] * z) * s + sh->offset_y;
    out_p[1] = (m[1] * x + m[4] * y + m[7] * z) * s + sh->offset_x;
    out_p[2] = (m[2] * x + m[5] * y + m[8] * z) * s + sh->offset_z;
}

static void bb_rot_vec(const RmCamState *cam, double *v)
{
    double x = v[0];
    double y = v[1];
    double z = v[2];
    if (cam->xz_len == 0.0)
    {
        v[0] = x * cam->bb_dir[0] + (-y) * cam->n[1] * cam->bb_dir[1];
        v[1] = z * cam->n[1];
        v[2] = (-y) * cam->n[1] * cam->bb_dir[0] - x * cam->bb_dir[1];
    }
    else
    {
        double t0 = x * cam->bb_dir[0] + z * cam->bb_dir[1];
        double t1 = z * cam->bb_dir[0] - x * cam->bb_dir[1];
        v[0] = cam->bb_mat[0] * t0 + cam->bb_mat[1] * y + cam->bb_mat[2] * t1;
        v[1] = cam->bb_mat[3] * t0 + cam->bb_mat[4] * y + cam->bb_mat[5] * t1;
        v[2] = cam->bb_mat[6] * t0 + cam->bb_mat[7] * y + cam->bb_mat[8] * t1;
    }
}

static RmCamState *query_cam_state(lua_State *L)
{
    static const double k_def_u[3] = { 0.0, -1.0, 0.0 };
    static const double k_def_n[3] = { 0.0, 0.0, 1.0 };
    static const double k_def_e[3] = { 1.0, 0.0, 0.0 };

    rm_query_shadow_info(&g_cam.shadow, L);

    lua_getglobal(L, "obj");
    lua_getfield(L, -1, "x");
    g_cam.obj_pos[0] = lua_tonumber(L, -1);
    lua_getfield(L, -2, "y");
    g_cam.obj_pos[1] = lua_tonumber(L, -1);
    lua_getfield(L, -3, "z");
    g_cam.obj_pos[2] = lua_tonumber(L, -1);
    lua_pop(L, 3);

    lua_getfield(L, -1, "getvalue");
    lua_pushstring(L, "zoom");
    lua_call(L, 1, 1);
    g_cam.zoom = lua_tonumber(L, -1) * 0.01;
    lua_pop(L, 1);

    lua_getfield(L, -1, "getvalue");
    lua_pushstring(L, "aspect");
    lua_call(L, 1, 1);
    g_cam.aspect = lua_tonumber(L, -1);
    lua_pop(L, 1);

    lua_getfield(L, -1, "getoption");
    lua_pushstring(L, "camera_mode");
    lua_call(L, 1, 1);
    g_cam.camera_mode = (uint8_t)lua_tointeger(L, -1);
    lua_pop(L, 1);

    if (g_cam.camera_mode == 0)
    {
        g_cam.cam_pos[0] = 0.0;
        g_cam.cam_pos[1] = 0.0;
        g_cam.cam_pos[2] = -1024.0;
        g_cam.n[0] = 0.0;
        g_cam.n[1] = 0.0;
        g_cam.n[2] = 1.0;
        g_cam.u[0] = 0.0;
        g_cam.u[1] = -1.0;
        g_cam.u[2] = 0.0;
        g_cam.e[0] = 1.0;
        g_cam.e[1] = 0.0;
        g_cam.e[2] = 0.0;
        g_cam.rz = 0.0;
        g_cam.d = 1024.0;
        g_cam.xz_len = 1.0;
    }
    else
    {
        double tx, ty, tz;
        double c, s, omc;
        double nx, ny, nz;

        lua_getfield(L, -1, "getoption");
        lua_pushstring(L, "camera_param");
        lua_call(L, 1, 1);

        lua_getfield(L, -1, "x");
        g_cam.cam_pos[0] = lua_tonumber(L, -1);
        lua_getfield(L, -2, "y");
        g_cam.cam_pos[1] = lua_tonumber(L, -1);
        lua_getfield(L, -3, "z");
        g_cam.cam_pos[2] = lua_tonumber(L, -1);
        lua_getfield(L, -4, "tx");
        tx = lua_tonumber(L, -1);
        lua_getfield(L, -5, "ty");
        ty = lua_tonumber(L, -1);
        lua_getfield(L, -6, "tz");
        tz = lua_tonumber(L, -1);
        lua_getfield(L, -7, "ux");
        g_cam.u[0] = lua_tonumber(L, -1);
        lua_getfield(L, -8, "uy");
        g_cam.u[1] = lua_tonumber(L, -1);
        lua_getfield(L, -9, "uz");
        g_cam.u[2] = lua_tonumber(L, -1);
        lua_getfield(L, -10, "rz");
        g_cam.rz = lua_tonumber(L, -1) * RM_DEG2RAD;
        lua_getfield(L, -11, "d");
        g_cam.d = lua_tonumber(L, -1);
        lua_pop(L, 12);

        rm_vec3_normalize_or_default(g_cam.u, k_def_u, 1.0);
        g_cam.n[0] = tx - g_cam.cam_pos[0];
        g_cam.n[1] = ty - g_cam.cam_pos[1];
        g_cam.n[2] = tz - g_cam.cam_pos[2];
        rm_vec3_normalize_or_default(g_cam.n, k_def_n, 1.0);
        vec3_cross(g_cam.n, g_cam.u, g_cam.e);
        rm_vec3_normalize_or_default(g_cam.e, k_def_e, 1.0);
        vec3_cross(g_cam.e, g_cam.n, g_cam.u);
        rm_vec3_normalize_or_default(g_cam.u, k_def_u, 1.0);

        g_cam.xz_len = sqrt(g_cam.n[0] * g_cam.n[0] + g_cam.n[2] * g_cam.n[2]);
        if (g_cam.xz_len == 0.0)
        {
            g_cam.bb_xz[0] = -1.0;
            g_cam.bb_xz[1] = 0.0;
            if (g_cam.n[1] <= 0.0)
            {
                g_cam.bb_dir[0] = -g_cam.u[2];
                g_cam.bb_dir[1] = -g_cam.u[0];
            }
            else
            {
                g_cam.bb_dir[0] = g_cam.u[2];
                g_cam.bb_dir[1] = g_cam.u[0];
            }
        }
        else
        {
            if (g_cam.xz_len == 1.0)
            {
                g_cam.bb_xz[0] = g_cam.n[2];
                g_cam.bb_xz[1] = g_cam.n[0];
            }
            else
            {
                g_cam.bb_xz[0] = g_cam.n[2] / g_cam.xz_len;
                g_cam.bb_xz[1] = g_cam.n[0] / g_cam.xz_len;
            }
            if (g_cam.u[1] < 0.0)
            {
                g_cam.bb_dir[0] = g_cam.bb_xz[0];
                g_cam.bb_dir[1] = g_cam.bb_xz[1];
                g_cam.bb_mat[4] = g_cam.xz_len;
            }
            else
            {
                g_cam.bb_dir[0] = -g_cam.bb_xz[0];
                g_cam.bb_dir[1] = -g_cam.bb_xz[1];
                g_cam.bb_mat[4] = -g_cam.xz_len;
            }
            omc = 1.0 - g_cam.bb_mat[4];
            g_cam.bb_mat[0] = g_cam.bb_dir[0] * g_cam.bb_dir[0] * omc + g_cam.bb_mat[4];
            g_cam.bb_mat[1] = g_cam.e[2] * g_cam.n[1];
            g_cam.bb_mat[2] = omc * (-g_cam.bb_dir[1]) * g_cam.bb_dir[0];
            g_cam.bb_mat[3] = g_cam.n[1] * g_cam.bb_dir[1];
            g_cam.bb_mat[5] = g_cam.n[1] * g_cam.bb_dir[0];
            g_cam.bb_mat[6] = g_cam.bb_mat[2];
            g_cam.bb_mat[7] = -g_cam.bb_dir[0] * g_cam.n[1];
            g_cam.bb_mat[8] = g_cam.bb_dir[1] * g_cam.bb_dir[1] * omc + g_cam.bb_mat[4];
        }

        c = cos(g_cam.rz);
        s = sin(g_cam.rz);
        omc = 1.0 - c;
        nx = g_cam.n[0];
        ny = g_cam.n[1];
        nz = g_cam.n[2];
        g_cam.rz_mat[0] = nx * nx * omc + c;
        g_cam.rz_mat[1] = nx * ny * omc - nz * s;
        g_cam.rz_mat[2] = nx * nz * omc + ny * s;
        g_cam.rz_mat[3] = nx * ny * omc + nz * s;
        g_cam.rz_mat[4] = ny * ny * omc + c;
        g_cam.rz_mat[5] = ny * nz * omc - nx * s;
        g_cam.rz_mat[6] = nx * nz * omc - ny * s;
        g_cam.rz_mat[7] = ny * nz * omc + nx * s;
        g_cam.rz_mat[8] = nz * nz * omc + c;
    }

    lua_pop(L, 1);
    return &g_cam;
}

static void query_obj_state(lua_State *L, RmObjState *out)
{
    lua_getglobal(L, "obj");
    lua_getfield(L, -1, "ox");
    out->ox = lua_tonumber(L, -1);
    lua_getfield(L, -2, "oy");
    out->oy = lua_tonumber(L, -1);
    lua_getfield(L, -3, "oz");
    out->oz = lua_tonumber(L, -1);
    lua_getfield(L, -4, "cx");
    out->cx = lua_tonumber(L, -1);
    lua_getfield(L, -5, "cy");
    out->cy = lua_tonumber(L, -1);
    lua_getfield(L, -6, "cz");
    out->cz = lua_tonumber(L, -1);
    lua_getfield(L, -7, "rx");
    out->rx = lua_tonumber(L, -1) * RM_DEG2RAD;
    lua_getfield(L, -8, "ry");
    out->ry = lua_tonumber(L, -1) * RM_DEG2RAD;
    lua_getfield(L, -9, "rz");
    out->rz = lua_tonumber(L, -1) * RM_DEG2RAD;
    lua_getfield(L, -10, "zoom");
    out->zoom = lua_tonumber(L, -1);
    lua_getfield(L, -11, "aspect");
    out->aspect = lua_tonumber(L, -1);
    lua_getfield(L, -12, "w");
    out->w = lua_tonumber(L, -1);
    lua_getfield(L, -13, "h");
    out->h = lua_tonumber(L, -1);
    lua_pop(L, 13);

    lua_getfield(L, -1, "getpixeldata");
    lua_call(L, 0, 3);
    out->pixel_data = lua_touserdata(L, -3);
    out->pixel_w = (int)lua_tointeger(L, -2);
    out->pixel_h = (int)lua_tointeger(L, -1);
    lua_pop(L, 4);
}

static RmCamState *compute_draw_state(
    lua_State *L,
    int mode,
    int start_idx,
    int tbl_idx,
    RmDrawState *out,
    const RmObjState *obj)
{
    static const double k_axis_z[3] = { 0.0, 0.0, 1.0 };
    static const double k_axis_y[3] = { 0.0, 1.0, 0.0 };
    static const double k_axis_x[3] = { 1.0, 0.0, 0.0 };
    static const double k_def_tan[3] = { 1.0, 0.0, 0.0 };
    static const double k_def_norm[3] = { 0.0, 0.0, -1.0 };

    int cnt = 0;
    int is_poly = 0;
    int i, k;
    double px, py, pz;
    double c4, drx, dry, drz;
    double base, sx, sy;
    double cl[3];
    double c[4][3];
    double wb[3];
    uint8_t bb_flag = 0;

    memset(out->args, 0, sizeof(out->args));

    if (mode == 2)
    {
        int n_read;
        out->args[3] = 1.0;
        out->args[4] = 1.0;
        out->args[20] = 1.0;
        cnt = lua_gettop(L) - start_idx + 1;
        if (cnt < 0)
        {
            cnt = 0;
        }
        n_read = cnt < 21 ? cnt : 21;
        for (i = 0; i < n_read; i++)
        {
            out->args[i] = lua_tonumber(L, start_idx + i);
        }
        is_poly = 0;
    }
    else if (mode == 3)
    {
        int n_read;
        out->args[20] = 1.0;
        cnt = lua_gettop(L) - start_idx + 1;
        if (cnt < 0)
        {
            cnt = 0;
        }
        n_read = cnt < 21 ? cnt : 21;
        for (i = 0; i < n_read; i++)
        {
            out->args[i] = lua_tonumber(L, start_idx + i);
        }
        is_poly = 1;
    }
    else
    {
        out->args[3] = 1.0;
        out->args[4] = 1.0;
        out->args[20] = 1.0;
        if (mode == 0)
        {
            cnt = lua_gettop(L) - start_idx + 1;
            if ((uint32_t)cnt <= 21u && ((1u << cnt) & 0x3011ffu) != 0)
            {
                for (i = 0; i < cnt; i++)
                {
                    out->args[i] = lua_tonumber(L, start_idx + i);
                }
            }
            else
            {
                cnt = 0;
            }
        }
        else if (mode == 1)
        {
            cnt = (int)lua_objlen(L, tbl_idx);
            if ((uint32_t)cnt <= 21u && ((1u << cnt) & 0x3011ffu) != 0)
            {
                for (i = 0; i < cnt; i++)
                {
                    lua_rawgeti(L, tbl_idx, i + 1);
                    out->args[i] = lua_tonumber(L, -1);
                    lua_pop(L, 1);
                }
            }
            else
            {
                cnt = 0;
            }
        }
        is_poly = (cnt >= 9) ? 1 : 0;
    }

    if (!is_poly)
    {
        px = out->args[0];
        py = out->args[1];
        pz = out->args[2];
        c4 = out->args[3] * obj->zoom;
        out->normal[0] = 0.0;
        out->normal[1] = 0.0;
        out->normal[2] = -1.0;
        out->tangent[0] = 1.0;
        out->tangent[1] = 0.0;
        out->tangent[2] = 0.0;
        drx = out->args[5] * RM_DEG2RAD;
        dry = out->args[6] * RM_DEG2RAD;
        drz = out->args[7] * RM_DEG2RAD;
    }
    else
    {
        double e1[3], e2[3];
        px = (out->args[0] + out->args[3] + out->args[6] + out->args[9]) * 0.25;
        py = (out->args[1] + out->args[4] + out->args[7] + out->args[10]) * 0.25;
        pz = (out->args[2] + out->args[5] + out->args[8] + out->args[11]) * 0.25;

        e1[0] = out->args[3] - out->args[0];
        e1[1] = out->args[4] - out->args[1];
        e1[2] = out->args[5] - out->args[2];
        if (sqrt(vec3_dot(e1, e1)) == 0.0)
        {
            e1[0] = out->args[6] - out->args[0];
            e1[1] = out->args[7] - out->args[1];
            e1[2] = out->args[8] - out->args[2];
            e2[0] = out->args[9] - out->args[0];
            e2[1] = out->args[10] - out->args[1];
            e2[2] = out->args[11] - out->args[2];
        }
        else
        {
            e2[0] = out->args[6] - out->args[0];
            e2[1] = out->args[7] - out->args[1];
            e2[2] = out->args[8] - out->args[2];
            if (sqrt(vec3_dot(e2, e2)) == 0.0)
            {
                e2[0] = out->args[9] - out->args[0];
                e2[1] = out->args[10] - out->args[1];
                e2[2] = out->args[11] - out->args[2];
            }
        }

        out->tangent[0] = e1[0];
        out->tangent[1] = e1[1];
        out->tangent[2] = e1[2];
        rm_vec3_normalize_or_default(out->tangent, k_def_tan, 1.0);
        vec3_cross(e2, out->tangent, out->normal);
        rm_vec3_normalize_or_default(out->normal, k_def_norm, 1.0);
        c4 = obj->zoom;
        drx = 0.0;
        dry = 0.0;
        drz = 0.0;
    }

    base = c4 * g_cam.zoom;
    if (g_cam.aspect < 0.0)
    {
        if (obj->aspect >= 0.0)
        {
            sy = (g_cam.aspect + 1.0) * base;
            sx = (1.0 - obj->aspect) * base;
        }
        else
        {
            sy = (obj->aspect + 1.0) * (g_cam.aspect + 1.0) * base;
            sx = base;
        }
    }
    else
    {
        double tmp = (1.0 - g_cam.aspect) * base;
        if (obj->aspect < 0.0)
        {
            sy = (obj->aspect + 1.0) * base;
            sx = tmp;
        }
        else
        {
            sy = base;
            sx = (1.0 - obj->aspect) * tmp;
        }
    }

    if (!is_poly)
    {
        double hw = (double)obj->pixel_w * 0.5 * sx;
        double hh = (double)obj->pixel_h * 0.5 * sy;
        cl[0] = -sx * obj->cx;
        cl[1] = -sy * obj->cy;
        cl[2] = -obj->cz;
        c[0][0] = cl[0] - hw;
        c[0][1] = cl[1] - hh;
        c[0][2] = cl[2];
        c[1][0] = cl[0] + hw;
        c[1][1] = cl[1] - hh;
        c[1][2] = cl[2];
        c[2][0] = cl[0] + hw;
        c[2][1] = cl[1] + hh;
        c[2][2] = cl[2];
        c[3][0] = cl[0] - hw;
        c[3][1] = cl[1] + hh;
        c[3][2] = cl[2];
        wb[0] = obj->ox + px;
        wb[1] = obj->oy + py;
        wb[2] = obj->oz + pz;
    }
    else
    {
        double psx = (obj->aspect < 0.0) ? c4 : (1.0 - obj->aspect) * c4;
        double psy = (obj->aspect < 0.0) ? (obj->aspect + 1.0) * c4 : c4;
        double off[3];
        off[0] = -obj->cx * sx;
        off[1] = -obj->cy * sy;
        off[2] = -obj->cz * sx;
        cl[0] = px * psx + off[0];
        cl[1] = py * psy + off[1];
        cl[2] = pz * psx + off[2];
        for (k = 0; k < 4; k++)
        {
            c[k][0] = out->args[k * 3] * psx + off[0];
            c[k][1] = out->args[k * 3 + 1] * psy + off[1];
            c[k][2] = out->args[k * 3 + 2] * psx + off[2];
        }
        wb[0] = obj->ox;
        wb[1] = obj->oy;
        wb[2] = obj->oz;
    }

    if (g_cam.camera_mode != 0)
    {
        uintptr_t *p_ctx = (uintptr_t *)RikkyGetMemoryData(0x72);
        uintptr_t off_bb = (uintptr_t)RikkyGetMemoryData(0x73);
        uint8_t bb = (p_ctx && *p_ctx) ? *(const uint8_t *)(*p_ctx + off_bb) : 0;

        if (bb & 1u)
        {
            bb_flag = 0x0c;
            bb_rot_vec(&g_cam, out->normal);
            mat3_mul_vec3(g_cam.rz_mat, out->normal);
            bb_rot_vec(&g_cam, out->tangent);
            mat3_mul_vec3(g_cam.rz_mat, out->tangent);
        }
        else if (bb & 2u)
        {
            bb_flag = 0x08;
            bb_rot_vec(&g_cam, out->normal);
            bb_rot_vec(&g_cam, out->tangent);
        }
        else if (bb & 4u)
        {
            double nx = out->normal[0];
            double nz = out->normal[2];
            double tx = out->tangent[0];
            double tz = out->tangent[2];
            bb_flag = 0x04;
            out->normal[0] = nx * g_cam.bb_xz[0] + nz * g_cam.bb_xz[1];
            out->normal[2] = nz * g_cam.bb_xz[0] - nx * g_cam.bb_xz[1];
            out->tangent[0] = tx * g_cam.bb_xz[0] + tz * g_cam.bb_xz[1];
            out->tangent[2] = tz * g_cam.bb_xz[0] - tx * g_cam.bb_xz[1];
        }
    }

    if (bb_flag == 0 && g_cam.shadow.enabled)
    {
        shadow_rot_vec3(&g_cam.shadow, out->normal);
        shadow_rot_vec3(&g_cam.shadow, out->tangent);
    }

    out->rot[0] = drx + obj->rx;
    out->rot[1] = dry + obj->ry;
    out->rot[2] = drz + obj->rz;

    rm_axis_angle_rotate_vec3(out->normal, out->normal, k_axis_z, out->rot[2]);
    rm_axis_angle_rotate_vec3(out->tangent, out->tangent, NULL, 0.0);
    rm_axis_angle_rotate_vec3(cl, cl, NULL, 0.0);
    for (k = 0; k < 4; k++)
    {
        rm_axis_angle_rotate_vec3(c[k], c[k], NULL, 0.0);
    }

    rm_axis_angle_rotate_vec3(out->normal, out->normal, k_axis_y, out->rot[1]);
    rm_axis_angle_rotate_vec3(out->tangent, out->tangent, NULL, 0.0);
    rm_axis_angle_rotate_vec3(cl, cl, NULL, 0.0);
    for (k = 0; k < 4; k++)
    {
        rm_axis_angle_rotate_vec3(c[k], c[k], NULL, 0.0);
    }

    rm_axis_angle_rotate_vec3(out->normal, out->normal, k_axis_x, out->rot[0]);
    rm_axis_angle_rotate_vec3(out->tangent, out->tangent, NULL, 0.0);
    rm_axis_angle_rotate_vec3(cl, cl, NULL, 0.0);
    for (k = 0; k < 4; k++)
    {
        rm_axis_angle_rotate_vec3(c[k], c[k], NULL, 0.0);
    }

    out->offset[0] = cl[0];
    out->offset[1] = cl[1];
    out->offset[2] = cl[2];

    wb[0] += g_cam.obj_pos[0];
    wb[1] += g_cam.obj_pos[1];
    wb[2] += g_cam.obj_pos[2];

    cl[0] += wb[0];
    cl[1] += wb[1];
    cl[2] += wb[2];
    shadow_transform_pt(&g_cam.shadow, cl, out->center);

    for (k = 0; k < 4; k++)
    {
        c[k][0] += wb[0];
        c[k][1] += wb[1];
        c[k][2] += wb[2];
        shadow_transform_pt(&g_cam.shadow, c[k], out->corners[k]);
    }

    out->nargs = cnt;
    out->eff_scale = base * g_cam.shadow.scale;
    out->flags = (uint8_t)((g_cam.camera_mode << 4) | is_poly | bb_flag);
    return &g_cam;
}

static void call_obj_draw_args(lua_State *L, int obj_idx, const char *method, const double *args, int nargs)
{
    int i;
    int n = nargs < 21 ? nargs : 21;
    lua_getfield(L, obj_idx, method);
    for (i = 0; i < n; i++)
    {
        lua_pushnumber(L, args[i]);
    }
    lua_call(L, n, 0);
}

static void forward_stack_to_obj_draw(lua_State *L, int first_arg, const char *method)
{
    int top = lua_gettop(L);
    int n = top - first_arg + 1;
    int i;
    if (n < 0)
    {
        n = 0;
    }
    lua_getglobal(L, "obj");
    lua_getfield(L, -1, method);
    for (i = 0; i < n; i++)
    {
        lua_pushvalue(L, first_arg + i);
    }
    lua_call(L, n, 0);
    lua_pop(L, 1);
}

static void set_table_num(lua_State *L, const char *key, double val)
{
    lua_pushnumber(L, val);
    lua_setfield(L, -2, key);
}

int __cdecl lua_rikky_camerainfo(lua_State *L)
{
    RmCamState *cam;
    RmObjState obj;
    RmDrawState ds;
    int cm;
    double tx = 0.0;
    double ty = 0.0;
    double tz = 0.0;
    double dv[3];
    double vx, vy, vz;
    double real_d, normal_d, persp;
    double pe, pu, crz, srz;
    const int *p_sh;
    const int16_t *p_conce;
    const int *p_accu;

    if (!rm_check_aviutl_ver() || !rm_check_exedit_ver())
    {
        return 0;
    }

    cam = query_cam_state(L);
    query_obj_state(L, &obj);
    if (lua_type(L, 1) == LUA_TTABLE)
    {
        compute_draw_state(L, 1, 0, 1, &ds, &obj);
    }
    else
    {
        compute_draw_state(L, 0, 1, 0, &ds, &obj);
    }

    cm = ds.flags >> 4;
    if (cm != 0)
    {
        lua_getglobal(L, "obj");
        lua_getfield(L, -1, "getoption");
        lua_pushstring(L, "camera_param");
        lua_call(L, 1, 1);
        lua_getfield(L, -1, "tx");
        tx = lua_tonumber(L, -1);
        lua_getfield(L, -2, "ty");
        ty = lua_tonumber(L, -1);
        lua_getfield(L, -3, "tz");
        tz = lua_tonumber(L, -1);
        lua_pop(L, 5);
    }

    lua_settop(L, 0);
    lua_newtable(L);

    set_table_num(L, "x", cam->cam_pos[0]);
    set_table_num(L, "y", cam->cam_pos[1]);
    set_table_num(L, "z", cam->cam_pos[2]);
    set_table_num(L, "tx", tx);
    set_table_num(L, "ty", ty);
    set_table_num(L, "tx", tz);
    set_table_num(L, "ux", cam->u[0]);
    set_table_num(L, "uy", cam->u[1]);
    set_table_num(L, "uz", cam->u[2]);
    set_table_num(L, "nx", cam->n[0]);
    set_table_num(L, "ny", cam->n[1]);
    set_table_num(L, "nz", cam->n[2]);
    set_table_num(L, "ex", cam->e[0]);
    set_table_num(L, "ey", cam->e[1]);
    set_table_num(L, "ez", cam->e[2]);
    set_table_num(L, "d", cam->d);
    set_table_num(L, "rz", cam->rz);

    dv[0] = ds.center[0] - cam->cam_pos[0];
    dv[1] = ds.center[1] - cam->cam_pos[1];
    dv[2] = ds.center[2] - cam->cam_pos[2];
    real_d = sqrt(vec3_dot(dv, dv));
    if (real_d == 0.0)
    {
        vx = cam->n[0];
        vy = cam->n[1];
        vz = cam->n[2];
    }
    else
    {
        vx = dv[0] / real_d;
        vy = dv[1] / real_d;
        vz = dv[2] / real_d;
    }
    set_table_num(L, "vx", vx);
    set_table_num(L, "vy", vy);
    set_table_num(L, "vz", vz);
    set_table_num(L, "real_d", real_d);

    normal_d = vec3_dot(dv, cam->n);
    set_table_num(L, "normal_d", normal_d);
    persp = (normal_d == 0.0) ? (cam->d * 4.49423283715579e+307) : (cam->d / normal_d);
    set_table_num(L, "zoom", persp * ds.eff_scale);

    if (cm == 0)
    {
        double dvx = dv[0] + ds.offset[0] * (persp - 1.0);
        double dvy = dv[1] + ds.offset[1] * (persp - 1.0);
        pe = cam->e[0] * dvx + cam->e[1] * dvy + cam->e[2] * dv[2];
        pu = cam->u[0] * dvx + cam->u[1] * dvy + cam->u[2] * dv[2];
    }
    else
    {
        pe = vec3_dot(dv, cam->e) * persp;
        pu = vec3_dot(dv, cam->u) * persp;
    }

    crz = cos(cam->rz);
    srz = sin(cam->rz);
    set_table_num(L, "ix", srz * pu + crz * pe);
    set_table_num(L, "iy", pe * srz - pu * crz);
    set_table_num(L, "mx", ds.center[0]);
    set_table_num(L, "my", ds.center[1]);
    set_table_num(L, "mz", ds.center[2]);
    set_table_num(L, "ax", ds.normal[0]);
    set_table_num(L, "ay", ds.normal[1]);
    set_table_num(L, "az", ds.normal[2]);

    p_sh = (const int *)RikkyGetMemoryData(0x7f);
    if (p_sh)
    {
        uint32_t raw_blur = *(const uint32_t *)((uintptr_t)p_sh + 0x0c);
        set_table_num(L, "shadow_x", (double)p_sh[1] * 0.1);
        set_table_num(L, "shadow_y", (double)p_sh[0] * 0.1);
        set_table_num(L, "shadow_z", (double)p_sh[2] * 0.1);
        set_table_num(L, "blur", floor((double)((((float)raw_blur / 655360.0f) * 10.0f + 0.05f) * 10.0f)) * 0.1);
    }
    else
    {
        set_table_num(L, "shadow_x", 0.0);
        set_table_num(L, "shadow_y", 0.0);
        set_table_num(L, "shadow_z", 0.0);
        set_table_num(L, "blur", 0.0);
    }

    p_conce = (const int16_t *)RikkyGetMemoryData(0x81);
    if (p_conce)
    {
        set_table_num(L, "shadow_conce", floor((double)(((float)(*p_conce) * 0.00024414062f * 100.0f + 0.05f) * 10.0f)) * 0.1);
    }
    else
    {
        set_table_num(L, "shadow_conce", 0.0);
    }

    p_accu = (const int *)RikkyGetMemoryData(0x80);
    if (p_accu)
    {
        set_table_num(L, "shadow_accu", (double)(int)(((float)(*p_accu - 0x199) / 1639.0f) * 80.0f + 20.0f + 0.5f));
    }
    else
    {
        set_table_num(L, "shadow_accu", 0.0);
    }

    set_table_num(L, "shadow_flag", (cm < 3) ? 0.0 : 1.0);
    set_table_num(L, "billboard", (double)((ds.flags & 0x0c) >> 2));
    return 1;
}

static int wrap_coord(int c, int dim, uint8_t boundary)
{
    if (dim <= 1)
    {
        return 0;
    }
    if (boundary == 1)
    {
        if (c < 0)
        {
            c += (1 - c / dim) * dim;
            if (c >= dim)
            {
                c %= dim;
            }
        }
        else if (c >= dim)
        {
            c %= dim;
        }
        return c;
    }
    if (boundary == 2)
    {
        int q = c / dim;
        if (c < 0)
        {
            c += (1 - q) * dim;
            q = q + 1;
            if (c >= dim)
            {
                c -= dim;
                q -= 1;
            }
        }
        else if (c >= dim)
        {
            c %= dim;
        }
        if (q & 1)
        {
            c = (dim - 1) - c;
        }
        return c;
    }
    return clamp_int(c, 0, dim - 1);
}

static uint8_t *capture_frame_rgb(int sw, int sh, int yc_stride, void **async_src)
{
    const uint8_t *yc_src;
    uint8_t *rgb;
    Yc2RgbFn yc2rgb;
    int y;

    if (!async_src || !(*async_src) || sw <= 0 || sh <= 0 || yc_stride <= 0)
    {
        return NULL;
    }
    yc_src = (const uint8_t *)(*async_src);
    rgb = (uint8_t *)rm_malloc((size_t)sw * (size_t)sh * 3u);
    if (!rgb)
    {
        return NULL;
    }
    RikkyGetMemoryData(0x15);
    yc2rgb = (Yc2RgbFn)RikkyGetMemoryData(0x16);
    if (!yc2rgb)
    {
        rm_free(rgb);
        return NULL;
    }
    for (y = 0; y < sh; y++)
    {
        yc2rgb(
            (PIXEL *)(rgb + (size_t)y * (size_t)sw * 3u),
            (const PIXEL_YC *)(yc_src + (size_t)y * (size_t)yc_stride),
            sw);
    }
    return rgb;
}

int __cdecl lua_rikky_glassdraw_init(lua_State *L)
{
    void **async_src = NULL;
    const int *p_max_w;

    rm_free(g_glass.async_rgb);
    g_glass.async_rgb = NULL;

    if (lua_type(L, 1) == LUA_TTABLE)
    {
        int rev_up = 0;
        int rev_side = 0;
        int rev_both = 0;

        lua_getfield(L, 1, "reverseUp");
        if (lua_type(L, -1) == LUA_TNUMBER && lua_tointeger(L, -1) == 1)
        {
            rev_up = 1;
        }
        lua_pop(L, 1);

        lua_getfield(L, 1, "reverseSide");
        if (lua_type(L, -1) == LUA_TNUMBER && lua_tointeger(L, -1) == 1)
        {
            rev_side = 1;
        }
        lua_pop(L, 1);

        lua_getfield(L, 1, "reverse");
        if (lua_type(L, -1) == LUA_TNUMBER && lua_tointeger(L, -1) == 1)
        {
            rev_both = 1;
        }
        lua_pop(L, 1);

        g_glass.reverse = rev_both ? 3u : (uint8_t)(rev_up | (rev_side << 1));

        lua_getfield(L, 1, "color");
        if (lua_type(L, -1) == LUA_TNUMBER)
        {
            int c = (int)lua_tointeger(L, -1);
            g_glass.color = c;
            if (c != -1)
            {
                g_glass.color_r = (uint8_t)((c >> 16) & 0xff);
                g_glass.color_g = (uint8_t)((c >> 8) & 0xff);
                g_glass.color_b = (uint8_t)(c & 0xff);
            }
        }
        else
        {
            g_glass.color = -1;
        }
        lua_pop(L, 1);

        lua_getfield(L, 1, "blur");
        if (lua_type(L, -1) == LUA_TNUMBER)
        {
            g_glass.blur = (int16_t)clamp_int((int)lua_tointeger(L, -1), 0, 30);
        }
        else
        {
            g_glass.blur = -1;
        }
        lua_pop(L, 1);

        lua_getfield(L, 1, "refractive");
        g_glass.refractive = (lua_type(L, -1) == LUA_TNUMBER) ? clamp_01(lua_tonumber(L, -1)) : 0.0;
        lua_pop(L, 1);

        lua_getfield(L, 1, "zoom");
        if (lua_type(L, -1) == LUA_TNUMBER)
        {
            double z = lua_tonumber(L, -1);
            g_glass.inv_zoom = (z <= 0.0) ? 1.0 : (1.0 / z);
        }
        else
        {
            g_glass.inv_zoom = 1.0;
        }
        lua_pop(L, 1);

        lua_getfield(L, 1, "offsetZ");
        if (lua_type(L, -1) == LUA_TNUMBER)
        {
            double oz = lua_tonumber(L, -1);
            g_glass.offset_z = (oz < 0.0) ? 0.0 : oz;
        }
        else
        {
            g_glass.offset_z = 300.0;
        }
        lua_pop(L, 1);

        lua_getfield(L, 1, "boundary");
        g_glass.boundary = 0;
        if (lua_type(L, -1) == LUA_TSTRING)
        {
            const char *s = lua_tolstring(L, -1, NULL);
            if (s && strcmp(s, "loop") == 0)
            {
                g_glass.boundary = 1;
            }
            else if (s && strcmp(s, "inverted") == 0)
            {
                g_glass.boundary = 2;
            }
        }
        lua_pop(L, 1);

        lua_getfield(L, 1, "culling");
        g_glass.culling = (lua_type(L, -1) == LUA_TNUMBER && lua_tointeger(L, -1) == 1) ? 1u : 0u;
        lua_pop(L, 1);

        lua_getfield(L, 1, "async");
        if (lua_type(L, -1) == LUA_TNUMBER && lua_tointeger(L, -1) == 1)
        {
            void ***pp;
            lua_getglobal(L, "obj");
            lua_getfield(L, -1, "pixeloption");
            lua_pushstring(L, "put");
            lua_pushstring(L, "frm");
            lua_call(L, 2, 0);
            lua_pop(L, 1);
            pp = (void ***)RikkyGetMemoryData(0x9f);
            if (pp)
            {
                async_src = *pp;
            }
        }
        lua_pop(L, 1);

        lua_getfield(L, 1, "lens");
        g_glass.lens = 0;
        if (lua_type(L, -1) == LUA_TSTRING)
        {
            const char *s = lua_tolstring(L, -1, NULL);
            if (s && strcmp(s, "convex") == 0)
            {
                g_glass.lens = 1;
            }
            else if (s && strcmp(s, "concave") == 0)
            {
                g_glass.lens = 2;
            }
        }
        lua_pop(L, 1);
    }
    else
    {
        g_glass.refractive = 0.0;
        g_glass.inv_zoom = 1.0;
        g_glass.reverse = 0;
        g_glass.offset_z = 300.0;
        g_glass.color = -1;
        g_glass.blur = -1;
        g_glass.boundary = 0;
        g_glass.culling = 0;
        g_glass.lens = 0;
    }

    lua_settop(L, 0);
    query_cam_state(L);
    query_obj_state(L, &g_glass.obj);

    lua_getglobal(L, "obj");
    lua_getfield(L, -1, "screen_w");
    g_glass.sw = (int)lua_tointeger(L, -1);
    lua_getfield(L, -2, "screen_h");
    g_glass.sh = (int)lua_tointeger(L, -1);
    lua_pop(L, 3);

    g_glass.half_sw = (double)((float)g_glass.sw * 0.5f);
    g_glass.half_sh = (double)((float)g_glass.sh * 0.5f);

    p_max_w = (const int *)RikkyGetMemoryData(0x13);
    g_glass.yc_stride = p_max_w ? ((*p_max_w) * 6 + 0x30) : 0;

    if (async_src)
    {
        g_glass.async_rgb = capture_frame_rgb(g_glass.sw, g_glass.sh, g_glass.yc_stride, async_src);
    }
    return 0;
}

int __cdecl lua_rikky_glassdraw(lua_State *L)
{
    int pw = g_glass.obj.pixel_w;
    int ph = g_glass.obj.pixel_h;
    const char *p_mode_str = (const char *)RikkyGetMemoryData(0x8a);
    uint32_t *dst = NULL;
    RmCamState *cam;
    RmDrawState ds;
    double vdot, ndu, nde, denom_v, ref_x, ref_y;
    double crz, srz;
    double sx[4], sy[4];
    double off_x, off_y;
    double dx03, dx12, dy03, dy12;
    uint8_t *rgb = NULL;
    const uint32_t *orig;
    int k, x, y;
    double denom_x, denom_y;
    double *u_base = NULL;
    double *u_sin = NULL;
    double *u_diff = NULL;
    uint8_t use_lens;
    const char *method;

    if (pw <= 0 || ph <= 0 || !g_glass.obj.pixel_data ||
        !rm_check_aviutl_ver() || !rm_check_exedit_ver() ||
        (p_mode_str && *p_mode_str == 't'))
    {
        forward_stack_to_obj_draw(L, 1, (lua_gettop(L) > 8) ? "drawpoly" : "draw");
        return 0;
    }

    dst = (uint32_t *)rm_malloc((size_t)pw * (size_t)ph * 4u);
    if (!dst)
    {
        forward_stack_to_obj_draw(L, 1, (lua_gettop(L) > 8) ? "drawpoly" : "draw");
        return 0;
    }

    cam = compute_draw_state(L, 0, 1, 0, &ds, &g_glass.obj);
    lua_settop(L, 0);

    if (ds.nargs > 12)
    {
        ds.args[12] = 0.0;
        ds.args[13] = 0.0;
        ds.args[14] = g_glass.obj.w;
        ds.args[15] = 0.0;
        ds.args[16] = g_glass.obj.w;
        ds.args[17] = g_glass.obj.h;
        ds.args[18] = 0.0;
        ds.args[19] = g_glass.obj.h;
    }

    if (cam->camera_mode == 0)
    {
        vdot = vec3_dot(cam->n, ds.normal);
    }
    else
    {
        double cd[3];
        cd[0] = ds.center[0] - cam->cam_pos[0];
        cd[1] = ds.center[1] - cam->cam_pos[1];
        cd[2] = ds.center[2] - cam->cam_pos[2];
        vdot = vec3_dot(cd, ds.normal);
    }

    if (vdot < 0.0)
    {
        ndu = vec3_dot(cam->u, ds.normal);
        nde = -vec3_dot(cam->e, ds.normal);
        vdot = -vdot;
    }
    else if (g_glass.culling == 1)
    {
        rm_free(dst);
        lua_getglobal(L, "obj");
        lua_getfield(L, -1, "setoption");
        lua_pushstring(L, "draw_state");
        lua_pushboolean(L, 1);
        lua_call(L, 2, 0);
        lua_pop(L, 1);
        return 0;
    }
    else
    {
        ndu = -vec3_dot(cam->u, ds.normal);
        nde = vec3_dot(cam->e, ds.normal);
    }

    denom_v = (vdot != 0.0) ? vdot : RM_DBL_MIN_POS;
    ref_x = nde * g_glass.refractive * (g_glass.offset_z / denom_v);
    ref_y = (g_glass.offset_z / denom_v) * ndu * g_glass.refractive;

    crz = cos(cam->rz);
    srz = sin(cam->rz);
    for (k = 0; k < 4; k++)
    {
        double rel[3];
        double zn, sc, px, py;
        rel[0] = ds.corners[k][0] - cam->cam_pos[0];
        rel[1] = ds.corners[k][1] - cam->cam_pos[1];
        rel[2] = ds.corners[k][2] - cam->cam_pos[2];
        zn = vec3_dot(rel, cam->n);
        if (zn == 0.0)
        {
            zn = RM_DBL_MIN_POS;
        }
        sc = cam->d / zn;
        px = vec3_dot(rel, cam->e) * sc;
        py = -vec3_dot(rel, cam->u) * sc;
        sx[k] = crz * px - py * srz + ref_x;
        sy[k] = px * srz + py * crz + ref_y;
    }

    if (g_glass.inv_zoom != 1.0)
    {
        double cx_off = (1.0 - g_glass.inv_zoom) * (sx[0] + sx[1] + sx[2] + sx[3]) * 0.25;
        double cy_off = (1.0 - g_glass.inv_zoom) * (sy[0] + sy[1] + sy[2] + sy[3]) * 0.25;
        for (k = 0; k < 4; k++)
        {
            sx[k] = sx[k] * g_glass.inv_zoom + cx_off;
            sy[k] = sy[k] * g_glass.inv_zoom + cy_off;
        }
    }

    off_x = g_glass.half_sw;
    off_y = g_glass.half_sh;
    if (g_glass.reverse & 1u)
    {
        off_y = (sy[0] + sy[1] + sy[2] + sy[3]) * 0.5 + g_glass.half_sh;
        for (k = 0; k < 4; k++)
        {
            sy[k] = -sy[k];
        }
    }
    if (g_glass.reverse & 2u)
    {
        off_x = (sx[0] + sx[1] + sx[2] + sx[3]) * 0.5 + g_glass.half_sw;
        for (k = 0; k < 4; k++)
        {
            sx[k] = -sx[k];
        }
    }

    dx03 = sx[3] - sx[0];
    dx12 = sx[2] - sx[1];
    dy03 = sy[3] - sy[0];
    dy12 = sy[2] - sy[1];

    method = (ds.flags & 3u) ? "drawpoly" : "draw";
    lua_getglobal(L, "obj");

    if (g_glass.async_rgb)
    {
        rgb = g_glass.async_rgb;
    }
    else
    {
        void ***pp;
        lua_getfield(L, 1, "pixeloption");
        lua_pushstring(L, "put");
        lua_pushstring(L, "frm");
        lua_call(L, 2, 0);
        pp = (void ***)RikkyGetMemoryData(0x9f);
        if (pp)
        {
            rgb = capture_frame_rgb(g_glass.sw, g_glass.sh, g_glass.yc_stride, *pp);
        }
    }

    if (!rgb || g_glass.sw <= 0 || g_glass.sh <= 0)
    {
        rm_free(dst);
        call_obj_draw_args(L, 1, method, ds.args, ds.nargs);
        lua_pop(L, 1);
        return 0;
    }

    denom_x = (pw > 1) ? (double)(pw - 1) : 1.0;
    denom_y = (ph > 1) ? (double)(ph - 1) : 1.0;
    use_lens = g_glass.lens;

    if (use_lens != 0)
    {
        size_t lut_len = (size_t)(pw > ph ? pw : ph);
        u_base = (double *)rm_malloc(lut_len * sizeof(double));
        u_sin = (double *)rm_malloc(lut_len * sizeof(double));
        u_diff = (double *)rm_malloc(lut_len * sizeof(double));
        if (!u_base || !u_sin || !u_diff)
        {
            rm_free(u_base);
            rm_free(u_sin);
            rm_free(u_diff);
            u_base = NULL;
            u_sin = NULL;
            u_diff = NULL;
            use_lens = 0;
        }
        else
        {
            for (x = 0; x < pw; x++)
            {
                double ub = (pw > 1) ? ((double)x / denom_x) : 0.0;
                double us = sin(ub * RM_PI);
                u_base[x] = ub;
                u_sin[x] = us;
                if (use_lens == 1)
                {
                    double t = us * 0.5;
                    if (ub > 0.5)
                    {
                        t = 1.0 - t;
                    }
                    u_diff[x] = t - ub;
                }
                else
                {
                    u_diff[x] = (1.0 - cos(ub * RM_PI)) * 0.5 - ub;
                }
            }
        }
    }

    orig = (const uint32_t *)g_glass.obj.pixel_data;
    for (y = 0; y < ph; y++)
    {
        double vy0 = (ph > 1) ? ((double)y / denom_y) : 0.0;
        double sy_sin = 0.0;
        double dy_diff = 0.0;
        size_t row_off = (size_t)y * (size_t)pw;

        if (use_lens == 1)
        {
            double ty;
            sy_sin = sin(vy0 * RM_PI);
            ty = sy_sin * 0.5;
            if (vy0 > 0.5)
            {
                ty = 1.0 - ty;
            }
            dy_diff = ty - vy0;
        }
        else if (use_lens == 2)
        {
            sy_sin = sin(vy0 * RM_PI);
            dy_diff = (1.0 - cos(vy0 * RM_PI)) * 0.5 - vy0;
        }

        for (x = 0; x < pw; x++)
        {
            double u, v, lx, ly;
            int ix, iy;
            const uint8_t *p;
            int r, g, b;

            if (use_lens != 0)
            {
                v = u_sin[x] * dy_diff + vy0;
                u = u_diff[x] * sy_sin + u_base[x];
            }
            else
            {
                v = vy0;
                u = (pw > 1) ? ((double)x / denom_x) : 0.0;
            }

            lx = dx03 * v + sx[0];
            ly = dy03 * v + sy[0];
            ix = (int)(((dx12 * v + sx[1]) - lx) * u + lx + off_x);
            iy = (int)(((dy12 * v + sy[1]) - ly) * u + ly + off_y);
            ix = wrap_coord(ix, g_glass.sw, g_glass.boundary);
            iy = wrap_coord(iy, g_glass.sh, g_glass.boundary);

            p = rgb + ((size_t)iy * (size_t)g_glass.sw + (size_t)ix) * 3u;
            b = p[0];
            g = p[1];
            r = p[2];

            if (g_glass.color != -1)
            {
                int cr = g_glass.color_r;
                int cg = g_glass.color_g;
                int cb = g_glass.color_b;
                double lum = (double)(b - cb) * 0.114478 +
                             (double)(g - cg) * 0.586611 +
                             (double)(r - cr) * 0.298912;
                r = clamp_int((int)((double)cr + lum), 0, 255);
                g = clamp_int((int)((double)cg + lum), 0, 255);
                b = clamp_int((int)((double)cb + lum), 0, 255);
            }

            dst[row_off + (size_t)x] = (orig[row_off + (size_t)x] & 0xff000000u) |
                                       ((uint32_t)r << 16) |
                                       ((uint32_t)g << 8) |
                                       (uint32_t)b;
        }
    }

    rm_free(u_base);
    rm_free(u_sin);
    rm_free(u_diff);
    if (!g_glass.async_rgb)
    {
        rm_free(rgb);
    }

    lua_getfield(L, 1, "putpixeldata");
    lua_pushlightuserdata(L, dst);
    lua_call(L, 1, 0);

    if (g_glass.blur > 0)
    {
        lua_getfield(L, 1, "effect");
        lua_pushstring(L, k_sjis_blur);
        lua_pushstring(L, k_sjis_range);
        lua_pushinteger(L, g_glass.blur);
        lua_pushstring(L, k_sjis_fix_size);
        lua_pushinteger(L, 1);
        lua_call(L, 5, 0);
    }

    call_obj_draw_args(L, 1, method, ds.args, ds.nargs);

    lua_getfield(L, 1, "putpixeldata");
    lua_pushlightuserdata(L, g_glass.obj.pixel_data);
    lua_call(L, 1, 0);

    lua_pop(L, 1);
    rm_free(dst);
    return 0;
}

static int read_field_or_idx_num(lua_State *L, int tbl_idx, const char *key, int idx, double *out)
{
    lua_getfield(L, tbl_idx, key);
    if (lua_type(L, -1) == LUA_TNUMBER)
    {
        *out = lua_tonumber(L, -1);
        lua_pop(L, 1);
        return 1;
    }
    lua_pop(L, 1);
    lua_rawgeti(L, tbl_idx, idx);
    if (lua_type(L, -1) == LUA_TNUMBER)
    {
        *out = lua_tonumber(L, -1);
        lua_pop(L, 1);
        return 1;
    }
    lua_pop(L, 1);
    return 0;
}

int __cdecl lua_rikky_materialdraw_init(lua_State *L)
{
    RmCamState *cam;
    int i;

    if (!rm_check_aviutl_ver() || !rm_check_exedit_ver())
    {
        lua_settop(L, 0);
        lua_pushboolean(L, 0);
        return 1;
    }

    if (lua_type(L, 1) == LUA_TNUMBER && lua_tointeger(L, 1) == 0)
    {
        memset(&g_mat, 0, sizeof(g_mat));
        lua_settop(L, 0);
        lua_pushboolean(L, 1);
        return 1;
    }

    if (lua_type(L, 1) == LUA_TTABLE)
    {
        static const char *const k_rgb_keys[3] = { "R", "G", "B" };
        static const char *const k_xyz_keys[3] = { "x", "y", "z" };
        int c;

        lua_getfield(L, 1, "light_num");
        if (lua_type(L, -1) == LUA_TNUMBER)
        {
            g_mat.light_num = clamp_int((int)lua_tointeger(L, -1), 0, 4);
        }
        lua_pop(L, 1);

        for (i = 0; i < g_mat.light_num; i++)
        {
            char key[16];
            double val;

            snprintf(key, sizeof(key), "position%d", i + 1);
            lua_getfield(L, 1, key);
            if (lua_type(L, -1) == LUA_TTABLE)
            {
                int pt = lua_gettop(L);
                for (c = 0; c < 3; c++)
                {
                    lua_getfield(L, pt, k_xyz_keys[c]);
                    if (lua_isnumber(L, -1))
                    {
                        g_mat.lights[i].pos[c] = lua_tonumber(L, -1);
                    }
                    else
                    {
                        lua_pop(L, 1);
                        lua_rawgeti(L, pt, c + 1);
                        if (lua_isnumber(L, -1))
                        {
                            g_mat.lights[i].pos[c] = lua_tonumber(L, -1);
                        }
                    }
                    lua_pop(L, 1);
                }

                lua_getfield(L, pt, "object");
                if (lua_type(L, -1) != LUA_TSTRING)
                {
                    lua_pop(L, 1);
                    lua_rawgeti(L, pt, 4);
                }
                if (lua_type(L, -1) == LUA_TSTRING)
                {
                    const char *s = lua_tolstring(L, -1, NULL);
                    if (s && strcmp(s, "shadow") == 0)
                    {
                        g_mat.lights[i].target = 101;
                    }
                    else if (s && strcmp(s, "camera") == 0)
                    {
                        g_mat.lights[i].target = 102;
                    }
                    else if (s && s[0] != '\0' && (uint8_t)(s[1] - '1') < 9u)
                    {
                        int lv = atoi(s + 1);
                        g_mat.lights[i].target = (lv >= 1 && lv <= 100) ? lv : 0;
                    }
                    else
                    {
                        g_mat.lights[i].target = 0;
                    }
                }
                else
                {
                    g_mat.lights[i].target = 0;
                }
                lua_pop(L, 1);
            }
            lua_pop(L, 1);

            snprintf(key, sizeof(key), "light%d", i + 1);
            lua_getfield(L, 1, key);
            if (lua_type(L, -1) == LUA_TTABLE)
            {
                int lt = lua_gettop(L);
                for (c = 0; c < 3; c++)
                {
                    if (read_field_or_idx_num(L, lt, k_rgb_keys[c], c + 1, &val))
                    {
                        g_mat.lights[i].col[c] = clamp_01(val / 255.0);
                    }
                }
            }
            lua_pop(L, 1);
        }

        lua_getfield(L, 1, "ambient");
        if (lua_type(L, -1) == LUA_TTABLE)
        {
            int at = lua_gettop(L);
            double val;
            for (c = 0; c < 3; c++)
            {
                if (read_field_or_idx_num(L, at, k_rgb_keys[c], c + 1, &val))
                {
                    g_mat.ambient[c] = clamp_01(val * 0.01);
                }
            }
        }
        lua_pop(L, 1);

        lua_getfield(L, 1, "specular");
        if (lua_type(L, -1) == LUA_TTABLE)
        {
            int st = lua_gettop(L);
            double val;
            if (read_field_or_idx_num(L, st, "R", 1, &val))
            {
                if (val < 0.0)
                {
                    val = 0.0;
                }
                else if ((float)g_mat.ambient[0] > 255.0f)
                {
                    val = 255.0;
                }
                g_mat.specular[0] = val;
            }
            for (c = 1; c < 3; c++)
            {
                if (read_field_or_idx_num(L, st, k_rgb_keys[c], c + 1, &val))
                {
                    if (val < 0.0)
                    {
                        val = 0.0;
                    }
                    else if (val > 255.0)
                    {
                        val = 255.0;
                    }
                    g_mat.specular[c] = val;
                }
            }
            if (read_field_or_idx_num(L, st, "shininess", 4, &val))
            {
                double sh = val * 0.01;
                g_mat.shininess = (sh < 0.0) ? 0.0 : sh;
            }
        }
        lua_pop(L, 1);

        lua_getfield(L, 1, "emissive");
        if (lua_type(L, -1) == LUA_TTABLE)
        {
            int et = lua_gettop(L);
            double val;
            for (c = 0; c < 3; c++)
            {
                if (read_field_or_idx_num(L, et, k_rgb_keys[c], c + 1, &val))
                {
                    g_mat.emissive[c] = clamp_int((int)val, 0, 255);
                }
            }
        }
        lua_pop(L, 1);
    }

    lua_settop(L, 0);
    cam = query_cam_state(L);
    query_obj_state(L, &g_mat.obj);
    g_mat.buf_bytes = (size_t)g_mat.obj.pixel_w * (size_t)g_mat.obj.pixel_h * 4u;

    lua_getglobal(L, "obj");
    for (i = g_mat.light_num - 1; i >= 0; i--)
    {
        int tgt = g_mat.lights[i].target;
        if (tgt == 101)
        {
            const int *p_sh = (const int *)RikkyGetMemoryData(0x7f);
            if (p_sh)
            {
                g_mat.lights[i].pos[0] = (double)p_sh[1] * 0.1;
                g_mat.lights[i].pos[1] = (double)p_sh[0] * 0.1;
                g_mat.lights[i].pos[2] = (double)p_sh[2] * 0.1;
            }
        }
        else if (tgt == 102)
        {
            g_mat.lights[i].pos[0] = cam->cam_pos[0];
            g_mat.lights[i].pos[1] = cam->cam_pos[1];
            g_mat.lights[i].pos[2] = cam->cam_pos[2];
        }
        else if (tgt >= 1 && tgt <= 100)
        {
            char buf[32];
            lua_getfield(L, 1, "getvalue");
            snprintf(buf, sizeof(buf), "layer%d.x", tgt);
            lua_pushstring(L, buf);
            lua_call(L, 1, 1);
            if (lua_type(L, -1) == LUA_TNUMBER)
            {
                double lp[3];
                lp[0] = lua_tonumber(L, -1);
                lua_pop(L, 1);
                lua_getfield(L, 1, "getvalue");
                snprintf(buf, sizeof(buf), "layer%d.y", tgt);
                lua_pushstring(L, buf);
                lua_call(L, 1, 1);
                lp[1] = lua_tonumber(L, -1);
                lua_pop(L, 1);
                lua_getfield(L, 1, "getvalue");
                snprintf(buf, sizeof(buf), "layer%d.z", tgt);
                lua_pushstring(L, buf);
                lua_call(L, 1, 1);
                lp[2] = lua_tonumber(L, -1);
                lua_pop(L, 1);
                if (cam->shadow.enabled)
                {
                    shadow_transform_pt(&cam->shadow, lp, g_mat.lights[i].pos);
                }
                else
                {
                    g_mat.lights[i].pos[0] = lp[0];
                    g_mat.lights[i].pos[1] = lp[1];
                    g_mat.lights[i].pos[2] = lp[2];
                }
            }
            else
            {
                lua_pop(L, 1);
            }
        }
    }

    lua_settop(L, 0);
    lua_pushboolean(L, 1);
    return 1;
}

static void compute_uv_bbox(
    const RmDrawState *ds,
    const RmObjState *obj,
    int *out_x0,
    int *out_x1,
    int *out_y0,
    int *out_y1)
{
    double min_u = ds->args[12];
    double max_u = ds->args[12];
    double min_v = ds->args[13];
    double max_v = ds->args[13];
    double u0, u1, v0, v1;
    int k;

    for (k = 1; k < 4; k++)
    {
        double u = ds->args[12 + k * 2];
        double v = ds->args[13 + k * 2];
        if (u < min_u)
        {
            min_u = u;
        }
        if (u > max_u)
        {
            max_u = u;
        }
        if (v < min_v)
        {
            min_v = v;
        }
        if (v > max_v)
        {
            max_v = v;
        }
    }

    u0 = (obj->w != 0.0) ? clamp_01((min_u - 2.0) / obj->w) : 0.0;
    u1 = (obj->w != 0.0) ? clamp_01((max_u + 2.0) / obj->w) : 1.0;
    v0 = (obj->h != 0.0) ? clamp_01((min_v - 2.0) / obj->h) : 0.0;
    v1 = (obj->h != 0.0) ? clamp_01((max_v + 2.0) / obj->h) : 1.0;

    *out_x0 = clamp_int((int)ceil(u0 * (double)obj->pixel_w), 0, obj->pixel_w);
    *out_x1 = clamp_int((int)ceil(u1 * (double)obj->pixel_w), 0, obj->pixel_w);
    *out_y0 = clamp_int((int)ceil(v0 * (double)obj->pixel_h), 0, obj->pixel_h);
    *out_y1 = clamp_int((int)ceil(v1 * (double)obj->pixel_h), 0, obj->pixel_h);
}

int __cdecl lua_rikky_materialdraw(lua_State *L)
{
    static const double k_def_dir[3] = { 0.0, 0.0, -1.0 };
    const char *p_mode_str = (const char *)RikkyGetMemoryData(0x8a);
    RmCamState *cam;
    RmDrawState ds;
    const char *method;
    uint32_t *dst;
    double v_dir[3], n_dir[3];
    double diff_r = 0.0, diff_g = 0.0, diff_b = 0.0;
    int spec_r = 0, spec_g = 0, spec_b = 0;
    double mul_r, mul_g, mul_b;
    int add_r, add_g, add_b;
    int i;

    if (!rm_check_aviutl_ver() || !rm_check_exedit_ver() ||
        (p_mode_str && *p_mode_str == 't') ||
        !g_mat.obj.pixel_data || g_mat.buf_bytes == 0)
    {
        int n = lua_gettop(L);
        if (n < 9 || n == 12 || n == 20 || n == 21)
        {
            forward_stack_to_obj_draw(L, 1, (n > 8) ? "drawpoly" : "draw");
        }
        else
        {
            lua_settop(L, 0);
            lua_getglobal(L, "obj");
            lua_getfield(L, -1, "draw");
            lua_call(L, 0, 0);
            lua_pop(L, 1);
        }
        return 0;
    }

    cam = compute_draw_state(L, 0, 1, 0, &ds, &g_mat.obj);
    method = (ds.flags & 3u) ? "drawpoly" : "draw";
    lua_settop(L, 0);
    lua_getglobal(L, "obj");

    dst = (uint32_t *)rm_malloc(g_mat.buf_bytes);
    if (!dst)
    {
        call_obj_draw_args(L, 1, method, ds.args, ds.nargs);
        lua_pop(L, 1);
        return 0;
    }
    memcpy(dst, g_mat.obj.pixel_data, g_mat.buf_bytes);

    v_dir[0] = cam->cam_pos[0] - ds.center[0];
    v_dir[1] = cam->cam_pos[1] - ds.center[1];
    v_dir[2] = cam->cam_pos[2] - ds.center[2];
    rm_vec3_normalize_or_default(v_dir, k_def_dir, -1.0);

    n_dir[0] = ds.normal[0];
    n_dir[1] = ds.normal[1];
    n_dir[2] = ds.normal[2];
    if (vec3_dot(v_dir, n_dir) < 0.0)
    {
        n_dir[0] = -n_dir[0];
        n_dir[1] = -n_dir[1];
        n_dir[2] = -n_dir[2];
    }

    for (i = g_mat.light_num - 1; i >= 0; i--)
    {
        double l_dir[3];
        double ndotl;
        l_dir[0] = g_mat.lights[i].pos[0] - ds.center[0];
        l_dir[1] = g_mat.lights[i].pos[1] - ds.center[1];
        l_dir[2] = g_mat.lights[i].pos[2] - ds.center[2];
        rm_vec3_normalize_or_default(l_dir, k_def_dir, -1.0);
        ndotl = vec3_dot(n_dir, l_dir);
        if (ndotl > 0.0)
        {
            double r_dir[3];
            double rdotv, sp;
            r_dir[0] = 2.0 * ndotl * n_dir[0] - l_dir[0];
            r_dir[1] = 2.0 * ndotl * n_dir[1] - l_dir[1];
            r_dir[2] = 2.0 * ndotl * n_dir[2] - l_dir[2];
            rm_vec3_normalize_or_default(r_dir, k_def_dir, -1.0);
            rdotv = vec3_dot(r_dir, v_dir);
            if (rdotv < 0.0)
            {
                rdotv = 0.0;
            }
            sp = pow(rdotv, g_mat.shininess);
            spec_r = (int)(g_mat.specular[0] * sp + (double)spec_r);
            spec_g = (int)(g_mat.specular[1] * sp + (double)spec_g);
            spec_b = (int)(g_mat.specular[2] * sp + (double)spec_b);
            diff_r += g_mat.lights[i].col[0] * ndotl;
            diff_g += g_mat.lights[i].col[1] * ndotl;
            diff_b += g_mat.lights[i].col[2] * ndotl;
        }
    }

    mul_r = diff_r + g_mat.ambient[0];
    mul_g = diff_g + g_mat.ambient[1];
    mul_b = diff_b + g_mat.ambient[2];
    add_r = spec_r + g_mat.emissive[0];
    add_g = spec_g + g_mat.emissive[1];
    add_b = spec_b + g_mat.emissive[2];

    if (ds.nargs < 13)
    {
        size_t total = (size_t)g_mat.obj.pixel_w * (size_t)g_mat.obj.pixel_h;
        size_t p;
        for (p = 0; p < total; p++)
        {
            uint32_t px = dst[p];
            int r = clamp_int((int)((double)((px >> 16) & 0xffu) * mul_r + (double)add_r), 0, 255);
            int g = clamp_int((int)((double)((px >> 8) & 0xffu) * mul_g + (double)add_g), 0, 255);
            int b = clamp_int((int)((double)(px & 0xffu) * mul_b + (double)add_b), 0, 255);
            dst[p] = (px & 0xff000000u) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        }
    }
    else
    {
        int x0, x1, y0, y1, x, y;
        int pw = g_mat.obj.pixel_w;
        compute_uv_bbox(&ds, &g_mat.obj, &x0, &x1, &y0, &y1);
        for (y = y0; y < y1; y++)
        {
            uint32_t *row = dst + (size_t)y * (size_t)pw;
            for (x = x0; x < x1; x++)
            {
                uint32_t px = row[x];
                int r = clamp_int((int)((double)((px >> 16) & 0xffu) * mul_r + (double)add_r), 0, 255);
                int g = clamp_int((int)((double)((px >> 8) & 0xffu) * mul_g + (double)add_g), 0, 255);
                int b = clamp_int((int)((double)(px & 0xffu) * mul_b + (double)add_b), 0, 255);
                row[x] = (px & 0xff000000u) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
            }
        }
    }

    lua_getfield(L, 1, "putpixeldata");
    lua_pushlightuserdata(L, dst);
    lua_call(L, 1, 0);

    call_obj_draw_args(L, 1, method, ds.args, ds.nargs);

    lua_getfield(L, 1, "putpixeldata");
    lua_pushlightuserdata(L, g_mat.obj.pixel_data);
    lua_call(L, 1, 0);

    lua_pop(L, 1);
    rm_free(dst);
    return 0;
}

static void free_matex_cache(void)
{
    uint32_t e;
    if (!g_matex_entries)
    {
        g_matex_count = 0;
        return;
    }

    for (e = 0; e < g_matex_count; e++)
    {
        RmMatExEntry *ent = &g_matex_entries[e];
        int i;
        rm_free(ent->obj.pixel_data);
        if (ent->lights)
        {
            for (i = 0; i < ent->num_lights; i++)
            {
                int8_t t = ent->lights[i].type;
                void *ex = ent->lights[i].extra;
                if (!ex)
                {
                    continue;
                }
                if (t == 1 || t == 2)
                {
                    rm_free(ex);
                }
                else if (t == 3 || t == 4)
                {
                    RmDirectExtra *dex = (RmDirectExtra *)ex;
                    rm_free(dex->verts);
                    rm_free(dex);
                }
                else if (t == 5 || t == 6)
                {
                    RmDirectExtra *dex = (RmDirectExtra *)ex;
                    rm_free(dex->quads);
                    rm_free(dex->verts);
                    rm_free(dex);
                }
            }
            rm_free(ent->lights);
        }
    }

    rm_free(g_matex_entries);
    g_matex_entries = NULL;
    g_matex_count = 0;
}

static void parse_cone_deg(
    lua_State *L,
    int tbl_idx,
    const char *key,
    double def_cos,
    double def_omc,
    double *out_cos,
    double *out_omc)
{
    lua_getfield(L, tbl_idx, key);
    if (lua_type(L, -1) == LUA_TNUMBER)
    {
        double deg = lua_tonumber(L, -1);
        if (deg < 0.0)
        {
            *out_cos = 1.0;
            *out_omc = 0.0;
        }
        else if (deg > 90.0)
        {
            *out_cos = 6.123233995736766e-17;
            *out_omc = 0.9999999999999999;
        }
        else
        {
            double c = cos(deg * RM_DEG2RAD);
            *out_cos = c;
            *out_omc = 1.0 - c;
        }
    }
    else
    {
        *out_cos = def_cos;
        *out_omc = def_omc;
    }
    lua_pop(L, 1);
}

static double read_obj_layer_prop(lua_State *L, int obj_idx, int layer, const char *prop)
{
    char key[32];
    double val;
    snprintf(key, sizeof(key), "layer%d.%s", layer, prop);
    lua_getfield(L, obj_idx, "getvalue");
    lua_pushstring(L, key);
    lua_call(L, 1, 1);
    val = lua_tonumber(L, -1);
    lua_pop(L, 1);
    return val;
}

static int check_obj_layer_exists(lua_State *L, int obj_idx, int layer, double *out_x)
{
    char key[32];
    int ok;
    snprintf(key, sizeof(key), "layer%d.x", layer);
    lua_getfield(L, obj_idx, "getvalue");
    lua_pushstring(L, key);
    lua_call(L, 1, 1);
    ok = (lua_type(L, -1) == LUA_TNUMBER);
    if (ok && out_x)
    {
        *out_x = lua_tonumber(L, -1);
    }
    lua_pop(L, 1);
    return ok;
}

static void accum_matex_phong(
    const RmExLight *lt,
    const double *qcol,
    const double *l_dir,
    double atten,
    const double *n_dir,
    const double *v_dir,
    double *acc)
{
    static const double k_def_dir[3] = { 0.0, 0.0, -1.0 };
    double ndotl;
    if (atten > 1.0)
    {
        atten = 1.0;
    }
    if (atten <= 0.0)
    {
        return;
    }
    ndotl = vec3_dot(n_dir, l_dir);
    if (ndotl > 0.0)
    {
        double r_dir[3];
        double rdotv, lum, sp;
        r_dir[0] = 2.0 * ndotl * n_dir[0] - l_dir[0];
        r_dir[1] = 2.0 * ndotl * n_dir[1] - l_dir[1];
        r_dir[2] = 2.0 * ndotl * n_dir[2] - l_dir[2];
        rm_vec3_normalize_or_default(r_dir, k_def_dir, -1.0);
        rdotv = vec3_dot(r_dir, v_dir);
        if (rdotv < 0.0)
        {
            rdotv = 0.0;
        }
        lum = 0.298912 * qcol[0] + 0.586611 * qcol[1] + 0.114478 * qcol[2];
        sp = pow(rdotv, lt->shininess) * lum;
        acc[0] += (lt->spec[0] * sp + ndotl) * qcol[0] * atten;
        acc[1] += (lt->spec[1] * sp + ndotl) * qcol[1] * atten;
        acc[2] += (lt->spec[2] * sp + ndotl) * qcol[2] * atten;
    }
}

static void eval_matex_quad(
    const RmMatExEntry *entry,
    const RmExLight *lt,
    const RmDirectExtra *dex,
    const double *v0,
    const double *v1,
    const double *v2,
    const double *v3,
    const double *qcol,
    uint8_t edge_mask,
    int is_double,
    const double *pt,
    const double *n_dir,
    const double *v_dir,
    double *acc)
{
    double r[4][3];
    const double *qv[4];
    uint8_t mask = 0;
    int k;

    qv[0] = v0;
    qv[1] = v1;
    qv[2] = v2;
    qv[3] = v3;
    for (k = 0; k < 4; k++)
    {
        r[k][0] = qv[k][0] - pt[0];
        r[k][1] = qv[k][1] - pt[1];
        r[k][2] = qv[k][2] - pt[2];
    }

    for (k = 0; k < 4; k++)
    {
        int kn = (k + 1) & 3;
        double cx = r[kn][1] * r[k][2] - r[kn][2] * r[k][1];
        double cy = r[kn][2] * r[k][0] - r[kn][0] * r[k][2];
        double cz = r[kn][0] * r[k][1] - r[kn][1] * r[k][0];
        if (cx * dex->dir[0] + cy * dex->dir[1] + cz * dex->dir[2] < 0.0)
        {
            mask |= (uint8_t)(1u << k);
        }
    }

    if (mask == 0)
    {
        double dist = -vec3_dot(r[0], dex->dir);
        if (dist > 0.0)
        {
            double l_dir[3];
            double atten = (entry->damping_sq != 0.0) ? (entry->damping_sq / (dist * dist)) : 1.0;
            if (atten > 1.0)
            {
                atten = 1.0;
            }
            l_dir[0] = -dex->dir[0];
            l_dir[1] = -dex->dir[1];
            l_dir[2] = -dex->dir[2];
            accum_matex_phong(lt, qcol, l_dir, atten, n_dir, v_dir, acc);
        }
        else if (is_double && dist < 0.0)
        {
            double atten = (entry->damping_sq != 0.0) ? (entry->damping_sq / (dist * dist)) : 1.0;
            if (atten > 1.0)
            {
                atten = 1.0;
            }
            accum_matex_phong(lt, qcol, dex->dir, atten, n_dir, v_dir, acc);
        }
    }
    else if ((mask & edge_mask) == 0)
    {
        double cp[3];
        double d2, base_atten, ldotd;
        double l_dir[3];

        if (mask == 1)
        {
            double t = -vec3_dot(r[0], dex->right);
            cp[0] = r[0][0] + dex->right[0] * t;
            cp[1] = r[0][1] + dex->right[1] * t;
            cp[2] = r[0][2] + dex->right[2] * t;
        }
        else if (mask == 2)
        {
            double t = -vec3_dot(r[1], dex->down);
            cp[0] = r[1][0] + dex->down[0] * t;
            cp[1] = r[1][1] + dex->down[1] * t;
            cp[2] = r[1][2] + dex->down[2] * t;
        }
        else if (mask == 4)
        {
            double t = vec3_dot(r[2], dex->right);
            cp[0] = r[2][0] - dex->right[0] * t;
            cp[1] = r[2][1] - dex->right[1] * t;
            cp[2] = r[2][2] - dex->right[2] * t;
        }
        else if (mask == 8)
        {
            double t = vec3_dot(r[3], dex->down);
            cp[0] = r[3][0] - dex->down[0] * t;
            cp[1] = r[3][1] - dex->down[1] * t;
            cp[2] = r[3][2] - dex->down[2] * t;
        }
        else if (mask == 3)
        {
            cp[0] = r[1][0];
            cp[1] = r[1][1];
            cp[2] = r[1][2];
        }
        else if (mask == 6)
        {
            cp[0] = r[2][0];
            cp[1] = r[2][1];
            cp[2] = r[2][2];
        }
        else if (mask == 9)
        {
            cp[0] = r[0][0];
            cp[1] = r[0][1];
            cp[2] = r[0][2];
        }
        else if (mask == 12)
        {
            cp[0] = r[3][0];
            cp[1] = r[3][1];
            cp[2] = r[3][2];
        }
        else
        {
            return;
        }

        d2 = vec3_dot(cp, cp);
        if (d2 == 0.0)
        {
            l_dir[0] = -dex->dir[2];
            l_dir[1] = cp[1];
            l_dir[2] = cp[2];
            base_atten = 1.0;
        }
        else
        {
            double inv_d = 1.0 / sqrt(d2);
            l_dir[0] = cp[0] * inv_d;
            l_dir[1] = cp[1] * inv_d;
            l_dir[2] = cp[2] * inv_d;
            base_atten = (entry->damping_sq != 0.0) ? (entry->damping_sq / d2) : 1.0;
            if (base_atten > 1.0)
            {
                base_atten = 1.0;
            }
        }

        ldotd = vec3_dot(l_dir, dex->dir);
        if (-ldotd >= dex->cos_deg)
        {
            double atten = (dex->omc_deg > 0.0) ? (base_atten * (1.0 - (ldotd + 1.0) / dex->omc_deg)) : base_atten;
            accum_matex_phong(lt, qcol, l_dir, atten, n_dir, v_dir, acc);
        }
        if (is_double && ldotd >= dex->cos_deg)
        {
            double atten = (dex->omc_deg > 0.0) ? (base_atten * (1.0 - (1.0 - ldotd) / dex->omc_deg)) : base_atten;
            accum_matex_phong(lt, qcol, l_dir, atten, n_dir, v_dir, acc);
        }
    }
}

static void shade_point_matex(
    const RmMatExEntry *entry,
    const double *pt,
    const double *n_dir,
    const double *v_dir,
    double *out_rgb)
{
    int i;
    out_rgb[0] = entry->ambient[0];
    out_rgb[1] = entry->ambient[1];
    out_rgb[2] = entry->ambient[2];

    for (i = entry->num_lights - 1; i >= 0; i--)
    {
        const RmExLight *lt = &entry->lights[i];
        if (lt->type == 0 || lt->type == 1 || lt->type == 2)
        {
            double dv[3];
            double d2, base_atten;
            double l_dir[3];
            dv[0] = lt->pos[0] - pt[0];
            dv[1] = lt->pos[1] - pt[1];
            dv[2] = lt->pos[2] - pt[2];
            d2 = vec3_dot(dv, dv);
            if (d2 == 0.0)
            {
                l_dir[0] = 0.0;
                l_dir[1] = 0.0;
                l_dir[2] = -1.0;
                base_atten = 1.0;
            }
            else
            {
                double inv_d = 1.0 / sqrt(d2);
                l_dir[0] = dv[0] * inv_d;
                l_dir[1] = dv[1] * inv_d;
                l_dir[2] = dv[2] * inv_d;
                base_atten = (entry->damping_sq != 0.0) ? (entry->damping_sq / d2) : 1.0;
                if (base_atten > 1.0)
                {
                    base_atten = 1.0;
                }
            }

            if (lt->type == 0)
            {
                accum_matex_phong(lt, lt->col, l_dir, base_atten, n_dir, v_dir, out_rgb);
            }
            else if (lt->extra)
            {
                const RmSpotExtra *sp = (const RmSpotExtra *)lt->extra;
                double d1 = vec3_dot(l_dir, sp->dir1);
                if (-d1 >= sp->cos1)
                {
                    double atten = (sp->omc1 > 0.0) ? (base_atten * (1.0 - (d1 + 1.0) / sp->omc1)) : base_atten;
                    accum_matex_phong(lt, lt->col, l_dir, atten, n_dir, v_dir, out_rgb);
                }
                if (lt->type == 2)
                {
                    double d2_dot = vec3_dot(l_dir, sp->dir2);
                    if (-d2_dot >= sp->cos2)
                    {
                        double atten = (sp->omc2 > 0.0) ? (base_atten * (1.0 - (d2_dot + 1.0) / sp->omc2)) : base_atten;
                        accum_matex_phong(lt, lt->col, l_dir, atten, n_dir, v_dir, out_rgb);
                    }
                }
            }
        }
        else if ((lt->type == 3 || lt->type == 4) && lt->extra)
        {
            const RmDirectExtra *dex = (const RmDirectExtra *)lt->extra;
            if (dex->verts)
            {
                eval_matex_quad(
                    entry,
                    lt,
                    dex,
                    dex->verts,
                    dex->verts + 3,
                    dex->verts + 6,
                    dex->verts + 9,
                    lt->col,
                    0,
                    lt->type == 4,
                    pt,
                    n_dir,
                    v_dir,
                    out_rgb);
            }
        }
        else if ((lt->type == 5 || lt->type == 6) && lt->extra)
        {
            const RmDirectExtra *dex = (const RmDirectExtra *)lt->extra;
            if (dex->verts && dex->quads)
            {
                int q;
                int is_dbl = (lt->type == 6);
                for (q = 0; q < dex->num_quads; q++)
                {
                    const RmTexQuad *tq = &dex->quads[q];
                    eval_matex_quad(
                        entry,
                        lt,
                        dex,
                        dex->verts + tq->v[0] * 3,
                        dex->verts + tq->v[1] * 3,
                        dex->verts + tq->v[2] * 3,
                        dex->verts + tq->v[3] * 3,
                        tq->col,
                        tq->edge_mask,
                        is_dbl,
                        pt,
                        n_dir,
                        v_dir,
                        out_rgb);
                }
            }
        }
    }
}

static uint32_t apply_matex_pixel(uint32_t px, const double *acc, const double *emissive)
{
    double r = (double)((px >> 16) & 0xffu) * RM_INV255 * acc[0] + emissive[0];
    double g = (double)((px >> 8) & 0xffu) * RM_INV255 * acc[1] + emissive[1];
    double b = (double)(px & 0xffu) * RM_INV255 * acc[2] + emissive[2];
    uint32_t ur = (r >= 1.0) ? 255u : ((r <= 0.0) ? 0u : (uint32_t)(uint8_t)(int)(r * 255.0));
    uint32_t ug = (g >= 1.0) ? 255u : ((g <= 0.0) ? 0u : (uint32_t)(uint8_t)(int)(g * 255.0));
    uint32_t ub = (b >= 1.0) ? 255u : ((b <= 0.0) ? 0u : (uint32_t)(uint8_t)(int)(b * 255.0));
    return (px & 0xff000000u) | (ur << 16) | (ug << 8) | ub;
}

static int get_valid_matex_id(lua_State *L)
{
    int id = 0;
    if (lua_type(L, 1) != LUA_TTABLE)
    {
        return 0;
    }
    lua_getfield(L, 1, "id");
    if (lua_type(L, -1) == LUA_TNUMBER)
    {
        id = (int)lua_tointeger(L, -1);
    }
    lua_pop(L, 1);
    if (id < 1 || (uint32_t)id > g_matex_count || !g_matex_entries)
    {
        return 0;
    }
    return id;
}

static int __cdecl matex_drawpoly(lua_State *L)
{
    static const double k_def_dir[3] = { 0.0, 0.0, -1.0 };
    const char *p_mode_str = (const char *)RikkyGetMemoryData(0x8a);
    int id;
    const RmMatExEntry *entry;
    RmCamState *cam;
    RmDrawState ds;
    uint32_t *dst;
    const uint32_t *src;
    double v_dir[3], n_dir[3], acc[3];

    id = get_valid_matex_id(L);
    if (!rm_check_aviutl_ver() || !rm_check_exedit_ver() ||
        (p_mode_str && *p_mode_str == 't') || id == 0)
    {
        int n = lua_gettop(L) - 1;
        if (n == 12 || n == 20 || n == 21)
        {
            forward_stack_to_obj_draw(L, 2, "drawpoly");
        }
        return 0;
    }

    entry = &g_matex_entries[id - 1];
    cam = compute_draw_state(L, 3, 2, 0, &ds, &entry->obj);
    if (ds.nargs != 12 && ds.nargs != 20 && ds.nargs != 21)
    {
        return 0;
    }

    lua_settop(L, 0);
    lua_getglobal(L, "obj");

    dst = (uint32_t *)rm_malloc(entry->buf_bytes);
    if (!dst)
    {
        call_obj_draw_args(L, 1, "drawpoly", ds.args, ds.nargs);
        lua_pop(L, 1);
        return 0;
    }
    src = (const uint32_t *)entry->obj.pixel_data;
    memcpy(dst, src, entry->buf_bytes);

    v_dir[0] = cam->cam_pos[0] - ds.center[0];
    v_dir[1] = cam->cam_pos[1] - ds.center[1];
    v_dir[2] = cam->cam_pos[2] - ds.center[2];
    rm_vec3_normalize_or_default(v_dir, k_def_dir, -1.0);

    n_dir[0] = ds.normal[0];
    n_dir[1] = ds.normal[1];
    n_dir[2] = ds.normal[2];
    if (vec3_dot(v_dir, n_dir) < 0.0)
    {
        n_dir[0] = -n_dir[0];
        n_dir[1] = -n_dir[1];
        n_dir[2] = -n_dir[2];
    }

    shade_point_matex(entry, ds.center, n_dir, v_dir, acc);

    if (ds.nargs < 13)
    {
        size_t total = (size_t)entry->obj.pixel_w * (size_t)entry->obj.pixel_h;
        size_t p;
        for (p = 0; p < total; p++)
        {
            dst[p] = apply_matex_pixel(src[p], acc, entry->emissive);
        }
    }
    else
    {
        int x0, x1, y0, y1, x, y;
        int pw = entry->obj.pixel_w;
        compute_uv_bbox(&ds, &entry->obj, &x0, &x1, &y0, &y1);
        for (y = y0; y < y1; y++)
        {
            uint32_t *drow = dst + (size_t)y * (size_t)pw;
            const uint32_t *srow = src + (size_t)y * (size_t)pw;
            for (x = x0; x < x1; x++)
            {
                drow[x] = apply_matex_pixel(srow[x], acc, entry->emissive);
            }
        }
    }

    lua_getfield(L, 1, "putpixeldata");
    lua_pushlightuserdata(L, dst);
    lua_call(L, 1, 0);

    call_obj_draw_args(L, 1, "drawpoly", ds.args, ds.nargs);

    lua_getglobal(L, "obj");
    lua_getfield(L, -1, "getpixeldata");
    lua_call(L, 0, 3);
    {
        void *cur_ptr = lua_touserdata(L, -3);
        lua_pop(L, 3);
        if (cur_ptr)
        {
            memcpy(cur_ptr, src, entry->buf_bytes);
            lua_getfield(L, -1, "putpixeldata");
            lua_pushlightuserdata(L, cur_ptr);
            lua_call(L, 1, 0);
        }
    }
    lua_pop(L, 2);
    rm_free(dst);
    return 0;
}

static int __cdecl matex_draw(lua_State *L)
{
    static const double k_def_dir[3] = { 0.0, 0.0, -1.0 };
    const char *p_mode_str = (const char *)RikkyGetMemoryData(0x8a);
    int id;
    const RmMatExEntry *entry;
    RmCamState *cam;
    RmDrawState ds;
    uint32_t *dst;
    const uint32_t *src;
    int pw, ph;

    id = get_valid_matex_id(L);
    if (!rm_check_aviutl_ver() || !rm_check_exedit_ver() ||
        (p_mode_str && *p_mode_str == 't') || id == 0)
    {
        forward_stack_to_obj_draw(L, 2, "draw");
        return 0;
    }

    entry = &g_matex_entries[id - 1];
    cam = compute_draw_state(L, 2, 2, 0, &ds, &entry->obj);
    lua_settop(L, 0);
    lua_getglobal(L, "obj");

    dst = (uint32_t *)rm_malloc(entry->buf_bytes);
    if (!dst)
    {
        call_obj_draw_args(L, 1, "draw", ds.args, ds.nargs);
        lua_pop(L, 1);
        return 0;
    }

    src = (const uint32_t *)entry->obj.pixel_data;
    pw = entry->obj.pixel_w;
    ph = entry->obj.pixel_h;

    if (!entry->drawhq)
    {
        double v_dir[3], n_dir[3], acc[3];
        size_t total = (size_t)pw * (size_t)ph;
        size_t p;

        v_dir[0] = cam->cam_pos[0] - ds.center[0];
        v_dir[1] = cam->cam_pos[1] - ds.center[1];
        v_dir[2] = cam->cam_pos[2] - ds.center[2];
        rm_vec3_normalize_or_default(v_dir, k_def_dir, -1.0);

        n_dir[0] = ds.normal[0];
        n_dir[1] = ds.normal[1];
        n_dir[2] = ds.normal[2];
        if (vec3_dot(v_dir, n_dir) < 0.0)
        {
            n_dir[0] = -n_dir[0];
            n_dir[1] = -n_dir[1];
            n_dir[2] = -n_dir[2];
        }

        shade_point_matex(entry, ds.center, n_dir, v_dir, acc);
        for (p = 0; p < total; p++)
        {
            dst[p] = apply_matex_pixel(src[p], acc, entry->emissive);
        }
    }
    else
    {
        double v_center[3], n_dir[3];
        double dc03[3], dc12[3];
        double denom_x = (pw > 1) ? (double)(pw - 1) : 1.0;
        double denom_y = (ph > 1) ? (double)(ph - 1) : 1.0;
        int part = (entry->drawhq_part > 1) ? (int)entry->drawhq_part : 1;

        v_center[0] = cam->cam_pos[0] - ds.center[0];
        v_center[1] = cam->cam_pos[1] - ds.center[1];
        v_center[2] = cam->cam_pos[2] - ds.center[2];
        rm_vec3_normalize_or_default(v_center, k_def_dir, -1.0);

        n_dir[0] = ds.normal[0];
        n_dir[1] = ds.normal[1];
        n_dir[2] = ds.normal[2];
        if (vec3_dot(v_center, n_dir) < 0.0)
        {
            n_dir[0] = -n_dir[0];
            n_dir[1] = -n_dir[1];
            n_dir[2] = -n_dir[2];
        }

        dc03[0] = ds.corners[3][0] - ds.corners[0][0];
        dc03[1] = ds.corners[3][1] - ds.corners[0][1];
        dc03[2] = ds.corners[3][2] - ds.corners[0][2];
        dc12[0] = ds.corners[2][0] - ds.corners[1][0];
        dc12[1] = ds.corners[2][1] - ds.corners[1][1];
        dc12[2] = ds.corners[2][2] - ds.corners[1][2];

        if (part == 1)
        {
            int x, y;
            for (y = 0; y < ph; y++)
            {
                double v = (ph > 1) ? ((double)y / denom_y) : 0.0;
                double lpt[3], rpt[3];
                size_t row_off = (size_t)y * (size_t)pw;

                lpt[0] = ds.corners[0][0] + dc03[0] * v;
                lpt[1] = ds.corners[0][1] + dc03[1] * v;
                lpt[2] = ds.corners[0][2] + dc03[2] * v;
                rpt[0] = ds.corners[1][0] + dc12[0] * v;
                rpt[1] = ds.corners[1][1] + dc12[1] * v;
                rpt[2] = ds.corners[1][2] + dc12[2] * v;

                for (x = 0; x < pw; x++)
                {
                    double u = (pw > 1) ? ((double)x / denom_x) : 0.0;
                    double pt[3], v_dir[3], acc[3];
                    pt[0] = lpt[0] + (rpt[0] - lpt[0]) * u;
                    pt[1] = lpt[1] + (rpt[1] - lpt[1]) * u;
                    pt[2] = lpt[2] + (rpt[2] - lpt[2]) * u;
                    v_dir[0] = cam->cam_pos[0] - pt[0];
                    v_dir[1] = cam->cam_pos[1] - pt[1];
                    v_dir[2] = cam->cam_pos[2] - pt[2];
                    rm_vec3_normalize_or_default(v_dir, k_def_dir, -1.0);
                    shade_point_matex(entry, pt, n_dir, v_dir, acc);
                    dst[row_off + (size_t)x] = apply_matex_pixel(src[row_off + (size_t)x], acc, entry->emissive);
                }
            }
        }
        else
        {
            int half_w = pw / 2;
            int rem_w = half_w - (half_w / part) * part;
            int x_init0 = (rem_w > 0) ? (rem_w - part) : 0;
            int x_init1 = (rem_w > 0) ? rem_w : part;
            int half_h = ph / 2;
            int rem_h = half_h - (half_h / part) * part;
            int y_cur0 = (rem_h > 0) ? (rem_h - part) : 0;
            int y_cur1 = (rem_h > 0) ? rem_h : part;

            for (;;)
            {
                int yc0 = (y_cur0 < 0) ? 0 : y_cur0;
                int yc1 = (y_cur1 > ph) ? ph : y_cur1;
                double v0 = (ph > 1) ? ((double)yc0 / denom_y) : 0.0;
                double v1 = (ph > 1) ? ((double)yc1 / denom_y) : 0.0;
                double l0[3], l1[3];
                int x_cur0 = x_init0;
                int x_cur1 = x_init1;

                l0[0] = ds.corners[0][0] + dc03[0] * v0;
                l0[1] = ds.corners[0][1] + dc03[1] * v0;
                l0[2] = ds.corners[0][2] + dc03[2] * v0;
                l1[0] = ds.corners[0][0] + dc03[0] * v1;
                l1[1] = ds.corners[0][1] + dc03[1] * v1;
                l1[2] = ds.corners[0][2] + dc03[2] * v1;

                for (;;)
                {
                    int xc0 = (x_cur0 < 0) ? 0 : x_cur0;
                    int xc1 = (x_cur1 > pw) ? pw : x_cur1;
                    double u0 = (pw > 1) ? ((double)xc0 / denom_x) : 0.0;
                    double u1 = (pw > 1) ? ((double)xc1 / denom_x) : 0.0;
                    double pt[3], v_dir[3], acc[3];
                    int x, y;

                    pt[0] = (l0[0] + l1[0]) * 0.5 +
                            ((ds.corners[1][0] + dc12[0] * v1 - l1[0]) * u1 +
                             (ds.corners[1][0] + dc12[0] * v0 - l0[0]) * u0) * 0.5;
                    pt[1] = (l0[1] + l1[1]) * 0.5 +
                            ((ds.corners[1][1] + dc12[1] * v1 - l1[1]) * u1 +
                             (ds.corners[1][1] + dc12[1] * v0 - l0[1]) * u0) * 0.5;
                    pt[2] = (l0[2] + l1[2]) * 0.5 +
                            ((ds.corners[1][2] + dc12[2] * v1 - l1[2]) * u1 +
                             (ds.corners[1][2] + dc12[2] * v0 - l0[2]) * u0) * 0.5;

                    v_dir[0] = cam->cam_pos[0] - pt[0];
                    v_dir[1] = cam->cam_pos[1] - pt[1];
                    v_dir[2] = cam->cam_pos[2] - pt[2];
                    rm_vec3_normalize_or_default(v_dir, k_def_dir, -1.0);
                    shade_point_matex(entry, pt, n_dir, v_dir, acc);

                    for (y = yc0; y < yc1; y++)
                    {
                        uint32_t *drow = dst + (size_t)y * (size_t)pw;
                        const uint32_t *srow = src + (size_t)y * (size_t)pw;
                        for (x = xc0; x < xc1; x++)
                        {
                            drow[x] = apply_matex_pixel(srow[x], acc, entry->emissive);
                        }
                    }

                    if (x_cur1 >= pw)
                    {
                        break;
                    }
                    x_cur0 = x_cur1;
                    x_cur1 += part;
                }

                if (y_cur1 >= ph)
                {
                    break;
                }
                y_cur0 = y_cur1;
                y_cur1 += part;
            }
        }
    }

    lua_getfield(L, 1, "putpixeldata");
    lua_pushlightuserdata(L, dst);
    lua_call(L, 1, 0);

    call_obj_draw_args(L, 1, "draw", ds.args, ds.nargs);

    lua_getglobal(L, "obj");
    lua_getfield(L, -1, "getpixeldata");
    lua_call(L, 0, 3);
    {
        void *cur_ptr = lua_touserdata(L, -3);
        lua_pop(L, 3);
        if (cur_ptr)
        {
            memcpy(cur_ptr, src, entry->buf_bytes);
            lua_getfield(L, -1, "putpixeldata");
            lua_pushlightuserdata(L, cur_ptr);
            lua_call(L, 1, 0);
        }
    }
    lua_pop(L, 2);
    rm_free(dst);
    return 0;
}

static int build_matex_tex_mesh(
    RmDirectExtra *dex,
    const double *pos,
    const uint32_t *tex_buf,
    int tw,
    int th,
    int part)
{
    float hw_f = (float)tw * 0.5f;
    float hh_f = (float)th * 0.5f;
    double bx = (double)((float)pos[0] - ((float)dex->down[0] * hh_f + (float)dex->right[0] * hw_f));
    double by = (double)((float)pos[1] - ((float)dex->down[1] * hh_f + (float)dex->right[1] * hw_f));
    double bz = (double)((float)pos[2] - ((float)dex->down[2] * hh_f + (float)dex->right[2] * hw_f));

    int half_w = tw / 2;
    int q_w = half_w / part;
    int rem_w = half_w - q_w * part;
    int x_init0, x_init1, nx_cells;

    int half_h = th / 2;
    int q_h = half_h / part;
    int rem_h = half_h - q_h * part;
    int y_init0, y_init1, ny_cells;

    size_t total_cells, num_verts;
    RmTexQuad *quads;
    double *verts;
    uint32_t quad_cnt = 0;
    int grid_mode;

    if (rem_w == 0)
    {
        x_init0 = 0;
        x_init1 = part;
    }
    else
    {
        q_w += 1;
        x_init1 = rem_w;
        x_init0 = rem_w - part;
    }
    nx_cells = q_w * 2;
    if (half_w * 2 != tw)
    {
        int rh = (tw - half_w) / part;
        nx_cells = q_w + rh + ((part * rh != tw - half_w) ? 1 : 0);
    }
    if (nx_cells < 1)
    {
        nx_cells = 1;
    }

    if (rem_h == 0)
    {
        y_init0 = 0;
        y_init1 = part;
    }
    else
    {
        q_h += 1;
        y_init1 = rem_h;
        y_init0 = rem_h - part;
    }
    ny_cells = q_h * 2;
    if (half_h * 2 != th)
    {
        int rh = (th - half_h) / part;
        ny_cells = q_h + rh + ((part * rh != th - half_h) ? 1 : 0);
    }
    if (ny_cells < 1)
    {
        ny_cells = 1;
    }

    total_cells = (size_t)nx_cells * (size_t)ny_cells;
    num_verts = (size_t)(nx_cells + 1) * (size_t)(ny_cells + 1);
    quads = (RmTexQuad *)rm_calloc(total_cells, sizeof(RmTexQuad));
    verts = (double *)rm_malloc(num_verts * 3u * sizeof(double));
    if (!quads || !verts)
    {
        rm_free(quads);
        rm_free(verts);
        return 0;
    }

    grid_mode = ((nx_cells != 1) ? 1 : 0) + ((ny_cells != 1) ? 2 : 0);

    if (grid_mode == 0)
    {
        size_t total_px = (size_t)tw * (size_t)th;
        double sa = 0.0, sr = 0.0, sg = 0.0, sb = 0.0;
        double denom = (double)total_px * 255.0;
        double na;
        size_t p;

        for (p = 0; p < total_px; p++)
        {
            uint32_t px = tex_buf[p];
            sa += (double)(px >> 24);
            sr += (double)((px >> 16) & 0xffu);
            sg += (double)((px >> 8) & 0xffu);
            sb += (double)(px & 0xffu);
        }
        na = (denom > 0.0) ? (sa / denom) : 0.0;
        if (na > 0.0)
        {
            quads[0].v[0] = 0;
            quads[0].v[1] = 1;
            quads[0].v[2] = 3;
            quads[0].v[3] = 2;
            quads[0].edge_mask = 0;
            quads[0].col[0] = (sr / denom) * na;
            quads[0].col[1] = (sg / denom) * na;
            quads[0].col[2] = (sb / denom) * na;
            quad_cnt = 1;
        }
        verts[0] = bx;
        verts[1] = by;
        verts[2] = bz;
        verts[3] = bx + dex->right[0] * (double)tw;
        verts[4] = by + dex->right[1] * (double)tw;
        verts[5] = bz + dex->right[2] * (double)tw;
        verts[6] = bx + dex->down[0] * (double)th;
        verts[7] = by + dex->down[1] * (double)th;
        verts[8] = bz + dex->down[2] * (double)th;
        verts[9] = verts[3] + dex->down[0] * (double)th;
        verts[10] = verts[4] + dex->down[1] * (double)th;
        verts[11] = verts[5] + dex->down[2] * (double)th;
    }
    else if (grid_mode == 1)
    {
        int col = 0;
        int x0 = x_init0;
        int x1 = x_init1;
        int stride_v = nx_cells + 1;

        for (;;)
        {
            int xc0 = (x0 < 0) ? 0 : x0;
            int xc1 = (x1 > tw) ? tw : x1;
            double sa = 0.0, sr = 0.0, sg = 0.0, sb = 0.0;
            double denom = (double)(xc1 - xc0) * (double)th * 255.0;
            double na;
            double dx = (double)xc0;
            double tx, ty, tz;
            int x, y;

            for (y = 0; y < th; y++)
            {
                const uint32_t *row = tex_buf + (size_t)y * (size_t)tw;
                for (x = xc0; x < xc1; x++)
                {
                    uint32_t px = row[x];
                    sa += (double)(px >> 24);
                    sr += (double)((px >> 16) & 0xffu);
                    sg += (double)((px >> 8) & 0xffu);
                    sb += (double)(px & 0xffu);
                }
            }
            na = (denom > 0.0) ? (sa / denom) : 0.0;
            if (na != 0.0)
            {
                RmTexQuad *tq = &quads[quad_cnt++];
                tq->v[0] = col;
                tq->v[1] = col + 1;
                tq->v[2] = col + stride_v + 1;
                tq->v[3] = col + stride_v;
                tq->col[0] = (sr / denom) * na;
                tq->col[1] = (sg / denom) * na;
                tq->col[2] = (sb / denom) * na;
            }

            tx = bx + dex->right[0] * dx;
            ty = by + dex->right[1] * dx;
            tz = bz + dex->right[2] * dx;
            verts[col * 3] = tx;
            verts[col * 3 + 1] = ty;
            verts[col * 3 + 2] = tz;
            verts[(col + stride_v) * 3] = tx + dex->down[0] * (double)th;
            verts[(col + stride_v) * 3 + 1] = ty + dex->down[1] * (double)th;
            verts[(col + stride_v) * 3 + 2] = tz + dex->down[2] * (double)th;

            col++;
            if (x1 >= tw)
            {
                break;
            }
            x0 = x1;
            x1 += part;
        }

        {
            double rx = bx + dex->right[0] * (double)tw;
            double ry = by + dex->right[1] * (double)tw;
            double rz = bz + dex->right[2] * (double)tw;
            verts[col * 3] = rx;
            verts[col * 3 + 1] = ry;
            verts[col * 3 + 2] = rz;
            verts[(col + stride_v) * 3] = rx + dex->down[0] * (double)th;
            verts[(col + stride_v) * 3 + 1] = ry + dex->down[1] * (double)th;
            verts[(col + stride_v) * 3 + 2] = rz + dex->down[2] * (double)th;
        }

        if (quad_cnt > 0)
        {
            quads[0].edge_mask = 2;
            quads[quad_cnt - 1].edge_mask = 8;
        }
    }
    else if (grid_mode == 2)
    {
        int row_v = 0;
        int y0 = y_init0;
        int y1 = y_init1;

        for (;;)
        {
            int yc0 = (y0 < 0) ? 0 : y0;
            int yc1 = (y1 > th) ? th : y1;
            double sa = 0.0, sr = 0.0, sg = 0.0, sb = 0.0;
            double denom = (double)(yc1 - yc0) * (double)tw * 255.0;
            double na;
            double dy = (double)yc0;
            double lx, ly, lz;
            int x, y;

            for (y = yc0; y < yc1; y++)
            {
                const uint32_t *row = tex_buf + (size_t)y * (size_t)tw;
                for (x = 0; x < tw; x++)
                {
                    uint32_t px = row[x];
                    sa += (double)(px >> 24);
                    sr += (double)((px >> 16) & 0xffu);
                    sg += (double)((px >> 8) & 0xffu);
                    sb += (double)(px & 0xffu);
                }
            }
            na = (denom > 0.0) ? (sa / denom) : 0.0;
            if (na != 0.0)
            {
                RmTexQuad *tq = &quads[quad_cnt++];
                tq->v[0] = row_v;
                tq->v[1] = row_v + 1;
                tq->v[2] = row_v + 3;
                tq->v[3] = row_v + 2;
                tq->col[0] = (sr / denom) * na;
                tq->col[1] = (sg / denom) * na;
                tq->col[2] = (sb / denom) * na;
            }

            lx = bx + dex->down[0] * dy;
            ly = by + dex->down[1] * dy;
            lz = bz + dex->down[2] * dy;
            verts[row_v * 3] = lx;
            verts[row_v * 3 + 1] = ly;
            verts[row_v * 3 + 2] = lz;
            verts[(row_v + 1) * 3] = lx + dex->right[0] * (double)tw;
            verts[(row_v + 1) * 3 + 1] = ly + dex->right[1] * (double)tw;
            verts[(row_v + 1) * 3 + 2] = lz + dex->right[2] * (double)tw;

            row_v += 2;
            if (y1 >= th)
            {
                break;
            }
            y0 = y1;
            y1 += part;
        }

        {
            double lx = bx + dex->down[0] * (double)th;
            double ly = by + dex->down[1] * (double)th;
            double lz = bz + dex->down[2] * (double)th;
            verts[row_v * 3] = lx;
            verts[row_v * 3 + 1] = ly;
            verts[row_v * 3 + 2] = lz;
            verts[(row_v + 1) * 3] = lx + dex->right[0] * (double)tw;
            verts[(row_v + 1) * 3 + 1] = ly + dex->right[1] * (double)tw;
            verts[(row_v + 1) * 3 + 2] = lz + dex->right[2] * (double)tw;
        }
    }
    else
    {
        int stride_v = nx_cells + 1;
        int v_idx = 0;
        int y0 = y_init0;
        int y1 = y_init1;

        for (;;)
        {
            int yc0 = (y0 < 0) ? 0 : y0;
            int yc1 = (y1 > th) ? th : y1;
            double dy = (double)yc0;
            int x0 = x_init0;
            int x1 = x_init1;

            for (;;)
            {
                int xc0 = (x0 < 0) ? 0 : x0;
                int xc1 = (x1 > tw) ? tw : x1;
                double sa = 0.0, sr = 0.0, sg = 0.0, sb = 0.0;
                double denom = (double)(xc1 - xc0) * (double)(yc1 - yc0) * 255.0;
                double na;
                double dx = (double)xc0;
                int x, y;

                for (y = yc0; y < yc1; y++)
                {
                    const uint32_t *row = tex_buf + (size_t)y * (size_t)tw;
                    for (x = xc0; x < xc1; x++)
                    {
                        uint32_t px = row[x];
                        sa += (double)(px >> 24);
                        sr += (double)((px >> 16) & 0xffu);
                        sg += (double)((px >> 8) & 0xffu);
                        sb += (double)(px & 0xffu);
                    }
                }
                na = (denom > 0.0) ? (sa / denom) : 0.0;
                if (na != 0.0)
                {
                    RmTexQuad *tq = &quads[quad_cnt++];
                    tq->v[0] = v_idx;
                    tq->v[1] = v_idx + 1;
                    tq->v[2] = v_idx + 1 + stride_v;
                    tq->v[3] = v_idx + stride_v;
                    tq->col[0] = (sr / denom) * na;
                    tq->col[1] = (sg / denom) * na;
                    tq->col[2] = (sb / denom) * na;
                }

                verts[v_idx * 3] = bx + dex->right[0] * dx + dex->down[0] * dy;
                verts[v_idx * 3 + 1] = by + dex->right[1] * dx + dex->down[1] * dy;
                verts[v_idx * 3 + 2] = bz + dex->right[2] * dx + dex->down[2] * dy;

                v_idx++;
                if (x1 >= tw)
                {
                    break;
                }
                x0 = x1;
                x1 += part;
            }

            verts[v_idx * 3] = bx + dex->right[0] * (double)tw + dex->down[0] * dy;
            verts[v_idx * 3 + 1] = by + dex->right[1] * (double)tw + dex->down[1] * dy;
            verts[v_idx * 3 + 2] = bz + dex->right[2] * (double)tw + dex->down[2] * dy;
            v_idx++;

            if (y1 >= th)
            {
                break;
            }
            y0 = y1;
            y1 += part;
        }

        {
            int x0 = x_init0;
            int x1 = x_init1;
            for (;;)
            {
                int xc0 = (x0 < 0) ? 0 : x0;
                double dx = (double)xc0;
                verts[v_idx * 3] = bx + dex->right[0] * dx + dex->down[0] * (double)th;
                verts[v_idx * 3 + 1] = by + dex->right[1] * dx + dex->down[1] * (double)th;
                verts[v_idx * 3 + 2] = bz + dex->right[2] * dx + dex->down[2] * (double)th;
                v_idx++;
                if (x1 >= tw)
                {
                    break;
                }
                x0 = x1;
                x1 += part;
            }
            verts[v_idx * 3] = bx + dex->right[0] * (double)tw + dex->down[0] * (double)th;
            verts[v_idx * 3 + 1] = by + dex->right[1] * (double)tw + dex->down[1] * (double)th;
            verts[v_idx * 3 + 2] = bz + dex->right[2] * (double)tw + dex->down[2] * (double)th;
        }
    }

    dex->verts = verts;
    dex->num_quads = (int)quad_cnt;
    dex->quads = quads;
    return 1;
}

static void compute_direct_basis_from_layer(lua_State *L, int obj_idx, int layer, RmDirectExtra *dex)
{
    static const double k_ux[3] = { 1.0, 0.0, 0.0 };
    static const double k_uy[3] = { 0.0, 1.0, 0.0 };
    static const double k_uz[3] = { 0.0, 0.0, -1.0 };
    double rx = read_obj_layer_prop(L, obj_idx, layer, "rx") * RM_DEG2RAD;
    double ry = read_obj_layer_prop(L, obj_idx, layer, "ry") * RM_DEG2RAD;
    double rz = read_obj_layer_prop(L, obj_idx, layer, "rz") * RM_DEG2RAD;
    rm_euler_set_trig(rx, ry, rz);
    rm_euler_rotate_vec3(dex->right, k_ux);
    rm_euler_rotate_vec3(dex->down, k_uy);
    rm_euler_rotate_vec3(dex->dir, k_uz);
}

static void compute_direct_basis_from_option(lua_State *L, int opt_idx, RmDirectExtra *dex)
{
    static const double k_def_dir[3] = { 0.0, 0.0, -1.0 };
    static const double k_def_right[3] = { 1.0, 0.0, 0.0 };
    static const double k_def_down[3] = { 0.0, 1.0, 0.0 };

    lua_getfield(L, opt_idx, "nx");
    dex->dir[0] = lua_tonumber(L, -1);
    lua_getfield(L, opt_idx, "ny");
    dex->dir[1] = lua_tonumber(L, -1);
    lua_getfield(L, opt_idx, "nz");
    dex->dir[2] = lua_tonumber(L, -1);
    lua_getfield(L, opt_idx, "wx");
    dex->right[0] = lua_tonumber(L, -1);
    lua_getfield(L, opt_idx, "wy");
    dex->right[1] = lua_tonumber(L, -1);
    lua_getfield(L, opt_idx, "wz");
    dex->right[2] = lua_tonumber(L, -1);
    lua_pop(L, 6);

    rm_vec3_normalize_or_default(dex->dir, k_def_dir, 1.0);
    rm_vec3_normalize_or_default(dex->right, k_def_right, 1.0);
    vec3_cross(dex->right, dex->dir, dex->down);
    rm_vec3_normalize_or_default(dex->down, k_def_down, 1.0);
}

static uint32_t *load_direct_texture_bgra(
    lua_State *L,
    int obj_idx,
    const RmMatExEntry *entry,
    int is_image,
    const char *img_path,
    int tex_layer,
    int req_w,
    int req_h,
    int *out_tw,
    int *out_th)
{
    static const char *const k_save_fields[12] =
    {
        "ox", "oy", "oz", "cx", "cy", "cz",
        "rx", "ry", "rz", "zoom", "alpha", "aspect"
    };
    double saved[12];
    int k, tw, th;
    size_t count;
    uint32_t *tex_buf;
    const void *src_px;

    for (k = 0; k < 12; k++)
    {
        lua_getfield(L, obj_idx, k_save_fields[k]);
        saved[k] = lua_tonumber(L, -1);
        lua_pop(L, 1);
    }

    lua_getfield(L, obj_idx, "load");
    if (is_image)
    {
        lua_pushstring(L, "image");
        lua_pushstring(L, img_path);
        lua_call(L, 2, 0);
    }
    else
    {
        lua_pushstring(L, "layer");
        lua_pushinteger(L, tex_layer);
        lua_pushboolean(L, 1);
        lua_call(L, 3, 0);
    }

    lua_getfield(L, obj_idx, "getpixeldata");
    lua_call(L, 0, 3);
    if (req_w > 0 || req_h > 0)
    {
        int orig_w = (int)lua_tointeger(L, -2);
        int orig_h = (int)lua_tointeger(L, -1);
        int nargs_fx = 1;
        lua_pop(L, 3);

        lua_getfield(L, obj_idx, "effect");
        lua_pushstring(L, k_sjis_resize);
        if (req_w > 0 && orig_w > 0)
        {
            lua_pushstring(L, "X");
            lua_pushnumber(L, (double)(((float)req_w / (float)orig_w) * 100.0f));
            nargs_fx += 2;
        }
        if (req_h > 0 && orig_h > 0)
        {
            lua_pushstring(L, "Y");
            lua_pushnumber(L, (double)(((float)req_h / (float)orig_h) * 100.0f));
            nargs_fx += 2;
        }
        lua_call(L, nargs_fx, 0);

        lua_getfield(L, obj_idx, "getpixeldata");
        lua_call(L, 0, 3);
    }

    src_px = lua_touserdata(L, -3);
    tw = (int)lua_tointeger(L, -2);
    th = (int)lua_tointeger(L, -1);
    lua_pop(L, 3);

    count = (tw > 0 && th > 0) ? ((size_t)tw * (size_t)th) : 0;
    tex_buf = (count > 0 && src_px) ? (uint32_t *)rm_malloc(count * 4u) : NULL;
    if (tex_buf)
    {
        memcpy(tex_buf, src_px, count * 4u);
        if (!is_image)
        {
            double la = read_obj_layer_prop(L, obj_idx, tex_layer, "alpha");
            int a_scale = (int)ceil(la * 255.0);
            if (a_scale != 255)
            {
                size_t p;
                for (p = 0; p < count; p++)
                {
                    uint32_t px = tex_buf[p];
                    uint32_t na = (((px >> 24) * (uint32_t)clamp_int(a_scale, 0, 255)) / 255u) & 0xffu;
                    tex_buf[p] = (na << 24) | (px & 0x00ffffffu);
                }
            }
        }
    }

    rm_update_obj_size(L, entry->obj.pixel_w, entry->obj.pixel_h, obj_idx);
    for (k = 11; k >= 0; k--)
    {
        lua_pushnumber(L, saved[k]);
        lua_setfield(L, obj_idx, k_save_fields[k]);
    }
    lua_getfield(L, obj_idx, "putpixeldata");
    lua_pushlightuserdata(L, entry->obj.pixel_data);
    lua_call(L, 1, 0);

    *out_tw = tw;
    *out_th = th;
    return tex_buf;
}

int __cdecl lua_rikky_materialdrawEx(lua_State *L)
{
    static const char *const k_rgb_keys[3] = { "R", "G", "B" };
    const int *p_frame;
    uint32_t ret_id = 0;

    if (rm_check_aviutl_ver() && rm_check_exedit_ver())
    {
        p_frame = (const int *)RikkyGetMemoryData(0x19);
        if (p_frame && *p_frame != g_matex_frame)
        {
            free_matex_cache();
            g_matex_frame = *p_frame;
        }

        if (lua_type(L, 1) == LUA_TTABLE)
        {
            RmMatExEntry *new_arr = (RmMatExEntry *)rm_realloc(
                g_matex_entries,
                (size_t)(g_matex_count + 1u) * sizeof(RmMatExEntry));
            if (new_arr)
            {
                RmMatExEntry *entry;
                RmCamState *cam;
                void *orig_px;
                size_t buf_bytes;

                g_matex_entries = new_arr;
                entry = &g_matex_entries[g_matex_count];
                memset(entry, 0, sizeof(*entry));

                cam = query_cam_state(L);
                query_obj_state(L, &entry->obj);
                orig_px = entry->obj.pixel_data;
                buf_bytes = (size_t)entry->obj.pixel_w * (size_t)entry->obj.pixel_h * 4u;
                entry->buf_bytes = buf_bytes;
                entry->obj.pixel_data = (buf_bytes > 0 && orig_px) ? rm_malloc(buf_bytes) : NULL;

                if (!entry->obj.pixel_data)
                {
                    if (g_matex_count == 0)
                    {
                        rm_free(g_matex_entries);
                        g_matex_entries = NULL;
                    }
                    else
                    {
                        RmMatExEntry *shrunk = (RmMatExEntry *)rm_realloc(
                            g_matex_entries,
                            (size_t)g_matex_count * sizeof(RmMatExEntry));
                        if (shrunk)
                        {
                            g_matex_entries = shrunk;
                        }
                    }
                }
                else
                {
                    int cur_layer;
                    int c;

                    memcpy(entry->obj.pixel_data, orig_px, buf_bytes);

                    lua_getglobal(L, "obj");
                    lua_insert(L, 1);

                    lua_getfield(L, 2, "ambient");
                    if (lua_type(L, -1) == LUA_TTABLE)
                    {
                        int at = lua_gettop(L);
                        for (c = 0; c < 3; c++)
                        {
                            lua_getfield(L, at, k_rgb_keys[c]);
                            entry->ambient[c] = (lua_type(L, -1) == LUA_TNUMBER)
                                                    ? clamp_01(lua_tonumber(L, -1) / 255.0)
                                                    : 0.0;
                            lua_pop(L, 1);
                        }
                    }
                    lua_pop(L, 1);

                    lua_getfield(L, 2, "emissive");
                    if (lua_type(L, -1) == LUA_TTABLE)
                    {
                        int et = lua_gettop(L);
                        for (c = 0; c < 3; c++)
                        {
                            lua_getfield(L, et, k_rgb_keys[c]);
                            entry->emissive[c] = (lua_type(L, -1) == LUA_TNUMBER)
                                                     ? clamp_01(lua_tonumber(L, -1) * 0.01)
                                                     : 0.1;
                            lua_pop(L, 1);
                        }
                    }
                    else
                    {
                        entry->emissive[0] = 0.1;
                        entry->emissive[1] = 0.1;
                        entry->emissive[2] = 0.1;
                    }
                    lua_pop(L, 1);

                    lua_getfield(L, 2, "drawhq");
                    if (lua_type(L, -1) == LUA_TBOOLEAN)
                    {
                        uint8_t hq = (uint8_t)lua_toboolean(L, -1);
                        lua_getfield(L, 2, "drawhq_partition");
                        if (lua_type(L, -1) == LUA_TNUMBER)
                        {
                            int p = (int)lua_tointeger(L, -1);
                            entry->drawhq = hq;
                            entry->drawhq_part = (int16_t)((p > 0) ? p : 1);
                        }
                        lua_pop(L, 1);
                    }
                    lua_pop(L, 1);

                    lua_getfield(L, 2, "damping");
                    if (lua_type(L, -1) == LUA_TNUMBER)
                    {
                        double dmp = lua_tonumber(L, -1);
                        entry->damping_sq = (dmp >= 0.0) ? (dmp * dmp) : 0.0;
                    }
                    lua_pop(L, 1);

                    lua_getfield(L, 1, "layer");
                    cur_layer = (int)lua_tointeger(L, -1);
                    lua_pop(L, 1);

                    lua_getfield(L, 2, "light");
                    if (lua_type(L, -1) == LUA_TTABLE)
                    {
                        int lt_tbl = lua_gettop(L);
                        int n_lights = (int)lua_objlen(L, lt_tbl);
                        if (n_lights > 0)
                        {
                            entry->lights = (RmExLight *)rm_calloc((size_t)n_lights, sizeof(RmExLight));
                        }
                        if (entry->lights)
                        {
                            int li;
                            entry->num_lights = n_lights;
                            for (li = 0; li < n_lights; li++)
                            {
                                RmExLight *lt = &entry->lights[li];
                                int l_item;
                                int pos_kind = -1;
                                int pos_layer = 0;

                                lua_rawgeti(L, lt_tbl, li + 1);
                                if (lua_type(L, -1) != LUA_TTABLE)
                                {
                                    lt->type = -1;
                                    lua_pop(L, 1);
                                    continue;
                                }
                                l_item = lua_gettop(L);

                                lua_getfield(L, l_item, "color");
                                if (lua_type(L, -1) == LUA_TTABLE)
                                {
                                    int ct = lua_gettop(L);
                                    for (c = 0; c < 3; c++)
                                    {
                                        lua_getfield(L, ct, k_rgb_keys[c]);
                                        lt->col[c] = (lua_type(L, -1) == LUA_TNUMBER)
                                                         ? clamp_01(lua_tonumber(L, -1) / 255.0)
                                                         : 1.0;
                                        lua_pop(L, 1);
                                    }
                                }
                                else
                                {
                                    lt->col[0] = 1.0;
                                    lt->col[1] = 1.0;
                                    lt->col[2] = 1.0;
                                }
                                lua_pop(L, 1);

                                lua_getfield(L, l_item, "specular");
                                if (lua_type(L, -1) == LUA_TTABLE)
                                {
                                    static const double k_spec_defs[3] = { 2.0, 1.0, 1.0 };
                                    int st = lua_gettop(L);
                                    for (c = 0; c < 3; c++)
                                    {
                                        lua_getfield(L, st, k_rgb_keys[c]);
                                        if (lua_type(L, -1) == LUA_TNUMBER)
                                        {
                                            double v = lua_tonumber(L, -1);
                                            lt->spec[c] = (v >= 0.0) ? (v * 0.01) : 0.0;
                                        }
                                        else
                                        {
                                            lt->spec[c] = k_spec_defs[c];
                                        }
                                        lua_pop(L, 1);
                                    }
                                    lua_getfield(L, st, "shininess");
                                    if (lua_type(L, -1) == LUA_TNUMBER)
                                    {
                                        double v = lua_tonumber(L, -1) * 0.01;
                                        lt->shininess = (v >= 0.0) ? v : 0.0;
                                    }
                                    else
                                    {
                                        lt->shininess = 10.0;
                                    }
                                    lua_pop(L, 1);
                                }
                                else
                                {
                                    lt->spec[0] = 1.0;
                                    lt->spec[1] = 1.0;
                                    lt->spec[2] = 1.0;
                                    lt->shininess = 10.0;
                                }
                                lua_pop(L, 1);

                                lua_getfield(L, l_item, "position");
                                if (lua_type(L, -1) == LUA_TSTRING)
                                {
                                    const char *s = lua_tolstring(L, -1, NULL);
                                    if (s && strcmp(s, "shadow") == 0)
                                    {
                                        const int *p_sh = (const int *)RikkyGetMemoryData(0x7f);
                                        if (p_sh)
                                        {
                                            lt->pos[0] = (double)p_sh[1] * 0.1;
                                            lt->pos[1] = (double)p_sh[0] * 0.1;
                                            lt->pos[2] = (double)p_sh[2] * 0.1;
                                        }
                                        pos_kind = 1;
                                    }
                                    else if (s && strcmp(s, "camera") == 0)
                                    {
                                        lt->pos[0] = cam->cam_pos[0];
                                        lt->pos[1] = cam->cam_pos[1];
                                        lt->pos[2] = cam->cam_pos[2];
                                        pos_kind = 2;
                                    }
                                    else if (s && s[0] == '@')
                                    {
                                        int lv = atoi(s + 1) + cur_layer;
                                        if (lv >= 1 && lv <= 100 && lv != cur_layer)
                                        {
                                            pos_layer = lv;
                                            pos_kind = 0;
                                        }
                                    }
                                }
                                else if (lua_type(L, -1) == LUA_TNUMBER)
                                {
                                    int lv = (int)lua_tointeger(L, -1);
                                    double lx = 0.0;
                                    if (lv >= 1 && lv <= 100 && lv != cur_layer &&
                                        check_obj_layer_exists(L, 1, lv, &lx))
                                    {
                                        pos_layer = lv;
                                        lt->pos[0] = lx;
                                        lt->pos[1] = read_obj_layer_prop(L, 1, lv, "y");
                                        lt->pos[2] = read_obj_layer_prop(L, 1, lv, "z");
                                        pos_kind = 3;
                                    }
                                }
                                else if (lua_type(L, -1) == LUA_TTABLE)
                                {
                                    int pt = lua_gettop(L);
                                    lua_getfield(L, pt, "x");
                                    lt->pos[0] = (lua_type(L, -1) == LUA_TNUMBER) ? lua_tonumber(L, -1) : 0.0;
                                    lua_pop(L, 1);
                                    lua_getfield(L, pt, "y");
                                    lt->pos[1] = (lua_type(L, -1) == LUA_TNUMBER) ? lua_tonumber(L, -1) : 0.0;
                                    lua_pop(L, 1);
                                    lua_getfield(L, pt, "z");
                                    lt->pos[2] = (lua_type(L, -1) == LUA_TNUMBER) ? lua_tonumber(L, -1) : -1024.0;
                                    lua_pop(L, 1);
                                    pos_kind = 0;
                                }
                                lua_pop(L, 1);

                                if (pos_kind < 0)
                                {
                                    lt->type = -1;
                                    lua_pop(L, 1);
                                    continue;
                                }

                                lua_getfield(L, l_item, "option");
                                if (lua_type(L, -1) == LUA_TTABLE)
                                {
                                    int opt_idx = lua_gettop(L);
                                    lua_getfield(L, opt_idx, "type");
                                    if (lua_type(L, -1) == LUA_TSTRING)
                                    {
                                        const char *otype = lua_tolstring(L, -1, NULL);
                                        if (otype && strcmp(otype, "spotlight") == 0)
                                        {
                                            RmSpotExtra *sp = (RmSpotExtra *)rm_calloc(1, sizeof(RmSpotExtra));
                                            lua_pop(L, 1);
                                            if (!sp)
                                            {
                                                lt->type = -1;
                                            }
                                            else
                                            {
                                                static const double k_def_sp1[3] = { 0.0, 0.0, 1.0 };
                                                static const double k_def_sp2[3] = { 0.0, 0.0, -1.0 };
                                                static const double k_neg_z[3] = { 0.0, 0.0, -1.0 };

                                                if (pos_kind == 3)
                                                {
                                                    double rx = read_obj_layer_prop(L, 1, pos_layer, "rx") * RM_DEG2RAD;
                                                    double ry = read_obj_layer_prop(L, 1, pos_layer, "ry") * RM_DEG2RAD;
                                                    rm_euler_set_trig(rx, ry, 0.0);
                                                    rm_euler_rotate_vec3(sp->dir1, k_neg_z);
                                                }
                                                else
                                                {
                                                    lua_getfield(L, opt_idx, "nx");
                                                    sp->dir1[0] = (lua_type(L, -1) == LUA_TNUMBER) ? lua_tonumber(L, -1) : 0.0;
                                                    lua_pop(L, 1);
                                                    lua_getfield(L, opt_idx, "ny");
                                                    sp->dir1[1] = (lua_type(L, -1) == LUA_TNUMBER) ? lua_tonumber(L, -1) : 0.0;
                                                    lua_pop(L, 1);
                                                    lua_getfield(L, opt_idx, "nz");
                                                    sp->dir1[2] = (lua_type(L, -1) == LUA_TNUMBER) ? lua_tonumber(L, -1) : 1.0;
                                                    lua_pop(L, 1);
                                                    rm_vec3_normalize_or_default(sp->dir1, k_def_sp1, 1.0);
                                                }

                                                parse_cone_deg(
                                                    L,
                                                    opt_idx,
                                                    "degree",
                                                    0.7071067811865476,
                                                    0.2928932188134524,
                                                    &sp->cos1,
                                                    &sp->omc1);

                                                lua_getfield(L, opt_idx, "double");
                                                if (lua_type(L, -1) == LUA_TBOOLEAN)
                                                {
                                                    lua_pop(L, 1);
                                                    parse_cone_deg(
                                                        L,
                                                        opt_idx,
                                                        "degree2",
                                                        sp->cos1,
                                                        sp->omc1,
                                                        &sp->cos2,
                                                        &sp->omc2);
                                                    if (pos_kind == 3)
                                                    {
                                                        sp->dir2[0] = -sp->dir1[0];
                                                        sp->dir2[1] = -sp->dir1[1];
                                                        sp->dir2[2] = -sp->dir1[2];
                                                    }
                                                    else
                                                    {
                                                        lua_getfield(L, opt_idx, "nx2");
                                                        sp->dir2[0] = (lua_type(L, -1) == LUA_TNUMBER) ? lua_tonumber(L, -1) : -sp->dir1[0];
                                                        lua_pop(L, 1);
                                                        lua_getfield(L, opt_idx, "ny2");
                                                        sp->dir2[1] = (lua_type(L, -1) == LUA_TNUMBER) ? lua_tonumber(L, -1) : -sp->dir1[1];
                                                        lua_pop(L, 1);
                                                        lua_getfield(L, opt_idx, "nz2");
                                                        sp->dir2[2] = (lua_type(L, -1) == LUA_TNUMBER) ? lua_tonumber(L, -1) : -sp->dir1[2];
                                                        lua_pop(L, 1);
                                                        rm_vec3_normalize_or_default(sp->dir2, k_def_sp2, 1.0);
                                                    }
                                                    lt->type = 2;
                                                }
                                                else
                                                {
                                                    lua_pop(L, 1);
                                                    lt->type = 1;
                                                }
                                                lt->extra = sp;
                                            }
                                        }
                                        else if (otype && strcmp(otype, "directlight") == 0)
                                        {
                                            RmDirectExtra *dex = (RmDirectExtra *)rm_calloc(1, sizeof(RmDirectExtra));
                                            lua_pop(L, 1);
                                            if (!dex)
                                            {
                                                lt->type = -1;
                                            }
                                            else
                                            {
                                                int is_dbl = 0;
                                                int part = 2;
                                                int req_w = -1;
                                                int req_h = -1;
                                                int tex_mode = 0;
                                                int tex_layer = pos_layer;
                                                const char *img_path = NULL;

                                                parse_cone_deg(
                                                    L,
                                                    opt_idx,
                                                    "degree",
                                                    0.984807753012208,
                                                    0.01519224698779198,
                                                    &dex->cos_deg,
                                                    &dex->omc_deg);

                                                lua_getfield(L, opt_idx, "double");
                                                if (lua_type(L, -1) == LUA_TBOOLEAN)
                                                {
                                                    is_dbl = lua_toboolean(L, -1) ? 1 : 0;
                                                }
                                                lua_pop(L, 1);

                                                lua_getfield(L, opt_idx, "partition");
                                                if (lua_type(L, -1) == LUA_TNUMBER)
                                                {
                                                    int pv = (int)lua_tointeger(L, -1);
                                                    part = (pv > 0) ? pv : 1;
                                                }
                                                lua_pop(L, 1);

                                                lua_getfield(L, opt_idx, "width");
                                                if (lua_type(L, -1) == LUA_TNUMBER)
                                                {
                                                    int wv = (int)lua_tointeger(L, -1);
                                                    req_w = (wv < 0) ? 0 : wv;
                                                }
                                                lua_pop(L, 1);

                                                lua_getfield(L, opt_idx, "height");
                                                if (lua_type(L, -1) == LUA_TNUMBER)
                                                {
                                                    int hv = (int)lua_tointeger(L, -1);
                                                    req_h = (hv < 0) ? 0 : hv;
                                                }
                                                lua_pop(L, 1);

                                                lua_getfield(L, opt_idx, "texture");
                                                if (lua_type(L, -1) == LUA_TNUMBER)
                                                {
                                                    tex_layer = (int)lua_tointeger(L, -1);
                                                    tex_mode = 2;
                                                }
                                                else if (lua_type(L, -1) == LUA_TSTRING)
                                                {
                                                    const char *ts = lua_tolstring(L, -1, NULL);
                                                    if (ts && strcmp(ts, "color") == 0)
                                                    {
                                                        tex_mode = 1;
                                                    }
                                                    else if (ts && ts[0] == '*')
                                                    {
                                                        FILE *fp = fopen(ts + 1, "r");
                                                        if (fp)
                                                        {
                                                            fclose(fp);
                                                            img_path = ts + 1;
                                                            tex_mode = 3;
                                                        }
                                                    }
                                                    else if (ts && ts[0] == '@')
                                                    {
                                                        tex_layer = atoi(ts + 1) + cur_layer;
                                                        tex_mode = 2;
                                                    }
                                                }

                                                if (tex_mode == 1)
                                                {
                                                    int tw = (req_w < 0) ? 0 : req_w;
                                                    int th = (req_h < 0) ? 0 : req_h;
                                                    lua_pop(L, 1);
                                                    if (tw * th <= 0)
                                                    {
                                                        rm_free(dex);
                                                        lt->type = -1;
                                                    }
                                                    else
                                                    {
                                                        double *verts = (double *)rm_malloc(12u * sizeof(double));
                                                        if (!verts)
                                                        {
                                                            rm_free(dex);
                                                            lt->type = -1;
                                                        }
                                                        else
                                                        {
                                                            float hw_f = (float)tw * 0.5f;
                                                            float hh_f = (float)th * 0.5f;
                                                            float dx_f, dy_f, dz_f;
                                                            float lx_f, ly_f, lz_f;
                                                            float rx_f, ry_f, rz_f;

                                                            if (pos_kind == 3)
                                                            {
                                                                compute_direct_basis_from_layer(L, 1, pos_layer, dex);
                                                            }
                                                            else
                                                            {
                                                                compute_direct_basis_from_option(L, opt_idx, dex);
                                                            }

                                                            dx_f = (float)dex->down[0] * hh_f;
                                                            dy_f = (float)dex->down[1] * hh_f;
                                                            dz_f = (float)dex->down[2] * hh_f;
                                                            lx_f = (float)lt->pos[0] - (float)dex->right[0] * hw_f;
                                                            ly_f = (float)lt->pos[1] - (float)dex->right[1] * hw_f;
                                                            lz_f = (float)lt->pos[2] - (float)dex->right[2] * hw_f;
                                                            rx_f = (float)lt->pos[0] + (float)dex->right[0] * hw_f;
                                                            ry_f = (float)lt->pos[1] + (float)dex->right[1] * hw_f;
                                                            rz_f = (float)lt->pos[2] + (float)dex->right[2] * hw_f;

                                                            verts[0] = (double)(lx_f - dx_f);
                                                            verts[1] = (double)(ly_f - dy_f);
                                                            verts[2] = (double)(lz_f - dz_f);
                                                            verts[3] = (double)(rx_f - dx_f);
                                                            verts[4] = (double)(ry_f - dy_f);
                                                            verts[5] = (double)(rz_f - dz_f);
                                                            verts[6] = (double)(rx_f + dx_f);
                                                            verts[7] = (double)(ry_f + dy_f);
                                                            verts[8] = (double)(rz_f + dz_f);
                                                            verts[9] = (double)(lx_f + dx_f);
                                                            verts[10] = (double)(ly_f + dy_f);
                                                            verts[11] = (double)(lz_f + dz_f);

                                                            dex->verts = verts;
                                                            lt->extra = dex;
                                                            lt->type = (int8_t)(3 + is_dbl);
                                                        }
                                                    }
                                                }
                                                else if (tex_mode == 2 || tex_mode == 3)
                                                {
                                                    int ok = 1;
                                                    if (tex_mode == 2)
                                                    {
                                                        if (tex_layer < 1 || tex_layer > 100 || tex_layer == cur_layer)
                                                        {
                                                            ok = 0;
                                                        }
                                                        else if (pos_kind != 3 && !check_obj_layer_exists(L, 1, tex_layer, NULL))
                                                        {
                                                            ok = 0;
                                                        }
                                                    }

                                                    if (!ok)
                                                    {
                                                        lua_pop(L, 1);
                                                        rm_free(dex);
                                                        lt->type = -1;
                                                    }
                                                    else
                                                    {
                                                        int tw = 0, th = 0;
                                                        uint32_t *tex_buf = load_direct_texture_bgra(
                                                            L,
                                                            1,
                                                            entry,
                                                            tex_mode == 3,
                                                            img_path,
                                                            tex_layer,
                                                            req_w,
                                                            req_h,
                                                            &tw,
                                                            &th);
                                                        lua_pop(L, 1);

                                                        if (!tex_buf || tw <= 0 || th <= 0)
                                                        {
                                                            rm_free(tex_buf);
                                                            rm_free(dex);
                                                            lt->type = -1;
                                                        }
                                                        else
                                                        {
                                                            if (pos_kind == 3)
                                                            {
                                                                int basis_layer = (tex_mode == 2) ? tex_layer : pos_layer;
                                                                compute_direct_basis_from_layer(L, 1, basis_layer, dex);
                                                            }
                                                            else
                                                            {
                                                                compute_direct_basis_from_option(L, opt_idx, dex);
                                                            }

                                                            if (!build_matex_tex_mesh(dex, lt->pos, tex_buf, tw, th, part))
                                                            {
                                                                rm_free(tex_buf);
                                                                rm_free(dex);
                                                                lt->type = -1;
                                                            }
                                                            else
                                                            {
                                                                rm_free(tex_buf);
                                                                lt->extra = dex;
                                                                lt->type = (int8_t)(5 + is_dbl);
                                                            }
                                                        }
                                                    }
                                                }
                                                else
                                                {
                                                    lua_pop(L, 1);
                                                    rm_free(dex);
                                                    lt->type = -1;
                                                }
                                            }
                                        }
                                        else
                                        {
                                            lua_pop(L, 1);
                                            lt->type = 0;
                                        }
                                    }
                                    else
                                    {
                                        lua_pop(L, 1);
                                        lt->type = 0;
                                    }
                                }
                                else
                                {
                                    lt->type = 0;
                                }
                                lua_pop(L, 2);
                            }
                        }
                    }
                    lua_pop(L, 1);

                    g_matex_count++;
                    ret_id = g_matex_count;
                }
            }
        }
    }

    lua_settop(L, 0);
    lua_newtable(L);
    lua_pushinteger(L, (lua_Integer)ret_id);
    lua_setfield(L, -2, "id");
    lua_pushcclosure(L, matex_draw, 0);
    lua_setfield(L, -2, "draw");
    lua_pushcclosure(L, matex_drawpoly, 0);
    lua_setfield(L, -2, "drawpoly");
    return 1;
}
