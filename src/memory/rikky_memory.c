#include <windows.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "rikky_memory.h"

typedef void *(__cdecl *MsvcrtMallocFn)(size_t);
typedef void *(__cdecl *MsvcrtCallocFn)(size_t, size_t);
typedef void *(__cdecl *MsvcrtReallocFn)(void *, size_t);
typedef void (__cdecl *MsvcrtFreeFn)(void *);

static MsvcrtMallocFn g_rt_malloc = NULL;
static MsvcrtCallocFn g_rt_calloc = NULL;
static MsvcrtReallocFn g_rt_realloc = NULL;
static MsvcrtFreeFn g_rt_free = NULL;

static void rt_alloc_init(void)
{
    HMODULE hm;
    if (g_rt_malloc)
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
        g_rt_malloc = (MsvcrtMallocFn)GetProcAddress(hm, "malloc");
        g_rt_calloc = (MsvcrtCallocFn)GetProcAddress(hm, "calloc");
        g_rt_realloc = (MsvcrtReallocFn)GetProcAddress(hm, "realloc");
        g_rt_free = (MsvcrtFreeFn)GetProcAddress(hm, "free");
    }
    if (!g_rt_malloc)
    {
        g_rt_malloc = malloc;
    }
    if (!g_rt_calloc)
    {
        g_rt_calloc = calloc;
    }
    if (!g_rt_realloc)
    {
        g_rt_realloc = realloc;
    }
    if (!g_rt_free)
    {
        g_rt_free = free;
    }
}

static void *rt_malloc(size_t sz)
{
    rt_alloc_init();
    return g_rt_malloc(sz);
}

static void *rt_calloc(size_t n, size_t sz)
{
    rt_alloc_init();
    return g_rt_calloc(n, sz);
}

static void *rt_realloc(void *p, size_t sz)
{
    rt_alloc_init();
    return g_rt_realloc(p, sz);
}

static void rt_free(void *p)
{
    if (p)
    {
        rt_alloc_init();
        g_rt_free(p);
    }
}

static char g_plugin_name[] =
    "rikky\x83\x81\x83\x82\x83\x8a\x8a\xc7\x97\x9d\x83\x76\x83\x89\x83\x4f\x83\x43\x83\x93";
static char g_plugin_info[] =
    "rikky\x83\x81\x83\x82\x83\x8a\x8a\xc7\x97\x9d\x83\x76\x83\x89\x83\x4f\x83\x43\x83\x93 ver0.6b";
static char g_plugin_ver[] = "0.6b";
static char g_aviutl_ver[] = "1.10";
static char g_exedit_ver[] = "0.92";
static char g_empty_ver[] = "";
static char g_unknown_obj[] = "\x95\x73\x96\xbe";

static FILTER *g_fp = NULL;
static HMODULE g_host_exe = NULL;
static int g_is_stock_aviutl110 = -1;
static HMODULE g_exedit_hmod = NULL;
static int g_initialized = 0;

static uint8_t g_fallback_editp[0x500];
static void *g_cur_editp = g_fallback_editp;
static int g_play_frame = 0;
static int g_is_playing = 0;
static int g_play_subwin = 0;
static int g_host_slot_0c = 0;
static int g_host_slot_0d = 0;
static HWND g_main_hwnd = NULL;
static HWND g_play_hwnd = NULL;
static int g_play_zoom = 1;
static int g_sys_max_w = 1920;
static int g_sys_max_h = 1080;
static int g_host_slot_19 = 0;
static int g_host_slot_1c = 0;
static FILTER *g_first_filter = NULL;

static void *g_slot_a2 = NULL;
static void *g_slot_a3 = NULL;
static int g_subclass_active = 0;
static HWND g_subclass_hwnd = NULL;
static WNDPROC g_orig_exedit_dlg = NULL;

#pragma pack(push, 1)
typedef struct
{
    uint8_t enabled;
    uint8_t pad[3];
    int32_t count;
    void *entries;
} RikkySoundRegState;
#pragma pack(pop)

static RikkySoundRegState g_sound_reg_state = { 0, { 0, 0, 0 }, 0, NULL };
static uint32_t g_state_c9[3] = { 0, 0, 0 };
static uint32_t g_sound_enabled = 0;
static int g_in_raudio = 0;

typedef struct
{
    char *key;
    RikkySaveCallback cb;
    uint32_t user;
    int queried_size;
} SaveEntry;

typedef struct
{
    RikkyResetCallback cb;
    uint32_t user;
} ResetEntry;

typedef struct
{
    char *key;
    void *data;
    EXFUNC *exfunc;
    void *editp;
    uint32_t size;
} LoadedRecord;

typedef struct
{
    RikkyExitCallback cb;
    uint32_t user;
} ExitEntry;

static SaveEntry *g_save_entries = NULL;
static int g_save_count = 0;
static ResetEntry *g_reset_entries = NULL;
static int g_reset_count = 0;
static LoadedRecord *g_loaded_records = NULL;
static int g_loaded_count = 0;
static ExitEntry *g_exit_entries = NULL;
static int g_exit_count = 0;
static int g_exit_ran = 0;
static FILTER **g_backup_filters = NULL;
static int g_backup_count = 0;

static uint32_t *g_exedit_bkp_last_tick = NULL;
static uint32_t *g_exedit_bkp_interval = NULL;
static int *g_exedit_bkp_dirty = NULL;
static uint32_t g_cached_bkp_tick = 0;
static int g_cached_bkp_dirty = 0;
static DWORD g_last_poll_tick = 0;
static int g_timer_armed = 0;

static void detect_host_exe(void)
{
    const IMAGE_DOS_HEADER *dos;
    const IMAGE_NT_HEADERS32 *nt;

    if (g_is_stock_aviutl110 >= 0)
    {
        return;
    }
    g_is_stock_aviutl110 = 0;
    g_host_exe = GetModuleHandleA(NULL);
    if (!g_host_exe)
    {
        return;
    }

    dos = (const IMAGE_DOS_HEADER *)g_host_exe;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 0x1000)
    {
        return;
    }
    nt = (const IMAGE_NT_HEADERS32 *)((const uint8_t *)g_host_exe + dos->e_lfanew);
    if (nt->Signature == IMAGE_NT_SIGNATURE &&
        nt->FileHeader.TimeDateStamp == 0x5d95ecccu &&
        nt->OptionalHeader.SizeOfImage == 0x00338000u)
    {
        g_is_stock_aviutl110 = 1;
    }
}

static void sync_host_state(FILTER *fp, void *editp)
{
    detect_host_exe();

    if (fp)
    {
        g_fp = fp;
    }
    if (!g_exedit_hmod)
    {
        g_exedit_hmod = GetModuleHandleA("exedit.auf");
    }
    if (editp)
    {
        g_cur_editp = editp;
    }
    else if (g_cur_editp == g_fallback_editp && g_exedit_hmod)
    {
        void *ex_editp = *(void **)((uintptr_t)g_exedit_hmod + 0x1a532cu);
        if (ex_editp)
        {
            g_cur_editp = ex_editp;
        }
    }

    if (g_fp && g_fp->hwnd && !g_main_hwnd)
    {
        HWND owner = GetWindow(g_fp->hwnd, GW_OWNER);
        if (owner)
        {
            g_main_hwnd = owner;
        }
    }
    if (!g_main_hwnd && g_cur_editp && g_cur_editp != g_fallback_editp)
    {
        uint32_t h32 = *(uint32_t *)((uint8_t *)g_cur_editp + 0x384);
        if (h32)
        {
            g_main_hwnd = (HWND)(uintptr_t)h32;
        }
    }

    if (g_fp && g_fp->exfunc)
    {
        if (g_fp->exfunc->get_sys_info)
        {
            SYS_INFO si;
            memset(&si, 0, sizeof(si));
            if (g_fp->exfunc->get_sys_info(NULL, &si))
            {
                if (si.max_w > 0)
                {
                    g_sys_max_w = si.max_w;
                }
                if (si.max_h > 0)
                {
                    g_sys_max_h = si.max_h;
                }
            }
        }
        if (g_fp->exfunc->get_filterp)
        {
            FILTER *f0 = g_fp->exfunc->get_filterp(0);
            if (f0)
            {
                g_first_filter = f0;
            }
        }
    }
    if (!g_first_filter)
    {
        g_first_filter = g_fp;
    }
}

static LRESULT CALLBACK subclass_exedit_dlg_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    if (msg == WM_NCACTIVATE && g_fp && g_fp->exfunc && g_fp->exfunc->get_pixel_filtered_ex)
    {
        HWND *p_active_dlg = (HWND *)RikkyGetMemoryData(0xa1);
        int *p_sel_obj = (int *)RikkyGetMemoryData(0x6b);
        int obj_stride = (int)(intptr_t)RikkyGetMemoryData(0x6d);
        uint8_t **p_objs = (uint8_t **)RikkyGetMemoryData(0x6c);
        void **p_editp = (void **)RikkyGetMemoryData(3);
        if (p_active_dlg && *p_active_dlg != NULL && (LPARAM)(uintptr_t)(*p_active_dlg) == lparam &&
            p_sel_obj && *p_sel_obj >= 0 && p_objs && *p_objs && p_editp && *p_editp)
        {
            int frame_s = *(int *)(*p_objs + (*p_sel_obj) * obj_stride + 8);
            g_fp->exfunc->get_pixel_filtered_ex(*p_editp, frame_s, NULL, NULL, NULL, 0);
        }
    }
    if (g_orig_exedit_dlg)
    {
        return CallWindowProcA(g_orig_exedit_dlg, hwnd, msg, wparam, lparam);
    }
    return DefWindowProcA(hwnd, msg, wparam, lparam);
}

static void unsubclass_exedit_dlg(void)
{
    if (g_subclass_active && g_orig_exedit_dlg && g_subclass_hwnd && IsWindow(g_subclass_hwnd))
    {
        WNDPROC cur = (WNDPROC)(uintptr_t)GetWindowLongA(g_subclass_hwnd, GWL_WNDPROC);
        if (cur == subclass_exedit_dlg_proc)
        {
            SetWindowLongA(g_subclass_hwnd, GWL_WNDPROC, (LONG)(uintptr_t)g_orig_exedit_dlg);
        }
    }
    g_subclass_active = 0;
    g_subclass_hwnd = NULL;
    g_orig_exedit_dlg = NULL;
}

