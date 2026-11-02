#include <windows.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rikky_module_internal.h"

static uint8_t g_datasave_registered = 0;
static uint8_t *g_clip_img_buf = NULL;

static const char *const k_lua_keywords[] =
{
    "and", "break", "do", "else", "elseif",
    "end", "false", "for", "function", "if",
    "in", "local", "nil", "not", "or",
    "repeat", "return", "then", "true", "until",
    "while"
};

static int is_lua_keyword(const char *s)
{
    size_t i;
    if (!s)
    {
        return 0;
    }
    for (i = 0; i < sizeof(k_lua_keywords) / sizeof(k_lua_keywords[0]); i++)
    {
        if (strcmp(s, k_lua_keywords[i]) == 0)
        {
            return 1;
        }
    }
    return 0;
}

static int is_valid_lua_ident(const char *s)
{
    size_t i;
    size_t len;
    unsigned char c0;

    if (!s || s[0] == '\0' || is_lua_keyword(s))
    {
        return 0;
    }
    c0 = (unsigned char)tolower((unsigned char)s[0]);
    if ((unsigned int)(c0 - 'a') >= 26u && s[0] != '_')
    {
        return 0;
    }
    len = strlen(s);
    for (i = 1; i < len; i++)
    {
        unsigned char ch = (unsigned char)s[i];
        if (!isalnum(ch) && ch != '_')
        {
            return 0;
        }
    }
    return 1;
}

static char *dup_lua_str(lua_State *L, int idx, size_t *out_len)
{
    size_t len = 0;
    const char *s = lua_tolstring(L, idx, &len);
    char *copy;

    if (!s)
    {
        return NULL;
    }
    copy = (char *)rm_malloc(len + 1u);
    if (!copy)
    {
        return NULL;
    }
    memcpy(copy, s, len);
    copy[len] = '\0';
    if (out_len)
    {
        *out_len = len;
    }
    return copy;
}

// datasave .aup binary serialization and callbacks

static int is_datasave_val_type(int t)
{
    return t == LUA_TBOOLEAN || t == LUA_TNUMBER || t == LUA_TSTRING || t == LUA_TTABLE;
}

static void buf_write_bytes(uint8_t *buf, size_t *off, const void *src, size_t n)
{
    if (buf && n > 0)
    {
        memcpy(buf + *off, src, n);
    }
    *off += n;
}

static void buf_write_u8(uint8_t *buf, size_t *off, uint8_t v)
{
    if (buf)
    {
        buf[*off] = v;
    }
    *off += 1u;
}

static void buf_write_i32(uint8_t *buf, size_t *off, int32_t v)
{
    buf_write_bytes(buf, off, &v, sizeof(v));
}

static void buf_write_f64(uint8_t *buf, size_t *off, double v)
{
    buf_write_bytes(buf, off, &v, sizeof(v));
}

static void buf_write_cstr(uint8_t *buf, size_t *off, const char *s)
{
    size_t n = s ? (strlen(s) + 1u) : 1u;
    buf_write_bytes(buf, off, s ? s : "", n);
}

static void datasave_write_table(lua_State *L, uint8_t *buf, size_t *off);

static void datasave_write_value(lua_State *L, uint8_t *buf, size_t *off)
{
    int vt = lua_type(L, -1);

    if (vt == LUA_TNUMBER)
    {
        double d = lua_tonumber(L, -1);
        buf_write_u8(buf, off, 1);
        buf_write_f64(buf, off, d);
    }
    else if (vt == LUA_TSTRING)
    {
        const char *s = lua_tolstring(L, -1, NULL);
        buf_write_u8(buf, off, 2);
        buf_write_cstr(buf, off, s);
    }
    else if (vt == LUA_TBOOLEAN)
    {
        uint8_t b = (lua_toboolean(L, -1) != 0) ? 1u : 0u;
        buf_write_u8(buf, off, 3);
        buf_write_u8(buf, off, b);
    }
    else if (vt == LUA_TTABLE)
    {
        buf_write_u8(buf, off, 4);
        datasave_write_table(L, buf, off);
    }
}

static void datasave_write_table(lua_State *L, uint8_t *buf, size_t *off)
{
    lua_pushnil(L);
    while (lua_next(L, -2) != 0)
    {
        int vt = lua_type(L, -1);
        if (is_datasave_val_type(vt))
        {
            if (lua_type(L, -2) == LUA_TSTRING)
            {
                const char *ks = lua_tolstring(L, -2, NULL);
                buf_write_u8(buf, off, 3);
                buf_write_cstr(buf, off, ks);
            }
            else
            {
                double kd = lua_tonumber(L, -2);
                if (!isnan(kd) && floor(kd) == ceil(kd))
                {
                    int32_t ki = (int32_t)lua_tointeger(L, -2);
                    buf_write_u8(buf, off, 1);
                    buf_write_i32(buf, off, ki);
                }
                else
                {
                    buf_write_u8(buf, off, 2);
                    buf_write_f64(buf, off, kd);
                }
            }
            datasave_write_value(L, buf, off);
        }
        lua_pop(L, 1);
    }
    buf_write_u8(buf, off, 0);
}

static int __cdecl datasave_save_cb(void *buf, int *inout_size, void *ctx3)
{
    lua_State *L = ctx3 ? *(lua_State **)ctx3 : NULL;
    uint8_t *out = (uint8_t *)buf;
    size_t off = 4u;
    int32_t count = 0;

    if (!L || !inout_size)
    {
        return 0;
    }

    lua_getfield(L, LUA_GLOBALSINDEX, "Rikky_datasave_Table");
    if (lua_type(L, -1) == LUA_TTABLE)
    {
        int total = (int)lua_objlen(L, -1);
        int i;

        for (i = 1; i <= total; i++)
        {
            lua_rawgeti(L, -1, i);
            if (lua_type(L, -1) == LUA_TSTRING)
            {
                const char *name = lua_tolstring(L, -1, NULL);
                if (name)
                {
                    lua_getfield(L, LUA_GLOBALSINDEX, name);
                    if (is_datasave_val_type(lua_type(L, -1)))
                    {
                        count++;
                    }
                    lua_pop(L, 1);
                }
            }
            lua_pop(L, 1);
        }

        if (out)
        {
            memcpy(out, &count, sizeof(count));
        }

        for (i = 1; i <= total; i++)
        {
            lua_rawgeti(L, -1, i);
            if (lua_type(L, -1) == LUA_TSTRING)
            {
                const char *name = lua_tolstring(L, -1, NULL);
                if (name)
                {
                    lua_getfield(L, LUA_GLOBALSINDEX, name);
                    if (is_datasave_val_type(lua_type(L, -1)))
                    {
                        buf_write_cstr(out, &off, name);
                        datasave_write_value(L, out, &off);
                    }
                    lua_pop(L, 1);
                }
            }
            lua_pop(L, 1);
        }
    }
    else if (out)
    {
        memcpy(out, &count, sizeof(count));
    }

    lua_pop(L, 1);
    *inout_size = (int)off;
    return 1;
}

static int buf_read_cstr(const uint8_t **pp, const uint8_t *end, const char **out_s)
{
    const uint8_t *p = *pp;
    const uint8_t *nul;

    if (p >= end)
    {
        return 0;
    }
    nul = (const uint8_t *)memchr(p, 0, (size_t)(end - p));
    if (!nul)
    {
        return 0;
    }
    *out_s = (const char *)p;
    *pp = nul + 1;
    return 1;
}

static int datasave_read_table(lua_State *L, const uint8_t **pp, const uint8_t *end);

static int datasave_read_value(lua_State *L, const uint8_t **pp, const uint8_t *end)
{
    uint8_t tag;

    if (*pp >= end)
    {
        return 0;
    }
    tag = *(*pp)++;

    if (tag == 1)
    {
        double d = 0.0;
        if ((size_t)(end - *pp) < sizeof(d))
        {
            return 0;
        }
        memcpy(&d, *pp, sizeof(d));
        *pp += sizeof(d);
        lua_pushnumber(L, d);
        return 1;
    }
    if (tag == 2)
    {
        const char *s = NULL;
        if (!buf_read_cstr(pp, end, &s))
        {
            return 0;
        }
        lua_pushstring(L, s);
        return 1;
    }
    if (tag == 3)
    {
        uint8_t b;
        if (*pp >= end)
        {
            return 0;
        }
        b = *(*pp)++;
        lua_pushboolean(L, b == 1u ? 1 : 0);
        return 1;
    }
    if (tag == 4)
    {
        return datasave_read_table(L, pp, end);
    }
    return 0;
}

