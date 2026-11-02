#include <windows.h>
#include <mmsystem.h>
#include <shlobj.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rikky_module_internal.h"

typedef struct
{
    HWND main_hwnd;
    lua_State *L;
    int count;
    int mode;
    DWORD pid;
} RmHwndEnumCtx;

typedef struct
{
    HANDLE handle;
    int index;
    char d_name[MAX_PATH];
} RmDir;

static uint8_t g_mouse_auto_state = 0;

static volatile uint8_t g_prog_active = 0;
static COLORREF g_prog_color = 0;
static volatile double g_prog_percent = 0.0;
static char g_prog_title[512];

// built-in figure names
static const char k_fig_bg[] = "\x94\x77\x8c\x69";
static const char k_fig_circle[] = "\x89\x7e";
static const char k_fig_rect[] = "\x8e\x6c\x8a\x70\x8c\x60";
static const char k_fig_tri[] = "\x8e\x4f\x8a\x70\x8c\x60";
static const char k_fig_pent[] = "\x8c\xdc\x8a\x70\x8c\x60";
static const char k_fig_hex[] = "\x98\x5a\x8a\x70\x8c\x60";
static const char k_fig_star[] = "\x90\xaf\x8c\x5e";

// exedit filter names
static const char k_name_custom_obj[] =
    "\x83\x4a\x83\x58\x83\x5e\x83\x80\x83\x49\x83\x75\x83\x57\x83\x46\x83\x4e\x83\x67";
static const char k_name_anim_eff[] =
    "\x83\x41\x83\x6a\x83\x81\x81\x5b\x83\x56\x83\x87\x83\x93\x8c\xf8\x89\xca";
static const char k_name_script_ctrl[] =
    "\x83\x58\x83\x4e\x83\x8a\x83\x76\x83\x67\x90\xa7\x8c\xe4";

static const char *const k_anim_names[25] =
    {
        "\x90\x6b\x82\xa6\x82\xe9",
        "\x90\x55\x82\xe8\x8e\x71",
        "\x92\x65\x82\xde",
        "\x8d\xc0\x95\x57\x82\xcc\x8a\x67\x91\xe5\x8f\x6b\x8f\xac(\x8c\xc2\x95\xca\x83\x49\x83\x75\x83\x57\x83\x46\x83\x4e\x83\x67)",
        "\x89\xe6\x96\xca\x8a\x4f\x82\xa9\x82\xe7\x93\x6f\x8f\xea",
        "\x83\x89\x83\x93\x83\x5f\x83\x80\x95\xfb\x8c\xfc\x82\xa9\x82\xe7\x93\x6f\x8f\xea",
        "\x8a\x67\x91\xe5\x8f\x6b\x8f\xac\x82\xb5\x82\xc4\x93\x6f\x8f\xea",
        "\x83\x89\x83\x93\x83\x5f\x83\x80\x8a\xd4\x8a\x75\x82\xc5\x97\x8e\x82\xbf\x82\xc8\x82\xaa\x82\xe7\x93\x6f\x8f\xea",
        "\x92\x65\x82\xf1\x82\xc5\x93\x6f\x8f\xea",
        "\x8d\x4c\x82\xaa\x82\xc1\x82\xc4\x93\x6f\x8f\xea",
        "\x8b\x4e\x82\xab\x8f\xe3\x82\xaa\x82\xc1\x82\xc4\x93\x6f\x8f\xea",
        "\x89\xbd\x8f\x88\x82\xa9\x82\xe7\x82\xc6\x82\xe0\x82\xc8\x82\xad\x93\x6f\x8f\xea",
        "\x94\xbd\x95\x9c\x88\xda\x93\xae",
        "\x8d\xc0\x95\x57\x82\xcc\x89\xf1\x93\x5d(\x8c\xc2\x95\xca\x83\x49\x83\x75\x83\x57\x83\x46\x83\x4e\x83\x67)",
        "\x97\xa7\x95\xfb\x91\xcc(\x83\x4a\x83\x81\x83\x89\x90\xa7\x8c\xe4)",
        "\x8b\x85\x91\xcc(\x83\x4a\x83\x81\x83\x89\x90\xa7\x8c\xe4)",
        "\x8d\xd3\x82\xaf\x8e\x55\x82\xe9",
        "\x93\x5f\x96\xc5",
        "\x93\x5f\x96\xc5\x82\xb5\x82\xc4\x93\x6f\x8f\xea",
        "\x8a\xc8\x88\xd5\x95\xcf\x8c\x60",
        "\x8a\xc8\x88\xd5\x95\xcf\x8c\x60(\x83\x4a\x83\x81\x83\x89\x90\xa7\x8c\xe4)",
        "\x83\x8a\x81\x5b\x83\x8b\x89\xf1\x93\x5d",
        "\x96\x9c\x89\xd8\x8b\xbe",
        "\x89\x7e\x8c\x60\x94\x7a\x92\x75",
        "\x83\x89\x83\x93\x83\x5f\x83\x80\x94\x7a\x92\x75"};

// media extension categories
static const char k_cat_image[] = "\x89\xe6\x91\x9c";
static const char k_cat_movie[] = "\x93\xae\x89\xe6";
static const char k_cat_audio[] = "\x89\xb9\x90\xba";
static const char k_cat_text[] = "\x83\x65\x83\x4c\x83\x58\x83\x67";

// progress window failure message
static const char k_prog_err_msg[] =
    "\x90\x69\x92\xbb\x95\x5c\x8e\xa6\x82\xc9\x8e\xb8\x94\x73\x82\xb5\x82\xdc\x82\xb5\x82\xbd\n"
    "\x8f\x88\x97\x9d\x82\xaa\x8f\x49\x82\xed\x82\xe9\x82\xdc\x82\xc5\x82\xb5\x82\xce\x82\xe7\x82\xad"
    "\x82\xa8\x91\xd2\x82\xbf\x82\xad\x82\xbe\x82\xb3\x82\xa2";

static BOOL CALLBACK enum_hwnd_proc(HWND hwnd, LPARAM lparam)
{
    RmHwndEnumCtx *ctx = (RmHwndEnumCtx *)lparam;
    DWORD pid = 0;

    if (ctx->main_hwnd == hwnd)
    {
        return TRUE;
    }
    if (ctx->mode != 5)
    {
        if (ctx->mode != 4)
        {
            return TRUE;
        }
        if (!IsWindowVisible(hwnd))
        {
            return TRUE;
        }
    }

    GetWindowThreadProcessId(hwnd, &pid);
    if (ctx->pid == pid)
    {
        char title[268];
        title[0] = '\0';
        GetWindowTextA(hwnd, title, 0x100);
        ctx->count++;
        lua_pushinteger(ctx->L, (lua_Integer)(intptr_t)hwnd);
        lua_rawseti(ctx->L, -3, ctx->count);
        lua_pushstring(ctx->L, title);
        lua_rawseti(ctx->L, -2, ctx->count);
    }
    return TRUE;
}

