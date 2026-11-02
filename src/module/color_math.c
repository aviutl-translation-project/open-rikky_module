#include <math.h>
#include <string.h>
#include "rikky_module_internal.h"

typedef BOOL (__cdecl *Rgb2YcFn)(PIXEL_YC *dst, const PIXEL *src, int w);
typedef BOOL (__cdecl *Yc2RgbFn)(PIXEL *dst, const PIXEL_YC *src, int w);

static int g_rot_mode = 0;
static double g_rot_cos = 0.0;
static double g_rot_sin = 0.0;
static double g_rot_cx = 0.0;
static double g_rot_cy = 0.0;
static double g_rot_cz = 0.0;
static double g_rot_mat[9];

static double clamp_double(double v, double lo, double hi)
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

static int clamp_byte_int(int v)
{
    if (v < 0)
    {
        return 0;
    }
    if (v > 255)
    {
        return 255;
    }
    return v;
}

static double lab_forward_f(double t)
{
    if (t <= 0.0088564516790356)
    {
        return t * 7.787037037037037 + 0.1379310344827586;
    }
    return cbrt(t);
}

static float lab_inverse_f(float t)
{
    if (t <= 0.20689656f)
    {
        return (t * 116.0f - 16.0f) * 0.0011070565f;
    }
    return t * t * t;
}

static void push_euler_from_triad(lua_State *L, const double *p0, const double *p1, const double *p2, double out_scale)
{
    double p0x = p0[0];
    double p0y = p0[1];
    double p0z = p0[2];
    double p1x = p1[0];
    double p1y = p1[1];
    double p1z = p1[2];
    double vx = (p1x - p0x) * 0.01;
    double vy = (p1y - p0y) * 0.01;
    double vz = (p1z - p0z) * 0.01;
    double ux = (p2[0] - p0x) * 0.01;
    double uy = (p2[1] - p0y) * 0.01;
    double uz = (p2[2] - p0z) * 0.01;
    double ny = uy * vz - uz * vy;
    double nx = uz * vx - vz * ux;
    double nz = ux * vy - vx * uy;
    double xz_len = sqrt(nz * nz + nx * nx);
    double cos_y_sq;
    double neg_cos_y;
    double r0y;
    double r1y;
    double dx;
    double rx_ang;
    double ry_ang;
    double rz_ang;

    if (xz_len == 0.0)
    {
        nz = 1.0;
        nx = 0.0;
    }
    else
    {
        nx /= xz_len;
        nz /= xz_len;
    }

    if (ny < -1.0)
    {
        ny = -1.0;
        cos_y_sq = 0.0;
    }
    else if (ny <= 1.0)
    {
        cos_y_sq = 1.0 - ny * ny;
    }
    else
    {
        ny = 1.0;
        cos_y_sq = 0.0;
    }

    neg_cos_y = -sqrt(cos_y_sq);
    r0y = p0y * nz - p0z * nx;
    p0z = p0z * nz + p0y * nx;
    r1y = p1y * nz - p1z * nx;
    p1z = p1z * nz + p1y * nx;
    p0x = neg_cos_y * p0x + p0z * ny;
    p1x = neg_cos_y * p1x + p1z * ny;
    dx = p1x - p0x;

    rx_ang = atan2(nx, nz);
    lua_pushnumber(L, -rx_ang * out_scale);
    ry_ang = atan2(ny, neg_cos_y);
    lua_pushnumber(L, -ry_ang * out_scale);
    rz_ang = atan2((r0y - r1y) * 0.01, dx * 0.01);
    lua_pushnumber(L, -rz_ang * out_scale);
}