static int datasave_read_table(lua_State *L, const uint8_t **pp, const uint8_t *end)
{
    lua_createtable(L, 0, 0);
    while (*pp < end)
    {
        uint8_t ktag = *(*pp)++;
        if (ktag == 0)
        {
            return 1;
        }
        if (ktag == 1)
        {
            int32_t ki = 0;
            if ((size_t)(end - *pp) < sizeof(ki))
            {
                return 0;
            }
            memcpy(&ki, *pp, sizeof(ki));
            *pp += sizeof(ki);
            lua_pushinteger(L, (lua_Integer)ki);
        }
        else if (ktag == 2)
        {
            double kd = 0.0;
            if ((size_t)(end - *pp) < sizeof(kd))
            {
                return 0;
            }
            memcpy(&kd, *pp, sizeof(kd));
            *pp += sizeof(kd);
            lua_pushnumber(L, kd);
        }
        else if (ktag == 3)
        {
            const char *ks = NULL;
            if (!buf_read_cstr(pp, end, &ks))
            {
                return 0;
            }
            lua_pushstring(L, ks);
        }
        else
        {
            return 0;
        }

        if (!datasave_read_value(L, pp, end))
        {
            lua_pop(L, 1);
            return 0;
        }
        lua_settable(L, -3);
    }
    return 1;
}

static void __cdecl datasave_load_cb(void *data, uint32_t size, void *ctx3)
{
    lua_State *L = ctx3 ? *(lua_State **)ctx3 : NULL;

    if (!L)
    {
        return;
    }

    lua_createtable(L, 0, 0);
    if (data && size >= 4u)
    {
        const uint8_t *p = (const uint8_t *)data;
        const uint8_t *end = p + size;
        int32_t count = 0;
        int32_t i;

        memcpy(&count, p, sizeof(count));
        p += sizeof(count);

        for (i = 1; i <= count && p < end; i++)
        {
            const char *name = NULL;
            if (!buf_read_cstr(&p, end, &name))
            {
                break;
            }
            if (!datasave_read_value(L, &p, end))
            {
                break;
            }
            lua_setfield(L, LUA_GLOBALSINDEX, name);
            lua_pushstring(L, name);
            lua_rawseti(L, -2, (int)i);
        }
    }
    lua_setfield(L, LUA_GLOBALSINDEX, "Rikky_datasave_Table");
}

static void __cdecl datasave_reset_cb(void *ctx3)
{
    lua_State *L = ctx3 ? *(lua_State **)ctx3 : NULL;

    if (L)
    {
        lua_pushnil(L);
        lua_setfield(L, LUA_GLOBALSINDEX, "Rikky_datasave_Table");
        lua_pushnil(L);
        lua_setfield(L, LUA_GLOBALSINDEX, "param_Rikky_custom_dialog");
    }
    g_datasave_registered = 0;
}

void rm_ensure_datasave_registered(lua_State *exedit_L)
{
    if (!g_datasave_registered && exedit_L)
    {
        uint32_t user_val = (uint32_t)(uintptr_t)exedit_L;
        g_datasave_registered = 1;
        RikkySaveRegister("Rikky_datasave_Table", datasave_save_cb, user_val);
        RikkyLoadRegister("Rikky_datasave_Table", datasave_load_cb, user_val, datasave_reset_cb, user_val);
    }
}

int __cdecl lua_rikky_datasave(lua_State *L)
{
    char *name;
    lua_State **pp_exL;
    lua_State *exL;

    if (lua_type(L, 1) != LUA_TSTRING)
    {
        lua_settop(L, 0);
        lua_pushboolean(L, 0);
        return 1;
    }

    name = dup_lua_str(L, 1, NULL);
    if (!name || name[0] == '\0' || is_lua_keyword(name))
    {
        rm_free(name);
        lua_settop(L, 0);
        lua_pushboolean(L, 0);
        return 1;
    }

    pp_exL = (lua_State **)RikkyGetMemoryData(0x96);
    exL = (pp_exL && *pp_exL) ? *pp_exL : L;
    rm_ensure_datasave_registered(exL);

    lua_getfield(exL, LUA_GLOBALSINDEX, "Rikky_datasave_Table");
    if (lua_type(exL, -1) != LUA_TTABLE)
    {
        lua_pop(exL, 1);
        lua_createtable(exL, 0, 0);
        lua_pushstring(exL, name);
        lua_rawseti(exL, -2, 1);
        lua_setfield(exL, LUA_GLOBALSINDEX, "Rikky_datasave_Table");
    }
    else
    {
        int len = (int)lua_objlen(exL, -1);
        int found = 0;
        int i;

        for (i = 1; i <= len; i++)
        {
            const char *cur;
            lua_rawgeti(exL, -1, i);
            cur = lua_tolstring(exL, -1, NULL);
            if (cur && strcmp(cur, name) == 0)
            {
                found = 1;
                lua_pop(exL, 1);
                break;
            }
            lua_pop(exL, 1);
        }
        if (!found)
        {
            lua_pushstring(exL, name);
            lua_rawseti(exL, -2, len + 1);
        }
        lua_pop(exL, 1);
    }

    rm_free(name);
    lua_settop(L, 0);
    lua_pushboolean(L, 1);
    return 1;
}

// deep table copy and lua_rikky_assign

static void deep_copy_table(lua_State *L)
{
    lua_createtable(L, 0, 0);
    lua_pushnil(L);
    while (lua_next(L, -3) != 0)
    {
        lua_pushvalue(L, -2);
        lua_insert(L, -2);
        if (lua_type(L, -1) == LUA_TTABLE)
        {
            deep_copy_table(L);
            lua_remove(L, -2);
        }
        lua_settable(L, -4);
    }
}

int __cdecl lua_rikky_assign(lua_State *L)
{
    char *mode_str;
    char *name_str;
    int mode = 0;

    if (lua_type(L, 1) != LUA_TSTRING || lua_type(L, 2) != LUA_TSTRING)
    {
        lua_settop(L, 0);
        lua_pushboolean(L, 0);
        return 1;
    }

    mode_str = dup_lua_str(L, 1, NULL);
    name_str = dup_lua_str(L, 2, NULL);
    if (!mode_str || !name_str)
    {
        rm_free(mode_str);
        rm_free(name_str);
        lua_settop(L, 0);
        lua_pushboolean(L, 0);
        return 1;
    }

    rm_str_tolower(mode_str);
    if (strcmp(mode_str, "make") == 0)
    {
        mode = 1;
    }
    else if (strcmp(mode_str, "copy") == 0)
    {
        mode = 2;
    }
    rm_free(mode_str);

    if (mode == 0 || !is_valid_lua_ident(name_str))
    {
        rm_free(name_str);
        lua_settop(L, 0);
        lua_pushboolean(L, 0);
        return 1;
    }

    if (mode == 1)
    {
        lua_settop(L, 3);
        if (lua_type(L, 3) == LUA_TTABLE)
        {
            deep_copy_table(L);
        }
        lua_setfield(L, LUA_GLOBALSINDEX, name_str);
        rm_free(name_str);
        lua_settop(L, 0);
        lua_pushboolean(L, 1);
        return 1;
    }

    lua_settop(L, 0);
    lua_getfield(L, LUA_GLOBALSINDEX, name_str);
    rm_free(name_str);
    if (lua_type(L, -1) == LUA_TTABLE)
    {
        deep_copy_table(L);
        lua_remove(L, -2);
    }
    return 1;
}

// encoding / hex conversion for lua_rikky_convert

static char *wide_to_utf8(int *out_len, const wchar_t *ws)
{
    int wlen;
    int need;
    char *u8;

    if (!ws)
    {
        return NULL;
    }
    wlen = (int)wcslen(ws);
    need = WideCharToMultiByte(CP_UTF8, 0, ws, wlen + 1, NULL, 0, NULL, NULL);
    if (need <= 0)
    {
        return NULL;
    }
    u8 = (char *)rm_malloc((size_t)need);
    if (!u8)
    {
        return NULL;
    }
    if (WideCharToMultiByte(CP_UTF8, 0, ws, wlen + 1, u8, need, NULL, NULL) <= 0)
    {
        rm_free(u8);
        return NULL;
    }
    if (out_len)
    {
        *out_len = (int)strlen(u8);
    }
    return u8;
}

static wchar_t *utf8_to_wide(int *out_len, const char *u8)
{
    int slen;
    int need;
    wchar_t *ws;

    if (!u8)
    {
        return NULL;
    }
    slen = (int)strlen(u8);
    need = MultiByteToWideChar(CP_UTF8, 0, u8, slen + 1, NULL, 0);
    if (need <= 0)
    {
        return NULL;
    }
    ws = (wchar_t *)rm_malloc((size_t)need * sizeof(wchar_t));
    if (!ws)
    {
        return NULL;
    }
    if (MultiByteToWideChar(CP_UTF8, 0, u8, slen + 1, ws, need) <= 0)
    {
        rm_free(ws);
        return NULL;
    }
    if (out_len)
    {
        *out_len = (int)wcslen(ws);
    }
    return ws;
}