int __cdecl lua_rikky_getinfo(lua_State *L)
{
    int ok_au = rm_check_aviutl_ver();
    int ok_ex = rm_check_exedit_ver();
    const char *raw_key;
    char key[64];
    int opt = 0;

    if (!ok_au || !ok_ex)
    {
        lua_settop(L, 0);
        lua_pushnil(L);
        return 1;
    }

    if (lua_type(L, 1) != LUA_TSTRING)
    {
        lua_pushnil(L);
        return 1;
    }

    raw_key = lua_tolstring(L, 1, NULL);
    if (!raw_key)
    {
        lua_pushnil(L);
        return 1;
    }
    strncpy(key, raw_key, sizeof(key) - 1);
    key[sizeof(key) - 1] = '\0';
    rm_str_tolower(key);

    if (!lua_isnoneornil(L, 2))
    {
        opt = (int)lua_tointeger(L, 2);
    }
    lua_settop(L, 0);

    if (strcmp(key, "version") == 0 || strcmp(key, "ver") == 0)
    {
        char buf[64];
        const char *ver_str;

        switch (opt)
        {
        case 1:
            ver_str = (const char *)RikkyGetMemoryData(0x65);
            lua_pushstring(L, ver_str ? ver_str : "");
            return 1;
        case 2:
            lua_pushstring(L, "1.4b");
            return 1;
        case 3:
            ver_str = (const char *)RikkyGetMemoryData(0);
            lua_pushstring(L, ver_str ? ver_str : "");
            return 1;
        case 4:
            sprintf(buf, "%d", (int)(intptr_t)RikkyGetMemoryData(1));
            lua_pushstring(L, buf);
            return 1;
        case 5:
            sprintf(buf, "%d", (int)(intptr_t)RikkyGetMemoryData(100));
            lua_pushstring(L, buf);
            return 1;
        default:
            ver_str = (const char *)RikkyGetMemoryData(2);
            lua_pushstring(L, ver_str ? ver_str : "");
            return 1;
        }
    }

    if (strcmp(key, "frame") == 0 || strcmp(key, "frm") == 0)
    {
        uintptr_t *p_cur_obj = (uintptr_t *)RikkyGetMemoryData(0x66);
        uint32_t cur_frame = 0;
        uint32_t total_frames = 1;

        if (p_cur_obj && *p_cur_obj && *(int *)(*p_cur_obj + 4) == -1)
        {
            uintptr_t *p_sub = (uintptr_t *)RikkyGetMemoryData(0x79);
            uintptr_t off_f = (uintptr_t)RikkyGetMemoryData(0x7a);
            if (p_sub && *p_sub)
            {
                const uint32_t *pf = (const uint32_t *)(*p_sub + off_f);
                cur_frame = pf[0];
                total_frames = pf[1];
            }
        }
        else
        {
            int *p_subwin = (int *)RikkyGetMemoryData(0x0b);
            int *p_playing = (int *)RikkyGetMemoryData(0x0a);
            uintptr_t *p_editp = (uintptr_t *)RikkyGetMemoryData(3);

            if (p_subwin && *p_subwin == 1 && p_playing && *p_playing == 1)
            {
                uint32_t *p_pf = (uint32_t *)RikkyGetMemoryData(0x09);
                if (p_pf)
                {
                    cur_frame = *p_pf;
                }
            }
            else if (p_editp && *p_editp)
            {
                uintptr_t off_frame = (uintptr_t)RikkyGetMemoryData(6);
                cur_frame = *(uint32_t *)(*p_editp + off_frame);
            }

            if (p_editp && *p_editp)
            {
                uintptr_t off_total = (uintptr_t)RikkyGetMemoryData(7);
                total_frames = *(uint32_t *)(*p_editp + off_total);
            }
        }

        lua_pushnumber(L, (double)cur_frame);
        lua_pushnumber(L, (double)(total_frames - 1u));
        return 2;
    }

    if (strcmp(key, "aup") == 0 || strcmp(key, "output") == 0)
    {
        uintptr_t *p_editp = (uintptr_t *)RikkyGetMemoryData(3);
        uintptr_t off = (uintptr_t)RikkyGetMemoryData(key[0] == 'a' ? 4 : 5);
        const char *full = (p_editp && *p_editp) ? (const char *)(*p_editp + off) : "";

        if (opt == 1)
        {
            char fname[MAX_PATH];
            fname[0] = '\0';
            _splitpath(full, NULL, NULL, fname, NULL);
            lua_pushstring(L, fname);
        }
        else
        {
            lua_pushstring(L, full);
        }
        return 1;
    }

    if (strcmp(key, "state") == 0)
    {
        int *p_subwin = (int *)RikkyGetMemoryData(0x0b);
        int *p_playing;

        if (!p_subwin || *p_subwin != 1)
        {
            lua_pushstring(L, "saving");
            return 1;
        }
        p_playing = (int *)RikkyGetMemoryData(0x0a);
        if (p_playing && *p_playing == 1)
        {
            lua_pushstring(L, "playing");
            return 1;
        }
        lua_pushstring(L, "editing");
        return 1;
    }

    if (strcmp(key, "path") == 0)
    {
        char full[MAX_PATH + 8];
        char drive[16];
        char dir[MAX_PATH];
        HMODULE hm;

        full[0] = '\0';
        if (opt == 2)
        {
            size_t len;
            SHGetFolderPathA(NULL, 0, NULL, 0, full);
            len = strlen(full);
            full[len] = '\\';
            full[len + 1] = '\0';
            lua_pushstring(L, full);
            return 1;
        }

        hm = (opt == 1) ? GetModuleHandleA("exedit.auf") : NULL;
        GetModuleFileNameA(hm, full, MAX_PATH);
        drive[0] = '\0';
        dir[0] = '\0';
        _splitpath(full, drive, dir, NULL, NULL);
        sprintf(full, "%s%s", drive, dir);
        lua_pushstring(L, full);
        return 1;
    }

    if (strcmp(key, "focus") == 0)
    {
        int *p_sel = (int *)RikkyGetMemoryData(0x6b);
        int sel = p_sel ? *p_sel : -1;

        if (sel != -1)
        {
            uintptr_t stride = (uintptr_t)RikkyGetMemoryData(0x6d);
            uintptr_t *p_base = (uintptr_t *)RikkyGetMemoryData(0x6c);
            uintptr_t *p_cur = (uintptr_t *)RikkyGetMemoryData(0x66);

            if (p_base && *p_base && p_cur && *p_cur)
            {
                uintptr_t sel_obj = *p_base + stride * (size_t)sel;
                int mid = *(int *)(sel_obj + 0x50);
                if (mid == -1)
                {
                    if (*p_cur == sel_obj)
                    {
                        lua_pushboolean(L, 1);
                        return 1;
                    }
                }
                else if (mid == *(int *)(*p_cur + 0x50))
                {
                    lua_pushboolean(L, 1);
                    return 1;
                }
            }
        }
        lua_pushboolean(L, 0);
        return 1;
    }

    if (strcmp(key, "blend") == 0)
    {
        uint32_t val = 0;

        if (opt == 1)
        {
            uint32_t **pp_blend = (uint32_t **)RikkyGetMemoryData(0x70);
            if (pp_blend && *pp_blend)
            {
                val = **pp_blend;
            }
        }
        else if (opt == 2)
        {
            uint8_t *p_mode = (uint8_t *)RikkyGetMemoryData(0x6f);
            val = p_mode ? (uint32_t)(*p_mode) : 0u;
            if (val == 0x14u)
            {
                lua_pushstring(L, "alpha_add");
                return 1;
            }
            if (val == 0x15u)
            {
                lua_pushstring(L, "alpha_max");
                return 1;
            }
            if (val == 0x16u)
            {
                lua_pushstring(L, "alpha_sub");
                return 1;
            }
            if (val == 0x19u)
            {
                lua_pushstring(L, "alpha_add2");
                return 1;
            }
        }
        else
        {
            uint8_t *p_mode = (uint8_t *)RikkyGetMemoryData(0x6f);
            val = p_mode ? (uint32_t)(*p_mode) : 0u;
        }
        lua_pushinteger(L, (lua_Integer)val);
        return 1;
    }

    if (strcmp(key, "group") == 0 || strcmp(key, "grp") == 0)
    {
        RmShadowInfo sh;
        rm_query_shadow_info(&sh, L);
        lua_createtable(L, 0, 0);
        lua_pushnumber(L, sh.offset_y);
        lua_setfield(L, -2, "x");
        lua_pushnumber(L, sh.offset_x);
        lua_setfield(L, -2, "y");
        lua_pushnumber(L, sh.offset_z);
        lua_setfield(L, -2, "z");
        lua_pushnumber(L, sh.scale);
        lua_setfield(L, -2, "zoom");
        lua_pushnumber(L, sh.mat[0]);
        lua_setfield(L, -2, "Xx");
        lua_pushnumber(L, sh.mat[1]);
        lua_setfield(L, -2, "Xy");
        lua_pushnumber(L, sh.mat[2]);
        lua_setfield(L, -2, "Xz");
        lua_pushnumber(L, sh.mat[3]);
        lua_setfield(L, -2, "Yx");
        lua_pushnumber(L, sh.mat[4]);
        lua_setfield(L, -2, "Yy");
        lua_pushnumber(L, sh.mat[5]);
        lua_setfield(L, -2, "Yz");
        lua_pushnumber(L, sh.mat[6]);
        lua_setfield(L, -2, "Zx");
        lua_pushnumber(L, sh.mat[7]);
        lua_setfield(L, -2, "Zy");
        lua_pushnumber(L, sh.mat[8]);
        lua_setfield(L, -2, "Zz");
        lua_pushboolean(L, (int)sh.enabled);
        return 2;
    }

    if (strcmp(key, "groups") == 0)
    {
        uintptr_t cur_obj = (uintptr_t)GetObjectProcessIndex(0);
        uintptr_t off_grp = (uintptr_t)RikkyGetMemoryData(0xbb);
        int grp_id = cur_obj ? *(int *)(cur_obj + off_grp) : 0;
        int self_idx = 0;
        int count = 0;
        int *p_num;
        uintptr_t *obj_ptrs;
        int i, n;

        if (grp_id == 0)
        {
            lua_pushinteger(L, 0);
            lua_pushinteger(L, 1);
            lua_pushinteger(L, 0);
            return 3;
        }

        p_num = (int *)RikkyGetMemoryData(0x93);
        n = p_num ? *p_num : 0;
        obj_ptrs = (uintptr_t *)RikkyGetMemoryData(0xae);
        if (obj_ptrs)
        {
            for (i = 0; i < n; i++)
            {
                uintptr_t obj_p = obj_ptrs[i];
                if (obj_p && *(int *)(obj_p + off_grp) == grp_id)
                {
                    if (obj_p == cur_obj)
                    {
                        self_idx = count;
                    }
                    count++;
                }
            }
        }
        lua_pushinteger(L, self_idx);
        lua_pushinteger(L, count);
        lua_pushinteger(L, grp_id);
        return 3;
    }

    if (strcmp(key, "root") == 0)
    {
        if ((uintptr_t)RikkyGetMemoryData(100) > 0x1du)
        {
            uintptr_t cur_obj = (uintptr_t)GetObjectProcessIndex(0);
            uintptr_t off_layer = (uintptr_t)RikkyGetMemoryData(0x67);
            if (cur_obj)
            {
                lua_pushnumber(L, (double)*(int *)(cur_obj + off_layer + 4u));
                return 1;
            }
        }
        lua_pushnil(L);
        return 1;
    }

    if (strcmp(key, "text") == 0 || strcmp(key, "txt") == 0)
    {
        wchar_t wbuf[0x401];
        const wchar_t *src_w = NULL;
        uint32_t layer_idx = (uint32_t)(opt - 1);
        size_t wlen = 0;
        int dummy_len = 0;
        char *sjis_str;

        memset(wbuf, 0, sizeof(wbuf));

        if (layer_idx < 100u)
        {
            int *p_num = (int *)RikkyGetMemoryData(0x93);
            int n = p_num ? *p_num : 0;
            uintptr_t *obj_ptrs = (uintptr_t *)RikkyGetMemoryData(0xae);
            uintptr_t off_layer = (uintptr_t)RikkyGetMemoryData(0x67);
            uintptr_t cur_obj = (uintptr_t)GetObjectProcessIndex(0);
            uint32_t cur_scene = 0xffffffffu;
            uint32_t cur_frame = 0;
            int i;

            if (cur_obj && (uintptr_t)RikkyGetMemoryData(100) > 0x1du)
            {
                cur_scene = *(uint32_t *)(cur_obj + off_layer + 4u);
            }

            if (cur_obj && *(int *)(cur_obj + 4) == -1)
            {
                uintptr_t *p_sub = (uintptr_t *)RikkyGetMemoryData(0x79);
                uintptr_t off_f = (uintptr_t)RikkyGetMemoryData(0x7a);
                if (p_sub && *p_sub)
                {
                    cur_frame = *(uint32_t *)(*p_sub + off_f);
                }
            }
            else
            {
                int *p_subwin = (int *)RikkyGetMemoryData(0x0b);
                int *p_playing = (int *)RikkyGetMemoryData(0x0a);
                if (p_subwin && *p_subwin == 1 && p_playing && *p_playing == 1)
                {
                    uint32_t *p_pf = (uint32_t *)RikkyGetMemoryData(0x09);
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
                        cur_frame = *(uint32_t *)(*p_editp + off_frame);
                    }
                }
            }

            for (i = 0; i < n; i++)
            {
                uintptr_t obj_p = obj_ptrs ? obj_ptrs[i] : 0;
                if (obj_p &&
                    *(int *)(obj_p + 0x54) == 3 &&
                    (uint32_t)*(uint8_t *)(obj_p + off_layer) == layer_idx &&
                    (cur_scene == 0xffffffffu || (uint32_t)*(uint8_t *)(obj_p + off_layer + 4u) == cur_scene) &&
                    *(uint32_t *)(obj_p + 8) <= cur_frame &&
                    cur_frame <= *(uint32_t *)(obj_p + 0xc))
                {
                    uintptr_t *p_exdata = (uintptr_t *)RikkyGetMemoryData(0x7b);
                    uintptr_t off_td = (uintptr_t)RikkyGetMemoryData(0x68);
                    if (p_exdata && *p_exdata)
                    {
                        src_w = (const wchar_t *)(*p_exdata + (uintptr_t)*(int *)(obj_p + off_td) + 0x34u);
                    }
                    break;
                }
            }

            if (!src_w)
            {
                lua_pushstring(L, "");
                return 1;
            }
        }
        else
        {
            src_w = (const wchar_t *)RikkyGetMemoryData(0x74);
            if (!src_w)
            {
                lua_pushstring(L, "");
                return 1;
            }
        }

        while (wlen < 0x400u)
        {
            wchar_t wc = src_w[wlen];
            if (wc == L'\0')
            {
                if (wlen == 0)
                {
                    lua_pushstring(L, "");
                    return 1;
                }
                break;
            }
            wbuf[wlen++] = wc;
        }
        wbuf[wlen] = L'\0';

        sjis_str = rm_wide_to_sjis_html(&dummy_len, wbuf);
        if (!sjis_str)
        {
            lua_pushstring(L, "");
            return 1;
        }

        if (layer_idx < 100u)
        {
            char *open_tag = strstr(sjis_str, "<?");
            char *close_tag = strstr(sjis_str, "?>");
            if (open_tag && close_tag && (close_tag - open_tag) > 0)
            {
                wchar_t *text_buf = (wchar_t *)RikkyGetMemoryData(0x74);
                uintptr_t *p_mes_ptr = (uintptr_t *)RikkyGetMemoryData(0xa7);
                uint32_t *p_mes_pos = (uint32_t *)RikkyGetMemoryData(0xa8);
                uint32_t *p_mes_cap = (uint32_t *)RikkyGetMemoryData(0xa9);

                if (text_buf && p_mes_ptr && p_mes_pos && p_mes_cap)
                {
                    size_t saved_len = 0;
                    size_t restore_count;
                    uintptr_t saved_a7 = *p_mes_ptr;
                    uint32_t saved_a8 = *p_mes_pos;
                    uint32_t saved_a9 = *p_mes_cap;

                    memset(wbuf, 0, sizeof(wbuf));
                    while (saved_len < 0x400u && text_buf[saved_len] != L'\0')
                    {
                        wbuf[saved_len] = text_buf[saved_len];
                        saved_len++;
                    }
                    restore_count = (saved_len < 0x400u) ? (saved_len + 1u) : 0x400u;

                    *p_mes_ptr = (uintptr_t)text_buf;
                    *p_mes_pos = 0;
                    *p_mes_cap = 0x3fffu;

                    while (open_tag != NULL)
                    {
                        close_tag = strstr(open_tag + 1, "?>");
                        if (!close_tag)
                        {
                            break;
                        }
                        *close_tag = '\0';
                        *p_mes_pos = 0;
                        text_buf[0] = L'\0';

                        if (luaL_loadstring(L, open_tag + 2) == 0)
                        {
                            int eval_len = 0;
                            char *eval_str;
                            size_t prefix_len;
                            size_t eval_slen;
                            size_t suffix_len;
                            char *merged;

                            lua_call(L, 0, 0);
                            *close_tag = '?';
                            *open_tag = '\0';
                            eval_str = rm_wide_to_sjis_html(&eval_len, text_buf);
                            prefix_len = strlen(sjis_str);
                            eval_slen = eval_str ? strlen(eval_str) : 0u;
                            suffix_len = strlen(close_tag + 2);
                            merged = (char *)rm_calloc(prefix_len + eval_slen + suffix_len + 1u, 1u);

                            if (!merged || !eval_str)
                            {
                                *open_tag = '<';
                                *p_mes_ptr = saved_a7;
                                *p_mes_pos = saved_a8;
                                *p_mes_cap = saved_a9;
                                memcpy(text_buf, wbuf, restore_count * sizeof(wchar_t));
                                lua_pushstring(L, sjis_str);
                                rm_free(sjis_str);
                                rm_free(eval_str);
                                rm_free(merged);
                                return 1;
                            }

                            memcpy(merged, sjis_str, prefix_len);
                            strcpy(merged + prefix_len, eval_str);
                            strcat(merged, close_tag + 2);
                            rm_free(sjis_str);
                            rm_free(eval_str);
                            sjis_str = merged;
                        }
                        else
                        {
                            lua_pop(L, 1);
                            *open_tag = '\0';
                            memmove(open_tag, close_tag + 2, strlen(close_tag + 2) + 1u);
                        }

                        open_tag = strstr(sjis_str, "<?");
                    }

                    *p_mes_ptr = saved_a7;
                    *p_mes_pos = saved_a8;
                    *p_mes_cap = saved_a9;
                    memcpy(text_buf, wbuf, restore_count * sizeof(wchar_t));
                }
            }
        }

        lua_pushstring(L, sjis_str);
        rm_free(sjis_str);
        return 1;
    }

    if (strcmp(key, "buffer") == 0 || strcmp(key, "buf") == 0)
    {
        const char *buf_str = (const char *)RikkyGetMemoryData(0x8a);
        lua_pushstring(L, buf_str ? buf_str : "");
        return 1;
    }

    if (strcmp(key, "dialog") == 0 || strcmp(key, "dlg") == 0)
    {
        int *p_dlg = (int *)RikkyGetMemoryData(0xa0);
        int *p_cur = (int *)RikkyGetMemoryData(0x99);
        int active = (p_dlg && *p_dlg != 0 && p_cur && *p_cur == *p_dlg) ? 1 : 0;
        lua_pushboolean(L, active);
        return 1;
    }

    if (strcmp(key, "object") == 0 || strcmp(key, "obj") == 0)
    {
        uintptr_t cur_obj = (uintptr_t)GetObjectProcessIndex(0);
        int canon_idx = -1;
        const char *obj_name = cur_obj ? GetObjectName(*(int *)(cur_obj + 0x54), NULL, &canon_idx) : "";

        if (opt == 1)
        {
            lua_pushinteger(L, canon_idx);
        }
        else
        {
            lua_pushstring(L, obj_name ? obj_name : "");
        }
        lua_createtable(L, 0, 0);

        if (!cur_obj)
        {
            return 2;
        }

        if (canon_idx == 0)
        {
            const char **pp_file = (const char **)RikkyGetMemoryData(0x6a);
            lua_pushstring(L, (pp_file && *pp_file) ? *pp_file : "");
            lua_setfield(L, -2, "file");
            lua_pushinteger(L, *(uint8_t *)(cur_obj + 0x3f8));
            lua_setfield(L, -2, "loop");
            lua_pushinteger(L, *(uint8_t *)(cur_obj + 0x3fc));
            lua_setfield(L, -2, "alphachannel");
            return 2;
        }

        if (canon_idx == 1)
        {
            uintptr_t *p_img = (uintptr_t *)RikkyGetMemoryData(0x69);
            lua_pushstring(L, (p_img && *p_img) ? (const char *)(*p_img + 4u) : "");
            lua_setfield(L, -2, "file");
            return 2;
        }

        if (canon_idx == 3)
        {
            const uint8_t **pp_txt = (const uint8_t **)RikkyGetMemoryData(0x89);
            const uint8_t *td = pp_txt ? *pp_txt : NULL;
            if (td)
            {
                uint32_t col1 = ((uint32_t)td[8] << 16) | ((uint32_t)td[9] << 8) | (uint32_t)td[10];
                uint32_t col2 = ((uint32_t)td[12] << 16) | ((uint32_t)td[13] << 8) | (uint32_t)td[14];

                lua_pushinteger(L, (lua_Integer)col1);
                lua_setfield(L, -2, "color");
                lua_pushinteger(L, (lua_Integer)col2);
                lua_setfield(L, -2, "color2");
                lua_pushinteger(L, td[0]);
                lua_setfield(L, -2, "type");
                lua_pushinteger(L, td[1]);
                lua_setfield(L, -2, "autoadjust");
                lua_pushinteger(L, td[2]);
                lua_setfield(L, -2, "soft");
                lua_pushinteger(L, td[3]);
                lua_setfield(L, -2, "monospace");
                lua_pushinteger(L, td[4]);
                lua_setfield(L, -2, "align");
                lua_pushinteger(L, (int)(int8_t)td[5]);
                lua_setfield(L, -2, "spacing_x");
                lua_pushinteger(L, (int)(int8_t)td[6]);
                lua_setfield(L, -2, "spacing_y");
                lua_pushinteger(L, td[7]);
                lua_setfield(L, -2, "precision");
                lua_pushstring(L, (const char *)(td + 0x10));
                lua_setfield(L, -2, "font");
            }
            lua_pushinteger(L, *(uint8_t *)(cur_obj + 0x3f8));
            lua_setfield(L, -2, "individual");
            lua_pushinteger(L, *(uint8_t *)(cur_obj + 0x3fc));
            lua_setfield(L, -2, "display");
            lua_pushinteger(L, *(uint8_t *)(cur_obj + 0x400));
            lua_setfield(L, -2, "autoscroll");
            lua_pushinteger(L, *(uint8_t *)(cur_obj + 0x404));
            lua_setfield(L, -2, "bold");
            lua_pushinteger(L, *(uint8_t *)(cur_obj + 0x408));
            lua_setfield(L, -2, "italic");
            return 2;
        }

        if (canon_idx == 4)
        {
            const uint8_t **pp_fig = (const uint8_t **)RikkyGetMemoryData(0x7c);
            const uint8_t *fd = pp_fig ? *pp_fig : NULL;
            if (fd)
            {
                uint32_t col;
                switch (fd[0])
                {
                case 0:
                    if (fd[8] == '\0')
                    {
                        lua_pushstring(L, k_fig_bg);
                    }
                    else if (fd[8] == '*')
                    {
                        lua_pushstring(L, (const char *)(fd + 9));
                    }
                    else
                    {
                        lua_pushstring(L, (const char *)(fd + 8));
                    }
                    lua_setfield(L, -2, "figure");
                    break;
                case 1:
                    lua_pushstring(L, k_fig_circle);
                    lua_setfield(L, -2, "figure");
                    break;
                case 2:
                    lua_pushstring(L, k_fig_rect);
                    lua_setfield(L, -2, "figure");
                    break;
                case 3:
                    lua_pushstring(L, k_fig_tri);
                    lua_setfield(L, -2, "figure");
                    break;
                case 4:
                    lua_pushstring(L, k_fig_pent);
                    lua_setfield(L, -2, "figure");
                    break;
                case 5:
                    lua_pushstring(L, k_fig_hex);
                    lua_setfield(L, -2, "figure");
                    break;
                case 6:
                    lua_pushstring(L, k_fig_star);
                    lua_setfield(L, -2, "figure");
                    break;
                default:
                    break;
                }
                col = ((uint32_t)fd[4] << 16) | ((uint32_t)fd[5] << 8) | (uint32_t)fd[6];
                lua_pushinteger(L, (lua_Integer)col);
                lua_setfield(L, -2, "color");
            }
            return 2;
        }

        if (canon_idx == 5)
        {
            lua_pushinteger(L, *(uint8_t *)(cur_obj + 0x3f8));
            lua_setfield(L, -2, "bufferclear");
            return 2;
        }

        if (canon_idx == 6)
        {
            const uint8_t **pp_aud = (const uint8_t **)RikkyGetMemoryData(0x8c);
            const uint8_t *ad = pp_aud ? *pp_aud : NULL;

            lua_pushinteger(L, *(uint8_t *)(cur_obj + 0x404));
            lua_setfield(L, -2, "projectsound");
            if (ad)
            {
                uint32_t col = ((uint32_t)ad[0x124] << 16) | ((uint32_t)ad[0x125] << 8) | (uint32_t)ad[0x126];
                lua_pushstring(L, (const char *)ad);
                lua_setfield(L, -2, "file");
                lua_pushinteger(L, ad[0x118]);
                lua_setfield(L, -2, "type");
                lua_pushinteger(L, ad[0x11a]);
                lua_setfield(L, -2, "mode");
                lua_pushinteger(L, ad[0x11c]);
                lua_setfield(L, -2, "res_w");
                lua_pushinteger(L, ad[0x11e]);
                lua_setfield(L, -2, "res_h");
                lua_pushinteger(L, ad[0x120]);
                lua_setfield(L, -2, "pad_w");
                lua_pushinteger(L, ad[0x122]);
                lua_setfield(L, -2, "pad_h");
                lua_pushinteger(L, ad[300]);
                lua_setfield(L, -2, "mirror");
                lua_pushinteger(L, (lua_Integer)col);
                lua_setfield(L, -2, "color");
            }
            return 2;
        }

        if (canon_idx == 7)
        {
            uintptr_t *p_exdata = (uintptr_t *)RikkyGetMemoryData(0x7b);
            uintptr_t *p_cur = (uintptr_t *)RikkyGetMemoryData(0x66);
            uintptr_t off_td = (uintptr_t)RikkyGetMemoryData(0x68);

            lua_pushinteger(L, *(uint8_t *)(cur_obj + 0x3f8));
            lua_setfield(L, -2, "loop");
            if (p_exdata && *p_exdata && p_cur && *p_cur)
            {
                int scene_no = *(int *)(*p_exdata + (uintptr_t)*(int *)(*p_cur + off_td) + 4u);
                lua_pushinteger(L, scene_no);
                lua_setfield(L, -2, "scene_number");
            }
            return 2;
        }

        if (canon_idx == 0x5f)
        {
            lua_pushinteger(L, *(uint8_t *)(cur_obj + 0x3f8));
            lua_setfield(L, -2, "zbuffer");
            return 2;
        }

        return 2;
    }

    if (strcmp(key, "start_end") == 0 || strcmp(key, "sted") == 0 || strcmp(key, "end") == 0)
    {
        uintptr_t cur_obj = (uintptr_t)GetObjectProcessIndex(0);
        int s_frame = 0;
        int e_frame = 0;

        if (cur_obj)
        {
            int mid = *(int *)(cur_obj + 0x50);
            if (mid < 0 || mid == 0xffffff)
            {
                s_frame = *(int *)(cur_obj + 8);
                e_frame = *(int *)(cur_obj + 0xc);
            }
            else
            {
                uintptr_t *p_base = (uintptr_t *)RikkyGetMemoryData(0x6c);
                uint32_t *p_num = (uint32_t *)RikkyGetMemoryData(0x93);
                uintptr_t stride = (uintptr_t)RikkyGetMemoryData(0x6d);
                uintptr_t *obj_ptrs = (uintptr_t *)RikkyGetMemoryData(0xae);

                if (p_base && *p_base && p_num && obj_ptrs)
                {
                    uintptr_t head_obj = *p_base + stride * (size_t)mid;
                    uint32_t n = *p_num;
                    uint32_t i;

                    s_frame = *(int *)(head_obj + 8);
                    e_frame = *(int *)(head_obj + 0xc);
                    for (i = 0; i < n; i++)
                    {
                        if (obj_ptrs[i] == head_obj)
                        {
                            uint32_t j = i + 1u;
                            while (j < n && obj_ptrs[j] && *(int *)(obj_ptrs[j] + 0x50) == mid)
                            {
                                e_frame = *(int *)(obj_ptrs[j] + 0xc);
                                j++;
                            }
                            break;
                        }
                    }
                }
            }
        }

        if (key[0] == 'e')
        {
            lua_pushinteger(L, e_frame);
            return 1;
        }
        lua_pushinteger(L, s_frame);
        lua_pushinteger(L, e_frame);
        return 2;
    }

    if (strcmp(key, "filter") == 0 || strcmp(key, "effect") == 0)
    {
        uint32_t *p_mode8e = (uint32_t *)RikkyGetMemoryData(0x8e);
        uint32_t mode8e = p_mode8e ? *p_mode8e : 0u;
        uintptr_t cur_obj;
        const char *head_name;
        uintptr_t *p_exbase;
        uintptr_t *p_cur_ex;
        int self_idx = 0;
        int out_count = 0;
        int slot_idx = 0;

        if ((uintptr_t)RikkyGetMemoryData(0x66) < 0x1du)
        {
            mode8e &= 1u;
        }

        cur_obj = (uintptr_t)GetObjectProcessIndex(0);
        head_name = cur_obj ? GetObjectName(*(int *)(cur_obj + 0x54), NULL, NULL) : "";
        if (head_name && strcmp(k_name_custom_obj, head_name) == 0)
        {
            p_exbase = (uintptr_t *)RikkyGetMemoryData(0x87);
        }
        else
        {
            p_exbase = (uintptr_t *)RikkyGetMemoryData(0x7c);
        }

        if (cur_obj && *(int *)(cur_obj + 0x50) != -1)
        {
            uintptr_t stride = (uintptr_t)RikkyGetMemoryData(0x6d);
            uintptr_t *p_base = (uintptr_t *)RikkyGetMemoryData(0x6c);
            if (p_base && *p_base)
            {
                cur_obj = *p_base + stride * (size_t)(*(int *)(cur_obj + 0x50));
            }
        }

        p_cur_ex = (uintptr_t *)RikkyGetMemoryData(0x8d);
        lua_createtable(L, 0, 0);

        while (cur_obj)
        {
            uint32_t flag = 0;
            int eff_id = *(int *)(cur_obj + 0x60u + (size_t)slot_idx * 0x0cu);
            const char *eff_name = GetObjectName(eff_id, &flag, NULL);

            if (flag != 3u || !eff_name)
            {
                break;
            }

            if (strcmp(k_name_anim_eff, eff_name) == 0)
            {
                uintptr_t ex_off = (uintptr_t)*(int *)(cur_obj + 0x68u + (size_t)slot_idx * 0x0cu);
                const uint8_t *anim_ex = (p_exbase && *p_exbase) ? (const uint8_t *)(*p_exbase + ex_off) : NULL;

                if (p_cur_ex && anim_ex && (const uint8_t *)(*p_cur_ex) == anim_ex && mode8e != 0u)
                {
                    self_idx = out_count + 1;
                }

                if (anim_ex && anim_ex[4] != '\0')
                {
                    lua_pushstring(L, (const char *)(anim_ex + 4));
                }
                else if (anim_ex && anim_ex[0] < 25u)
                {
                    lua_pushstring(L, k_anim_names[anim_ex[0]]);
                }
                else
                {
                    lua_pushstring(L, k_name_anim_eff);
                }
            }
            else if (strcmp(k_name_script_ctrl, eff_name) == 0)
            {
                if (mode8e == 0u)
                {
                    self_idx = out_count + 1;
                }
                lua_pushstring(L, k_name_script_ctrl);
            }
            else
            {
                lua_pushstring(L, eff_name);
            }

            if (opt == 1 && (*(uint8_t *)(cur_obj + 0xe5u + (size_t)slot_idx) & 1u) == 0u)
            {
                lua_remove(L, -1);
            }
            else
            {
                out_count++;
                lua_rawseti(L, -2, out_count);
            }

            slot_idx++;
        }

        lua_pushinteger(L, self_idx);
        lua_pushinteger(L, out_count);
        return 3;
    }

    if (strcmp(key, "shadow") == 0 || strcmp(key, "antialias") == 0 ||
        strcmp(key, "culling") == 0 || strcmp(key, "billboard") == 0 ||
        strcmp(key, "force") == 0)
    {
        uintptr_t *p_ctx = (uintptr_t *)RikkyGetMemoryData(0x72);
        uintptr_t off_flags = (uintptr_t)RikkyGetMemoryData(0x73);
        const uint8_t *flags_p = (p_ctx && *p_ctx) ? (const uint8_t *)(*p_ctx + off_flags) : NULL;
        uint32_t res = 0;

        if (!flags_p)
        {
            lua_pushinteger(L, 0);
            return 1;
        }

        if (key[0] == 'c')
        {
            uintptr_t cur_obj = (uintptr_t)GetObjectProcessIndex(0);
            int canon_idx = -1;
            uintptr_t cull_off = 0;

            if (cur_obj)
            {
                GetObjectName(*(int *)(cur_obj + 0x54), NULL, &canon_idx);
            }

            switch (canon_idx)
            {
            case 0:
                cull_off = 0x408u;
                break;
            case 1:
            case 5:
                cull_off = 0x400u;
                break;
            case 3:
            case 6:
                cull_off = 0x410u;
                break;
            case 4:
            case 7:
            case 0x50:
                cull_off = 0x404u;
                break;
            case 9:
                cull_off = 0x3fcu;
                break;
            default:
                res = (flags_p[0] >> 3) & 1u;
                lua_pushinteger(L, (lua_Integer)res);
                return 1;
            }

            {
                uintptr_t *p_cur = (uintptr_t *)RikkyGetMemoryData(0x66);
                if (p_cur && *p_cur && *(int8_t *)(*p_cur + cull_off) == 1)
                {
                    lua_pushinteger(L, 1);
                    return 1;
                }
            }
            res = (flags_p[0] >> 3) & 1u;
        }
        else if (key[0] == 'b')
        {
            uint8_t b = flags_p[0];
            if ((b & 1u) != 0u)
            {
                res = 3u;
            }
            else if ((b & 2u) != 0u)
            {
                res = 2u;
            }
            else if ((b & 4u) != 0u)
            {
                res = 1u;
            }
            else
            {
                res = 0u;
            }
        }
        else if (key[0] == 'a')
        {
            res = (~(uint32_t)(flags_p[0] >> 4)) & 1u;
        }
        else if (key[0] == 'f')
        {
            res = (uint32_t)flags_p[2] & 1u;
        }
        else
        {
            res = (~(uint32_t)(flags_p[0] >> 5)) & 1u;
        }

        lua_pushinteger(L, (lua_Integer)res);
        return 1;
    }

    if (strcmp(key, "count") == 0)
    {
        uint32_t *p_cnt = (uint32_t *)RikkyGetMemoryData(0x19);
        lua_pushinteger(L, p_cnt ? (lua_Integer)(*p_cnt) : 0);
        return 1;
    }

    if (strcmp(key, "input") == 0)
    {
        int *p_num = (int *)RikkyGetMemoryData(0x0c);
        int num_plugins = p_num ? *p_num : 0;
        uintptr_t entry = (uintptr_t)RikkyGetMemoryData(0x0d);
        char filter_buf[4096];
        char seen_buf[2048];
        char ext_buf[256];
        size_t pos = 0;
        size_t seen_pos = 0;
        int p_idx;
        int out_count = 0;
        size_t i = 0;
        size_t start_idx = 1;

        for (p_idx = 0; p_idx < num_plugins + 2 && entry != 0; p_idx++)
        {
            const char *s = (const char *)entry;
            size_t j = 0;
            while (s[j] != '\0')
            {
                while (s[j + 1] != '\0')
                {
                    j++;
                }
                for (j += 2; s[j] != '\0'; j++)
                {
                    if (pos + 2 < sizeof(filter_buf))
                    {
                        filter_buf[pos++] = s[j];
                    }
                }
                j++;
            }
            entry += 0x49cu;
        }
        filter_buf[pos] = '*';
        filter_buf[pos + 1] = '\0';

        memset(seen_buf, 0, sizeof(seen_buf));
        lua_createtable(L, 0, 0);

        while (filter_buf[i] != '\0')
        {
            size_t next_i = i + 1;
            size_t next_start = start_idx;

            if (filter_buf[i] == '*')
            {
                if (start_idx + 1 == i)
                {
                    next_start = start_idx + 3;
                    next_i = start_idx + 3;
                }
                else
                {
                    size_t k = 0;
                    size_t ext_len;
                    while (start_idx + k < i && k + 1 < sizeof(ext_buf))
                    {
                        ext_buf[k] = filter_buf[start_idx + k];
                        k++;
                    }
                    next_start = i + 1;
                    next_i = next_start;
                    ext_len = (k > 0 && ext_buf[k - 1] == ';') ? (k - 1) : k;
                    ext_buf[ext_len] = '\0';

                    if (strstr(seen_buf, ext_buf) == NULL)
                    {
                        if (seen_pos + ext_len < sizeof(seen_buf))
                        {
                            memcpy(seen_buf + seen_pos, ext_buf, ext_len);
                            seen_pos += ext_len;
                            seen_buf[seen_pos] = '\0';
                        }
                        lua_pushstring(L, ext_buf);
                        out_count++;
                        lua_rawseti(L, -2, out_count);
                    }
                }
            }

            start_idx = next_start;
            i = next_i;
        }

        lua_pushinteger(L, out_count);
        return 2;
    }

    if (strcmp(key, "font") == 0)
    {
        HFONT *p_hfont = (HFONT *)RikkyGetMemoryData(0xc2);
        const uint8_t *p_col = (const uint8_t *)RikkyGetMemoryData(0xc1);
        const int *p_type = (const int *)RikkyGetMemoryData(0xc6);
        LOGFONTA lf;

        memset(&lf, 0, sizeof(lf));
        if (p_hfont && *p_hfont)
        {
            GetObjectA(*p_hfont, sizeof(lf), &lf);
        }

        lua_createtable(L, 0, 0);
        lua_pushstring(L, lf.lfFaceName);
        lua_setfield(L, -2, "name");
        lua_pushinteger(L, -((int)lf.lfHeight / 2));
        lua_setfield(L, -2, "size");
        lua_pushboolean(L, lf.lfWeight > 400);
        lua_setfield(L, -2, "bold");
        lua_pushboolean(L, lf.lfItalic != 0);
        lua_setfield(L, -2, "italic");
        if (p_col)
        {
            uint32_t c1 = ((uint32_t)p_col[0] << 16) | ((uint32_t)p_col[1] << 8) | (uint32_t)p_col[2];
            uint32_t c2 = ((uint32_t)p_col[4] << 16) | ((uint32_t)p_col[5] << 8) | (uint32_t)p_col[6];
            lua_pushinteger(L, (lua_Integer)c1);
            lua_setfield(L, -2, "color");
            lua_pushinteger(L, (lua_Integer)c2);
            lua_setfield(L, -2, "color2");
        }
        lua_pushinteger(L, p_type ? *p_type : 0);
        lua_setfield(L, -2, "type");
        return 1;
    }

    if (strcmp(key, "draw_state") == 0)
    {
        int *p_ds = (int *)RikkyGetMemoryData(0x6e);
        lua_pushboolean(L, p_ds ? *p_ds : 0);
        return 1;
    }

    if (strcmp(key, "cache") == 0)
    {
        uintptr_t cache_tbl = (uintptr_t)RikkyGetMemoryData(0x9d);
        uintptr_t stride = (uintptr_t)RikkyGetMemoryData(0x9e);
        int count = 0;

        lua_settop(L, 0);
        lua_createtable(L, 0, 0);

        if (cache_tbl && stride)
        {
            uintptr_t p;
            for (p = cache_tbl + 0x239u;
                 *(int *)(p - 0x249u) != 0 && *(const char *)(p - 0x235u) == '*';
                 p += stride)
            {
                uint32_t sub_cnt;
                if (*(const char *)(p - 0x234u) == 'l')
                {
                    count++;
                    lua_pushstring(L, (const char *)(p - 0x230u));
                    lua_rawseti(L, 1, count);
                }
                sub_cnt = *(uint32_t *)(p - 0x12du);
                if (sub_cnt != 0u)
                {
                    uint32_t idx = 1u;
                    uintptr_t sub_p;
                    for (sub_p = p;
                         idx <= sub_cnt && *(uint32_t *)(sub_p - 5u) == 0x61756c2au;
                         sub_p += 0x114u)
                    {
                        count++;
                        lua_pushstring(L, (const char *)sub_p);
                        lua_rawseti(L, 1, count);
                        idx++;
                    }
                }
            }
        }

        lua_pushinteger(L, count);
        lua_insert(L, 1);
        return 2;
    }

    if (strcmp(key, "hwnd") == 0)
    {
        HWND *p_hwnd = NULL;
        HWND hwnd = NULL;
        char title[268];

        if (opt == 4 || opt == 5)
        {
            RmHwndEnumCtx ctx;
            p_hwnd = (HWND *)RikkyGetMemoryData(0x0e);
            hwnd = p_hwnd ? *p_hwnd : NULL;

            lua_createtable(L, 0, 0);
            lua_createtable(L, 0, 0);
            lua_pushinteger(L, (lua_Integer)(intptr_t)hwnd);
            lua_rawseti(L, -3, 1);
            title[0] = '\0';
            GetWindowTextA(hwnd, title, 0x105);
            lua_pushstring(L, title);
            lua_rawseti(L, -2, 1);

            ctx.main_hwnd = hwnd;
            ctx.L = L;
            ctx.count = 1;
            ctx.mode = opt;
            ctx.pid = 0;
            GetWindowThreadProcessId(hwnd, &ctx.pid);
            EnumWindows(enum_hwnd_proc, (LPARAM)&ctx);
            lua_pushinteger(L, ctx.count);
            return 3;
        }

        if (opt == 1)
        {
            p_hwnd = (HWND *)RikkyGetMemoryData(0x76);
        }
        else if (opt == 2)
        {
            p_hwnd = (HWND *)RikkyGetMemoryData(0x0f);
        }
        else if (opt == 3)
        {
            p_hwnd = (HWND *)RikkyGetMemoryData(0x77);
        }
        else
        {
            p_hwnd = (HWND *)RikkyGetMemoryData(0x0e);
        }

        hwnd = p_hwnd ? *p_hwnd : NULL;
        lua_pushinteger(L, (lua_Integer)(intptr_t)hwnd);
        title[0] = '\0';
        GetWindowTextA(hwnd, title, 0x105);
        lua_pushstring(L, title);
        return 2;
    }

    lua_pushnil(L);
    return 1;
}