typedef struct
{
    uint8_t id;
    uint8_t is_const;
    uint32_t value;
} ExEditMemEntry;

static const ExEditMemEntry k_exedit092_table[] =
{
    { 0x66, 0, 0x1b28d4u }, { 0x67, 1, 0x0005c0u }, { 0x68, 1, 0x0004b8u }, { 0x69, 0, 0x0a03ccu },
    { 0x6a, 0, 0x09dd54u }, { 0x6b, 0, 0x177a10u }, { 0x6c, 0, 0x1e0fa4u }, { 0x6d, 1, 0x0005c8u },
    { 0x6e, 0, 0x1b28d0u }, { 0x6f, 0, 0x1b28dfu }, { 0x70, 0, 0x0b8b14u }, { 0x71, 1, 0x000404u },
    { 0x72, 0, 0x1b2b20u }, { 0x73, 1, 0x0000f9u }, { 0x74, 0, 0x1a6bd0u }, { 0x75, 0, 0x0a2a30u },
    { 0x76, 0, 0x1539c8u }, { 0x77, 0, 0x177a44u }, { 0x78, 0, 0x168a20u }, { 0x79, 0, 0x1b236cu },
    { 0x7a, 1, 0x000788u }, { 0x7b, 0, 0x1e0fa8u }, { 0x7c, 0, 0x0adb54u }, { 0x7d, 0, 0x09d264u },
    { 0x7e, 0, 0x0b8220u }, { 0x7f, 0, 0x135924u }, { 0x80, 0, 0x135068u }, { 0x81, 0, 0x135910u },
    { 0x82, 0, 0x0a6c88u }, { 0x83, 1, 0x0003fcu }, { 0x84, 0, 0x1e9ed0u }, { 0x85, 0, 0x1ec880u },
    { 0x86, 0, 0x1ec740u }, { 0x87, 0, 0x09d42cu }, { 0x88, 0, 0x146274u }, { 0x89, 0, 0x0b98c4u },
    { 0x8a, 0, 0x1b21e4u }, { 0x8b, 0, 0x0b8becu }, { 0x8c, 0, 0x0ba34cu }, { 0x8d, 0, 0x09d304u },
    { 0x8e, 0, 0x1b2b4cu }, { 0x8f, 0, 0x1b2b10u }, { 0x90, 0, 0x187dd8u }, { 0x91, 0, 0x1ec794u },
    { 0x92, 0, 0x1bac98u }, { 0x93, 0, 0x146250u }, { 0x94, 0, 0x167d88u }, { 0x95, 0, 0x179230u },
    { 0x96, 0, 0x1baca8u }, { 0x97, 0, 0x188498u }, { 0x98, 0, 0x0b81ecu }, { 0x99, 0, 0x09d39cu },
    { 0x9a, 0, 0x09d4c4u }, { 0x9b, 0, 0x0c1f08u }, { 0x9c, 0, 0x0d44d0u }, { 0x9d, 0, 0x0d7720u },
    { 0x9e, 1, 0x0023a0u }, { 0x9f, 0, 0x1b29fcu }, { 0xa0, 0, 0x0d44e4u }, { 0xa1, 0, 0x0d4524u },
    { 0xa7, 0, 0x1b2368u }, { 0xa8, 0, 0x1b2204u }, { 0xa9, 0, 0x1b28b4u }, { 0xaa, 0, 0x1b28b0u },
    { 0xab, 0, 0x1b28c0u }, { 0xac, 0, 0x1b28b8u }, { 0xad, 0, 0x1b28c4u }, { 0xae, 0, 0x168fa8u },
    { 0xaf, 0, 0x1b2b04u }, { 0xb0, 0, 0x1bacccu }, { 0xb1, 0, 0x14ded0u }, { 0xb2, 0, 0x1b2b08u },
    { 0xb3, 0, 0x1b24b8u }, { 0xb4, 1, 0x000958u }, { 0xb5, 1, 0x000a68u }, { 0xb6, 1, 0x000040u },
    { 0xb8, 0, 0x0d4744u }, { 0xb9, 0, 0x135aa8u }, { 0xba, 0, 0x1a5950u }, { 0xbb, 1, 0x0004bcu },
    { 0xbc, 0, 0x1b2370u }, { 0xbd, 0, 0x1a5310u }, { 0xbe, 0, 0x02c7fcu }, { 0xbf, 0, 0x0a6edcu },
    { 0xc0, 0, 0x1b2b10u }, { 0xc1, 0, 0x1b28e8u }, { 0xc2, 0, 0x236388u }, { 0xc3, 0, 0x177a20u },
    { 0xc4, 0, 0x178e80u }, { 0xc5, 0, 0x15918cu }, { 0xc6, 0, 0x1b2b0cu }, { 0xc7, 0, 0x149808u },
    { 0xca, 0, 0x0a29bcu }, { 0xcb, 1, 0x000060u }, { 0xcc, 0, 0x1b236cu }, { 0xcd, 0, 0x1b23b0u }
};

void *__cdecl RikkyGetMemoryData(int id)
{
    uintptr_t base;
    sync_host_state(NULL, NULL);
    base = (uintptr_t)g_host_exe;

    if (g_is_stock_aviutl110 && base != 0)
    {
        switch (id)
        {
        case 3:  return (void *)(base + 0x08717cu);
        case 9:  return (void *)(base + 0x2c4d54u);
        case 10: return (void *)(base + 0x2c4dbcu);
        case 11: return (void *)(base + 0x0824e4u);
        case 12: return (void *)(base + 0x0b87b4u);
        case 13: return (void *)(base + 0x0b8f28u);
        case 14: return (void *)(base + 0x079908u);
        case 15: return (void *)(base + 0x0806c4u);
        case 16: return (void *)(base + 0x2c4e40u);
        case 19: return (void *)(base + 0x086444u);
        case 20: return (void *)(base + 0x08626cu);
        case 25: return (void *)(base + 0x0b51fcu);
        case 26: return (void *)(base + 0x0a8c34u);
        case 28: return (void *)(base + 0x0b874cu);
        case 30: return (void *)(base + 0x27984cu);
        default: break;
        }
    }

    switch (id)
    {
    case 0:   return g_plugin_ver;
    case 1:   return (void *)(uintptr_t)56;
    case 2:   return g_aviutl_ver;
    case 3:   return &g_cur_editp;
    case 4:   return (void *)(uintptr_t)0x20c;
    case 5:   return (void *)(uintptr_t)0x108;
    case 6:   return (void *)(uintptr_t)0x330;
    case 7:   return (void *)(uintptr_t)0x318;
    case 8:   return (void *)(uintptr_t)0x328;
    case 9:   return &g_play_frame;
    case 10:  return &g_is_playing;
    case 11:  return &g_play_subwin;
    case 12:  return &g_host_slot_0c;
    case 13:  return &g_host_slot_0d;
    case 14:  return &g_main_hwnd;
    case 15:  return &g_play_hwnd;
    case 16:  return &g_play_zoom;
    case 17:  return (void *)(uintptr_t)0x368;
    case 18:  return (void *)(uintptr_t)0x3fc;
    case 19:  return &g_sys_max_w;
    case 20:  return &g_sys_max_h;
    case 21:  return (g_fp && g_fp->exfunc) ? (void *)g_fp->exfunc->rgb2yc : NULL;
    case 22:  return (g_fp && g_fp->exfunc) ? (void *)g_fp->exfunc->yc2rgb : NULL;
    case 23:  return (g_fp && g_fp->exfunc) ? (void *)g_fp->exfunc->get_audio_filtered : NULL;
    case 24:  return (g_fp && g_fp->exfunc) ? (void *)g_fp->exfunc->get_pixel_filtered_ex : NULL;
    case 25:  return &g_host_slot_19;
    case 26:  return g_fp ? (void *)g_fp->exfunc : NULL;
    case 27:  return g_fp;
    case 28:  return &g_host_slot_1c;
    case 29:  return (void *)(uintptr_t)0x414;
    case 30:  return &g_first_filter;
    case 100: return g_exedit_hmod ? (void *)(uintptr_t)0x34 : NULL;
    case 101: return g_exedit_hmod ? g_exedit_ver : g_empty_ver;
    case 0xa2: return g_slot_a2;
    case 0xa3: return g_slot_a3;
    case 0xa4:
        g_slot_a2 = NULL;
        return NULL;
    case 0xa5:
        return (void *)(uintptr_t)g_subclass_active;
    case 0xa6:
        if (!g_subclass_active)
        {
            HWND *p_dlg = (HWND *)RikkyGetMemoryData(0x76);
            if (p_dlg && *p_dlg && IsWindow(*p_dlg))
            {
                WNDPROC prev = (WNDPROC)(uintptr_t)SetWindowLongA(
                    *p_dlg, GWL_WNDPROC, (LONG)(uintptr_t)subclass_exedit_dlg_proc);
                if (prev)
                {
                    g_orig_exedit_dlg = prev;
                    g_subclass_hwnd = *p_dlg;
                    g_subclass_active = 1;
                }
            }
        }
        return NULL;
    case 0xb7:
        g_slot_a3 = NULL;
        return NULL;
    case 200: return &g_sound_reg_state;
    case 201: return g_state_c9;
    case 206: return &g_sound_enabled;
    default:  break;
    }

    if (id >= 0x66 && id <= 0xcd && g_exedit_hmod)
    {
        size_t i;
        for (i = 0; i < sizeof(k_exedit092_table) / sizeof(k_exedit092_table[0]); i++)
        {
            if (k_exedit092_table[i].id == (uint8_t)id)
            {
                if (k_exedit092_table[i].is_const)
                {
                    return (void *)(uintptr_t)k_exedit092_table[i].value;
                }
                return (void *)((uintptr_t)g_exedit_hmod + k_exedit092_table[i].value);
            }
        }
    }
    return NULL;
}

int __cdecl CloseAviHandle(AVI_FILE_HANDLE afh)
{
    if (afh && g_fp && g_fp->exfunc && g_fp->exfunc->avi_file_close)
    {
        g_fp->exfunc->avi_file_close(afh);
    }
    return 1;
}