static int parse_hex_str(const char *s)
{
    size_t len;
    size_t i;
    unsigned int acc = 0;
    unsigned int shift = 0;

    if (!s || s[0] == '\0')
    {
        return 0;
    }
    len = strlen(s);
    for (i = len; i > 0; i--)
    {
        unsigned char c = (unsigned char)s[i - 1];
        unsigned int digit;
        if (c >= '0' && c <= '9')
        {
            digit = (unsigned int)(c - '0');
        }
        else if (c >= 'A' && c <= 'F')
        {
            digit = (unsigned int)(c - 'A' + 10);
        }
        else if (c >= 'a' && c <= 'f')
        {
            digit = (unsigned int)(c - 'a' + 10);
        }
        else
        {
            return 0;
        }
        acc += (digit << shift);
        shift += 4u;
    }
    return (int)acc;
}

int __cdecl lua_rikky_convert(lua_State *L)
{
    int t1 = lua_type(L, 1);
    int to_str = 0;
    char *in_str = NULL;
    int enc = 0;
    int hex_mode = 0;
    int little = 0;
    const char *hex_fmt = "";

    if (t1 == LUA_TSTRING)
    {
        in_str = dup_lua_str(L, 1, NULL);
        if (!in_str)
        {
            return 0;
        }
        to_str = 0;
    }
    else if (t1 == LUA_TTABLE)
    {
        to_str = 1;
    }
    else
    {
        return 0;
    }

    if (lua_isstring(L, 2))
    {
        char *s2 = dup_lua_str(L, 2, NULL);
        if (s2)
        {
            rm_str_tolower(s2);
            if (strcmp(s2, "unicode") == 0 || strcmp(s2, "uni") == 0)
            {
                enc = 1;
            }
            else if (strcmp(s2, "utf8") == 0 || strcmp(s2, "u8") == 0)
            {
                enc = 2;
            }
            rm_free(s2);
        }
    }

    if (lua_isstring(L, 3))
    {
        char *s3 = dup_lua_str(L, 3, NULL);
        if (s3)
        {
            if (strcmp(s3, "HEX") == 0 || strcmp(s3, "H") == 0)
            {
                hex_mode = 2;
            }
            else
            {
                rm_str_tolower(s3);
                if (strcmp(s3, "hex") == 0 || strcmp(s3, "h") == 0)
                {
                    hex_mode = 1;
                }
            }
            rm_free(s3);
        }
        if (hex_mode != 0)
        {
            hex_fmt = (enc == 1) ? "%04X" : "%02X";
        }
    }

    if (lua_isstring(L, 4))
    {
        char *s4 = dup_lua_str(L, 4, NULL);
        if (s4)
        {
            rm_str_tolower(s4);
            if (strcmp(s4, "little") == 0 || strcmp(s4, "l") == 0)
            {
                little = 1;
            }
            rm_free(s4);
        }
    }

    if (to_str)
    {
        int n = (int)lua_objlen(L, 1);
        uint32_t *vals = (uint32_t *)rm_calloc(n > 0 ? (size_t)n : 1u, sizeof(uint32_t));
        char *out_str = NULL;
        int i;

        if (!vals)
        {
            return 0;
        }
        for (i = 0; i < n; i++)
        {
            int et;
            lua_rawgeti(L, 1, i + 1);
            et = lua_type(L, -1);
            if (et == LUA_TNUMBER)
            {
                vals[i] = (uint32_t)lua_tointeger(L, -1);
            }
            else if (et == LUA_TSTRING)
            {
                const char *es = lua_tolstring(L, -1, NULL);
                vals[i] = (uint32_t)(hex_mode == 0 ? atoi(es ? es : "") : parse_hex_str(es));
            }
            lua_pop(L, 1);
        }

        lua_settop(L, 0);
        if (enc == 1)
        {
            wchar_t *ws = (wchar_t *)rm_calloc((size_t)n + 1u, sizeof(wchar_t));
            int dummy = 0;
            if (!ws)
            {
                rm_free(vals);
                return 0;
            }
            for (i = 0; i < n; i++)
            {
                uint16_t w = (uint16_t)vals[i];
                if (little)
                {
                    w = (uint16_t)((w << 8) | (w >> 8));
                }
                ws[i] = (wchar_t)w;
            }
            out_str = rm_wide_to_sjis_html(&dummy, ws);
            rm_free(ws);
        }
        else if (enc == 2)
        {
            char *u8 = (char *)rm_calloc((size_t)n + 1u, 1u);
            wchar_t *ws;
            int dummy = 0;
            if (!u8)
            {
                rm_free(vals);
                return 0;
            }
            for (i = 0; i < n; i++)
            {
                u8[i] = (char)vals[i];
            }
            ws = utf8_to_wide(&dummy, u8);
            rm_free(u8);
            if (!ws)
            {
                rm_free(vals);
                return 0;
            }
            out_str = rm_wide_to_sjis_html(&dummy, ws);
            rm_free(ws);
        }
        else
        {
            out_str = (char *)rm_calloc((size_t)n + 1u, 1u);
            if (!out_str)
            {
                rm_free(vals);
                return 0;
            }
            for (i = 0; i < n; i++)
            {
                out_str[i] = (char)vals[i];
            }
        }

        rm_free(vals);
        if (!out_str)
        {
            return 0;
        }
        lua_pushstring(L, out_str);
        rm_free(out_str);
        return 1;
    }
    else
    {
        uint16_t *codes = NULL;
        int count = 0;
        int i;

        lua_settop(L, 0);
        lua_createtable(L, 0, 0);

        if (enc == 1)
        {
            codes = (uint16_t *)rm_sjis_html_to_wide(&count, in_str);
            rm_free(in_str);
            if (!codes)
            {
                lua_pushinteger(L, 0);
                return 2;
            }
            if (little)
            {
                for (i = 0; i < count; i++)
                {
                    codes[i] = (uint16_t)((codes[i] << 8) | (codes[i] >> 8));
                }
            }
        }
        else if (enc == 2)
        {
            int wlen = 0;
            wchar_t *ws = rm_sjis_html_to_wide(&wlen, in_str);
            char *u8;
            rm_free(in_str);
            if (!ws)
            {
                lua_pushinteger(L, 0);
                return 2;
            }
            u8 = wide_to_utf8(&count, ws);
            rm_free(ws);
            if (!u8)
            {
                lua_pushinteger(L, 0);
                return 2;
            }
            codes = (uint16_t *)rm_calloc((size_t)count + 1u, sizeof(uint16_t));
            if (!codes)
            {
                rm_free(u8);
                lua_pushinteger(L, 0);
                return 2;
            }
            for (i = 0; i < count; i++)
            {
                codes[i] = (uint16_t)(uint8_t)u8[i];
            }
            rm_free(u8);
        }
        else
        {
            count = (int)strlen(in_str);
            codes = (uint16_t *)rm_calloc((size_t)count + 1u, sizeof(uint16_t));
            if (!codes)
            {
                rm_free(in_str);
                lua_pushinteger(L, 0);
                return 2;
            }
            for (i = 0; i < count; i++)
            {
                codes[i] = (uint16_t)(uint8_t)in_str[i];
            }
            rm_free(in_str);
        }

        if (hex_mode == 0)
        {
            for (i = 0; i < count; i++)
            {
                lua_pushinteger(L, (lua_Integer)codes[i]);
                lua_rawseti(L, -2, i + 1);
            }
        }
        else
        {
            char hbuf[24];
            for (i = 0; i < count; i++)
            {
                sprintf(hbuf, hex_fmt, (unsigned int)codes[i]);
                lua_pushstring(L, hbuf);
                lua_rawseti(L, -2, i + 1);
            }
        }

        rm_free(codes);
        lua_pushinteger(L, count);
        return 2;
    }
}

// exedit rich-text tokenizer shared by textsplit and textload

static void free_token_list(char **tokens, size_t count)
{
    size_t i;
    if (!tokens)
    {
        return;
    }
    for (i = 0; i < count; i++)
    {
        rm_free(tokens[i]);
    }
    rm_free(tokens);
}