static RmDir *rm_opendir(const char *path)
{
    char full[MAX_PATH + 4];
    size_t len;
    RmDir *dir;
    WIN32_FIND_DATAA ffd;
    HANDLE hfind;

    if (!path)
    {
        errno = EINVAL;
        return NULL;
    }
    if (path[0] == '\0')
    {
        errno = ENOENT;
        return NULL;
    }

    full[0] = '\0';
    _fullpath(full, path, MAX_PATH);
    len = strlen(full);
    if (len > 0 && full[len - 1] != '\\' && full[len - 1] != '/')
    {
        full[len++] = '\\';
    }
    full[len] = '*';
    full[len + 1] = '\0';

    dir = (RmDir *)rm_malloc(sizeof(RmDir));
    if (!dir)
    {
        errno = ENOMEM;
        return NULL;
    }

    hfind = FindFirstFileA(full, &ffd);
    if (hfind == INVALID_HANDLE_VALUE)
    {
        DWORD err = GetLastError();
        if (err == ERROR_PATH_NOT_FOUND || err == ERROR_FILE_NOT_FOUND)
        {
            errno = ENOENT;
        }
        else if (err == ERROR_DIRECTORY)
        {
            errno = ENOTDIR;
        }
        else
        {
            errno = EINVAL;
        }
        rm_free(dir);
        return NULL;
    }

    dir->handle = hfind;
    dir->index = 0;
    strncpy(dir->d_name, ffd.cFileName, MAX_PATH - 1);
    dir->d_name[MAX_PATH - 1] = '\0';
    return dir;
}