AVI_FILE_HANDLE __cdecl GetAviHandle(const char *path, FILE_INFO *fip)
{
    FILE *fp_in;
    uint32_t riff_id = 0;
    uint32_t wave_id = 0;
    size_t path_len;
    char *raudio_path;
    AVI_FILE_HANDLE src_afh;
    AVI_FILE_HANDLE out_afh;

    if (!path || !fip || !g_fp || !g_fp->exfunc || !g_fp->exfunc->avi_file_open)
    {
        return NULL;
    }

    fp_in = fopen(path, "rb");
    if (!fp_in)
    {
        return NULL;
    }
    (void)fread(&riff_id, 1, 4, fp_in);
    (void)fseek(fp_in, 4, SEEK_CUR);
    (void)fread(&wave_id, 1, 4, fp_in);
    fclose(fp_in);

    memset(fip, 0, sizeof(FILE_INFO));

    if (riff_id == 0x46464952u && wave_id == 0x45564157u)
    {
        return g_fp->exfunc->avi_file_open((LPSTR)path, fip, AVI_FILE_OPEN_FLAG_AUDIO_ONLY);
    }

    path_len = strlen(path);
    raudio_path = (char *)rt_malloc(path_len + 12);
    if (!raudio_path)
    {
        return NULL;
    }
    sprintf(raudio_path, "%s.raudio", path);

    fp_in = fopen(raudio_path, "rb");
    if (fp_in)
    {
        fclose(fp_in);
        out_afh = g_fp->exfunc->avi_file_open(raudio_path, fip, AVI_FILE_OPEN_FLAG_AUDIO_ONLY);
        rt_free(raudio_path);
        return out_afh;
    }

    if (g_in_raudio || !g_fp->exfunc->avi_file_read_audio_sample || !g_fp->exfunc->avi_file_close)
    {
        rt_free(raudio_path);
        return NULL;
    }

    g_in_raudio = 1;
    src_afh = g_fp->exfunc->avi_file_open((LPSTR)path, fip, AVI_FILE_OPEN_FLAG_AUDIO_ONLY);
    if (!src_afh)
    {
        g_in_raudio = 0;
        rt_free(raudio_path);
        return NULL;
    }

    if (fip->audio_rate <= 0 || fip->audio_ch <= 0 || fip->audio_n <= 0)
    {
        g_fp->exfunc->avi_file_close(src_afh);
        g_in_raudio = 0;
        rt_free(raudio_path);
        return NULL;
    }

    {
        int rate = fip->audio_rate;
        int ch = fip->audio_ch;
        int total_smp = fip->audio_n;
        int chunk_smp = rate * 2;
        int16_t *pcm_buf = (int16_t *)rt_malloc((size_t)chunk_smp * (size_t)ch * sizeof(int16_t));
        FILE *wav_fp = pcm_buf ? fopen(raudio_path, "wb") : NULL;
        uint8_t hdr[44];
        uint32_t data_bytes = 0;
        int cur = 0;

        if (!pcm_buf || !wav_fp)
        {
            if (wav_fp)
            {
                fclose(wav_fp);
            }
            rt_free(pcm_buf);
            g_fp->exfunc->avi_file_close(src_afh);
            g_in_raudio = 0;
            rt_free(raudio_path);
            return NULL;
        }

        memset(hdr, 0, sizeof(hdr));
        fwrite(hdr, 1, sizeof(hdr), wav_fp);

        while (cur < total_smp)
        {
            int req = total_smp - cur;
            int got;
            size_t bytes;
            if (req > chunk_smp)
            {
                req = chunk_smp;
            }
            got = g_fp->exfunc->avi_file_read_audio_sample(src_afh, cur, req, pcm_buf);
            if (got <= 0)
            {
                break;
            }
            bytes = (size_t)got * (size_t)ch * sizeof(int16_t);
            fwrite(pcm_buf, 1, bytes, wav_fp);
            data_bytes += (uint32_t)bytes;
            cur += got;
        }

        {
            uint32_t riff_size = 36u + data_bytes;
            uint32_t fmt_size = 16u;
            uint16_t fmt_tag = 1u;
            uint16_t channels = (uint16_t)ch;
            uint32_t sample_rate = (uint32_t)rate;
            uint32_t byte_rate = sample_rate * (uint32_t)channels * 2u;
            uint16_t block_align = (uint16_t)(channels * 2u);
            uint16_t bits = 16u;

            fseek(wav_fp, 0, SEEK_SET);
            fwrite("RIFF", 1, 4, wav_fp);
            fwrite(&riff_size, 4, 1, wav_fp);
            fwrite("WAVEfmt ", 1, 8, wav_fp);
            fwrite(&fmt_size, 4, 1, wav_fp);
            fwrite(&fmt_tag, 2, 1, wav_fp);
            fwrite(&channels, 2, 1, wav_fp);
            fwrite(&sample_rate, 4, 1, wav_fp);
            fwrite(&byte_rate, 4, 1, wav_fp);
            fwrite(&block_align, 2, 1, wav_fp);
            fwrite(&bits, 2, 1, wav_fp);
            fwrite("data", 1, 4, wav_fp);
            fwrite(&data_bytes, 4, 1, wav_fp);
        }

        fclose(wav_fp);
        rt_free(pcm_buf);
    }

    g_fp->exfunc->avi_file_close(src_afh);
    memset(fip, 0, sizeof(FILE_INFO));
    out_afh = g_fp->exfunc->avi_file_open(raudio_path, fip, AVI_FILE_OPEN_FLAG_AUDIO_ONLY);
    rt_free(raudio_path);
    g_in_raudio = 0;
    return out_afh;
}

void __cdecl HSV2RGB(double h, double s, double v, int *r, int *g, int *b)
{
    int iv;
    double inv_s;
    double dv;
    double tmp;
    int sector;

    if (h < 0.0 || h >= 360.0)
    {
        h -= floor(h / 360.0) * 360.0;
    }
    if (s < 0.0)
    {
        inv_s = 100.0;
    }
    else if (s <= 100.0)
    {
        inv_s = 100.0 - s;
    }
    else
    {
        inv_s = 0.0;
    }

    if (v < 0.0)
    {
        dv = 0.0;
        iv = 0;
    }
    else if (v > 100.0)
    {
        iv = 255;
        dv = 254.99999999999997;
    }
    else
    {
        dv = v * 2.55;
        iv = (int)ceil(dv);
    }

    inv_s = inv_s * dv * 0.01;
    sector = (int)floor(h / 60.0);
    switch (sector)
    {
    case 0:
        if (r) *r = iv;
        tmp = ceil((dv - inv_s) * (h / 60.0) + inv_s);
        if (g) *g = (int)tmp;
        if (b) *b = (int)ceil(inv_s);
        break;
    case 1:
        tmp = ceil((dv - inv_s) * ((120.0 - h) / 60.0) + inv_s);
        if (r) *r = (int)tmp;
        if (g) *g = iv;
        if (b) *b = (int)ceil(inv_s);
        break;
    case 2:
        if (r) *r = (int)ceil(inv_s);
        if (g) *g = iv;
        tmp = inv_s + (dv - inv_s) * ((h - 120.0) / 60.0);
        if (b) *b = (int)ceil(tmp);
        break;
    case 3:
        if (r) *r = (int)ceil(inv_s);
        tmp = inv_s + (dv - inv_s) * ((240.0 - h) / 60.0);
        if (g) *g = (int)ceil(tmp);
        if (b) *b = iv;
        break;
    case 4:
        tmp = ceil((dv - inv_s) * ((h - 240.0) / 60.0) + inv_s);
        if (r) *r = (int)tmp;
        if (g) *g = (int)ceil(inv_s);
        if (b) *b = iv;
        break;
    default:
        if (r) *r = iv;
        if (g) *g = (int)ceil(inv_s);
        tmp = inv_s + (dv - inv_s) * ((360.0 - h) / 60.0);
        if (b) *b = (int)ceil(tmp);
        break;
    }
}

void __cdecl RGB2HSV(int r, int g, int b, double *h, double *s, double *v)
{
    float fr = (float)r;
    float fg = (float)g;
    float fb = (float)b;
    float cmax = fr;
    float cmin;
    int min_rb;

    if (cmax < fg) cmax = fg;
    if (cmax < fb) cmax = fb;

    if (fr < fb)
    {
        min_rb = 2;
        cmin = fr;
    }
    else
    {
        min_rb = 1;
        cmin = fb;
    }

    if (cmin <= fg)
    {
        float delta;
        if (cmax == cmin)
        {
            if (h) *h = 0.0;
            if (s) *s = 0.0;
            if (v) *v = (double)((cmax * 100.0) / 255.0);
            return;
        }
        delta = cmax - cmin;
        if (min_rb == 2)
        {
            if (h) *h = (double)(((fb - fg) * 60.0) / delta + 180.0);
        }
        else
        {
            if (h) *h = (double)(((fg - fr) * 60.0) / delta + 60.0);
        }
        if (s) *s = (double)((delta / cmax) * 100.0);
    }
    else
    {
        float delta;
        if (cmax == fg)
        {
            if (h) *h = 0.0;
            if (s) *s = 0.0;
            if (v) *v = (double)((cmax * 100.0) / 255.0);
            return;
        }
        delta = cmax - fg;
        if (h) *h = (double)(((fr - fb) * 60.0) / delta + 300.0);
        if (s) *s = (double)((delta / cmax) * 100.0);
    }
    if (v) *v = (double)((cmax * 100.0) / 255.0);
}

typedef struct
{
    int mode;
    int lo_i;
    int hi_i;
    double lo_d;
    double hi_d;
} FillCriterion;

typedef struct
{
    uint16_t x;
    uint16_t y;
    uint32_t idx;
} FillCoord;

typedef struct
{
    int x;
    int y;
} FillSeed;