static size_t match_exedit_tag(const char *s, size_t slen, size_t pos, uint8_t exver,
                               const char *const *custom_tags, int custom_count)
{
    unsigned char b1;
    size_t j;
    int k;

    if (pos + 2u > slen)
    {
        return 1u;
    }
    b1 = (unsigned char)s[pos + 1u];

    if (b1 == 'p')
    {
        int commas = 0;
        int has_dot = 0;
        int at_field_start = 1;

        for (j = pos + 2u; j < slen; j++)
        {
            unsigned char ch = (unsigned char)s[j];
            if (ch == '>')
            {
                if (commas >= 1)
                {
                    return (j - pos) + 1u;
                }
                break;
            }
            if (ch == ',')
            {
                commas++;
                if (commas >= 3)
                {
                    break;
                }
                has_dot = 0;
                at_field_start = 1;
            }
            else if (ch == '.')
            {
                if (has_dot)
                {
                    break;
                }
                has_dot = 1;
                at_field_start = 0;
            }
            else if ((ch == '+' || ch == '-') && exver >= 0x34u)
            {
                if (!at_field_start)
                {
                    break;
                }
                at_field_start = 0;
            }
            else if (ch >= '0' && ch <= '9')
            {
                at_field_start = 0;
            }
            else
            {
                break;
            }
        }
    }
    else if (b1 == '?')
    {
        const char *end = strstr(s + pos + 2u, "?>");
        if (end)
        {
            return (size_t)((end + 2) - (s + pos));
        }
    }
    else if (b1 == 'c' || b1 == 'w')
    {
        int has_dot = 0;
        for (j = pos + 2u; j < slen; j++)
        {
            unsigned char ch = (unsigned char)s[j];
            if (ch == '>')
            {
                return (j - pos) + 1u;
            }
            if (ch == '*' && j == pos + 2u)
            {
                continue;
            }
            if (ch == '.')
            {
                if (has_dot)
                {
                    break;
                }
                has_dot = 1;
            }
            else if (ch < '0' || ch > '9')
            {
                break;
            }
        }
    }
    else if (b1 == '#')
    {
        int commas = 0;
        int hex_digits = 0;
        for (j = pos + 2u; j < slen; j++)
        {
            unsigned char ch = (unsigned char)s[j];
            if (ch == '>')
            {
                if (hex_digits == 6 || (hex_digits == 0 && commas == 0))
                {
                    return (j - pos) + 1u;
                }
                break;
            }
            if (ch == ',')
            {
                if (hex_digits != 6 || commas >= 1)
                {
                    break;
                }
                commas++;
                hex_digits = 0;
            }
            else if ((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'))
            {
                hex_digits++;
            }
            else
            {
                break;
            }
        }
    }
    else if (b1 == 's')
    {
        int commas = 0;
        int max_commas = (exver >= 0x1cu) ? 2 : 1;
        for (j = pos + 2u; j < slen; j++)
        {
            unsigned char ch = (unsigned char)s[j];
            if (ch == '>')
            {
                return (j - pos) + 1u;
            }
            if (ch == ',')
            {
                commas++;
                if (commas > max_commas)
                {
                    break;
                }
            }
            else if (ch >= '0' && ch <= '9')
            {
                if (commas == 2)
                {
                    break;
                }
            }
            else if (ch == 'B' || ch == 'I')
            {
                if (commas == 0)
                {
                    break;
                }
            }
            else
            {
                if (commas != 1)
                {
                    break;
                }
            }
        }
    }
    else if (b1 == 'r')
    {
        int has_dot = 0;
        for (j = pos + 2u; j < slen; j++)
        {
            unsigned char ch = (unsigned char)s[j];
            if (ch == '>')
            {
                return (j - pos) + 1u;
            }
            if (ch == '.')
            {
                if (has_dot)
                {
                    break;
                }
                has_dot = 1;
            }
            else if (ch < '0' || ch > '9')
            {
                break;
            }
        }
    }

    for (k = 0; k < custom_count; k++)
    {
        const char *tag = custom_tags[k];
        size_t tlen = strlen(tag);
        if (pos + 1u + tlen < slen && strncmp(s + pos + 1u, tag, tlen) == 0)
        {
            const char *end = strchr(s + pos + 1u + tlen, '>');
            if (end)
            {
                return (size_t)((end + 1) - (s + pos));
            }
        }
    }

    return 1u;
}

static char **split_richtext(const char *s, size_t *out_count,
                             const char *const *custom_tags, int custom_count)
{
    size_t slen;
    size_t cap;
    size_t count = 0;
    size_t pos = 0;
    char **tokens;
    uint8_t exver;

    if (!s || !out_count)
    {
        if (out_count)
        {
            *out_count = 0;
        }
        return NULL;
    }

    slen = strlen(s);
    cap = slen > 0 ? slen : 1u;
    tokens = (char **)rm_calloc(cap, sizeof(char *));
    if (!tokens)
    {
        *out_count = 0;
        return NULL;
    }

    exver = (uint8_t)(uintptr_t)RikkyGetMemoryData(100);

    while (pos < slen)
    {
        unsigned char b = (unsigned char)s[pos];
        size_t tok_len = 1u;
        char *tok;

        if (count >= cap)
        {
            size_t new_cap = cap + 10u;
            char **grown = (char **)rm_realloc(tokens, new_cap * sizeof(char *));
            if (!grown)
            {
                free_token_list(tokens, count);
                *out_count = 0;
                return NULL;
            }
            tokens = grown;
            cap = new_cap;
        }

        if ((b >= 0x81u && b <= 0x9fu) || (b >= 0xe0u && b <= 0xffu))
        {
            tok_len = (pos + 1u < slen) ? 2u : 1u;
        }
        else if (b == '&')
        {
            if (pos + 3u < slen && s[pos + 1u] == '#' &&
                (unsigned char)(s[pos + 2u] - '0') < 10u)
            {
                size_t j = pos + 2u;
                while (j < slen && (unsigned char)(s[j] - '0') < 10u)
                {
                    j++;
                }
                if (j < slen && s[j] == ';')
                {
                    tok_len = (j - pos) + 1u;
                }
            }
        }
        else if (b == '<')
        {
            tok_len = match_exedit_tag(s, slen, pos, exver, custom_tags, custom_count);
        }

        tok = (char *)rm_calloc(tok_len + 1u, 1u);
        if (!tok)
        {
            free_token_list(tokens, count);
            *out_count = 0;
            return NULL;
        }
        memcpy(tok, s + pos, tok_len);
        tokens[count++] = tok;
        pos += tok_len;
    }

    *out_count = count;
    return tokens;
}

int __cdecl lua_rikky_textsplit(lua_State *L)
{
    char *src;
    char **custom = NULL;
    int custom_count = 0;
    char **tokens;
    size_t tok_count = 0;
    size_t i;

    if (lua_type(L, 1) != LUA_TSTRING)
    {
        lua_pushstring(L, "");
        return 1;
    }

    src = dup_lua_str(L, 1, NULL);
    if (!src)
    {
        return 0;
    }

    if (lua_type(L, 2) == LUA_TTABLE)
    {
        int n = (int)lua_objlen(L, 2);
        if (n > 0)
        {
            custom = (char **)rm_calloc((size_t)n, sizeof(char *));
            if (!custom)
            {
                rm_free(src);
                return 0;
            }
            for (i = 1; (int)i <= n; i++)
            {
                lua_rawgeti(L, 2, (int)i);
                if (lua_type(L, -1) == LUA_TSTRING)
                {
                    char *ct = dup_lua_str(L, -1, NULL);
                    if (ct && ct[0] != '\0')
                    {
                        custom[custom_count++] = ct;
                    }
                    else
                    {
                        rm_free(ct);
                    }
                }
                lua_pop(L, 1);
            }
        }
    }

    tokens = split_richtext(src, &tok_count, (const char *const *)custom, custom_count);
    rm_free(src);
    free_token_list(custom, (size_t)custom_count);

    if (!tokens)
    {
        return 0;
    }

    lua_settop(L, 0);
    lua_createtable(L, 0, 0);
    for (i = 0; i < tok_count; i++)
    {
        lua_pushstring(L, tokens[i]);
        lua_rawseti(L, -2, (int)i + 1);
    }
    free_token_list(tokens, tok_count);
    return 1;
}

int __cdecl lua_rikky_textload(lua_State *L)
{
    int saved_text = 0;
    wchar_t saved_wbuf[1025];
    size_t saved_wlen = 0;
    int ***ppp_obj;
    char *src_text = NULL;
    int align = 0;
    int origin = 4;
    int vertical = 0;
    char *def_s_tag = NULL;
    size_t def_s_len = 0;
    char def_c_tag[32];
    size_t def_c_len = 0;
    char **tokens = NULL;
    size_t tok_count = 0;
    int line_count = 0;
    int max_w = 0;
    int sum_h = 0;
    uint32_t *canvas = NULL;
    int final_w = 0;
    int final_h = 0;
    double cx = 0.0;
    double cy = 0.0;

    if (!rm_check_exedit_ver())
    {
        return 0;
    }

    ppp_obj = (int ***)RikkyGetMemoryData(0x66);
    if (ppp_obj && *ppp_obj && **ppp_obj && (**ppp_obj)[0x54 / 4] == 3)
    {
        const wchar_t *src_w = (const wchar_t *)RikkyGetMemoryData(0x74);
        if (src_w)
        {
            while (saved_wlen < 1024u && src_w[saved_wlen] != L'\0')
            {
                saved_wbuf[saved_wlen] = src_w[saved_wlen];
                saved_wlen++;
            }
            saved_wbuf[saved_wlen] = L'\0';
            saved_text = 1;
        }
    }

    if (lua_type(L, 1) == LUA_TSTRING)
    {
        size_t slen = 0;
        src_text = dup_lua_str(L, 1, &slen);
        if (!src_text || slen == 0)
        {
            rm_free(src_text);
            src_text = NULL;
        }
    }

    if (!src_text)
    {
        lua_settop(L, 0);
        lua_getfield(L, LUA_GLOBALSINDEX, "obj");
        lua_getfield(L, -1, "load");
        lua_pushstring(L, "text");
        lua_pushstring(L, "");
        lua_call(L, 2, 0);
        lua_settop(L, 0);
        if (saved_text)
        {
            wchar_t *dst_w = (wchar_t *)RikkyGetMemoryData(0x74);
            if (dst_w)
            {
                memcpy(dst_w, saved_wbuf, (saved_wlen + 1u) * sizeof(wchar_t));
            }
        }
        return 0;
    }

    if (lua_type(L, 2) == LUA_TNUMBER)
    {
        int v = (int)lua_tointeger(L, 2);
        if (v >= 0 && v <= 2)
        {
            align = v;
        }
    }

    if (lua_type(L, 3) == LUA_TNUMBER)
    {
        int v = (int)lua_tointeger(L, 3);
        if (v >= 0 && v <= 8)
        {
            origin = v;
        }
    }

    if (lua_tointeger(L, 4) == 1)
    {
        vertical = 1;
    }

    if (lua_type(L, 5) == LUA_TSTRING)
    {
        size_t flen = 0;
        char *font = dup_lua_str(L, 5, &flen);
        int has_size = (lua_type(L, 6) == LUA_TNUMBER);
        int sz = 0;
        char sz_buf[32];
        size_t alloc_len;

        sz_buf[0] = '\0';
        if (has_size)
        {
            int raw_sz = (int)lua_tointeger(L, 6);
            sz = raw_sz > 0 ? raw_sz : 0;
            sprintf(sz_buf, "%d", sz);
        }

        if (font && (flen > 0 || has_size))
        {
            alloc_len = strlen(sz_buf) + flen + 8u;
            def_s_tag = (char *)rm_calloc(alloc_len, 1u);
            if (def_s_tag)
            {
                strcpy(def_s_tag, "<s");
                if (has_size)
                {
                    strcat(def_s_tag, sz_buf);
                }
                if (flen > 0)
                {
                    strcat(def_s_tag, ",");
                    if (vertical && font[0] != '@')
                    {
                        strcat(def_s_tag, "@");
                    }
                    strcat(def_s_tag, font);
                }
                strcat(def_s_tag, ">");
                def_s_len = strlen(def_s_tag);
            }
        }
        rm_free(font);
    }
    else if (lua_type(L, 6) == LUA_TNUMBER)
    {
        int raw_sz = (int)lua_tointeger(L, 6);
        int sz = raw_sz > 0 ? raw_sz : 0;
        def_s_tag = (char *)rm_calloc(24u, 1u);
        if (def_s_tag)
        {
            sprintf(def_s_tag, "<s%d>", sz);
            def_s_len = strlen(def_s_tag);
        }
    }

    def_c_tag[0] = '\0';
    if (lua_type(L, 7) == LUA_TNUMBER)
    {
        unsigned int c1 = (unsigned int)lua_tointeger(L, 7) & 0xffffffu;
        if (lua_type(L, 8) == LUA_TNUMBER)
        {
            unsigned int c2 = (unsigned int)lua_tointeger(L, 8) & 0xffffffu;
            sprintf(def_c_tag, "<#%06x,%06x>", c1, c2);
        }
        else
        {
            sprintf(def_c_tag, "<#%06x>", c1);
        }
        def_c_len = strlen(def_c_tag);
    }

    lua_settop(L, 0);
    lua_getfield(L, LUA_GLOBALSINDEX, "obj");

    tokens = split_richtext(src_text, &tok_count, NULL, 0);
    rm_free(src_text);
    if (!tokens)
    {
        rm_free(def_s_tag);
        lua_settop(L, 0);
        goto restore_and_exit;
    }

    if (tok_count > 0)
    {
        size_t idx = 0;
        size_t line_start = 0;
        size_t line_chars = 0;
        int active_s = -1;
        int active_c = -1;

        while (idx < tok_count)
        {
            char *tok = tokens[idx];
            unsigned char b0 = (unsigned char)tok[0];
            int flush_line = 0;

            if (b0 == '<')
            {
                unsigned char b1 = (unsigned char)tok[1];
                if (b1 == 'p' || b1 == 'c' || b1 == 'w' || b1 == 'r')
                {
                    tok[0] = '@';
                    tok[1] = '\0';
                    line_chars += 1u;
                }
                else if (b1 == '#' && tok[2] == '>' && def_c_len > 0)
                {
                    line_chars += def_c_len;
                }
                else if (b1 == 's' && tok[2] == '>' && def_s_len > 0)
                {
                    line_chars += def_s_len;
                }
                else
                {
                    line_chars += strlen(tok);
                }

                if (line_chars > 0 && idx + 1u == tok_count)
                {
                    flush_line = 1;
                }
                else
                {
                    idx++;
                }
            }
            else
            {
                line_chars += strlen(tok);
                if (b0 == '\n' || (line_chars > 0 && idx + 1u == tok_count))
                {
                    flush_line = 1;
                }
                else
                {
                    idx++;
                }
            }

            if (flush_line)
            {
                size_t cap_len = line_chars + def_s_len + def_c_len + 4u;
                size_t end_idx = (idx + 1u == tok_count) ? tok_count : idx;
                size_t k;
                char *line_buf;
                int lw;
                int lh;

                if (active_s >= 0)
                {
                    cap_len += strlen(tokens[active_s]);
                }
                if (active_c >= 0)
                {
                    cap_len += strlen(tokens[active_c]);
                }

                line_buf = (char *)rm_calloc(cap_len, 1u);
                if (!line_buf)
                {
                    free_token_list(tokens, tok_count);
                    rm_free(def_s_tag);
                    lua_settop(L, 0);
                    goto restore_and_exit;
                }

                if (def_s_len > 0)
                {
                    strcat(line_buf, def_s_tag);
                }
                if (def_c_len > 0)
                {
                    strcat(line_buf, def_c_tag);
                }
                if (active_s >= 0)
                {
                    strcat(line_buf, tokens[active_s]);
                }
                if (active_c >= 0)
                {
                    strcat(line_buf, tokens[active_c]);
                }

                for (k = line_start; k < end_idx; k++)
                {
                    const char *piece = tokens[k];
                    if (piece[0] == '<')
                    {
                        if (piece[1] == '#')
                        {
                            if (piece[2] == '>')
                            {
                                strcat(line_buf, def_c_len > 0 ? def_c_tag : piece);
                                active_c = -1;
                            }
                            else
                            {
                                strcat(line_buf, piece);
                                active_c = (int)k;
                            }
                            continue;
                        }
                        if (piece[1] == 's')
                        {
                            if (piece[2] == '>')
                            {
                                strcat(line_buf, def_s_len > 0 ? def_s_tag : piece);
                                active_s = -1;
                            }
                            else
                            {
                                strcat(line_buf, piece);
                                active_s = (int)k;
                            }
                            continue;
                        }
                    }
                    strcat(line_buf, piece);
                }

                lua_pushstring(L, line_buf);
                lua_getfield(L, 1, "load");
                lua_pushstring(L, "text");
                lua_pushstring(L, line_buf);
                lua_call(L, 2, 0);
                lua_getfield(L, 1, "getpixel");
                lua_call(L, 0, 2);
                lw = (int)lua_tointeger(L, -2);
                lh = (int)lua_tointeger(L, -1);
                lua_pop(L, 2);

                if (lw > max_w)
                {
                    max_w = lw;
                }
                sum_h += lh;

                rm_free(line_buf);
                line_count++;
                idx++;
                line_start = idx;
                line_chars = 0;
            }
        }
    }

    canvas = (uint32_t *)rm_calloc((max_w * sum_h) > 0 ? (size_t)max_w * (size_t)sum_h : 1u, sizeof(uint32_t));
    if (!canvas)
    {
        free_token_list(tokens, tok_count);
        rm_free(def_s_tag);
        lua_settop(L, 0);
        goto restore_and_exit;
    }

    if (line_count > 0 && max_w > 0 && sum_h > 0)
    {
        int li;
        int cur_y = 0;

        for (li = 0; li < line_count; li++)
        {
            const uint32_t *lpx;
            int lw;
            int lh;
            int x_off = 0;
            int y;

            lua_getfield(L, 1, "load");
            lua_pushstring(L, "text");
            lua_pushvalue(L, li + 2);
            lua_call(L, 2, 0);
            lua_getfield(L, 1, "getpixeldata");
            lua_call(L, 0, 3);
            lpx = (const uint32_t *)lua_touserdata(L, -3);
            lw = (int)lua_tointeger(L, -2);
            lh = (int)lua_tointeger(L, -1);
            lua_pop(L, 3);

            if (align == 1)
            {
                x_off = (max_w - lw) / 2;
            }
            else if (align == 2)
            {
                x_off = max_w - lw;
            }

            if (lpx && lw > 0 && lh > 0)
            {
                for (y = 0; y < lh; y++)
                {
                    if (cur_y + y < sum_h)
                    {
                        memcpy(canvas + (size_t)(cur_y + y) * (size_t)max_w + (size_t)x_off,
                               lpx + (size_t)y * (size_t)lw,
                               (size_t)lw * sizeof(uint32_t));
                    }
                }
                cur_y += lh;
            }
        }
    }

    if (vertical == 1)
    {
        final_w = sum_h;
        final_h = max_w;
        if (max_w > 0 && sum_h > 0)
        {
            uint32_t *rot = (uint32_t *)rm_calloc((size_t)max_w * (size_t)sum_h, sizeof(uint32_t));
            if (rot)
            {
                int x;
                int y;
                for (y = 0; y < sum_h; y++)
                {
                    for (x = 0; x < max_w; x++)
                    {
                        rot[(size_t)x * (size_t)sum_h + (size_t)(sum_h - 1 - y)] =
                            canvas[(size_t)y * (size_t)max_w + (size_t)x];
                    }
                }
                rm_free(canvas);
                canvas = rot;
            }
        }
    }
    else
    {
        final_w = max_w;
        final_h = sum_h;
    }

    rm_update_obj_size(L, final_w, final_h, 1);
    lua_getfield(L, 1, "putpixeldata");
    lua_pushlightuserdata(L, canvas);
    lua_call(L, 1, 0);

    switch (origin)
    {
    case 0:
    default:
        cx = (double)(-((float)final_w * 0.5f));
        cy = -((double)final_h * 0.5);
        break;
    case 1:
        cx = 0.0;
        cy = -((double)final_h * 0.5);
        break;
    case 2:
        cx = (double)((float)final_w * 0.5f);
        cy = -((double)final_h * 0.5);
        break;
    case 3:
        cx = (double)(-((float)final_w * 0.5f));
        cy = 0.0;
        break;
    case 4:
        cx = 0.0;
        cy = 0.0;
        break;
    case 5:
        cx = (double)((float)final_w * 0.5f);
        cy = 0.0;
        break;
    case 6:
        cx = (double)(-((float)final_w * 0.5f));
        cy = (double)final_h * 0.5;
        break;
    case 7:
        cx = 0.0;
        cy = (double)((float)final_h * 0.5f);
        break;
    case 8:
        cx = (double)((float)final_w * 0.5f);
        cy = (double)final_h * 0.5;
        break;
    }

    lua_pushnumber(L, cx);
    lua_pushnumber(L, cy);
    lua_setfield(L, 1, "cy");
    lua_setfield(L, 1, "cx");
    lua_settop(L, 0);

    rm_free(canvas);
    free_token_list(tokens, tok_count);
    rm_free(def_s_tag);

restore_and_exit:
    if (saved_text)
    {
        wchar_t *dst_w = (wchar_t *)RikkyGetMemoryData(0x74);
        if (dst_w)
        {
            memcpy(dst_w, saved_wbuf, (saved_wlen + 1u) * sizeof(wchar_t));
        }
    }
    return 0;
}

// string2table

int __cdecl lua_rikky_string2table(lua_State *L)
{
    size_t len = 0;
    const char *s;
    char *chunk;
    int rt;

    if (lua_type(L, 1) != LUA_TSTRING)
    {
        return 0;
    }

    s = lua_tolstring(L, 1, &len);
    if (!s)
    {
        return 0;
    }

    chunk = (char *)rm_calloc(len + 8u, 1u);
    if (!chunk)
    {
        return 0;
    }
    sprintf(chunk, "return %s", s);

    lua_settop(L, 0);
    lua_getfield(L, LUA_GLOBALSINDEX, "loadstring");
    lua_pushstring(L, chunk);
    rm_free(chunk);
    lua_call(L, 1, 1);

    if (lua_type(L, -1) == LUA_TFUNCTION)
    {
        lua_call(L, 0, 1);
    }
    else
    {
        lua_settop(L, 0);
        lua_pushnil(L);
        return 1;
    }

    rt = lua_type(L, -1);
    if (rt != LUA_TNIL && rt != LUA_TTABLE)
    {
        lua_pop(L, 1);
        lua_pushnil(L);
    }
    return 1;
}

// win32 clipboard read/write for text and images

static int clipboard_write_text(lua_State *L)
{
    size_t slen = 0;
    char *text = dup_lua_str(L, 2, &slen);
    int ok = 0;

    lua_settop(L, 0);
    if (!text)
    {
        CloseClipboard();
        lua_pushboolean(L, 0);
        return 1;
    }

    EmptyClipboard();
    if (slen == 0)
    {
        rm_free(text);
        CloseClipboard();
        lua_pushboolean(L, 1);
        return 1;
    }

    {
        HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, slen + 1u);
        if (hg)
        {
            void *p = GlobalLock(hg);
            if (p)
            {
                memcpy(p, text, slen + 1u);
                GlobalUnlock(hg);
                if (SetClipboardData(CF_TEXT, hg))
                {
                    ok = 1;
                }
                else
                {
                    GlobalFree(hg);
                }
            }
            else
            {
                GlobalFree(hg);
            }
        }
    }

    {
        HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, slen + 1u);
        if (hg)
        {
            void *p = GlobalLock(hg);
            if (p)
            {
                memcpy(p, text, slen + 1u);
                GlobalUnlock(hg);
                if (SetClipboardData(CF_OEMTEXT, hg))
                {
                    ok = 1;
                }
                else
                {
                    GlobalFree(hg);
                }
            }
            else
            {
                GlobalFree(hg);
            }
        }
    }

    {
        int wlen = 0;
        wchar_t *ws = rm_sjis_html_to_wide(&wlen, text);
        if (ws)
        {
            size_t wbytes = ((size_t)wlen + 1u) * sizeof(wchar_t);
            HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, wbytes);
            if (hg)
            {
                void *p = GlobalLock(hg);
                if (p)
                {
                    memcpy(p, ws, wbytes);
                    GlobalUnlock(hg);
                    if (SetClipboardData(CF_UNICODETEXT, hg))
                    {
                        ok = 1;
                    }
                    else
                    {
                        GlobalFree(hg);
                    }
                }
                else
                {
                    GlobalFree(hg);
                }
            }
            rm_free(ws);
        }
    }

    rm_free(text);
    CloseClipboard();
    lua_pushboolean(L, ok);
    return 1;
}