static RmDir *rm_readdir(RmDir *dir)
{
    if (!dir)
    {
        errno = EBADF;
        return NULL;
    }
    if (dir->index == 0)
    {
        dir->index = 1;
        return dir;
    }

    {
        WIN32_FIND_DATAA ffd;
        dir->index++;
        if (!FindNextFileA(dir->handle, &ffd))
        {
            return NULL;
        }
        strncpy(dir->d_name, ffd.cFileName, MAX_PATH - 1);
        dir->d_name[MAX_PATH - 1] = '\0';
        return dir;
    }
}

static void rm_closedir(RmDir *dir)
{
    if (!dir)
    {
        return;
    }
    if (dir->handle != INVALID_HANDLE_VALUE)
    {
        FindClose(dir->handle);
    }
    rm_free(dir);
}

int __cdecl lua_rikky_dir(lua_State *L)
{
    int nargs = lua_gettop(L);
    const char *raw_path;
    size_t path_len;
    char input_path[MAX_PATH * 2];
    char **exts = NULL;
    int ext_count = 0;
    int filter_mode = 0;
    RmDir *dir = NULL;
    char *prefix = NULL;
    int i;

    if (nargs == 0 || lua_type(L, 1) != LUA_TSTRING)
    {
        lua_settop(L, 0);
        lua_createtable(L, 0, 0);
        return 1;
    }

    raw_path = lua_tolstring(L, 1, NULL);
    if (!raw_path)
    {
        lua_settop(L, 0);
        lua_createtable(L, 0, 0);
        return 1;
    }
    path_len = strlen(raw_path);
    if (path_len < 3 || path_len >= sizeof(input_path))
    {
        lua_settop(L, 0);
        lua_createtable(L, 0, 0);
        return 1;
    }
    memcpy(input_path, raw_path, path_len + 1);

    if (nargs == 2 && lua_type(L, 2) == LUA_TSTRING)
    {
        const char *s2 = lua_tolstring(L, 2, NULL);
        if (s2 && s2[0] == '\0')
        {
            filter_mode = 1;
        }
        else if (s2 && strcmp(s2, "*all") == 0)
        {
            filter_mode = 2;
        }
    }

    if (filter_mode != 0)
    {
        exts = (char **)rm_malloc(sizeof(char *));
        if (!exts)
        {
            lua_settop(L, 0);
            lua_createtable(L, 0, 0);
            return 1;
        }
        exts[0] = (char *)rm_malloc(1);
        if (!exts[0])
        {
            rm_free(exts);
            lua_settop(L, 0);
            lua_createtable(L, 0, 0);
            return 1;
        }
        exts[0][0] = '\0';
        ext_count = 1;
        lua_settop(L, 0);
        lua_createtable(L, 0, 0);
    }
    else
    {
        int max_exts = nargs - 1;
        if (max_exts < 1)
        {
            lua_settop(L, 0);
            lua_createtable(L, 0, 0);
            return 1;
        }
        exts = (char **)rm_malloc((size_t)max_exts * sizeof(char *));
        if (!exts)
        {
            lua_settop(L, 0);
            lua_createtable(L, 0, 0);
            return 1;
        }

        for (i = 2; i <= nargs; i++)
        {
            if (lua_type(L, i) == LUA_TSTRING)
            {
                const char *es = lua_tolstring(L, i, NULL);
                size_t elen;
                char *copy;
                int j;
                int dup = 0;

                if (!es)
                {
                    continue;
                }
                if (es[0] == '.')
                {
                    es++;
                }
                elen = strlen(es);
                copy = (char *)rm_malloc(elen + 1);
                if (!copy)
                {
                    for (j = 0; j < ext_count; j++)
                    {
                        rm_free(exts[j]);
                    }
                    rm_free(exts);
                    lua_settop(L, 0);
                    lua_createtable(L, 0, 0);
                    return 1;
                }
                memcpy(copy, es, elen + 1);
                rm_str_tolower(copy);

                for (j = 0; j < ext_count; j++)
                {
                    if (strcmp(exts[j], copy) == 0)
                    {
                        dup = 1;
                        break;
                    }
                }
                if (dup)
                {
                    rm_free(copy);
                }
                else
                {
                    exts[ext_count++] = copy;
                }
            }
        }

        lua_settop(L, 0);
        lua_createtable(L, 0, 0);
        if (ext_count == 0)
        {
            rm_free(exts);
            return 1;
        }
    }

    dir = rm_opendir(input_path);
    if (!dir)
    {
        char dir_part[273];
        size_t dlen;
        dir_part[0] = '\0';
        _splitpath(input_path, NULL, dir_part, NULL, NULL);
        dlen = strlen(dir_part);
        prefix = (char *)rm_malloc(dlen + 1);
        if (prefix)
        {
            memcpy(prefix, dir_part, dlen + 1);
            dir = rm_opendir(prefix);
            if (!dir)
            {
                rm_free(prefix);
                prefix = NULL;
            }
        }
    }
    else
    {
        prefix = (char *)rm_malloc(path_len + 2);
        if (prefix)
        {
            size_t plen = path_len;
            memcpy(prefix, input_path, plen);
            if (prefix[plen - 1] != '\\' ||
                (plen > 2 &&
                 (uint8_t)((uint8_t)prefix[plen - 2] - 0xa1u) > 0x40u &&
                 (uint8_t)prefix[plen - 2] > 0x7fu))
            {
                prefix[plen++] = '\\';
            }
            prefix[plen] = '\0';
        }
        else
        {
            rm_closedir(dir);
            dir = NULL;
        }
    }

    if (dir && prefix)
    {
        int out_idx = (filter_mode == 1) ? -2 : 0;
        char entry_path[MAX_PATH * 2];

        for (;;)
        {
            RmDir *ent = rm_readdir(dir);
            RmDir *sub;

            if (!ent)
            {
                break;
            }

            sprintf(entry_path, "%s%s", prefix, ent->d_name);
            errno = 0;
            sub = rm_opendir(entry_path);
            if (sub != NULL)
            {
                if (filter_mode == 1)
                {
                    out_idx++;
                    if (out_idx > 0)
                    {
                        lua_pushstring(L, entry_path);
                        lua_rawseti(L, 1, out_idx);
                    }
                }
                rm_closedir(sub);
                continue;
            }

            if (errno == 0)
            {
                continue;
            }

            if (filter_mode == 2)
            {
                out_idx++;
                lua_pushstring(L, entry_path);
                lua_rawseti(L, 1, out_idx);
            }
            else if (filter_mode == 0)
            {
                char *dot = strrchr(ent->d_name, '.');
                if (dot != NULL)
                {
                    rm_str_tolower(dot);
                    for (i = 0; i < ext_count; i++)
                    {
                        if (strcmp(dot + 1, exts[i]) == 0)
                        {
                            out_idx++;
                            lua_pushstring(L, entry_path);
                            lua_rawseti(L, 1, out_idx);
                            break;
                        }
                    }
                }
            }
        }

        rm_closedir(dir);
        rm_free(prefix);
    }

    for (i = 0; i < ext_count; i++)
    {
        rm_free(exts[i]);
    }
    rm_free(exts);
    return 1;
}