static int fill_init_criterion(const uint8_t *px, int mode, uint32_t threshold, FillCriterion *crit)
{
    double h = 0.0;
    double s = 0.0;
    double v = 0.0;
    double luma;
    int th = (int)threshold;
    int a = (int)px[3];

    crit->mode = mode;
    crit->lo_i = 0;
    crit->hi_i = 0;
    crit->lo_d = 0.0;
    crit->hi_d = 0.0;

    switch (mode)
    {
    case 0:  if (a < th) return 0; crit->lo_i = th; break;
    case 1:  if (a > th) return 0; crit->lo_i = th; break;
    case 2:
    case 3:  crit->lo_i = a; break;
    case 4:  crit->lo_i = a - th; crit->hi_i = a + th; break;
    case 5:  RGB2HSV(px[2], px[1], px[0], &h, &s, &v); if (h < (double)th) return 0; crit->lo_d = (double)th; break;
    case 6:  RGB2HSV(px[2], px[1], px[0], &h, &s, &v); if (h > (double)th) return 0; crit->lo_d = (double)th; break;
    case 7:
    case 8:  RGB2HSV(px[2], px[1], px[0], &h, &s, &v); crit->lo_d = h; break;
    case 9:  RGB2HSV(px[2], px[1], px[0], &h, &s, &v); crit->lo_d = h - (double)th; crit->hi_d = h + (double)th; break;
    case 10: RGB2HSV(px[2], px[1], px[0], &h, &s, &v); if (s < (double)th) return 0; crit->lo_d = (double)th; break;
    case 11: RGB2HSV(px[2], px[1], px[0], &h, &s, &v); if (s > (double)th) return 0; crit->lo_d = (double)th; break;
    case 12:
    case 13: RGB2HSV(px[2], px[1], px[0], &h, &s, &v); crit->lo_d = s; break;
    case 14: RGB2HSV(px[2], px[1], px[0], &h, &s, &v); crit->lo_d = s - (double)th; crit->hi_d = s + (double)th; break;
    case 15: RGB2HSV(px[2], px[1], px[0], &h, &s, &v); if (v < (double)th) return 0; crit->lo_d = (double)th; break;
    case 16: RGB2HSV(px[2], px[1], px[0], &h, &s, &v); if (v > (double)th) return 0; crit->lo_d = (double)th; break;
    case 17:
    case 18: RGB2HSV(px[2], px[1], px[0], &h, &s, &v); crit->lo_d = v; break;
    case 19: RGB2HSV(px[2], px[1], px[0], &h, &s, &v); crit->lo_d = v - (double)th; crit->hi_d = v + (double)th; break;
    case 20:
        luma = (double)px[0] * 11.4 + (double)px[1] * 58.7 + (double)px[2] * 29.9;
        crit->lo_d = (double)(int)(threshold * 100u);
        if (luma < crit->lo_d) return 0;
        break;
    case 21:
        luma = (double)px[0] * 11.4 + (double)px[1] * 58.7 + (double)px[2] * 29.9;
        crit->lo_d = (double)(int)(threshold * 100u);
        if (luma > crit->lo_d) return 0;
        break;
    case 22:
    case 23:
        crit->lo_d = (double)px[0] * 11.4 + (double)px[1] * 58.7 + (double)px[2] * 29.9;
        break;
    case 24:
        luma = (double)px[0] * 11.4 + (double)px[1] * 58.7 + (double)px[2] * 29.9;
        crit->lo_d = luma - (double)th;
        crit->hi_d = luma + (double)th;
        break;
    default:
        break;
    }
    return 1;
}

static int fill_test_pixel(const uint8_t *px, const FillCriterion *crit)
{
    double h;
    double s;
    double v;
    double luma;
    int a = (int)px[3];

    switch (crit->mode)
    {
    case 0:
    case 2:  return a >= crit->lo_i;
    case 1:
    case 3:  return a <= crit->lo_i;
    case 4:  return a >= crit->lo_i && a <= crit->hi_i;
    case 5:
    case 7:  RGB2HSV(px[2], px[1], px[0], &h, &s, &v); return (s > 0.0) && (h >= crit->lo_d);
    case 6:
    case 8:  RGB2HSV(px[2], px[1], px[0], &h, &s, &v); return (s > 0.0) && (h <= crit->lo_d);
    case 9:
        RGB2HSV(px[2], px[1], px[0], &h, &s, &v);
        if (s <= 0.0) return 0;
        if (h >= crit->lo_d && h <= crit->hi_d) return 1;
        if ((h - 360.0) >= crit->lo_d && (h - 360.0) <= crit->hi_d) return 1;
        if ((h + 360.0) >= crit->lo_d && (h + 360.0) <= crit->hi_d) return 1;
        return 0;
    case 10:
    case 12: RGB2HSV(px[2], px[1], px[0], &h, &s, &v); return s >= crit->lo_d;
    case 11:
    case 13: RGB2HSV(px[2], px[1], px[0], &h, &s, &v); return s <= crit->lo_d;
    case 14: RGB2HSV(px[2], px[1], px[0], &h, &s, &v); return s >= crit->lo_d && s <= crit->hi_d;
    case 15:
    case 17: RGB2HSV(px[2], px[1], px[0], &h, &s, &v); return v >= crit->lo_d;
    case 16:
    case 18: RGB2HSV(px[2], px[1], px[0], &h, &s, &v); return v <= crit->lo_d;
    case 19: RGB2HSV(px[2], px[1], px[0], &h, &s, &v); return v >= crit->lo_d && v <= crit->hi_d;
    case 20:
    case 22:
        luma = (double)px[0] * 11.4 + (double)px[1] * 58.7 + (double)px[2] * 29.9;
        return luma >= crit->lo_d;
    case 21:
    case 23:
        luma = (double)px[0] * 11.4 + (double)px[1] * 58.7 + (double)px[2] * 29.9;
        return luma <= crit->lo_d;
    case 24:
        luma = (double)px[0] * 11.4 + (double)px[1] * 58.7 + (double)px[2] * 29.9;
        return luma >= crit->lo_d && luma <= crit->hi_d;
    default:
        return 0;
    }
}

void *__cdecl FileArea(const uint8_t *bgra, int w, int h, int seed_x, int seed_y,
                       int mode, uint32_t threshold, void **out_coords, int *out_bbox4)
{
    FillCriterion crit;
    uint8_t *mask;
    FillSeed *stack;
    size_t stack_cap = 256;
    size_t stack_len = 0;
    FillCoord *coords = NULL;
    size_t coord_cap = 0;
    size_t coord_len = 0;
    int min_x;
    int max_x;
    int min_y;
    int max_y;

    if (!bgra || w <= 0 || h <= 0 || seed_x < 0 || seed_x >= w || seed_y < 0 || seed_y >= h)
    {
        return NULL;
    }

    if (!fill_init_criterion(bgra + ((size_t)seed_y * (size_t)w + (size_t)seed_x) * 4u,
                             mode, threshold, &crit))
    {
        return NULL;
    }

    mask = (uint8_t *)rt_calloc((size_t)w * (size_t)h, 1);
    stack = (FillSeed *)rt_malloc(stack_cap * sizeof(FillSeed));
    if (!mask || !stack)
    {
        rt_free(mask);
        rt_free(stack);
        return NULL;
    }

    if (out_coords)
    {
        *out_coords = NULL;
        coord_cap = 128;
        coords = (FillCoord *)rt_malloc(coord_cap * sizeof(FillCoord));
        if (!coords)
        {
            rt_free(mask);
            rt_free(stack);
            return NULL;
        }
    }

    min_x = max_x = seed_x;
    min_y = max_y = seed_y;

    #define RECORD_PIXEL(px_x, px_y, px_idx) do { \
        mask[px_idx] = 1; \
        if ((px_x) < min_x) min_x = (px_x); \
        if ((px_x) > max_x) max_x = (px_x); \
        if ((px_y) < min_y) min_y = (px_y); \
        if ((px_y) > max_y) max_y = (px_y); \
        if (out_coords) { \
            if (coord_len + 1 >= coord_cap) { \
                size_t new_cap = coord_cap * 2; \
                FillCoord *nc = (FillCoord *)rt_realloc(coords, new_cap * sizeof(FillCoord)); \
                if (!nc) { rt_free(coords); rt_free(stack); rt_free(mask); return NULL; } \
                coords = nc; \
                coord_cap = new_cap; \
            } \
            coords[coord_len].x = (uint16_t)(px_x); \
            coords[coord_len].y = (uint16_t)(px_y); \
            coords[coord_len].idx = (uint32_t)(px_idx); \
            coord_len++; \
        } \
    } while (0)

    #define PUSH_SEED(sx, sy) do { \
        if (stack_len >= stack_cap) { \
            size_t new_cap = stack_cap * 2; \
            FillSeed *ns = (FillSeed *)rt_realloc(stack, new_cap * sizeof(FillSeed)); \
            if (!ns) { rt_free(coords); rt_free(stack); rt_free(mask); return NULL; } \
            stack = ns; \
            stack_cap = new_cap; \
        } \
        stack[stack_len].x = (sx); \
        stack[stack_len].y = (sy); \
        stack_len++; \
    } while (0)

    RECORD_PIXEL(seed_x, seed_y, seed_y * w + seed_x);
    PUSH_SEED(seed_x, seed_y);

    while (stack_len > 0)
    {
        FillSeed cur = stack[--stack_len];
        int lx = cur.x;
        int rx = cur.x;
        int y = cur.y;
        int row_off = y * w;
        int dir;

        while (lx > 0 && !mask[row_off + lx - 1] &&
               fill_test_pixel(bgra + (size_t)(row_off + lx - 1) * 4u, &crit))
        {
            lx--;
            RECORD_PIXEL(lx, y, row_off + lx);
        }
        while (rx + 1 < w && !mask[row_off + rx + 1] &&
               fill_test_pixel(bgra + (size_t)(row_off + rx + 1) * 4u, &crit))
        {
            rx++;
            RECORD_PIXEL(rx, y, row_off + rx);
        }

        for (dir = -1; dir <= 1; dir += 2)
        {
            int ny = y + dir;
            int nrow;
            int in_span = 0;
            int x;
            if (ny < 0 || ny >= h)
            {
                continue;
            }
            nrow = ny * w;
            for (x = lx; x <= rx; x++)
            {
                int idx = nrow + x;
                if (!mask[idx] && fill_test_pixel(bgra + (size_t)idx * 4u, &crit))
                {
                    RECORD_PIXEL(x, ny, idx);
                    if (!in_span)
                    {
                        PUSH_SEED(x, ny);
                        in_span = 1;
                    }
                }
                else
                {
                    in_span = 0;
                }
            }
        }
    }

    #undef RECORD_PIXEL
    #undef PUSH_SEED

    rt_free(stack);

    if (out_coords)
    {
        coords[coord_len].x = 0xffffu;
        coords[coord_len].y = 0xffffu;
        coords[coord_len].idx = 0xffffffffu;
        *out_coords = coords;
    }
    if (out_bbox4)
    {
        out_bbox4[0] = min_x;
        out_bbox4[1] = min_y;
        out_bbox4[2] = (max_x - min_x) + 1;
        out_bbox4[3] = (max_y - min_y) + 1;
    }
    return mask;
}