static int clipboard_write_image(lua_State *L, int src_type)
{
    uint8_t *owned_bgra = NULL;
    const uint8_t *bgra = NULL;
    int w = 0;
    int h = 0;
    int ok = 0;

    EmptyClipboard();

    if (src_type == 4)
    {
        bgra = (const uint8_t *)lua_touserdata(L, 2);
        w = (int)lua_tointeger(L, 3);
        h = (int)lua_tointeger(L, 4);
        lua_settop(L, 0);
    }
    else
    {
        lua_settop(L, 0);
        owned_bgra = rm_read_exedit_bgra(L, src_type - 1, &w, &h);
        bgra = owned_bgra;
    }

    if (!bgra || w <= 0 || h <= 0)
    {
        rm_free(owned_bgra);
        CloseClipboard();
        lua_pushboolean(L, 0);
        return 1;
    }

    // cf_dibv5 (bottom-up 32bpp bgra)
    {
        size_t img_bytes = (size_t)w * (size_t)h * 4u;
        HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, 124u + img_bytes);
        if (hg)
        {
            uint8_t *p = (uint8_t *)GlobalLock(hg);
            if (p)
            {
                BITMAPV5HEADER *v5 = (BITMAPV5HEADER *)p;
                uint32_t *dst = (uint32_t *)(p + 124u);
                const uint32_t *src = (const uint32_t *)bgra;
                int y;

                memset(v5, 0, 124u);
                v5->bV5Size = 124u;
                v5->bV5Width = w;
                v5->bV5Height = h;
                v5->bV5Planes = 1;
                v5->bV5BitCount = 32;
                v5->bV5Compression = BI_RGB;
                v5->bV5SizeImage = (DWORD)img_bytes;
                v5->bV5CSType = 0x73524742u;
                v5->bV5Intent = LCS_GM_IMAGES;

                for (y = 0; y < h; y++)
                {
                    memcpy(dst + (size_t)(h - 1 - y) * (size_t)w,
                           src + (size_t)y * (size_t)w,
                           (size_t)w * 4u);
                }
                GlobalUnlock(hg);
                if (SetClipboardData(CF_DIBV5, hg))
                {
                    ok = 1;
                }
                else
                {
                    GlobalFree(hg);
                }
            }
            else
            {
                GlobalFree(hg);
            }
        }
    }

    // cf_bitmap + cf_dib (24bpp composited onto white background)
    {
        size_t stride24 = ((size_t)w * 3u + 3u) & ~3u;
        size_t dib_bytes = stride24 * (size_t)h;
        HGLOBAL hg_dib = GlobalAlloc(GMEM_MOVEABLE, sizeof(BITMAPINFOHEADER) + dib_bytes);
        if (hg_dib)
        {
            uint8_t *p = (uint8_t *)GlobalLock(hg_dib);
            if (p)
            {
                BITMAPINFO *bmi = (BITMAPINFO *)p;
                uint8_t *bits = p + sizeof(BITMAPINFOHEADER);
                const uint32_t *src = (const uint32_t *)bgra;
                int x;
                int y;
                HDC hdc;

                memset(&bmi->bmiHeader, 0, sizeof(BITMAPINFOHEADER));
                bmi->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                bmi->bmiHeader.biWidth = w;
                bmi->bmiHeader.biHeight = h;
                bmi->bmiHeader.biPlanes = 1;
                bmi->bmiHeader.biBitCount = 24;
                bmi->bmiHeader.biCompression = BI_RGB;
                bmi->bmiHeader.biSizeImage = (DWORD)dib_bytes;

                memset(bits, 0, dib_bytes);
                for (y = 0; y < h; y++)
                {
                    uint8_t *row = bits + (size_t)(h - 1 - y) * stride24;
                    const uint32_t *srow = src + (size_t)y * (size_t)w;
                    for (x = 0; x < w; x++)
                    {
                        uint32_t px = srow[x];
                        uint32_t b = px & 0xffu;
                        uint32_t g = (px >> 8) & 0xffu;
                        uint32_t r = (px >> 16) & 0xffu;
                        uint32_t inv_a = 255u - (px >> 24);
                        row[x * 3 + 0] = (uint8_t)(((255u - b) * inv_a) / 255u + b);
                        row[x * 3 + 1] = (uint8_t)(((255u - g) * inv_a) / 255u + g);
                        row[x * 3 + 2] = (uint8_t)(((255u - r) * inv_a) / 255u + r);
                    }
                }

                hdc = GetDC(NULL);
                if (hdc)
                {
                    HBITMAP hbm = CreateCompatibleBitmap(hdc, w, h);
                    if (hbm)
                    {
                        SetDIBits(hdc, hbm, 0, (UINT)h, bits, bmi, DIB_RGB_COLORS);
                        if (SetClipboardData(CF_BITMAP, hbm))
                        {
                            ok = 1;
                        }
                        else
                        {
                            DeleteObject(hbm);
                        }
                    }
                    ReleaseDC(NULL, hdc);
                }

                GlobalUnlock(hg_dib);
                if (SetClipboardData(CF_DIB, hg_dib))
                {
                    ok = 1;
                }
                else
                {
                    GlobalFree(hg_dib);
                }
            }
            else
            {
                GlobalFree(hg_dib);
            }
        }
    }

    rm_free(owned_bgra);
    CloseClipboard();
    lua_pushboolean(L, ok);
    return 1;
}