int __cdecl lua_rikky_ext(lua_State *L)
{
    int nargs = lua_gettop(L);
    const char *first_arg;
    int req_cats[4];
    int req_count = 0;
    char exe_path[261];
    char dir_part[261];
    char ini_path[273];
    FILE *fp;
    const char *cat_labels[8];
    int i;

    if (nargs == 0)
    {
        return 0;
    }
    if (nargs > 8)
    {
        nargs = 8;
    }

    first_arg = lua_tolstring(L, 1, NULL);
    if (first_arg && strcmp(first_arg, "*") == 0)
    {
        req_cats[0] = 0;
        req_cats[1] = 1;
        req_cats[2] = 2;
        req_cats[3] = 3;
        req_count = 4;
    }
    else if (first_arg && strcmp(first_arg, ".*") == 0)
    {
        req_cats[0] = 4;
        req_cats[1] = 5;
        req_cats[2] = 6;
        req_cats[3] = 7;
        req_count = 4;
    }
    else
    {
        int seen_img = 0;
        int seen_mov = 0;
        int seen_aud = 0;
        int seen_txt = 0;

        for (i = 1; i <= nargs; i++)
        {
            const char *raw_s = lua_tolstring(L, i, NULL);
            char buf[64];
            const char *s;
            int base_idx;

            if (!raw_s)
            {
                continue;
            }
            strncpy(buf, raw_s, sizeof(buf) - 1);
            buf[sizeof(buf) - 1] = '\0';
            rm_str_tolower(buf);
            s = buf;

            if (s[0] == '.')
            {
                s++;
                base_idx = 4;
            }
            else
            {
                base_idx = 0;
            }

            if (!seen_img &&
                (strcmp(s, k_cat_image) == 0 || strcmp(s, "image") == 0 || strcmp(s, "img") == 0))
            {
                seen_img = 1;
                req_cats[req_count++] = base_idx + 0;
            }
            else if (!seen_mov &&
                     (strcmp(s, k_cat_movie) == 0 || strcmp(s, "movie") == 0 ||
                      strcmp(s, "mov") == 0 || strcmp(s, "video") == 0))
            {
                seen_mov = 1;
                req_cats[req_count++] = base_idx + 1;
            }
            else if (!seen_aud &&
                     (strcmp(s, k_cat_audio) == 0 || strcmp(s, "audio") == 0 || strcmp(s, "aud") == 0))
            {
                seen_aud = 1;
                req_cats[req_count++] = base_idx + 2;
            }
            else if (!seen_txt &&
                     (strcmp(s, k_cat_text) == 0 || strcmp(s, "text") == 0 || strcmp(s, "txt") == 0))
            {
                seen_txt = 1;
                req_cats[req_count++] = base_idx + 3;
            }
        }

        if (req_count == 0)
        {
            return 0;
        }
    }

    exe_path[0] = '\0';
    dir_part[0] = '\0';
    GetModuleFileNameA(NULL, exe_path, MAX_PATH);
    _splitpath(exe_path, NULL, dir_part, NULL, NULL);
    sprintf(ini_path, "%s\\exedit.ini", dir_part);
    fp = fopen(ini_path, "r");
    if (!fp)
    {
        sprintf(ini_path, "%s\\plugins\\exedit.ini", dir_part);
        fp = fopen(ini_path, "r");
        if (!fp)
        {
            return 0;
        }
    }
    fclose(fp);

    cat_labels[0] = k_cat_image;
    cat_labels[1] = k_cat_movie;
    cat_labels[2] = k_cat_audio;
    cat_labels[3] = k_cat_text;
    cat_labels[4] = k_cat_image;
    cat_labels[5] = k_cat_movie;
    cat_labels[6] = k_cat_audio;
    cat_labels[7] = k_cat_text;

    lua_settop(L, 0);
    for (i = 0; i < req_count; i++)
    {
        int cat = req_cats[i];
        int idx = 0;
        char line[256];

        lua_createtable(L, 0, 0);
        fp = fopen(ini_path, "r");
        if (fp)
        {
            while (fgets(line, sizeof(line), fp) != NULL)
            {
                if (strstr(line, cat_labels[cat]) != NULL)
                {
                    char *tok = strtok(line, "=");
                    if (tok)
                    {
                        lua_pushstring(L, tok + (cat < 4 ? 1 : 0));
                        idx++;
                        lua_rawseti(L, -2, idx);
                    }
                }
            }
            fclose(fp);
        }
    }

    return (req_count > 0) ? req_count : 1;
}