typedef struct
{
    uint32_t flag;
    const char *name;
} ExEditObjEffectInfo;

static const ExEditObjEffectInfo k_exedit_effects[108] =
{
    { 0u, "\x93\xae\x89\xe6\x83\x74\x83\x40\x83\x43\x83\x8b" },
    { 0u, "\x89\xe6\x91\x9c\x83\x74\x83\x40\x83\x43\x83\x8b" },
    { 0u, "\x89\xb9\x90\xba\x83\x74\x83\x40\x83\x43\x83\x8b" },
    { 0u, "\x83\x65\x83\x4c\x83\x58\x83\x67" },
    { 0u, "\x90\x7d\x8c\x60" },
    { 0u, "\x83\x74\x83\x8c\x81\x5b\x83\x80\x83\x6f\x83\x62\x83\x74\x83\x40" },
    { 0u, "\x89\xb9\x90\xba\x94\x67\x8c\x60" },
    { 0u, "\x83\x56\x81\x5b\x83\x93" },
    { 0u, "\x83\x56\x81\x5b\x83\x93(\x89\xb9\x90\xba)" },
    { 0u, "\x92\xbc\x91\x4f\x83\x49\x83\x75\x83\x57\x83\x46\x83\x4e\x83\x67" },
    { 1u, "\x95\x57\x8f\x80\x95\x60\x89\xe6" },
    { 1u, "\x8a\x67\x92\xa3\x95\x60\x89\xe6" },
    { 0u, "\x89\xb9\x90\xba\x95\x57\x8f\x80\x8d\xc4\x90\xb6" },
    { 1u, "\x83\x70\x81\x5b\x83\x65\x83\x42\x83\x4e\x83\x8b\x8f\x6f\x97\xcd" },
    { 2u, "\x83\x56\x81\x5b\x83\x93\x83\x60\x83\x46\x83\x93\x83\x57(f)" },
    { 3u, "\x90\x46\x92\xb2\x95\xe2\x90\xb3" },
    { 2u, "\x90\x46\x92\xb2\x95\xe2\x90\xb3(f)" },
    { 3u, "\x83\x4e\x83\x8a\x83\x62\x83\x73\x83\x93\x83\x4f" },
    { 3u, "\x82\xda\x82\xa9\x82\xb5" },
    { 3u, "\x8b\xab\x8a\x45\x82\xda\x82\xa9\x82\xb5" },
    { 2u, "\x82\xda\x82\xa9\x82\xb5(f)" },
    { 3u, "\x83\x82\x83\x55\x83\x43\x83\x4e" },
    { 2u, "\x83\x82\x83\x55\x83\x43\x83\x4e(f)" },
    { 3u, "\x94\xad\x8c\xf5" },
    { 2u, "\x94\xad\x8c\xf5(f)" },
    { 3u, "\x91\x4d\x8c\xf5" },
    { 3u, "\x8a\x67\x8e\x55\x8c\xf5" },
    { 2u, "\x8a\x67\x8e\x55\x8c\xf5(f)" },
    { 3u, "\x83\x4f\x83\x8d\x81\x5b" },
    { 2u, "\x83\x4f\x83\x8d\x81\x5b(f)" },
    { 3u, "\x83\x4e\x83\x8d\x83\x7d\x83\x4c\x81\x5b" },
    { 3u, "\x83\x4a\x83\x89\x81\x5b\x83\x4c\x81\x5b" },
    { 3u, "\x83\x8b\x83\x7e\x83\x69\x83\x93\x83\x58\x83\x4c\x81\x5b" },
    { 3u, "\x83\x89\x83\x43\x83\x67" },
    { 3u, "\x83\x56\x83\x83\x83\x68\x81\x5b" },
    { 3u, "\x89\x8f\x8e\xe6\x82\xe8" },
    { 3u, "\x93\xca\x83\x47\x83\x62\x83\x57" },
    { 3u, "\x83\x47\x83\x62\x83\x57\x92\x8a\x8f\x6f" },
    { 3u, "\x83\x56\x83\x83\x81\x5b\x83\x76" },
    { 3u, "\x83\x74\x83\x46\x81\x5b\x83\x68" },
    { 3u, "\x83\x8f\x83\x43\x83\x76" },
    { 3u, "\x83\x7d\x83\x58\x83\x4e" },
    { 3u, "\x8e\xce\x82\xdf\x83\x4e\x83\x8a\x83\x62\x83\x73\x83\x93\x83\x4f" },
    { 3u, "\x95\xfa\x8e\xcb\x83\x75\x83\x89\x81\x5b" },
    { 2u, "\x95\xfa\x8e\xcb\x83\x75\x83\x89\x81\x5b(f)" },
    { 3u, "\x95\xfb\x8c\xfc\x83\x75\x83\x89\x81\x5b" },
    { 2u, "\x95\xfb\x8c\xfc\x83\x75\x83\x89\x81\x5b(f)" },
    { 3u, "\x83\x8c\x83\x93\x83\x59\x83\x75\x83\x89\x81\x5b" },
    { 2u, "\x83\x8c\x83\x93\x83\x59\x83\x75\x83\x89\x81\x5b(f)" },
    { 3u, "\x83\x82\x81\x5b\x83\x56\x83\x87\x83\x93\x83\x75\x83\x89\x81\x5b" },
    { 2u, "\x83\x82\x81\x5b\x83\x56\x83\x87\x83\x93\x83\x75\x83\x89\x81\x5b(f)" },
    { 3u, "\x8d\xc0\x95\x57" },
    { 3u, "\x8a\x67\x91\xe5\x97\xa6" },
    { 3u, "\x93\xa7\x96\xbe\x93\x78" },
    { 3u, "\x89\xf1\x93\x5d" },
    { 3u, "\x97\xcc\x88\xe6\x8a\x67\x92\xa3" },
    { 3u, "\x83\x8a\x83\x54\x83\x43\x83\x59" },
    { 3u, "\x83\x8d\x81\x5b\x83\x65\x81\x5b\x83\x56\x83\x87\x83\x93" },
    { 3u, "\x90\x55\x93\xae" },
    { 2u, "\x90\x55\x93\xae(f)" },
    { 3u, "\x94\xbd\x93\x5d" },
    { 2u, "\x94\xbd\x93\x5d(f)" },
    { 3u, "\x83\x7e\x83\x89\x81\x5b" },
    { 3u, "\x83\x89\x83\x58\x83\x5e\x81\x5b" },
    { 2u, "\x83\x89\x83\x58\x83\x5e\x81\x5b(f)" },
    { 3u, "\x94\x67\x96\xe4" },
    { 3u, "\x89\xe6\x91\x9c\x83\x8b\x81\x5b\x83\x76" },
    { 2u, "\x89\xe6\x91\x9c\x83\x8b\x81\x5b\x83\x76(f)" },
    { 3u, "\x8b\xc9\x8d\xc0\x95\x57\x95\xcf\x8a\xb7" },
    { 3u, "\x83\x66\x83\x42\x83\x58\x83\x76\x83\x8c\x83\x43\x83\x58\x83\x81\x83\x93\x83\x67\x83\x7d\x83\x62\x83\x76" },
    { 3u, "\x83\x6d\x83\x43\x83\x59" },
    { 3u, "\x90\x46\x82\xb8\x82\xea" },
    { 2u, "\x90\x46\x82\xb8\x82\xea(f)" },
    { 3u, "\x92\x50\x90\x46\x89\xbb" },
    { 2u, "\x92\x50\x90\x46\x89\xbb(f)" },
    { 3u, "\x83\x4f\x83\x89\x83\x66\x81\x5b\x83\x56\x83\x87\x83\x93" },
    { 3u, "\x8a\x67\x92\xa3\x90\x46\x8e\x77\x92\xe8" },
    { 2u, "\x8a\x67\x92\xa3\x90\x46\x8e\x77\x92\xe8(f)" },
    { 3u, "\x93\xc1\x92\xe8\x90\x46\x88\xe6\x95\xcf\x8a\xb7" },
    { 3u, "\x83\x41\x83\x6a\x83\x81\x81\x5b\x83\x56\x83\x87\x83\x93\x8c\xf8\x89\xca" },
    { 0u, "\x83\x4a\x83\x58\x83\x5e\x83\x80\x83\x49\x83\x75\x83\x57\x83\x46\x83\x4e\x83\x67" },
    { 3u, "\x83\x58\x83\x4e\x83\x8a\x83\x76\x83\x67\x90\xa7\x8c\xe4" },
    { 3u, "\x93\xae\x89\xe6\x83\x74\x83\x40\x83\x43\x83\x8b\x8d\x87\x90\xac" },
    { 3u, "\x89\xe6\x91\x9c\x83\x74\x83\x40\x83\x43\x83\x8b\x8d\x87\x90\xac" },
    { 3u, "\x83\x43\x83\x93\x83\x5e\x81\x5b\x83\x8c\x81\x5b\x83\x58\x89\xf0\x8f\x9c" },
    { 3u, "\x83\x4a\x83\x81\x83\x89\x90\xa7\x8c\xe4\x83\x49\x83\x76\x83\x56\x83\x87\x83\x93" },
    { 3u, "\x83\x49\x83\x74\x83\x58\x83\x4e\x83\x8a\x81\x5b\x83\x93\x95\x60\x89\xe6" },
    { 3u, "\x83\x49\x83\x75\x83\x57\x83\x46\x83\x4e\x83\x67\x95\xaa\x8a\x84" },
    { 2u, "\x95\x94\x95\xaa\x83\x74\x83\x42\x83\x8b\x83\x5e(f)" },
    { 2u, "\x89\xb9\x97\xca\x83\x74\x83\x46\x81\x5b\x83\x68(f)" },
    { 2u, "\x89\xb9\x90\xba\x83\x66\x83\x42\x83\x8c\x83\x43(f)" },
    { 2u, "\x89\xb9\x90\xba\x83\x66\x83\x42\x83\x8c\x83\x43(f)" },
    { 2u, "\x83\x82\x83\x6d\x83\x89\x83\x8b\x89\xbb(f)" },
    { 0u, "\x8e\x9e\x8a\xd4\x90\xa7\x8c\xe4" },
    { 0u, "\x83\x4f\x83\x8b\x81\x5b\x83\x76\x90\xa7\x8c\xe4" },
    { 0u, "\x83\x4a\x83\x81\x83\x89\x90\xa7\x8c\xe4" },
    { 0u, "\x83\x4a\x83\x81\x83\x89\x90\xa7\x8c\xe4(\x8a\x67\x92\xa3\x95\x60\x89\xe6)" },
    { 3u, "\x83\x4a\x83\x81\x83\x89\x8c\xf8\x89\xca" },
    { 3u, "\x83\x56\x83\x83\x83\x68\x81\x5b(\x83\x4a\x83\x81\x83\x89\x8c\xf8\x89\xca)" },
    { 3u, "\x83\x58\x83\x4e\x83\x8a\x83\x76\x83\x67(\x83\x4a\x83\x81\x83\x89\x8c\xf8\x89\xca)" },
    { 2u, "\x83\x6d\x83\x43\x83\x59\x8f\x9c\x8b\x8e\x83\x74\x83\x42\x83\x8b\x83\x5e(f)" },
    { 2u, "\x83\x56\x83\x83\x81\x5b\x83\x76\x83\x74\x83\x42\x83\x8b\x83\x5e(f)" },
    { 2u, "\x82\xda\x82\xa9\x82\xb5\x83\x74\x83\x42\x83\x8b\x83\x5e(f)" },
    { 2u, "\x83\x4e\x83\x8a\x83\x62\x83\x73\x83\x93\x83\x4f&\x83\x8a\x83\x54\x83\x43\x83\x59(f)" },
    { 2u, "\x89\x8f\x8e\xe6\x82\xe8\x82\xc2\x82\xd4\x82\xb5(f)" },
    { 2u, "\x90\x46\x92\xb2\x95\xe2\x90\xb3""2(f)" },
    { 2u, "\x8a\x67\x92\xa3\x90\x46\x92\xb2\x95\xe2\x90\xb3(f)" },
    { 2u, "\x89\xb9\x97\xca\x82\xcc\x92\xb2\x90\xdf(f)" }
};