int __cdecl lua_rikky_colorconvert(lua_State *L)
{
    char mode_buf[32];
    const char *mode = NULL;
    int t1;
    int nargs;

    if (!rm_check_aviutl_ver())
    {
        return 0;
    }

    t1 = lua_type(L, 1);
    if (t1 == LUA_TNUMBER)
    {
        switch ((int)lua_tointeger(L, 1))
        {
        case 1:
            mode = "rgb";
            break;
        case 2:
            mode = "yc";
            break;
        case 3:
            mode = "hsv";
            break;
        case 4:
            mode = "hsl";
            break;
        case 5:
            mode = "xyz";
            break;
        case 6:
            mode = "lab";
            break;
        case 7:
            mode = "cmy";
            break;
        case 8:
            mode = "cmyk";
            break;
        case 9:
            mode = "complementary";
            break;
        case 10:
            mode = "opposite";
            break;
        default:
            lua_settop(L, 0);
            return 0;
        }
    }
    else if (t1 == LUA_TSTRING)
    {
        const char *raw_mode = lua_tolstring(L, 1, NULL);
        if (!raw_mode)
        {
            lua_settop(L, 0);
            return 0;
        }
        strncpy(mode_buf, raw_mode, sizeof(mode_buf) - 1);
        mode_buf[sizeof(mode_buf) - 1] = '\0';
        rm_str_tolower(mode_buf);
        mode = mode_buf;
    }
    else
    {
        lua_settop(L, 0);
        return 0;
    }

    nargs = lua_gettop(L);
    if (nargs == 5)
    {
        double c, m, y, k, inv_k;
        uint32_t r, g, b;

        if (strcmp(mode, "cmyk") != 0)
        {
            lua_settop(L, 0);
            return 0;
        }

        c = clamp_double(lua_tonumber(L, 2) * 0.01, 0.0, 1.0);
        m = clamp_double(lua_tonumber(L, 3) * 0.01, 0.0, 1.0);
        y = clamp_double(lua_tonumber(L, 4) * 0.01, 0.0, 1.0);
        k = clamp_double(lua_tonumber(L, 5) * 0.01, 0.0, 1.0);
        lua_settop(L, 0);

        inv_k = 1.0 - k;
        r = (uint32_t)(int)((1.0 - (c * inv_k + k)) * 255.0);
        g = (uint32_t)(int)((1.0 - (m * inv_k + k)) * 255.0);
        b = (uint32_t)(int)((1.0 - (y * inv_k + k)) * 255.0);
        lua_pushinteger(L, (lua_Integer)((r << 16) | (g << 8) | b));
        return 1;
    }

    if (nargs == 2)
    {
        uint32_t col = (uint32_t)lua_tointeger(L, 2);
        uint32_t r = (col >> 16) & 0xffu;
        uint32_t g = (col >> 8) & 0xffu;
        uint32_t b = col & 0xffu;
        double dr, dg, db;

        lua_settop(L, 0);

        if (strcmp(mode, "rgb") == 0)
        {
            lua_pushinteger(L, (lua_Integer)r);
            lua_pushinteger(L, (lua_Integer)g);
            lua_pushinteger(L, (lua_Integer)b);
            return 3;
        }

        if (strcmp(mode, "yc") == 0 || strcmp(mode, "ycbcr") == 0)
        {
            PIXEL px = { (unsigned char)b, (unsigned char)g, (unsigned char)r };
            PIXEL_YC yc = { 0, 0, 0 };
            Rgb2YcFn rgb2yc = (Rgb2YcFn)RikkyGetMemoryData(0x15);
            if (rgb2yc)
            {
                rgb2yc(&yc, &px, 1);
            }
            lua_pushinteger(L, (lua_Integer)yc.y);
            lua_pushinteger(L, (lua_Integer)yc.cb);
            lua_pushinteger(L, (lua_Integer)yc.cr);
            return 3;
        }

        if (strcmp(mode, "hsv") == 0)
        {
            double h = 0.0;
            double s = 0.0;
            double v = 0.0;
            RGB2HSV((int)r, (int)g, (int)b, &h, &s, &v);
            lua_pushnumber(L, h);
            lua_pushnumber(L, s);
            lua_pushnumber(L, v);
            return 3;
        }

        if (strcmp(mode, "xyz") == 0)
        {
            dr = (double)((float)r / 255.0);
            dg = (double)((float)g / 255.0);
            db = (double)((float)b / 255.0);
            lua_pushnumber(L, (db * 0.180481 + dg * 0.357584 + dr * 0.412391) * 100.0);
            lua_pushnumber(L, (db * 0.072192 + dg * 0.715169 + dr * 0.212639) * 100.0);
            lua_pushnumber(L, (db * 0.950532 + dg * 0.119195 + dr * 0.019331) * 100.0);
            return 3;
        }

        if (strcmp(mode, "lab") == 0 || strcmp(mode, "l*a*b*") == 0)
        {
            double nr = (double)r / 255.0;
            double ng = (double)g / 255.0;
            double nb = (double)b / 255.0;
            double fx = lab_forward_f((nb * 0.180481 + ng * 0.357584 + nr * 0.412391) / 0.950456);
            double fy = lab_forward_f(nb * 0.072192 + ng * 0.715169 + nr * 0.212639);
            double fz = lab_forward_f((nb * 0.950532 + ng * 0.119195 + nr * 0.019331) / 1.089058);

            lua_pushnumber(L, fy * 116.0 - 16.0);
            lua_pushnumber(L, (fx - fy) * 500.0);
            lua_pushnumber(L, (fy - fz) * 200.0);
            return 3;
        }

        dr = (double)r;
        dg = (double)g;
        db = (double)b;

        if (strcmp(mode, "hsl") == 0)
        {
            double cmax = dr;
            double cmin;
            int min_rb_idx;
            double sum;

            if (cmax < dg)
            {
                cmax = dg;
            }
            if (cmax < db)
            {
                cmax = db;
            }
            if (dr < db)
            {
                min_rb_idx = 2;
                cmin = dr;
            }
            else
            {
                min_rb_idx = 1;
                cmin = db;
            }

            if (cmin <= dg)
            {
                if (cmin != cmax)
                {
                    double delta = cmax - cmin;
                    double h = (min_rb_idx == 2)
                        ? (((db - dg) * 60.0) / delta + 180.0)
                        : (((dg - dr) * 60.0) / delta + 60.0);
                    double denom;

                    lua_pushnumber(L, h);
                    sum = cmax + cmin;
                    denom = (sum * 0.5 >= 127.0) ? ((510.0 - cmax) - cmin) : sum;
                    lua_pushnumber(L, (delta / denom) * 100.0);
                    lua_pushnumber(L, (sum * 10.0) / 51.0);
                    return 3;
                }
            }
            else
            {
                cmin = dg;
                if (dg != cmax)
                {
                    double delta = cmax - dg;
                    double denom;

                    lua_pushnumber(L, ((dr - db) * 60.0) / delta + 300.0);
                    sum = cmax + cmin;
                    denom = (sum * 0.5 >= 127.0) ? ((510.0 - cmax) - cmin) : sum;
                    lua_pushnumber(L, (delta / denom) * 100.0);
                    lua_pushnumber(L, (sum * 10.0) / 51.0);
                    return 3;
                }
            }

            lua_pushnumber(L, 0.0);
            lua_pushnumber(L, 0.0);
            sum = cmax + cmin;
            lua_pushnumber(L, (sum * 10.0) / 51.0);
            return 3;
        }

        if (strcmp(mode, "cmyk") == 0)
        {
            double max_gb = (db < dg) ? dg : db;
            double inv_r = 255.0 - dr;
            double k = 255.0 - max_gb;

            if (inv_r < k)
            {
                k = inv_r;
            }
            if (k == 255.0)
            {
                lua_pushnumber(L, 0.0);
                lua_pushnumber(L, 0.0);
                lua_pushnumber(L, 0.0);
            }
            else
            {
                double denom = 255.0 - k;
                lua_pushnumber(L, ((inv_r - k) / denom) * 100.0);
                lua_pushnumber(L, (((255.0 - dg) - k) / denom) * 100.0);
                lua_pushnumber(L, (((255.0 - db) - k) / denom) * 100.0);
            }
            lua_pushnumber(L, (k / 255.0) * 100.0);
            return 4;
        }

        if (strcmp(mode, "cmy") == 0)
        {
            lua_pushnumber(L, ((255.0 - dr) / 255.0) * 100.0);
            lua_pushnumber(L, ((255.0 - dg) / 255.0) * 100.0);
            lua_pushnumber(L, ((255.0 - db) / 255.0) * 100.0);
            return 3;
        }

        if (strcmp(mode, "complementary") == 0)
        {
            uint32_t umax = (r < g) ? g : r;
            uint32_t umin = (b <= g) ? b : g;
            uint32_t sum;

            if (umax < b)
            {
                umax = b;
            }
            if (r < umin)
            {
                umin = r;
            }
            sum = umax + umin;
            lua_pushinteger(L, (lua_Integer)(((sum - r) << 16) | ((sum - g) << 8) | (sum - b)));
            return 1;
        }

        if (strcmp(mode, "opposite") == 0)
        {
            lua_pushinteger(L, (lua_Integer)(((255u - r) << 16) | ((255u - g) << 8) | (255u - b)));
            return 1;
        }

        return 0;
    }

    if (nargs == 4)
    {
        uint32_t r = 0;
        uint32_t g = 0;
        uint32_t b = 0;

        if (strcmp(mode, "rgb") == 0)
        {
            int ri = (int)lua_tointeger(L, 2);
            int gi = (int)lua_tointeger(L, 3);
            int bi = (int)lua_tointeger(L, 4);
            lua_settop(L, 0);
            r = (uint32_t)clamp_byte_int(ri);
            g = (uint32_t)clamp_byte_int(gi);
            b = (uint32_t)clamp_byte_int(bi);
            lua_pushinteger(L, (lua_Integer)((r << 16) | (g << 8) | b));
            return 1;
        }

        if (strcmp(mode, "yc") == 0 || strcmp(mode, "ycbcr") == 0)
        {
            PIXEL_YC yc;
            PIXEL px = { 0, 0, 0 };
            Yc2RgbFn yc2rgb = (Yc2RgbFn)RikkyGetMemoryData(0x16);

            yc.y = (short)lua_tointeger(L, 2);
            yc.cb = (short)lua_tointeger(L, 3);
            yc.cr = (short)lua_tointeger(L, 4);
            if (yc2rgb)
            {
                yc2rgb(&px, &yc, 1);
            }
            lua_settop(L, 0);
            r = (uint32_t)px.r;
            g = (uint32_t)px.g;
            b = (uint32_t)px.b;
            lua_pushinteger(L, (lua_Integer)((r << 16) | (g << 8) | b));
            return 1;
        }

        if (strcmp(mode, "hsv") == 0)
        {
            double h = lua_tonumber(L, 2);
            double s = lua_tonumber(L, 3);
            double v = lua_tonumber(L, 4);
            int ri = 0;
            int gi = 0;
            int bi = 0;

            lua_settop(L, 0);
            HSV2RGB(h, s, v, &ri, &gi, &bi);
            r = (uint32_t)ri;
            g = (uint32_t)gi;
            b = (uint32_t)bi;
            lua_pushinteger(L, (lua_Integer)((r << 16) | (g << 8) | b));
            return 1;
        }

        if (strcmp(mode, "xyz") == 0)
        {
            double x = clamp_double(lua_tonumber(L, 2), 0.0, 95.0);
            double y = clamp_double(lua_tonumber(L, 3), 0.0, 100.0);
            double z = clamp_double(lua_tonumber(L, 4), 0.0, 109.0);

            lua_settop(L, 0);
            r = (uint32_t)(int)(((x * 3.24097 - y * 1.537383) - z * 0.498611) * 2.55);
            g = (uint32_t)(int)((z * 0.041555 + y * 1.875968 + x * -0.969244) * 2.55);
            b = (uint32_t)(int)((z * 1.056972 + (x * 0.05563 - y * 0.203977)) * 2.55);
            lua_pushinteger(L, (lua_Integer)((r << 16) | (g << 8) | b));
            return 1;
        }

        if (strcmp(mode, "lab") == 0 || strcmp(mode, "l*a*b*") == 0)
        {
            double l_val = clamp_double(lua_tonumber(L, 2), 0.0, 100.0);
            double a_val = clamp_double(lua_tonumber(L, 3), -86.0, 98.0);
            double b_val = clamp_double(lua_tonumber(L, 4), -108.0, 94.0);
            float fy, fx, fz;
            int ri, gi, bi;

            lua_settop(L, 0);
            fy = ((float)l_val + 16.0f) / 116.0f;
            fx = (float)a_val / 500.0f + fy;
            fz = fy - (float)b_val / 200.0f;

            fy = lab_inverse_f(fy);
            fx = lab_inverse_f(fx) * 0.950456f;
            fz = lab_inverse_f(fz) * 1.089058f;

            ri = (int)(((fx * 3.24097 - fy * 1.537383) - fz * 0.498611) * 255.0);
            gi = (int)((fz * 0.041555 + fy * 1.875968 + fx * -0.969244) * 255.0);
            bi = (int)((fz * 1.056972 + (fx * 0.05563 - fy * 0.203977)) * 255.0);
            r = (uint32_t)clamp_byte_int(ri);
            g = (uint32_t)clamp_byte_int(gi);
            b = (uint32_t)clamp_byte_int(bi);
            lua_pushinteger(L, (lua_Integer)((r << 16) | (g << 8) | b));
            return 1;
        }

        if (strcmp(mode, "hsl") == 0)
        {
            double h = lua_tonumber(L, 2);
            double s, l_val, vmax, vmin, t;

            if (h < 0.0 || h >= 360.0)
            {
                h -= floor(h / 360.0) * 360.0;
            }
            s = clamp_double(lua_tonumber(L, 3), 0.0, 100.0);
            l_val = clamp_double(lua_tonumber(L, 4), 0.0, 100.0);
            lua_settop(L, 0);

            if (l_val < 49.0)
            {
                t = s / 100.0;
                vmax = (t + 1.0) * l_val * 2.55;
                vmin = (1.0 - t) * l_val * 2.55;
            }
            else
            {
                t = (s / 100.0) * (100.0 - l_val);
                vmax = (l_val + t) * 2.55;
                vmin = (l_val - t) * 2.55;
            }

            switch ((int)(h / 60.0))
            {
            case 0:
                r = (uint32_t)(int)vmax;
                g = (uint32_t)(int)((vmax - vmin) * (h / 60.0) + vmin);
                b = (uint32_t)(int)vmin;
                break;
            case 1:
                r = (uint32_t)(int)((vmax - vmin) * ((120.0 - h) / 60.0) + vmin);
                g = (uint32_t)(int)vmax;
                b = (uint32_t)(int)vmin;
                break;
            case 2:
                r = (uint32_t)(int)vmin;
                g = (uint32_t)(int)vmax;
                b = (uint32_t)(int)((vmax - vmin) * ((h - 120.0) / 60.0) + vmin);
                break;
            case 3:
                r = (uint32_t)(int)vmin;
                g = (uint32_t)(int)((vmax - vmin) * ((240.0 - h) / 60.0) + vmin);
                b = (uint32_t)(int)vmax;
                break;
            case 4:
                r = (uint32_t)(int)((vmax - vmin) * ((h - 240.0) / 60.0) + vmin);
                g = (uint32_t)(int)vmin;
                b = (uint32_t)(int)vmax;
                break;
            default:
                r = (uint32_t)(int)vmax;
                g = (uint32_t)(int)vmin;
                b = (uint32_t)(int)((vmax - vmin) * ((360.0 - h) / 60.0) + vmin);
                break;
            }

            lua_pushinteger(L, (lua_Integer)((r << 16) | (g << 8) | b));
            return 1;
        }

        if (strcmp(mode, "cmy") == 0)
        {
            double c = clamp_double(lua_tonumber(L, 2), 0.0, 100.0);
            double m = clamp_double(lua_tonumber(L, 3), 0.0, 100.0);
            double y = clamp_double(lua_tonumber(L, 4), 0.0, 100.0);

            lua_settop(L, 0);
            r = (uint32_t)(int)((100.0 - c) * 2.55);
            g = (uint32_t)(int)((100.0 - m) * 2.55);
            b = (uint32_t)(int)((100.0 - y) * 2.55);
            lua_pushinteger(L, (lua_Integer)((r << 16) | (g << 8) | b));
            return 1;
        }
    }

    lua_settop(L, 0);
    return 0;
}