int __cdecl lua_rikky_find(lua_State *L)
{
    const char *s1;
    const char *s2;
    const char *found;

    if (!lua_isstring(L, 1) && !lua_isstring(L, 2))
    {
        lua_settop(L, 0);
        lua_pushnil(L);
        return 1;
    }

    s1 = lua_tolstring(L, 1, NULL);
    s2 = lua_tolstring(L, 2, NULL);
    if (s1 && s2)
    {
        found = strstr(s1, s2);
        if (found != NULL)
        {
            lua_settop(L, 0);
            lua_pushinteger(L, (lua_Integer)((found - s1) + 1));
            return 1;
        }
    }

    lua_settop(L, 0);
    lua_pushnil(L);
    return 1;
}

int __cdecl lua_rikky_type(lua_State *L)
{
    int n = lua_gettop(L);
    int i;

    for (i = 0; i < n; i++)
    {
        int tp = lua_type(L, 1);
        lua_pushstring(L, lua_typename(L, tp));
        lua_remove(L, 1);
    }
    return n;
}

int __cdecl lua_rikky_key(lua_State *L)
{
    int nargs = lua_gettop(L);
    uint8_t *vkeys;
    uint32_t mask = 0x8000u;
    int has_joypad = 0;
    int joypad_idx = 0;
    int count;
    int i;

    if (nargs <= 0)
    {
        return 0;
    }

    vkeys = (uint8_t *)rm_calloc((size_t)nargs + 1u, 1u);
    if (!vkeys)
    {
        return 0;
    }

    count = nargs;
    for (i = 1; i <= count; i++)
    {
        int tp = lua_type(L, i);
        if (tp == LUA_TNUMBER)
        {
            vkeys[i] = (uint8_t)lua_tointeger(L, i);
        }
        else if (tp == LUA_TSTRING)
        {
            const char *s = lua_tolstring(L, i, NULL);
            size_t slen = s ? strlen(s) : 0u;

            if (slen == 1u)
            {
                unsigned char ch = (unsigned char)toupper((unsigned char)s[0]);
                if ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9'))
                {
                    vkeys[i] = ch;
                }
                else
                {
                    vkeys[i] = 0;
                }
            }
            else if (s)
            {
                if (strcmp(s, "\x81\xa9") == 0)
                {
                    vkeys[i] = VK_LEFT;
                }
                else if (strcmp(s, "\x81\xaa") == 0)
                {
                    vkeys[i] = VK_UP;
                }
                else if (strcmp(s, "\x81\xa8") == 0)
                {
                    vkeys[i] = VK_RIGHT;
                }
                else if (strcmp(s, "\x81\xab") == 0)
                {
                    vkeys[i] = VK_DOWN;
                }
                else if (strcmp(s, "LB") == 0)
                {
                    vkeys[i] = VK_LBUTTON;
                }
                else if (strcmp(s, "RB") == 0)
                {
                    vkeys[i] = VK_RBUTTON;
                }
                else if (strcmp(s, "RETURN") == 0 || strcmp(s, "ENTER") == 0)
                {
                    vkeys[i] = VK_RETURN;
                }
                else if (strcmp(s, "SHIFT") == 0)
                {
                    vkeys[i] = VK_SHIFT;
                }
                else if (strcmp(s, "CTRL") == 0)
                {
                    vkeys[i] = VK_CONTROL;
                }
                else if (strcmp(s, "ALT") == 0)
                {
                    vkeys[i] = VK_MENU;
                }
                else if (strcmp(s, "PASSED") == 0 && i == count)
                {
                    count = i - 1;
                    mask = 1u;
                    break;
                }
                else if (s[0] == 'F')
                {
                    unsigned char d1 = (unsigned char)s[1];
                    if (d1 >= '1' && d1 <= '9')
                    {
                        unsigned char d2 = (unsigned char)s[2];
                        if (d2 == '\0' || (unsigned int)(d2 - '0') > 9u)
                        {
                            vkeys[i] = (uint8_t)(d1 + 0x3fu);
                        }
                        else
                        {
                            unsigned int fn_num = (unsigned int)(d1 - '0') * 10u + (unsigned int)(d2 - '0');
                            vkeys[i] = (fn_num >= 1u && fn_num <= 24u) ? (uint8_t)(fn_num + 0x6fu) : 0u;
                        }
                    }
                }
                else if (strcmp(s, "JOYPAD") == 0)
                {
                    joypad_idx = i;
                    has_joypad = 1;
                }
            }
        }
    }

    lua_settop(L, 0);
    lua_createtable(L, 0, 0);
    for (i = 1; i <= count; i++)
    {
        if (vkeys[i] == 0)
        {
            lua_pushboolean(L, 0);
        }
        else
        {
            SHORT st = GetAsyncKeyState((int)vkeys[i]);
            lua_pushboolean(L, ((uint32_t)(uint16_t)st & mask) != 0u);
        }
        lua_rawseti(L, 1, i);
    }
    rm_free(vkeys);

    if (has_joypad)
    {
        JOYINFO ji;
        int left_on = 0;
        int b;

        lua_createtable(L, 0, 0);
        memset(&ji, 0, sizeof(ji));
        if (joyGetPos(0, &ji) == JOYERR_NOERROR)
        {
            if (joypad_idx >= 1 && joypad_idx <= count)
            {
                lua_pushboolean(L, 1);
                lua_rawseti(L, -3, joypad_idx);
            }
            left_on = (ji.wXpos < 0x4000u) ? 1 : 0;
        }
        else
        {
            ji.wXpos = 0x7fffu;
            ji.wYpos = 0x7fffu;
            ji.wButtons = 0u;
        }

        lua_pushboolean(L, left_on);
        lua_setfield(L, -2, "left");
        lua_pushboolean(L, ji.wXpos > 0xc000u);
        lua_setfield(L, -2, "right");
        lua_pushboolean(L, ji.wYpos < 0x4000u);
        lua_setfield(L, -2, "up");
        lua_pushboolean(L, ji.wYpos > 0xc000u);
        lua_setfield(L, -2, "down");

        for (b = 1; b <= 24; b++)
        {
            char bname[16];
            sprintf(bname, "button%d", b);
            lua_pushboolean(L, (ji.wButtons & (1u << (b - 1))) != 0u);
            lua_setfield(L, -2, bname);
        }
        return 2;
    }

    return 1;
}