static int decode_clipboard_dib(const uint8_t *dib, size_t dib_size, int *out_w, int *out_h)
{
    uint32_t hdr_size;
    int32_t w;
    int32_t raw_h;
    int top_down = 0;
    int32_t h;
    uint16_t bpp;
    uint32_t comp;
    uint32_t clr_used = 0;
    size_t pal_entries = 0;
    const uint32_t *palette = NULL;
    const uint8_t *bits;
    uint32_t *dst;
    int x;
    int y;

    if (!dib || dib_size < sizeof(BITMAPINFOHEADER))
    {
        return 0;
    }

    memcpy(&hdr_size, dib, 4u);
    if (hdr_size < sizeof(BITMAPINFOHEADER) || hdr_size > dib_size)
    {
        return 0;
    }

    memcpy(&w, dib + 4u, 4u);
    memcpy(&raw_h, dib + 8u, 4u);
    memcpy(&bpp, dib + 14u, 2u);
    memcpy(&comp, dib + 16u, 4u);
    memcpy(&clr_used, dib + 32u, 4u);

    if (w <= 0 || raw_h == 0)
    {
        return 0;
    }
    if (raw_h < 0)
    {
        top_down = 1;
        h = -raw_h;
    }
    else
    {
        h = raw_h;
    }

    if (bpp <= 8u)
    {
        pal_entries = clr_used ? (size_t)clr_used : ((size_t)1u << bpp);
        palette = (const uint32_t *)(dib + hdr_size);
    }
    else if (comp == BI_BITFIELDS && hdr_size == sizeof(BITMAPINFOHEADER))
    {
        pal_entries = 3u;
    }

    bits = dib + hdr_size + pal_entries * 4u;
    if (bits > dib + dib_size)
    {
        return 0;
    }

    rm_free(g_clip_img_buf);
    g_clip_img_buf = (uint8_t *)rm_malloc((size_t)w * (size_t)h * 4u);
    if (!g_clip_img_buf)
    {
        return 0;
    }
    dst = (uint32_t *)g_clip_img_buf;

    if (bpp == 32u)
    {
        size_t stride = (size_t)w * 4u;
        int has_alpha = 0;
        for (y = 0; y < h; y++)
        {
            int sy = top_down ? y : (h - 1 - y);
            const uint8_t *srow = bits + (size_t)sy * stride;
            uint32_t *drow = dst + (size_t)y * (size_t)w;
            if (srow + stride > dib + dib_size)
            {
                break;
            }
            memcpy(drow, srow, stride);
            for (x = 0; x < w; x++)
            {
                if ((drow[x] & 0xff000000u) != 0)
                {
                    has_alpha = 1;
                }
            }
        }
        if (!has_alpha)
        {
            size_t total = (size_t)w * (size_t)h;
            size_t i;
            for (i = 0; i < total; i++)
            {
                dst[i] |= 0xff000000u;
            }
        }
    }
    else if (bpp == 24u)
    {
        size_t stride = ((size_t)w * 3u + 3u) & ~3u;
        for (y = 0; y < h; y++)
        {
            int sy = top_down ? y : (h - 1 - y);
            const uint8_t *srow = bits + (size_t)sy * stride;
            uint32_t *drow = dst + (size_t)y * (size_t)w;
            if (srow + (size_t)w * 3u > dib + dib_size)
            {
                break;
            }
            for (x = 0; x < w; x++)
            {
                drow[x] = (uint32_t)srow[x * 3 + 0] |
                          ((uint32_t)srow[x * 3 + 1] << 8) |
                          ((uint32_t)srow[x * 3 + 2] << 16) |
                          0xff000000u;
            }
        }
    }
    else if (bpp == 8u && palette)
    {
        size_t stride = ((size_t)w + 3u) & ~3u;
        for (y = 0; y < h; y++)
        {
            int sy = top_down ? y : (h - 1 - y);
            const uint8_t *srow = bits + (size_t)sy * stride;
            uint32_t *drow = dst + (size_t)y * (size_t)w;
            if (srow + (size_t)w > dib + dib_size)
            {
                break;
            }
            for (x = 0; x < w; x++)
            {
                uint8_t idx = srow[x];
                uint32_t c = (idx < pal_entries) ? palette[idx] : 0u;
                drow[x] = (c & 0x00ffffffu) | 0xff000000u;
            }
        }
    }
    else if (bpp == 4u && palette)
    {
        size_t stride = (((size_t)w + 1u) / 2u + 3u) & ~3u;
        for (y = 0; y < h; y++)
        {
            int sy = top_down ? y : (h - 1 - y);
            const uint8_t *srow = bits + (size_t)sy * stride;
            uint32_t *drow = dst + (size_t)y * (size_t)w;
            for (x = 0; x < w; x++)
            {
                uint8_t byte_v = srow[x / 2];
                uint8_t idx = (x & 1) ? (byte_v & 0x0fu) : (byte_v >> 4);
                uint32_t c = (idx < pal_entries) ? palette[idx] : 0u;
                drow[x] = (c & 0x00ffffffu) | 0xff000000u;
            }
        }
    }
    else if (bpp == 1u && palette)
    {
        size_t stride = (((size_t)w + 7u) / 8u + 3u) & ~3u;
        for (y = 0; y < h; y++)
        {
            int sy = top_down ? y : (h - 1 - y);
            const uint8_t *srow = bits + (size_t)sy * stride;
            uint32_t *drow = dst + (size_t)y * (size_t)w;
            for (x = 0; x < w; x++)
            {
                uint8_t idx = (srow[x / 8] >> (7 - (x & 7))) & 1u;
                uint32_t c = (idx < pal_entries) ? palette[idx] : 0u;
                drow[x] = (c & 0x00ffffffu) | 0xff000000u;
            }
        }
    }
    else
    {
        rm_free(g_clip_img_buf);
        g_clip_img_buf = NULL;
        return 0;
    }

    *out_w = (int)w;
    *out_h = (int)h;
    return 1;
}