int __cdecl lua_rikky_rotation(lua_State *L)
{
    double c = 0.0;
    double s = 0.0;
    double cx, cy, cz;
    double mat[9] = { 0.0 };
    int mode;
    double x, y, z;

    if (lua_gettop(L) == 3)
    {
        mode = g_rot_mode;
        if (mode == 4)
        {
            memcpy(mat, g_rot_mat, sizeof(mat));
        }
        else
        {
            c = g_rot_cos;
            s = g_rot_sin;
        }
        cx = g_rot_cx;
        cy = g_rot_cy;
        cz = g_rot_cz;
    }
    else
    {
        double angle = lua_tonumber(L, 4);
        int t5;

        c = cos(angle);
        s = sin(angle);
        g_rot_cos = c;
        g_rot_sin = s;

        t5 = lua_type(L, 5);
        if (t5 == LUA_TSTRING)
        {
            const char *s5 = lua_tolstring(L, 5, NULL);
            if (s5 && s5[0] == 'x')
            {
                mode = 1;
            }
            else if (s5 && s5[0] == 'y')
            {
                mode = 2;
            }
            else
            {
                mode = 3;
            }
        }
        else if (t5 == LUA_TTABLE)
        {
            double ax, ay, az, len_sq;

            lua_rawgeti(L, 5, 1);
            ax = lua_tonumber(L, -1);
            lua_rawgeti(L, 5, 2);
            ay = lua_tonumber(L, -1);
            lua_rawgeti(L, 5, 3);
            az = lua_tonumber(L, -1);
            lua_pop(L, 3);

            len_sq = az * az + ay * ay + ax * ax;
            mode = 3;
            if (len_sq != 0.0)
            {
                double len = sqrt(len_sq);
                double omc = 1.0 - c;
                double xy, xz, yz;

                ax /= len;
                ay /= len;
                az /= len;
                mode = 4;

                xy = ax * ay * omc;
                xz = az * ax * omc;
                yz = ay * az * omc;

                g_rot_mat[0] = ax * ax * omc + c;
                g_rot_mat[1] = xy - s * az;
                g_rot_mat[2] = xz + s * ay;
                g_rot_mat[3] = xy + s * az;
                g_rot_mat[4] = ay * ay * omc + c;
                g_rot_mat[5] = yz - ax * s;
                g_rot_mat[6] = xz - s * ay;
                g_rot_mat[7] = ax * s + yz;
                g_rot_mat[8] = az * az * omc + c;
                memcpy(mat, g_rot_mat, sizeof(mat));
            }
        }
        else
        {
            mode = 3;
        }
        g_rot_mode = mode;

        if (lua_type(L, 6) == LUA_TTABLE)
        {
            lua_rawgeti(L, 6, 1);
            cx = lua_tonumber(L, -1);
            lua_rawgeti(L, 6, 2);
            cy = lua_tonumber(L, -1);
            lua_rawgeti(L, 6, 3);
            cz = lua_tonumber(L, -1);
            lua_pop(L, 3);
        }
        else
        {
            cx = 0.0;
            cy = 0.0;
            cz = 0.0;
        }
        g_rot_cx = 0.0;
        g_rot_cy = 0.0;
        g_rot_cz = 0.0;
    }

    x = lua_tonumber(L, 1) - cx;
    y = lua_tonumber(L, 2) - cy;
    z = lua_tonumber(L, 3) - cz;
    lua_settop(L, 0);

    if (mode == 1)
    {
        double tmp = s * z;
        z = s * y + c * z;
        y = c * y - tmp;
    }
    else if (mode == 2)
    {
        double tmp = s * x;
        x = s * z + c * x;
        z = c * z - tmp;
    }
    else if (mode == 3)
    {
        double tmp = s * y;
        y = s * x + c * y;
        x = c * x - tmp;
    }
    else if (mode == 4)
    {
        double nx = mat[0] * x + mat[1] * y + mat[2] * z;
        double ny = mat[3] * x + mat[4] * y + mat[5] * z;
        double nz = mat[6] * x + mat[7] * y + mat[8] * z;
        x = nx;
        y = ny;
        z = nz;
    }

    lua_pushnumber(L, x + cx);
    lua_pushnumber(L, y + cy);
    lua_pushnumber(L, z + cz);
    return 3;
}