int __cdecl lua_rikky_mouse(lua_State *L)
{
    uintptr_t *p_editp;
    uintptr_t editp;
    uint32_t vid_w, vid_h;
    double dw, dh;
    POINT pt;
    RECT rc;
    HWND target_hwnd = NULL;
    int target_kind = 0;
    int off_x = 0;
    int off_y = 0;
    uint32_t disp_w = 1;
    uint32_t disp_h = 1;
    int t1;

    if (!rm_check_aviutl_ver())
    {
        return 0;
    }

    p_editp = (uintptr_t *)RikkyGetMemoryData(3);
    if (!p_editp || !*p_editp)
    {
        return 0;
    }
    editp = *p_editp;
    vid_w = *(uint32_t *)(editp + 0x328u);
    vid_h = *(uint32_t *)(editp + 0x32cu);
    dw = (double)vid_w;
    dh = (double)vid_h;

    GetCursorPos(&pt);
    memset(&rc, 0, sizeof(rc));

    t1 = lua_type(L, 1);
    if (t1 == LUA_TNUMBER)
    {
        target_hwnd = (HWND)(intptr_t)lua_tointeger(L, 1);
        target_kind = GetClientRect(target_hwnd, &rc) ? 1 : 0;
        lua_settop(L, 0);
    }
    else if (t1 == LUA_TSTRING)
    {
        const char *raw_s = lua_tolstring(L, 1, NULL);
        char s_buf[32];
        HWND *p_play = (HWND *)RikkyGetMemoryData(0x0f);
        target_hwnd = p_play ? *p_play : NULL;

        s_buf[0] = '\0';
        if (raw_s)
        {
            strncpy(s_buf, raw_s, sizeof(s_buf) - 1);
            s_buf[sizeof(s_buf) - 1] = '\0';
            rm_str_tolower(s_buf);
        }

        if (strcmp(s_buf, "playwindow") == 0 && target_hwnd && GetClientRect(target_hwnd, &rc))
        {
            target_kind = 2;
            lua_settop(L, 0);
        }
        else if (strcmp(s_buf, "auto") == 0)
        {
            lua_settop(L, 0);
            if (g_mouse_auto_state == 0)
            {
                char exe_path[261];
                char drive[16];
                char dir_part[273];
                char ini_path[261];
                FILE *fp;

                exe_path[0] = '\0';
                drive[0] = '\0';
                dir_part[0] = '\0';
                GetModuleFileNameA(NULL, exe_path, MAX_PATH);
                _splitpath(exe_path, drive, dir_part, NULL, NULL);
                sprintf(ini_path, "%s%s\\aviutl.ini", drive, dir_part);
                fp = fopen(ini_path, "r");
                if (fp)
                {
                    char line[264];
                    int in_system = 0;
                    int decided = 0;

                    while (fgets(line, 0x105, fp) != NULL)
                    {
                        if (in_system)
                        {
                            if (strstr(line, "movieplaymain=") != NULL)
                            {
                                if (line[14] == '0' && target_hwnd && GetClientRect(target_hwnd, &rc))
                                {
                                    g_mouse_auto_state = 2;
                                    target_kind = 2;
                                }
                                else
                                {
                                    g_mouse_auto_state = 1;
                                    target_kind = 0;
                                }
                                decided = 1;
                                break;
                            }
                        }
                        else if (strstr(line, "[system]") != NULL)
                        {
                            in_system = 1;
                        }
                    }
                    fclose(fp);

                    if (!decided)
                    {
                        if (target_hwnd && GetClientRect(target_hwnd, &rc))
                        {
                            g_mouse_auto_state = 2;
                            target_kind = 2;
                        }
                        else
                        {
                            g_mouse_auto_state = 1;
                            target_kind = 0;
                        }
                    }
                }
                else
                {
                    g_mouse_auto_state = 1;
                    target_kind = 0;
                }
            }
            else if (g_mouse_auto_state == 2)
            {
                if (target_hwnd && GetClientRect(target_hwnd, &rc))
                {
                    target_kind = 2;
                }
            }
        }
        else
        {
            lua_settop(L, 0);
        }
    }
    else
    {
        lua_settop(L, 0);
    }

    if (target_kind == 1)
    {
        ScreenToClient(target_hwnd, &pt);
        off_x = 0;
        off_y = 0;
        disp_w = (uint32_t)(rc.right - rc.left);
        disp_h = (uint32_t)(rc.bottom - rc.top);
    }
    else if (target_kind == 2)
    {
        int cw = rc.right - rc.left;
        int ch = (rc.bottom - rc.top) - 0x20;
        int *p_zoom = (int *)RikkyGetMemoryData(0x10);
        int zoom_mode = p_zoom ? *p_zoom : -1;

        ScreenToClient(target_hwnd, &pt);
        if (zoom_mode == 1)
        {
            off_x = (int)(((double)cw - dw) * 0.5);
            off_y = (int)(((double)ch - dh) * 0.5);
            disp_w = vid_w;
            disp_h = vid_h;
        }
        else if (zoom_mode == 0)
        {
            off_x = (int)(((float)cw - (float)vid_w * 0.5f) * 0.5f);
            off_y = (int)(((float)ch - (float)vid_h * 0.5f) * 0.5f);
            disp_w = (uint32_t)(int)((float)vid_w * 0.5f);
            disp_h = (uint32_t)(int)((float)vid_h * 0.5f);
        }
        else if (zoom_mode == 2)
        {
            double sw = dw + dw;
            double sh = dh + dh;
            off_x = (int)(((double)cw - sw) * 0.5);
            off_y = (int)(((double)ch - sh) * 0.5);
            disp_w = (uint32_t)(int)sw;
            disp_h = (uint32_t)(int)sh;
        }
        else if (zoom_mode == 7)
        {
            double sw = dw * 0.25;
            double sh = dh * 0.25;
            off_x = (int)(((double)cw - sw) * 0.5);
            off_y = (int)(((double)ch - sh) * 0.5);
            disp_w = (uint32_t)(int)sw;
            disp_h = (uint32_t)(int)sh;
        }
        else
        {
            double lhs = dh * (double)cw;
            double rhs = dw * (double)ch;
            if (lhs <= rhs)
            {
                off_x = 0;
                off_y = (int)(((double)ch - lhs / dw) * 0.5);
                disp_w = (uint32_t)cw;
                disp_h = (uint32_t)(ch - off_y * 2);
            }
            else
            {
                off_y = 0;
                off_x = (int)(((double)cw - rhs / dh) * 0.5);
                disp_w = (uint32_t)(cw - off_x * 2);
                disp_h = (uint32_t)ch;
            }
        }
    }
    else
    {
        HWND *p_main = (HWND *)RikkyGetMemoryData(0x0e);
        if (p_main && *p_main)
        {
            ScreenToClient(*p_main, &pt);
        }
        off_x = *(int *)(editp + 0x3a0u);
        off_y = *(int *)(editp + 0x3a4u);
        disp_w = *(uint32_t *)(editp + 0x3a8u);
        disp_h = *(uint32_t *)(editp + 0x3acu);

        switch (*(uint32_t *)(editp + 0x380u))
        {
        case 0:
        {
            float sh = 0.25f * (float)vid_h;
            disp_w = (uint32_t)(int)((float)vid_w * 0.25f);
            if (off_y == 0)
            {
                off_y = (int)((float)(int)disp_h - sh);
            }
            disp_h = (uint32_t)(int)sh;
            break;
        }
        case 1:
        {
            float sh = 0.5f * (float)vid_h;
            disp_w = (uint32_t)(int)((float)vid_w * 0.5f);
            if (off_y == 0)
            {
                off_y = (int)((float)(int)disp_h - sh);
            }
            disp_h = (uint32_t)(int)sh;
            break;
        }
        case 2:
            disp_w = vid_w;
            disp_h = vid_h;
            break;
        case 3:
            disp_w = (uint32_t)(int)(dw + dw);
            disp_h = (uint32_t)(int)(dh + dh);
            break;
        case 4:
            disp_w = (uint32_t)(int)((float)vid_w * 3.0f);
            disp_h = (uint32_t)(int)((float)vid_h * 3.0f);
            break;
        case 5:
            disp_w = (uint32_t)(int)((float)vid_w * 4.0f);
            disp_h = (uint32_t)(int)((float)vid_h * 4.0f);
            break;
        default:
            break;
        }
    }

    lua_pushnumber(L, (double)(((float)(pt.x - off_x) / (float)(int)disp_w - 0.5f) * (float)vid_w));
    lua_pushnumber(L, ((double)(pt.y - off_y) / (double)(int)disp_h - 0.5) * dh);
    return 2;
}