int __cdecl lua_rikky_clipboard(lua_State *L)
{
    const char *mode_s;
    int mode_ch;

    if (lua_type(L, 1) != LUA_TSTRING)
    {
        lua_settop(L, 0);
        lua_pushnil(L);
        return 1;
    }

    mode_s = lua_tolstring(L, 1, NULL);
    if (!mode_s || mode_s[0] == '\0')
    {
        lua_settop(L, 0);
        lua_pushnil(L);
        return 1;
    }
    mode_ch = tolower((unsigned char)mode_s[0]);

    if (mode_ch == 'w')
    {
        int top = lua_gettop(L);
        int t2;

        if (!OpenClipboard(NULL))
        {
            lua_settop(L, 0);
            lua_pushboolean(L, 0);
            return 1;
        }

        if (top == 1)
        {
            return clipboard_write_image(L, 1);
        }

        t2 = lua_type(L, 2);
        if (t2 == LUA_TSTRING)
        {
            return clipboard_write_text(L);
        }
        if (t2 == LUA_TUSERDATA || t2 == LUA_TLIGHTUSERDATA)
        {
            if (lua_type(L, 3) == LUA_TNUMBER && lua_tointeger(L, 3) > 0 &&
                lua_type(L, 4) == LUA_TNUMBER && lua_tointeger(L, 4) > 0)
            {
                return clipboard_write_image(L, 4);
            }
            return clipboard_write_image(L, 1);
        }
        if (t2 == LUA_TNUMBER)
        {
            int src_idx = (int)lua_tointeger(L, 2);
            if (src_idx >= 0 && src_idx <= 2)
            {
                return clipboard_write_image(L, src_idx + 1);
            }
        }

        CloseClipboard();
        lua_settop(L, 0);
        lua_pushboolean(L, 0);
        return 1;
    }

    if (mode_ch == 'r')
    {
        char *kind;
        int raw_mode = 0;

        if (!OpenClipboard(NULL) || lua_type(L, 2) != LUA_TSTRING)
        {
            CloseClipboard();
            lua_settop(L, 0);
            lua_pushnil(L);
            return 1;
        }

        kind = dup_lua_str(L, 2, NULL);
        if (!kind)
        {
            CloseClipboard();
            lua_settop(L, 0);
            lua_pushnil(L);
            return 1;
        }
        rm_str_tolower(kind);

        if (lua_gettop(L) >= 3 && lua_type(L, 3) == LUA_TBOOLEAN && lua_toboolean(L, 3) != 0)
        {
            raw_mode = 1;
        }
        lua_settop(L, 0);

        if (strcmp(kind, "text") == 0)
        {
            HANDLE hdata;
            rm_free(kind);

            hdata = GetClipboardData(CF_UNICODETEXT);
            if (hdata)
            {
                const wchar_t *ws = (const wchar_t *)GlobalLock(hdata);
                if (ws)
                {
                    int dummy = 0;
                    char *sjis = rm_wide_to_sjis_html(&dummy, ws);
                    GlobalUnlock(hdata);
                    CloseClipboard();
                    if (sjis)
                    {
                        lua_pushstring(L, sjis);
                        rm_free(sjis);
                        return 1;
                    }
                    lua_pushnil(L);
                    return 1;
                }
            }

            hdata = GetClipboardData(CF_TEXT);
            if (!hdata)
            {
                hdata = GetClipboardData(CF_OEMTEXT);
            }
            if (hdata)
            {
                const char *cs = (const char *)GlobalLock(hdata);
                if (cs)
                {
                    lua_pushstring(L, cs);
                    GlobalUnlock(hdata);
                    CloseClipboard();
                    return 1;
                }
            }

            CloseClipboard();
            lua_pushnil(L);
            return 1;
        }

        if (strcmp(kind, "image") == 0)
        {
            int w = 0;
            int h = 0;
            int decoded = 0;
            HANDLE hdata;

            rm_free(kind);

            hdata = GetClipboardData(CF_DIBV5);
            if (hdata)
            {
                const uint8_t *p = (const uint8_t *)GlobalLock(hdata);
                size_t sz = (size_t)GlobalSize(hdata);
                if (p)
                {
                    decoded = decode_clipboard_dib(p, sz, &w, &h);
                    GlobalUnlock(hdata);
                }
            }

            if (!decoded)
            {
                hdata = GetClipboardData(CF_DIB);
                if (hdata)
                {
                    const uint8_t *p = (const uint8_t *)GlobalLock(hdata);
                    size_t sz = (size_t)GlobalSize(hdata);
                    if (p)
                    {
                        decoded = decode_clipboard_dib(p, sz, &w, &h);
                        GlobalUnlock(hdata);
                    }
                }
            }

            if (!decoded)
            {
                HBITMAP hbm = (HBITMAP)GetClipboardData(CF_BITMAP);
                if (hbm)
                {
                    BITMAP bm;
                    if (GetObjectA(hbm, sizeof(bm), &bm) && bm.bmWidth > 0 && bm.bmHeight > 0)
                    {
                        HDC hdc = GetDC(NULL);
                        if (hdc)
                        {
                            BITMAPINFO bmi;
                            w = bm.bmWidth;
                            h = bm.bmHeight;
                            rm_free(g_clip_img_buf);
                            g_clip_img_buf = (uint8_t *)rm_malloc((size_t)w * (size_t)h * 4u);
                            if (g_clip_img_buf)
                            {
                                memset(&bmi, 0, sizeof(bmi));
                                bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                                bmi.bmiHeader.biWidth = w;
                                bmi.bmiHeader.biHeight = -h;
                                bmi.bmiHeader.biPlanes = 1;
                                bmi.bmiHeader.biBitCount = 32;
                                bmi.bmiHeader.biCompression = BI_RGB;
                                if (GetDIBits(hdc, hbm, 0, (UINT)h, g_clip_img_buf, &bmi, DIB_RGB_COLORS))
                                {
                                    size_t total = (size_t)w * (size_t)h;
                                    size_t i;
                                    uint32_t *px = (uint32_t *)g_clip_img_buf;
                                    for (i = 0; i < total; i++)
                                    {
                                        px[i] |= 0xff000000u;
                                    }
                                    decoded = 1;
                                }
                                else
                                {
                                    rm_free(g_clip_img_buf);
                                    g_clip_img_buf = NULL;
                                }
                            }
                            ReleaseDC(NULL, hdc);
                        }
                    }
                }
            }

            CloseClipboard();
            if (!decoded || !g_clip_img_buf)
            {
                if (raw_mode)
                {
                    lua_pushnil(L);
                    lua_pushnil(L);
                    lua_pushnil(L);
                    return 3;
                }
                lua_pushboolean(L, 0);
                return 1;
            }

            if (raw_mode)
            {
                lua_pushlightuserdata(L, g_clip_img_buf);
                lua_pushinteger(L, w);
                lua_pushinteger(L, h);
                return 3;
            }

            lua_getfield(L, LUA_GLOBALSINDEX, "obj");
            rm_update_obj_size(L, w, h, 1);
            lua_getfield(L, 1, "putpixeldata");
            lua_pushlightuserdata(L, g_clip_img_buf);
            lua_call(L, 1, 0);
            lua_settop(L, 0);
            rm_free(g_clip_img_buf);
            g_clip_img_buf = NULL;
            lua_pushboolean(L, 1);
            return 1;
        }

        rm_free(kind);
        CloseClipboard();
        lua_pushnil(L);
        return 1;
    }

    lua_settop(L, 0);
    lua_pushnil(L);
    return 1;
}