int __cdecl lua_rikky_axisconvert(lua_State *L)
{
    static const double k_def_x[3] = { 1.0, 0.0, 0.0 };
    static const double k_def_y[3] = { 0.0, 1.0, 0.0 };
    static const double k_def_z[3] = { 0.0, 0.0, 1.0 };

    int rad_mode;
    double in_scale;
    double out_scale;
    double x_axis[3];
    double y_axis[3];
    double z_axis[3];
    double rx, ry, rz;
    float yx50, yy50, yz50;
    double p0[3], p1[3], p2[3];
    double tmp[3];

    lua_type(L, 1);
    rad_mode = (int)lua_tointeger(L, 2);
    in_scale = (rad_mode == 1) ? 1.0 : 0.017453292519943295;

    lua_getfield(L, 1, "Xx");
    x_axis[0] = lua_tonumber(L, -1);
    lua_getfield(L, 1, "Xy");
    x_axis[1] = lua_tonumber(L, -1);
    lua_getfield(L, 1, "Xz");
    x_axis[2] = lua_tonumber(L, -1);
    rm_vec3_normalize_or_default(x_axis, k_def_x, 1.0);

    lua_getfield(L, 1, "rx");
    rx = lua_tonumber(L, -1);

    lua_getfield(L, 1, "Yx");
    y_axis[0] = lua_tonumber(L, -1);
    lua_getfield(L, 1, "Yy");
    y_axis[1] = lua_tonumber(L, -1);
    lua_getfield(L, 1, "Yz");
    y_axis[2] = lua_tonumber(L, -1);
    rm_vec3_normalize_or_default(y_axis, k_def_y, 1.0);

    lua_getfield(L, 1, "ry");
    ry = lua_tonumber(L, -1);

    lua_getfield(L, 1, "Zx");
    z_axis[0] = lua_tonumber(L, -1);
    lua_getfield(L, 1, "Zy");
    z_axis[1] = lua_tonumber(L, -1);
    lua_getfield(L, 1, "Zz");
    z_axis[2] = lua_tonumber(L, -1);
    rm_vec3_normalize_or_default(z_axis, k_def_z, 1.0);

    lua_getfield(L, 1, "rz");
    rz = lua_tonumber(L, -1);

    lua_settop(L, 0);

    yx50 = (float)y_axis[0] * 50.0f;
    yy50 = (float)y_axis[1] * 50.0f;
    yz50 = (float)y_axis[2] * 50.0f;

    p0[0] = (double)((float)x_axis[0] * -50.0f - yx50);
    p0[1] = (double)((float)x_axis[1] * -50.0f - yy50);
    p0[2] = (double)((float)x_axis[2] * -50.0f - yz50);

    p1[0] = (double)((float)x_axis[0] * 50.0f - yx50);
    p1[1] = (double)((float)x_axis[1] * 50.0f - yy50);
    p1[2] = (double)((float)x_axis[2] * 50.0f - yz50);

    p2[0] = (double)(yx50 + (float)x_axis[0] * -50.0f);
    p2[1] = (double)(yy50 + (float)x_axis[1] * -50.0f);
    p2[2] = (double)(yz50 + (float)x_axis[2] * -50.0f);

    rm_axis_angle_rotate_vec3(tmp, p0, z_axis, rz * in_scale);
    memcpy(p0, tmp, sizeof(p0));
    rm_axis_angle_rotate_vec3(tmp, p1, NULL, 0.0);
    memcpy(p1, tmp, sizeof(p1));
    rm_axis_angle_rotate_vec3(tmp, p2, NULL, 0.0);
    memcpy(p2, tmp, sizeof(p2));

    rm_axis_angle_rotate_vec3(tmp, p0, y_axis, ry * in_scale);
    memcpy(p0, tmp, sizeof(p0));
    rm_axis_angle_rotate_vec3(tmp, p1, NULL, 0.0);
    memcpy(p1, tmp, sizeof(p1));
    rm_axis_angle_rotate_vec3(tmp, p2, NULL, 0.0);
    memcpy(p2, tmp, sizeof(p2));

    rm_axis_angle_rotate_vec3(tmp, p0, x_axis, rx * in_scale);
    memcpy(p0, tmp, sizeof(p0));
    rm_axis_angle_rotate_vec3(tmp, p1, NULL, 0.0);
    memcpy(p1, tmp, sizeof(p1));
    rm_axis_angle_rotate_vec3(tmp, p2, NULL, 0.0);
    memcpy(p2, tmp, sizeof(p2));

    out_scale = (rad_mode == 1) ? 1.0 : 57.29577951308232;
    push_euler_from_triad(L, p0, p1, p2, out_scale);
    return 3;
}