const char *__cdecl GetObjectName(int effect_idx, uint32_t *out_flag, int *out_canon_idx)
{
    sync_host_state(NULL, NULL);
    if (g_exedit_hmod && effect_idx >= 0 && effect_idx < 108)
    {
        if (out_flag) *out_flag = k_exedit_effects[effect_idx].flag;
        if (out_canon_idx) *out_canon_idx = effect_idx;
        return k_exedit_effects[effect_idx].name;
    }
    if (out_flag) *out_flag = 0xffffffffu;
    if (out_canon_idx) *out_canon_idx = -1;
    return g_unknown_obj;
}

uint32_t __cdecl GetObjectProcessIndex(int return_index_mode)
{
    uint32_t *p_cur_obj_ptr = (uint32_t *)RikkyGetMemoryData(0x66);
    uint32_t *p_obj_array = (uint32_t *)RikkyGetMemoryData(0x6c);
    uint32_t obj_stride = (uint32_t)(uintptr_t)RikkyGetMemoryData(0x6d);
    int *p_proc_mode = (int *)RikkyGetMemoryData(0x72);
    uint32_t *p_alt_idx = (uint32_t *)RikkyGetMemoryData(0xaf);
    int *p_flag_b0 = (int *)RikkyGetMemoryData(0xb0);
    int *p_flag_b1 = (int *)RikkyGetMemoryData(0xb1);
    uint32_t *p_prev_obj = (uint32_t *)RikkyGetMemoryData(0xb2);
    uint32_t *p_stack_frame = (uint32_t *)RikkyGetMemoryData(0xb3);
    int off_b4 = (int)(intptr_t)RikkyGetMemoryData(0xb4);
    int off_b5 = (int)(intptr_t)RikkyGetMemoryData(0xb5);
    int off_b6 = (int)(intptr_t)RikkyGetMemoryData(0xb6);
    uint32_t obj_ptr = 0;

    if (!g_exedit_hmod || !p_cur_obj_ptr || !p_obj_array || !obj_stride ||
        !p_proc_mode || !p_alt_idx || !p_flag_b0 || !p_flag_b1 || !p_prev_obj || !p_stack_frame)
    {
        return 0;
    }

    if (*p_proc_mode == 0)
    {
        if (return_index_mode == 0)
        {
            return (*p_alt_idx) * obj_stride + (*p_obj_array);
        }
        return *p_alt_idx;
    }

    if (*p_flag_b0 == 1)
    {
        obj_ptr = *p_cur_obj_ptr;
    }
    else if (*p_prev_obj != *p_cur_obj_ptr && *p_flag_b1 != 0)
    {
        obj_ptr = *p_cur_obj_ptr;
    }
    else
    {
        uintptr_t base = (uintptr_t)(*p_stack_frame);
        if (base != 0)
        {
            uint32_t *slot;
            if (*(int *)(base - (uintptr_t)off_b4) == 0)
            {
                slot = (uint32_t *)(base - (uintptr_t)off_b5);
            }
            else
            {
                slot = (uint32_t *)(base - (uintptr_t)off_b5 - 4u);
            }
            obj_ptr = *slot;
            if (obj_ptr == 0)
            {
                obj_ptr = *(uint32_t *)((uintptr_t)slot - (uintptr_t)off_b6);
            }
        }
    }

    if (return_index_mode != 0 && obj_ptr >= *p_obj_array)
    {
        return (obj_ptr - *p_obj_array) / obj_stride;
    }
    return obj_ptr;
}

int __cdecl RikkyExitRegister(RikkyExitCallback cb, int user_data)
{
    int i;
    ExitEntry *next;
    if (!cb) return 0;
    for (i = 0; i < g_exit_count; i++)
    {
        if (g_exit_entries[i].cb == cb) return 1;
    }
    next = (ExitEntry *)rt_realloc(g_exit_entries, (size_t)(g_exit_count + 1) * sizeof(ExitEntry));
    if (!next) return 0;
    g_exit_entries = next;
    g_exit_entries[g_exit_count].cb = cb;
    g_exit_entries[g_exit_count].user = (uint32_t)user_data;
    g_exit_count++;
    return 1;
}

int __cdecl RikkySaveRegister(const char *key, RikkySaveCallback save_cb, uint32_t save_user)
{
    int i;
    char *key_copy;
    SaveEntry *next;

    if (!key || !save_cb) return 0;
    for (i = 0; i < g_save_count; i++)
    {
        if (g_save_entries[i].cb == save_cb) return 1;
    }

    key_copy = (char *)rt_malloc(strlen(key) + 1);
    if (!key_copy) return 0;
    next = (SaveEntry *)rt_realloc(g_save_entries, (size_t)(g_save_count + 1) * sizeof(SaveEntry));
    if (!next)
    {
        rt_free(key_copy);
        return 0;
    }
    strcpy(key_copy, key);
    g_save_entries = next;
    g_save_entries[g_save_count].key = key_copy;
    g_save_entries[g_save_count].cb = save_cb;
    g_save_entries[g_save_count].user = save_user;
    g_save_entries[g_save_count].queried_size = 0;
    g_save_count++;
    return 1;
}

int __cdecl RikkyLoadRegister(const char *key, RikkyLoadCallback load_cb, uint32_t load_user,
                              RikkyResetCallback reset_cb, uint32_t reset_user)
{
    int i;

    if (reset_cb)
    {
        int exists = 0;
        for (i = 0; i < g_reset_count; i++)
        {
            if (g_reset_entries[i].cb == reset_cb)
            {
                exists = 1;
                break;
            }
        }
        if (!exists)
        {
            ResetEntry *next = (ResetEntry *)rt_realloc(
                g_reset_entries, (size_t)(g_reset_count + 1) * sizeof(ResetEntry));
            if (next)
            {
                g_reset_entries = next;
                g_reset_entries[g_reset_count].cb = reset_cb;
                g_reset_entries[g_reset_count].user = reset_user;
                g_reset_count++;
            }
        }
    }

    if (!key || !load_cb) return 0;

    for (i = 0; i < g_loaded_count; i++)
    {
        if (strcmp(g_loaded_records[i].key, key) == 0)
        {
            uint32_t ctx3[3];
            ctx3[0] = load_user;
            ctx3[1] = (uint32_t)(uintptr_t)g_loaded_records[i].exfunc;
            ctx3[2] = (uint32_t)(uintptr_t)g_loaded_records[i].editp;
            load_cb(g_loaded_records[i].data, g_loaded_records[i].size, ctx3);

            rt_free(g_loaded_records[i].key);
            rt_free(g_loaded_records[i].data);
            if (i + 1 < g_loaded_count)
            {
                memmove(&g_loaded_records[i], &g_loaded_records[i + 1],
                        (size_t)(g_loaded_count - i - 1) * sizeof(LoadedRecord));
            }
            g_loaded_count--;
            if (g_loaded_count == 0)
            {
                rt_free(g_loaded_records);
                g_loaded_records = NULL;
            }
            else
            {
                LoadedRecord *shrunk = (LoadedRecord *)rt_realloc(
                    g_loaded_records, (size_t)g_loaded_count * sizeof(LoadedRecord));
                if (shrunk) g_loaded_records = shrunk;
            }
            return 1;
        }
    }
    return 0;
}