static HBITMAP rm_create_24bpp_dib(LONG w, LONG h)
{
    BITMAPINFO bmi;
    void *bits = NULL;

    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 24;
    bmi.bmiHeader.biCompression = BI_RGB;
    return CreateDIBSection(NULL, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
}

static void rm_draw_cursor_on_dc(HDC hdc)
{
    CURSORINFO ci;
    ICONINFO ii;

    memset(&ci, 0, sizeof(ci));
    ci.cbSize = sizeof(ci);
    if (!GetCursorInfo(&ci) || !ci.hCursor)
    {
        return;
    }
    memset(&ii, 0, sizeof(ii));
    if (!GetIconInfo(ci.hCursor, &ii))
    {
        return;
    }
    DrawIcon(hdc, ci.ptScreenPos.x - (int)ii.xHotspot, ci.ptScreenPos.y - (int)ii.yHotspot, ci.hCursor);
    if (ii.hbmMask)
    {
        DeleteObject(ii.hbmMask);
    }
    if (ii.hbmColor)
    {
        DeleteObject(ii.hbmColor);
    }
}

int __cdecl lua_rikky_desk(lua_State *L)
{
    int req_x = (int)lua_tointeger(L, 1);
    int req_y = (int)lua_tointeger(L, 2);
    int req_w = (int)lua_tointeger(L, 3);
    int req_h = (int)lua_tointeger(L, 4);
    int t5 = lua_type(L, 5);
    int draw_cur = 1;
    HWND hwnd_desk;
    RECT rc;
    HDC hdc_mem;
    HBITMAP hbmp;
    HGDIOBJ old_obj;
    HDC hdc_desk;
    BITMAP bm;
    int crop_x, crop_y, out_w, out_h, end_y;
    uint32_t *pixels;
    uint32_t stride;

    if (t5 == LUA_TBOOLEAN)
    {
        draw_cur = lua_toboolean(L, 5);
    }
    else if (t5 == LUA_TNUMBER && (int)lua_tointeger(L, 5) != 1)
    {
        draw_cur = 0;
    }

    lua_settop(L, 0);

    hwnd_desk = GetDesktopWindow();
    memset(&rc, 0, sizeof(rc));
    GetWindowRect(hwnd_desk, &rc);
    hdc_mem = CreateCompatibleDC(NULL);
    hbmp = rm_create_24bpp_dib(rc.right, rc.bottom);
    old_obj = SelectObject(hdc_mem, hbmp);
    hdc_desk = GetWindowDC(hwnd_desk);
    BitBlt(hdc_mem, 0, 0, rc.right, rc.bottom, hdc_desk, 0, 0, SRCCOPY);
    ReleaseDC(hwnd_desk, hdc_desk);

    if (draw_cur)
    {
        rm_draw_cursor_on_dc(hdc_mem);
    }

    memset(&bm, 0, sizeof(bm));
    GetObjectA(hbmp, sizeof(bm), &bm);
    lua_getglobal(L, "obj");

    if (req_w > bm.bmWidth || req_w == 0)
    {
        req_w = bm.bmWidth;
    }
    else if (req_w < 0)
    {
        req_w += bm.bmWidth;
        if (req_w < 0)
        {
            req_w = 0;
        }
    }

    if (req_h > bm.bmHeight || req_h == 0)
    {
        req_h = bm.bmHeight;
    }
    else if (req_h < 0)
    {
        req_h += bm.bmHeight;
        if (req_h < 0)
        {
            req_h = 0;
        }
    }

    crop_x = 0;
    if (req_x >= 0)
    {
        crop_x = (req_x > bm.bmWidth) ? bm.bmWidth : req_x;
    }

    crop_y = 0;
    if (req_y >= 0)
    {
        crop_y = (req_y > bm.bmHeight) ? bm.bmHeight : req_y;
    }

    out_w = (crop_x + req_w <= bm.bmWidth) ? req_w : (bm.bmWidth - crop_x);
    if (crop_y + req_h > bm.bmHeight)
    {
        out_h = bm.bmHeight - crop_y;
        end_y = bm.bmHeight;
    }
    else
    {
        out_h = req_h;
        end_y = crop_y + req_h;
    }

    rm_update_obj_size(L, out_w, out_h, 1);
    pixels = (uint32_t *)rm_malloc((size_t)out_w * (size_t)out_h * 4u);
    stride = ((uint32_t)(bm.bmWidth * 3 + 3)) & ~3u;

    if (pixels && bm.bmBits && crop_y < end_y)
    {
        int dst_idx = 0;
        int y;
        const uint8_t *row_p = (const uint8_t *)bm.bmBits +
                               (size_t)stride * (size_t)(bm.bmHeight - crop_y - 1) + (size_t)crop_x * 3u;

        for (y = crop_y; y < end_y; y++)
        {
            int x;
            const uint8_t *p = row_p;
            for (x = 0; x < out_w; x++)
            {
                pixels[dst_idx++] = (uint32_t)p[0] |
                                    ((uint32_t)p[1] << 8) |
                                    ((uint32_t)p[2] << 16) |
                                    0xff000000u;
                p += 3;
            }
            row_p -= stride;
        }
    }

    lua_getfield(L, 1, "putpixeldata");
    lua_pushlightuserdata(L, pixels);
    lua_call(L, 1, 0);

    SelectObject(hdc_mem, old_obj);
    DeleteObject(hbmp);
    DeleteDC(hdc_mem);
    rm_free(pixels);
    return 0;
}

int __cdecl lua_rikky_settext(lua_State *L)
{
    if (lua_type(L, 1) == LUA_TSTRING)
    {
        const char *s = lua_tolstring(L, 1, NULL);
        int wlen = 0;
        wchar_t *ws = rm_sjis_html_to_wide(&wlen, s);
        if (ws != NULL)
        {
            void *dst;
            if (wlen <= 0x400)
            {
                if (wlen != 0x400)
                {
                    wlen++;
                }
            }
            else
            {
                wlen = 0x400;
            }

            dst = RikkyGetMemoryData(0x74);
            if (dst != NULL)
            {
                memcpy(dst, ws, (size_t)wlen * sizeof(wchar_t));
                rm_free(ws);
                lua_pushboolean(L, 1);
                return 1;
            }
            rm_free(ws);
        }
    }

    lua_pushboolean(L, 0);
    return 0;
}

static LRESULT CALLBACK progress_wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    if (msg == WM_DESTROY)
    {
        PostQuitMessage(0);
        return 0;
    }
    if (msg == WM_CREATE)
    {
        SetTimer(hwnd, 1, 10, NULL);
    }
    else if (msg == WM_PAINT)
    {
        PAINTSTRUCT ps;
        RECT rc;
        HDC hdc = BeginPaint(hwnd, &ps);
        HBRUSH hbr = CreateSolidBrush(g_prog_color);
        HGDIOBJ old_br = SelectObject(hdc, hbr);
        GetClientRect(hwnd, &rc);
        Rectangle(hdc, 0, 0, (int)((double)rc.right * g_prog_percent * 0.01), rc.bottom);
        SelectObject(hdc, old_br);
        DeleteObject(hbr);
        EndPaint(hwnd, &ps);
    }
    else if (msg == WM_TIMER)
    {
        if (g_prog_percent >= 100.0)
        {
            KillTimer(hwnd, 1);
            g_prog_active = 0;
            DestroyWindow(hwnd);
            return 0;
        }
        InvalidateRect(hwnd, NULL, FALSE);
    }

    return DefWindowProcA(hwnd, msg, wparam, lparam);
}

static DWORD WINAPI progress_thread_proc(LPVOID param)
{
    LPCSTR title = (LPCSTR)param;
    HINSTANCE hinst = GetModuleHandleA("rikky_module.dll");
    WNDCLASSA wc;
    HWND hwnd;

    memset(&wc, 0, sizeof(wc));
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = progress_wnd_proc;
    wc.hInstance = hinst;
    wc.hIcon = LoadIconA(NULL, (LPCSTR)0x7f00);
    wc.hCursor = LoadCursorA(NULL, (LPCSTR)0x7f00);
    wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    wc.lpszClassName = "PROGRESS";

    if (RegisterClassA(&wc) == 0)
    {
        return 0;
    }

    hwnd = CreateWindowExA(
        0,
        "PROGRESS",
        title,
        0x10ca0000u,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        300,
        60,
        NULL,
        NULL,
        hinst,
        NULL);

    if (hwnd == NULL)
    {
        HWND *p_main = (HWND *)RikkyGetMemoryData(0x0e);
        MessageBoxA(p_main ? *p_main : NULL, k_prog_err_msg, "midi", MB_OK);
    }
    else
    {
        MSG msg;
        while (GetMessageA(&msg, NULL, 0, 0) > 0)
        {
            DispatchMessageA(&msg);
        }
        g_prog_active = 0xffu;
    }

    UnregisterClassA(wc.lpszClassName, hinst);
    g_prog_active = 0;
    ExitThread(1);
}

int __cdecl lua_rikky_progressbar(lua_State *L)
{
    const char *cmd;
    int ok = 0;

    if (lua_type(L, 1) != LUA_TSTRING)
    {
        lua_pushboolean(L, 0);
        return 1;
    }

    cmd = lua_tolstring(L, 1, NULL);
    if (!cmd)
    {
        lua_pushboolean(L, 0);
        return 1;
    }

    if (strcmp(cmd, "start") == 0)
    {
        const char *title = lua_tolstring(L, 2, NULL);
        uint32_t col = (uint32_t)lua_tointeger(L, 3);
        DWORD tid = 0;
        HANDLE ht;
        LPVOID title_arg = NULL;

        if (g_prog_active != 0)
        {
            g_prog_percent = 100.0;
            Sleep(11);
        }
        g_prog_percent = 0.0;
        g_prog_active = 1;
        if (title)
        {
            strncpy(g_prog_title, title, sizeof(g_prog_title) - 1);
            g_prog_title[sizeof(g_prog_title) - 1] = '\0';
            title_arg = g_prog_title;
        }
        g_prog_color = ((col & 0xffu) << 16) | (col & 0xff00u) | ((col >> 16) & 0xffu);
        ht = CreateThread(NULL, 0, progress_thread_proc, title_arg, 0, &tid);
        if (ht)
        {
            CloseHandle(ht);
        }
        ok = 1;
    }
    else if (strcmp(cmd, "processing") == 0)
    {
        if (g_prog_active != 0)
        {
            double pct = lua_tonumber(L, 2);
            ok = ((int8_t)g_prog_active > 0) ? 1 : 0;
            g_prog_percent = pct;
        }
    }
    else if (strcmp(cmd, "end") == 0)
    {
        if (g_prog_active != 0)
        {
            g_prog_percent = 100.0;
            Sleep(11);
            g_prog_active = 0;
            ok = 1;
        }
    }

    lua_pushboolean(L, ok);
    return 1;
}