int __cdecl lua_rikky_axisconvertEx(lua_State *L)
{
    static const double k_def_x[3] = { 1.0, 0.0, 0.0 };
    static const double k_def_z[3] = { 0.0, 0.0, 1.0 };

    int rad_mode;
    int chain_mode;
    int n;
    double in_scale;
    double out_scale;
    double x_axis[3];
    double y_axis[3];
    double p0[3], p1[3], p2[3];
    double yx50, yy50, yz50;

    if (lua_type(L, 1) != LUA_TTABLE)
    {
        lua_settop(L, 0);
        lua_pushnumber(L, 0.0);
        lua_pushnumber(L, 0.0);
        lua_pushnumber(L, 0.0);
        return 3;
    }

    rad_mode = (int)lua_tointeger(L, 2);
    chain_mode = (int)lua_tointeger(L, 3);
    n = (int)lua_objlen(L, 1);

    lua_getfield(L, 1, "Xx");
    x_axis[0] = lua_tonumber(L, -1);
    lua_getfield(L, 1, "Xy");
    x_axis[1] = lua_tonumber(L, -1);
    lua_getfield(L, 1, "Xz");
    x_axis[2] = lua_tonumber(L, -1);
    rm_vec3_normalize_or_default(x_axis, k_def_x, 1.0);

    lua_getfield(L, 1, "Yx");
    y_axis[0] = lua_tonumber(L, -1);
    lua_getfield(L, 1, "Yy");
    y_axis[1] = lua_tonumber(L, -1);
    lua_getfield(L, 1, "Yz");
    y_axis[2] = lua_tonumber(L, -1);
    rm_vec3_normalize_or_default(y_axis, k_def_x, 1.0);

    if (x_axis[2] * y_axis[2] + x_axis[1] * y_axis[1] + x_axis[0] * y_axis[0] != 0.0)
    {
        double z_axis[3];
        z_axis[0] = x_axis[1] * y_axis[2] - x_axis[2] * y_axis[1];
        z_axis[1] = x_axis[2] * y_axis[0] - y_axis[2] * x_axis[0];
        z_axis[2] = x_axis[0] * y_axis[1] - y_axis[0] * x_axis[1];
        rm_vec3_normalize_or_default(z_axis, k_def_z, 1.0);
        y_axis[0] = x_axis[2] * z_axis[1] - z_axis[2] * x_axis[1];
        y_axis[1] = z_axis[2] * x_axis[0] - x_axis[2] * z_axis[0];
        y_axis[2] = z_axis[0] * x_axis[1] - z_axis[1] * x_axis[0];
    }

    yx50 = y_axis[0] * 50.0;
    yy50 = y_axis[1] * 50.0;
    yz50 = y_axis[2] * 50.0;

    p0[0] = x_axis[0] * -50.0 - yx50;
    p0[1] = x_axis[1] * -50.0 - yy50;
    p0[2] = x_axis[2] * -50.0 - yz50;

    p1[0] = x_axis[0] * 50.0 - yx50;
    p1[1] = x_axis[1] * 50.0 - yy50;
    p1[2] = x_axis[2] * 50.0 - yz50;

    p2[0] = yx50 + x_axis[0] * -50.0;
    p2[1] = yy50 + x_axis[1] * -50.0;
    p2[2] = yz50 + x_axis[2] * -50.0;

    in_scale = (rad_mode == 1) ? 1.0 : 0.017453292519943295;

    if (n > 0)
    {
        int next_base = 5;
        for (;;)
        {
            double axis[3];
            double tmp[3];
            double ang;

            lua_rawgeti(L, 1, next_base - 4);
            axis[0] = lua_tonumber(L, -1);
            lua_rawgeti(L, 1, next_base - 3);
            axis[1] = lua_tonumber(L, -1);
            lua_rawgeti(L, 1, next_base - 2);
            axis[2] = lua_tonumber(L, -1);
            rm_vec3_normalize_or_default(axis, k_def_z, 1.0);

            lua_rawgeti(L, 1, next_base - 1);
            ang = lua_tonumber(L, -1);
            lua_pop(L, 4);

            rm_axis_angle_rotate_vec3(tmp, p0, axis, ang * in_scale);
            memcpy(p0, tmp, sizeof(p0));
            rm_axis_angle_rotate_vec3(tmp, p1, NULL, 0.0);
            memcpy(p1, tmp, sizeof(p1));
            rm_axis_angle_rotate_vec3(tmp, p2, NULL, 0.0);
            memcpy(p2, tmp, sizeof(p2));

            if (chain_mode == 1)
            {
                int j;
                if (n < next_base)
                {
                    break;
                }
                for (j = next_base; j <= n; j += 4)
                {
                    double sub_axis[3];
                    lua_rawgeti(L, 1, j);
                    sub_axis[0] = lua_tonumber(L, -1);
                    lua_rawgeti(L, 1, j + 1);
                    sub_axis[1] = lua_tonumber(L, -1);
                    lua_rawgeti(L, 1, j + 2);
                    sub_axis[2] = lua_tonumber(L, -1);
                    lua_pop(L, 3);

                    rm_axis_angle_rotate_vec3(tmp, sub_axis, NULL, 0.0);
                    lua_pushnumber(L, tmp[0]);
                    lua_rawseti(L, 1, j);
                    lua_pushnumber(L, tmp[1]);
                    lua_rawseti(L, 1, j + 1);
                    lua_pushnumber(L, tmp[2]);
                    lua_rawseti(L, 1, j + 2);
                }
            }
            else if (n < next_base)
            {
                break;
            }

            next_base += 4;
        }
    }

    lua_settop(L, 0);
    out_scale = (rad_mode == 1) ? 1.0 : 57.29577951308232;
    push_euler_from_triad(L, p0, p1, p2, out_scale);
    return 3;
}