int __cdecl RikkyBackupRegister_impl(FILTER *fp)
{
    int i;
    FILTER **next;
    if (!fp || (!fp->func_project_load && !fp->func_project_save))
    {
        return 0;
    }
    for (i = 0; i < g_backup_count; i++)
    {
        if (g_backup_filters[i] == fp) return 1;
    }
    next = (FILTER **)rt_realloc(g_backup_filters, (size_t)(g_backup_count + 1) * sizeof(FILTER *));
    if (!next) return 0;
    g_backup_filters = next;
    g_backup_filters[g_backup_count++] = fp;
    return 1;
}

static int get_exedit_backup_dir(char *out_dir, size_t cap)
{
    char mod_path[MAX_PATH * 2];
    char *pos;
    if (!g_exedit_hmod || cap < MAX_PATH) return 0;
    if (!GetModuleFileNameA(g_exedit_hmod, mod_path, sizeof(mod_path))) return 0;
    pos = strstr(mod_path, "exedit.auf");
    if (!pos || pos == mod_path || (pos[-1] != '\\' && pos[-1] != '/') || pos[10] != '\0') return 0;
    *pos = '\0';
    snprintf(out_dir, cap, "%sbackup", mod_path);
    return 1;
}

static int try_load_rikkym_backup(FILTER *fp, void *editp)
{
    SYS_INFO si;
    char bkp_dir[MAX_PATH * 2];
    char pattern[MAX_PATH * 2];
    WIN32_FIND_DATAA fd;
    HANDLE h_find;

    if (!g_exedit_hmod || !fp || !fp->exfunc || !fp->exfunc->get_sys_info) return 0;
    memset(&si, 0, sizeof(si));
    if (!fp->exfunc->get_sys_info(editp, &si)) return 0;
    if (si.project_name && si.project_name[0] != '\0') return 0;
    if (!get_exedit_backup_dir(bkp_dir, sizeof(bkp_dir))) return 0;

    snprintf(pattern, sizeof(pattern), "%s\\*.exedit_backup", bkp_dir);
    h_find = FindFirstFileA(pattern, &fd);
    if (h_find == INVALID_HANDLE_VALUE) return 0;

    do
    {
        char ex_file[MAX_PATH * 2];
        HANDLE h_probe;
        snprintf(ex_file, sizeof(ex_file), "%s\\%s", bkp_dir, fd.cFileName);
        h_probe = CreateFileA(ex_file, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h_probe != INVALID_HANDLE_VALUE)
        {
            CloseHandle(h_probe);
            continue;
        }
        if (GetLastError() == ERROR_SHARING_VIOLATION)
        {
            char rk_file[MAX_PATH * 2];
            char *ext;
            FILE *rk_fp;
            strcpy(rk_file, ex_file);
            ext = strstr(rk_file, ".exedit_backup");
            if (ext) strcpy(ext, ".rikkym_backup");
            rk_fp = fopen(rk_file, "rb");
            if (rk_fp)
            {
                for (;;)
                {
                    char flt_name[256];
                    int ch;
                    int idx = 0;
                    uint32_t chunk_sz = 0;
                    int matched = 0;
                    int i;

                    while ((ch = fgetc(rk_fp)) != EOF && idx < 255)
                    {
                        flt_name[idx++] = (char)ch;
                        if (ch == 0) break;
                    }
                    if (ch == EOF) break;
                    flt_name[255] = '\0';
                    if (fread(&chunk_sz, 4, 1, rk_fp) != 1) break;

                    for (i = 0; i < g_backup_count; i++)
                    {
                        FILTER *bf = g_backup_filters[i];
                        if (bf && bf->name && bf->func_project_load && strcmp(flt_name, bf->name) == 0)
                        {
                            void *buf = chunk_sz ? rt_malloc(chunk_sz) : NULL;
                            if (chunk_sz == 0 || buf)
                            {
                                if (chunk_sz == 0 || fread(buf, 1, chunk_sz, rk_fp) == chunk_sz)
                                {
                                    bf->func_project_load(bf, editp, buf, (int)chunk_sz);
                                }
                                rt_free(buf);
                                matched = 1;
                            }
                            break;
                        }
                    }
                    if (!matched)
                    {
                        fseek(rk_fp, (long)chunk_sz, SEEK_CUR);
                    }
                }
                fclose(rk_fp);
                FindClose(h_find);
                return 1;
            }
            break;
        }
    }
    while (FindNextFileA(h_find, &fd));

    FindClose(h_find);
    return 0;
}

static int try_save_rikkym_backup(void *editp, int prune_orphans)
{
    char bkp_dir[MAX_PATH * 2];
    char stamp[64];
    char ex_path[MAX_PATH * 2];
    char rk_path[MAX_PATH * 2];
    time_t now;
    struct tm *lt;
    FILE *probe_fp;
    FILE *out_fp;
    int wrote_any = 0;
    int i;

    if (!get_exedit_backup_dir(bkp_dir, sizeof(bkp_dir))) return 1;

    if (prune_orphans)
    {
        char pattern[MAX_PATH * 2];
        WIN32_FIND_DATAA fd;
        HANDLE h_find;
        snprintf(pattern, sizeof(pattern), "%s\\*.rikkym_backup", bkp_dir);
        h_find = FindFirstFileA(pattern, &fd);
        if (h_find != INVALID_HANDLE_VALUE)
        {
            do
            {
                char pair_ex[MAX_PATH * 2];
                char *ext;
                FILE *f;
                snprintf(pair_ex, sizeof(pair_ex), "%s\\%s", bkp_dir, fd.cFileName);
                ext = strstr(pair_ex, ".rikkym_backup");
                if (ext)
                {
                    strcpy(ext, ".exedit_backup");
                    f = fopen(pair_ex, "rb");
                    if (f)
                    {
                        fclose(f);
                    }
                    else
                    {
                        char stale[MAX_PATH * 2];
                        snprintf(stale, sizeof(stale), "%s\\%s", bkp_dir, fd.cFileName);
                        remove(stale);
                    }
                }
            }
            while (FindNextFileA(h_find, &fd));
            FindClose(h_find);
        }
    }

    if (g_backup_count <= 0) return 1;

    now = time(NULL);
    lt = localtime(&now);
    if (!lt) return 0;
    snprintf(stamp, sizeof(stamp), "%d-%02d-%02d_%02d-%02d-%02d",
             lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday,
             lt->tm_hour, lt->tm_min, lt->tm_sec);

    snprintf(ex_path, sizeof(ex_path), "%s\\%s.exedit_backup", bkp_dir, stamp);
    probe_fp = fopen(ex_path, "rb");
    if (!probe_fp) return 0;
    fclose(probe_fp);

    snprintf(rk_path, sizeof(rk_path), "%s\\%s.rikkym_backup", bkp_dir, stamp);
    out_fp = fopen(rk_path, "wb");
    if (!out_fp) return 1;

    for (i = 0; i < g_backup_count; i++)
    {
        FILTER *bf = g_backup_filters[i];
        int sz = 0;
        if (!bf || !bf->func_project_save || !bf->name) continue;
        bf->func_project_save(bf, editp, NULL, &sz);
        if (sz > 0)
        {
            void *buf = rt_malloc((size_t)sz);
            if (buf)
            {
                bf->func_project_save(bf, editp, buf, &sz);
                fwrite(bf->name, 1, strlen(bf->name) + 1, out_fp);
                fwrite(&sz, 4, 1, out_fp);
                fwrite(buf, 1, (size_t)sz, out_fp);
                rt_free(buf);
                wrote_any = 1;
            }
        }
    }
    fclose(out_fp);
    if (!wrote_any) remove(rk_path);
    return 1;
}

static void run_exit_callbacks_once(FILTER *fp)
{
    if (g_exit_ran) return;
    g_exit_ran = 1;

    unsubclass_exedit_dlg();

    if (g_exit_entries && g_exit_count > 0)
    {
        EXFUNC *exf = fp ? fp->exfunc : (g_fp ? g_fp->exfunc : NULL);
        int i;
        for (i = 0; i < g_exit_count; i++)
        {
            uint32_t ctx2[2];
            ctx2[0] = g_exit_entries[i].user;
            ctx2[1] = (uint32_t)(uintptr_t)exf;
            if (g_exit_entries[i].cb)
            {
                g_exit_entries[i].cb(ctx2);
            }
        }
    }
    rt_free(g_exit_entries);
    g_exit_entries = NULL;
    g_exit_count = 0;
}

static void free_loaded_records(void)
{
    int i;
    for (i = 0; i < g_loaded_count; i++)
    {
        rt_free(g_loaded_records[i].key);
        rt_free(g_loaded_records[i].data);
    }
    rt_free(g_loaded_records);
    g_loaded_records = NULL;
    g_loaded_count = 0;
}

static void free_save_entries(void)
{
    int i;
    for (i = 0; i < g_save_count; i++)
    {
        rt_free(g_save_entries[i].key);
    }
    rt_free(g_save_entries);
    g_save_entries = NULL;
    g_save_count = 0;
}

static void free_reset_entries(void)
{
    rt_free(g_reset_entries);
    g_reset_entries = NULL;
    g_reset_count = 0;
}

static BOOL __cdecl filter_func_init(FILTER *fp)
{
    rt_alloc_init();
    g_exit_ran = 0;
    g_initialized = 1;
    sync_host_state(fp, NULL);
    RikkyBackupRegister_impl(fp);
    return TRUE;
}

static BOOL __cdecl filter_func_exit(FILTER *fp)
{
    run_exit_callbacks_once(fp);
    free_save_entries();
    free_loaded_records();
    free_reset_entries();
    rt_free(g_backup_filters);
    g_backup_filters = NULL;
    g_backup_count = 0;
    rt_free(g_sound_reg_state.entries);
    memset(&g_sound_reg_state, 0, sizeof(g_sound_reg_state));
    g_initialized = 0;
    return TRUE;
}

static BOOL __cdecl filter_func_WndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam,
                                        void *editp, FILTER *fp)
{
    (void)wparam;
    (void)lparam;
    sync_host_state(fp, editp);

    if (message == WM_FILTER_INIT)
    {
        return FALSE;
    }
    if (message == WM_FILTER_EXIT)
    {
        if (g_timer_armed && hwnd)
        {
            KillTimer(hwnd, 1);
            g_timer_armed = 0;
        }
        run_exit_callbacks_once(fp);
        return FALSE;
    }

    if (message == WM_FILTER_FILE_OPEN)
    {
        if (g_exedit_hmod)
        {
            try_load_rikkym_backup(fp, editp);
            g_exedit_bkp_last_tick = (uint32_t *)RikkyGetMemoryData(0xb8);
            g_exedit_bkp_interval = g_exedit_bkp_last_tick ? (g_exedit_bkp_last_tick - 1) : NULL;
            g_exedit_bkp_dirty = (int *)RikkyGetMemoryData(0xb9);
            if (g_exedit_bkp_last_tick && g_exedit_bkp_dirty)
            {
                g_cached_bkp_tick = *g_exedit_bkp_last_tick;
                g_cached_bkp_dirty = *g_exedit_bkp_dirty;
            }
        }
        return FALSE;
    }

    if (message == WM_TIMER)
    {
        if (try_save_rikkym_backup(editp, 0) != 0)
        {
            if (GetTickCount() - g_last_poll_tick >= 1001u)
            {
                KillTimer(hwnd, 1);
                g_timer_armed = 0;
            }
        }
        return FALSE;
    }

    if (message == WM_FILTER_UPDATE || message == WM_FILTER_SAVE_START ||
        message == WM_FILTER_SAVE_END || message == WM_FILTER_FILE_UPDATE)
    {
        if (g_exedit_bkp_last_tick && g_exedit_bkp_interval && g_exedit_bkp_dirty)
        {
            if (*g_exedit_bkp_dirty != g_cached_bkp_dirty)
            {
                g_cached_bkp_tick = *g_exedit_bkp_last_tick;
                g_cached_bkp_dirty = *g_exedit_bkp_dirty;
            }
            g_last_poll_tick = GetTickCount();
            if (g_cached_bkp_dirty == 0 ||
                g_last_poll_tick < (*g_exedit_bkp_interval + g_cached_bkp_tick))
            {
                if (g_timer_armed)
                {
                    KillTimer(hwnd, 1);
                    g_timer_armed = 0;
                }
            }
            else
            {
                if (try_save_rikkym_backup(editp, 1) == 0)
                {
                    SetTimer(hwnd, 1, 2, NULL);
                    g_timer_armed = 1;
                }
                g_cached_bkp_tick = *g_exedit_bkp_last_tick;
            }
        }
    }

    return FALSE;
}

static BOOL __cdecl filter_func_project_load(FILTER *fp, void *editp, void *data, int size)
{
    const uint8_t *p = (const uint8_t *)data;
    const uint8_t *end = p ? (p + size) : NULL;
    int valid_count = 0;
    const uint8_t *scan;
    int out_idx = 0;

    sync_host_state(fp, editp);

    if (g_reset_entries && g_reset_count > 0)
    {
        int i;
        for (i = 0; i < g_reset_count; i++)
        {
            uint32_t ctx3[3];
            ctx3[0] = g_reset_entries[i].user;
            ctx3[1] = (uint32_t)(uintptr_t)(fp ? fp->exfunc : NULL);
            ctx3[2] = (uint32_t)(uintptr_t)editp;
            if (g_reset_entries[i].cb)
            {
                g_reset_entries[i].cb(ctx3);
            }
        }
    }
    free_reset_entries();
    free_save_entries();
    free_loaded_records();

    if (!p || size <= 0)
    {
        return TRUE;
    }

    scan = p;
    while (scan < end)
    {
        size_t max_rem = (size_t)(end - scan);
        const uint8_t *nul = (const uint8_t *)memchr(scan, 0, max_rem);
        uint8_t valid;
        uint32_t rec_sz;
        if (!nul || (size_t)(end - (nul + 1)) < 5u)
        {
            break;
        }
        valid = nul[1];
        memcpy(&rec_sz, nul + 2, 4);
        if ((size_t)(end - (nul + 6)) < (size_t)rec_sz)
        {
            break;
        }
        if (valid != 0)
        {
            valid_count++;
        }
        scan = nul + 6 + rec_sz;
    }

    if (valid_count <= 0)
    {
        return TRUE;
    }

    g_loaded_records = (LoadedRecord *)rt_malloc((size_t)valid_count * sizeof(LoadedRecord));
    if (!g_loaded_records)
    {
        return TRUE;
    }

    scan = p;
    while (scan < end && out_idx < valid_count)
    {
        size_t max_rem = (size_t)(end - scan);
        const uint8_t *nul = (const uint8_t *)memchr(scan, 0, max_rem);
        size_t key_len;
        uint8_t valid;
        uint32_t rec_sz;
        if (!nul) break;
        key_len = (size_t)(nul - scan);
        if ((size_t)(end - (nul + 1)) < 5u) break;
        valid = nul[1];
        memcpy(&rec_sz, nul + 2, 4);
        if ((size_t)(end - (nul + 6)) < (size_t)rec_sz) break;

        if (valid != 0)
        {
            char *kcopy = (char *)rt_malloc(key_len + 1);
            void *dcopy = rt_malloc(rec_sz ? rec_sz : 1u);
            if (kcopy && dcopy)
            {
                memcpy(kcopy, scan, key_len + 1);
                if (rec_sz > 0) memcpy(dcopy, nul + 6, rec_sz);
                g_loaded_records[out_idx].key = kcopy;
                g_loaded_records[out_idx].data = dcopy;
                g_loaded_records[out_idx].exfunc = fp ? fp->exfunc : NULL;
                g_loaded_records[out_idx].editp = editp;
                g_loaded_records[out_idx].size = rec_sz;
                out_idx++;
            }
            else
            {
                rt_free(kcopy);
                rt_free(dcopy);
            }
        }
        scan = nul + 6 + rec_sz;
    }
    g_loaded_count = out_idx;
    return TRUE;
}

static BOOL __cdecl filter_func_project_save(FILTER *fp, void *editp, void *data, int *size)
{
    int i;
    sync_host_state(fp, editp);
    if (!size) return FALSE;

    if (data == NULL)
    {
        int total = 0;
        for (i = 0; i < g_save_count; i++)
        {
            uint32_t ctx3[3];
            int qsz = 0;
            int ok;
            ctx3[0] = g_save_entries[i].user;
            ctx3[1] = (uint32_t)(uintptr_t)(fp ? fp->exfunc : NULL);
            ctx3[2] = (uint32_t)(uintptr_t)editp;
            ok = g_save_entries[i].cb(NULL, &qsz, ctx3);
            if (ok != 0 && qsz > 0)
            {
                g_save_entries[i].queried_size = qsz;
                total += (int)strlen(g_save_entries[i].key) + 6 + qsz;
            }
            else
            {
                g_save_entries[i].queried_size = 0;
            }
        }
        for (i = 0; i < g_loaded_count; i++)
        {
            total += (int)strlen(g_loaded_records[i].key) + 6 + (int)g_loaded_records[i].size;
        }
        *size = total;
        return TRUE;
    }
    else
    {
        uint8_t *dst = (uint8_t *)data;
        for (i = 0; i < g_save_count; i++)
        {
            int qsz = g_save_entries[i].queried_size;
            size_t klen;
            void *tmp;
            uint32_t sz_le;
            if (qsz <= 0) continue;
            klen = strlen(g_save_entries[i].key);
            tmp = rt_malloc((size_t)qsz);
            memcpy(dst, g_save_entries[i].key, klen + 1);
            dst += klen + 1;

            sz_le = (uint32_t)qsz;
            if (!tmp)
            {
                dst[0] = 0;
                memcpy(dst + 1, &sz_le, 4);
                memset(dst + 5, 0, (size_t)qsz);
            }
            else
            {
                uint32_t ctx3[3];
                int io_sz = qsz;
                ctx3[0] = g_save_entries[i].user;
                ctx3[1] = (uint32_t)(uintptr_t)(fp ? fp->exfunc : NULL);
                ctx3[2] = (uint32_t)(uintptr_t)editp;
                dst[0] = 1;
                memcpy(dst + 1, &sz_le, 4);
                if (g_save_entries[i].cb(tmp, &io_sz, ctx3) != 0)
                {
                    memcpy(dst + 5, tmp, (size_t)qsz);
                }
                else
                {
                    memset(dst + 5, 0, (size_t)qsz);
                }
                rt_free(tmp);
            }
            dst += 5 + qsz;
            g_save_entries[i].queried_size = 0;
        }
        for (i = 0; i < g_loaded_count; i++)
        {
            size_t klen = strlen(g_loaded_records[i].key);
            uint32_t sz_le = g_loaded_records[i].size;
            memcpy(dst, g_loaded_records[i].key, klen + 1);
            dst += klen + 1;
            dst[0] = 1;
            memcpy(dst + 1, &sz_le, 4);
            if (sz_le > 0)
            {
                memcpy(dst + 5, g_loaded_records[i].data, sz_le);
            }
            dst += 5 + sz_le;
        }
        return TRUE;
    }
}

static FILTER_DLL g_filter_dll =
{
    FILTER_FLAG_ALWAYS_ACTIVE | FILTER_FLAG_PRIORITY_LOWEST |
        FILTER_FLAG_DISP_FILTER | FILTER_FLAG_EX_INFORMATION,
    0, 0,
    g_plugin_name,
    0, NULL, NULL, NULL, NULL,
    0, NULL, NULL,
    NULL,
    filter_func_init,
    filter_func_exit,
    NULL,
    filter_func_WndProc,
    NULL, NULL, NULL, 0,
    g_plugin_info,
    NULL, NULL,
    NULL, NULL, NULL, NULL,
    NULL,
    filter_func_project_load,
    filter_func_project_save,
    NULL,
    NULL,
    { 0, 0 }
};

FILTER_DLL *__stdcall GetFilterTable(void)
{
    return &g_filter_dll;
}
#pragma comment(linker, "/EXPORT:GetFilterTable@0=_GetFilterTable@0,@5")

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hinst);
    }
    else if (reason == DLL_PROCESS_DETACH && g_initialized)
    {
        unsubclass_exedit_dlg();
    }
    return TRUE;
}

