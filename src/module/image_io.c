#define COBJMACROS
#include <windows.h>
#include <objidl.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rikky_module_internal.h"

// gdi+ flat c api declarations (msvc <gdiplus.h> is c++ only)

typedef int GpStatus;
typedef int PixelFormat;
typedef struct GpImage GpImage;
typedef struct GpBitmap GpBitmap;

#define GDIP_OK 0
#define GDIP_PIXELFORMAT_24BPP_RGB 0x00021808
#define GDIP_PIXELFORMAT_32BPP_ARGB 0x0026200A
#define GDIP_LOCKMODE_READ 0x0001u
#define GDIP_ENCODER_VALUE_TYPE_LONG 4u

typedef struct
{
    INT X;
    INT Y;
    INT Width;
    INT Height;
} GpRect;

typedef struct
{
    UINT Width;
    UINT Height;
    INT Stride;
    PixelFormat PixelFormat;
    VOID *Scan0;
    UINT_PTR Reserved;
} GpBitmapData;

typedef struct
{
    UINT32 GdiplusVersion;
    void *DebugEventCallback;
    BOOL SuppressBackgroundThread;
    BOOL SuppressExternalCodecs;
} GdiplusStartupInput;

typedef struct
{
    GUID Guid;
    ULONG NumberOfValues;
    ULONG Type;
    VOID *Value;
} GdipEncoderParameter;

typedef struct
{
    UINT Count;
    GdipEncoderParameter Parameter[1];
} GdipEncoderParameters;

__declspec(dllimport) GpStatus __stdcall GdiplusStartup(
    ULONG_PTR *token,
    const GdiplusStartupInput *input,
    void *output);
__declspec(dllimport) GpStatus __stdcall GdipCreateBitmapFromScan0(
    INT width,
    INT height,
    INT stride,
    PixelFormat format,
    BYTE *scan0,
    GpBitmap **bitmap);
__declspec(dllimport) GpStatus __stdcall GdipCreateBitmapFromStream(
    IStream *stream,
    GpBitmap **bitmap);
__declspec(dllimport) GpStatus __stdcall GdipSaveImageToFile(
    GpImage *image,
    const WCHAR *filename,
    const CLSID *clsidEncoder,
    const GdipEncoderParameters *encoderParams);
__declspec(dllimport) GpStatus __stdcall GdipSaveImageToStream(
    GpImage *image,
    IStream *stream,
    const CLSID *clsidEncoder,
    const GdipEncoderParameters *encoderParams);
__declspec(dllimport) GpStatus __stdcall GdipGetImageWidth(
    GpImage *image,
    UINT *width);
__declspec(dllimport) GpStatus __stdcall GdipGetImageHeight(
    GpImage *image,
    UINT *height);
__declspec(dllimport) GpStatus __stdcall GdipBitmapLockBits(
    GpBitmap *bitmap,
    const GpRect *rect,
    UINT flags,
    PixelFormat format,
    GpBitmapData *lockedBitmapData);
__declspec(dllimport) GpStatus __stdcall GdipBitmapUnlockBits(
    GpBitmap *bitmap,
    GpBitmapData *lockedBitmapData);
__declspec(dllimport) GpStatus __stdcall GdipDisposeImage(
    GpImage *image);

static const CLSID k_clsid_png =
    {
        0x557cf406u, 0x1a04u, 0x11d3u, {0x9au, 0x73u, 0x00u, 0x00u, 0xf8u, 0x1eu, 0xf3u, 0x2eu}};

static const CLSID k_clsid_jpeg =
    {
        0x557cf401u, 0x1a04u, 0x11d3u, {0x9au, 0x73u, 0x00u, 0x00u, 0xf8u, 0x1eu, 0xf3u, 0x2eu}};

static const GUID k_guid_encoder_quality =
    {
        0x1d5be4b5u, 0xfa4au, 0x452du, {0x9cu, 0xddu, 0x5du, 0xb3u, 0x51u, 0x05u, 0xe7u, 0xebu}};

static ULONG_PTR g_gdip_token = 0;
static int g_gdip_ready = 0;

static int ensure_gdiplus(void)
{
    GdiplusStartupInput input;
    if (g_gdip_ready)
    {
        return 1;
    }
    memset(&input, 0, sizeof(input));
    input.GdiplusVersion = 1;
    if (GdiplusStartup(&g_gdip_token, &input, NULL) == GDIP_OK)
    {
        g_gdip_ready = 1;
        return 1;
    }
    return 0;
}

// path and exedit pixel helpers for bmp / png / jpg

static int str_ieq(const char *a, const char *b)
{
    while (*a && *b)
    {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
        {
            return 0;
        }
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static int resolve_save_path(const char *in_path, int fmt_kind, char *out_mb, size_t out_mb_cap,
                             WCHAR *out_wide, int out_wide_cap)
{
    char drive[_MAX_DRIVE + 8];
    char dir[_MAX_DIR + 8];
    char fname[_MAX_FNAME + 8];
    char ext[_MAX_EXT + 16];
    char folder[MAX_PATH * 2];
    int has_valid_dir = 0;

    if (!in_path || in_path[0] == '\0')
    {
        return 0;
    }

    memset(drive, 0, sizeof(drive));
    memset(dir, 0, sizeof(dir));
    memset(fname, 0, sizeof(fname));
    memset(ext, 0, sizeof(ext));
    _splitpath(in_path, drive, dir, fname, ext);

    if (fmt_kind == 0)
    {
        if (!str_ieq(ext, ".bmp"))
        {
            strcat(ext, ".bmp");
        }
    }
    else if (fmt_kind == 1)
    {
        if (!str_ieq(ext, ".png"))
        {
            strcat(ext, ".png");
        }
    }
    else
    {
        if (!str_ieq(ext, ".jpg") && !str_ieq(ext, ".jpeg"))
        {
            strcat(ext, ".jpg");
        }
    }

    snprintf(folder, sizeof(folder), "%s%s", drive, dir);
    if (folder[0] != '\0')
    {
        DWORD attrs = GetFileAttributesA(folder);
        if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0)
        {
            has_valid_dir = 1;
        }
    }

    if (!has_valid_dir)
    {
        char exe_path[MAX_PATH + 4];
        memset(exe_path, 0, sizeof(exe_path));
        if (GetModuleFileNameA(NULL, exe_path, MAX_PATH) != 0)
        {
            memset(drive, 0, sizeof(drive));
            memset(dir, 0, sizeof(dir));
            _splitpath(exe_path, drive, dir, NULL, NULL);
            snprintf(folder, sizeof(folder), "%s%s", drive, dir);
        }
        else
        {
            folder[0] = '\0';
        }
    }

    snprintf(out_mb, out_mb_cap, "%s%s%s", folder, fname, ext);
    if (out_wide && out_wide_cap > 0)
    {
        if (MultiByteToWideChar(CP_ACP, 0, out_mb, -1, out_wide, out_wide_cap) <= 0)
        {
            return 0;
        }
    }
    return 1;
}

static const uint32_t *query_obj_pixels(lua_State *L, int *out_w, int *out_h)
{
    const uint32_t *px;
    int w;
    int h;

    lua_getfield(L, LUA_GLOBALSINDEX, "obj");
    lua_getfield(L, -1, "getpixeldata");
    lua_call(L, 0, 3);
    px = (const uint32_t *)lua_touserdata(L, -3);
    w = (int)lua_tonumber(L, -2);
    h = (int)lua_tonumber(L, -1);
    lua_pop(L, 4);

    if (!px || w <= 0 || h <= 0)
    {
        return NULL;
    }
    *out_w = w;
    *out_h = h;
    return px;
}

int __cdecl lua_rikky_bmp(lua_State *L)
{
    const char *in_path;
    char out_mb[MAX_PATH * 3];
    WCHAR out_wide[MAX_PATH * 3];
    const uint32_t *bgra;
    int w = 0;
    int h = 0;
    int pad;
    uint32_t img_bytes;
    uint32_t file_bytes;
    uint8_t hdr[54];
    uint8_t *row_buf;
    size_t row_stride;
    FILE *fp;
    int y;

    if (lua_gettop(L) != 1)
    {
        return 0;
    }
    in_path = lua_tolstring(L, 1, NULL);
    if (!in_path || in_path[0] == '\0')
    {
        return 0;
    }
    if (!resolve_save_path(in_path, 0, out_mb, sizeof(out_mb), out_wide, MAX_PATH * 3))
    {
        return 0;
    }

    bgra = query_obj_pixels(L, &w, &h);
    if (!bgra)
    {
        return 0;
    }

    pad = (4 - ((w * 3) % 4)) % 4;
    row_stride = (size_t)(w * 3 + pad);
    img_bytes = (uint32_t)(row_stride * (size_t)h);
    file_bytes = img_bytes + 54u;

    fp = _wfopen(out_wide, L"wb");
    if (!fp)
    {
        fp = fopen(out_mb, "wb");
        if (!fp)
        {
            return 0;
        }
    }

    memset(hdr, 0, sizeof(hdr));
    hdr[0] = 0x42u;
    hdr[1] = 0x4du;
    memcpy(hdr + 2, &file_bytes, 4u);
    {
        uint32_t off_bits = 54u;
        uint32_t bi_size = 40u;
        int32_t bi_w = w;
        int32_t bi_h = h;
        uint32_t planes_bpp = 0x00180001u;
        memcpy(hdr + 10, &off_bits, 4u);
        memcpy(hdr + 14, &bi_size, 4u);
        memcpy(hdr + 18, &bi_w, 4u);
        memcpy(hdr + 22, &bi_h, 4u);
        memcpy(hdr + 26, &planes_bpp, 4u);
        memcpy(hdr + 34, &img_bytes, 4u);
    }
    fwrite(hdr, 1u, sizeof(hdr), fp);

    row_buf = (uint8_t *)rm_calloc(row_stride, 1u);
    if (row_buf)
    {
        for (y = h - 1; y >= 0; y--)
        {
            const uint32_t *srow = bgra + (size_t)y * (size_t)w;
            int x;
            for (x = 0; x < w; x++)
            {
                uint32_t px = srow[x];
                float af = (float)(px >> 24) / 255.0f;
                row_buf[x * 3 + 0] = (uint8_t)(int)((float)(px & 0xffu) * af);
                row_buf[x * 3 + 1] = (uint8_t)(int)((float)((px >> 8) & 0xffu) * af);
                row_buf[x * 3 + 2] = (uint8_t)(int)((float)((px >> 16) & 0xffu) * af);
            }
            fwrite(row_buf, 1u, row_stride, fp);
        }
        rm_free(row_buf);
    }

    fclose(fp);
    return 0;
}

int __cdecl lua_rikky_png(lua_State *L)
{
    const char *in_path;
    char out_mb[MAX_PATH * 3];
    WCHAR out_wide[MAX_PATH * 3];
    const uint32_t *bgra;
    int w = 0;
    int h = 0;
    GpBitmap *bmp = NULL;

    if (lua_gettop(L) != 1)
    {
        return 0;
    }
    in_path = lua_tolstring(L, 1, NULL);
    if (!in_path || in_path[0] == '\0')
    {
        return 0;
    }
    if (!resolve_save_path(in_path, 1, out_mb, sizeof(out_mb), out_wide, MAX_PATH * 3))
    {
        return 0;
    }

    bgra = query_obj_pixels(L, &w, &h);
    if (!bgra || !ensure_gdiplus())
    {
        return 0;
    }

    if (GdipCreateBitmapFromScan0(w, h, w * 4, GDIP_PIXELFORMAT_32BPP_ARGB, (BYTE *)bgra, &bmp) == GDIP_OK && bmp)
    {
        GdipSaveImageToFile((GpImage *)bmp, out_wide, &k_clsid_png, NULL);
        GdipDisposeImage((GpImage *)bmp);
    }
    return 0;
}

int __cdecl lua_rikky_jpg(lua_State *L)
{
    int top = lua_gettop(L);
    const char *in_path;
    int quality = 100;
    ULONG q_val;
    char out_mb[MAX_PATH * 3];
    WCHAR out_wide[MAX_PATH * 3];
    const uint32_t *bgra;
    int w = 0;
    int h = 0;
    int stride;
    uint8_t *bgr_buf;
    GpBitmap *bmp = NULL;
    GdipEncoderParameters enc_params;
    int x;
    int y;

    if (top < 1 || top > 2)
    {
        return 0;
    }
    in_path = lua_tolstring(L, 1, NULL);
    if (!in_path || in_path[0] == '\0')
    {
        return 0;
    }
    if (top >= 2 && lua_isnumber(L, 2))
    {
        quality = (int)lua_tointeger(L, 2);
    }
    if (quality < 0)
    {
        quality = 0;
    }
    else if (quality > 100)
    {
        quality = 100;
    }
    q_val = (ULONG)quality;

    if (!resolve_save_path(in_path, 2, out_mb, sizeof(out_mb), out_wide, MAX_PATH * 3))
    {
        return 0;
    }

    bgra = query_obj_pixels(L, &w, &h);
    if (!bgra || !ensure_gdiplus())
    {
        return 0;
    }

    stride = (w * 3 + 3) & ~3;
    bgr_buf = (uint8_t *)rm_calloc((size_t)stride * (size_t)h, 1u);
    if (!bgr_buf)
    {
        return 0;
    }

    for (y = 0; y < h; y++)
    {
        const uint32_t *srow = bgra + (size_t)y * (size_t)w;
        uint8_t *drow = bgr_buf + (size_t)y * (size_t)stride;
        for (x = 0; x < w; x++)
        {
            uint32_t px = srow[x];
            float af = (float)(px >> 24) / 255.0f;
            drow[x * 3 + 0] = (uint8_t)(int)((float)(px & 0xffu) * af);
            drow[x * 3 + 1] = (uint8_t)(int)((float)((px >> 8) & 0xffu) * af);
            drow[x * 3 + 2] = (uint8_t)(int)((float)((px >> 16) & 0xffu) * af);
        }
    }

    enc_params.Count = 1;
    enc_params.Parameter[0].Guid = k_guid_encoder_quality;
    enc_params.Parameter[0].NumberOfValues = 1;
    enc_params.Parameter[0].Type = GDIP_ENCODER_VALUE_TYPE_LONG;
    enc_params.Parameter[0].Value = &q_val;

    if (GdipCreateBitmapFromScan0(w, h, stride, GDIP_PIXELFORMAT_24BPP_RGB, bgr_buf, &bmp) == GDIP_OK && bmp)
    {
        GdipSaveImageToFile((GpImage *)bmp, out_wide, &k_clsid_jpeg, &enc_params);
        GdipDisposeImage((GpImage *)bmp);
    }

    rm_free(bgr_buf);
    return 0;
}

// in-memory png encode/decode and image banks for lua_rikky_image

typedef struct
{
    uint32_t w;
    uint32_t h;
    uint8_t key_type;
    int id;
    char *name;
    uint8_t *pixels;
} RmImageEntry;

typedef struct
{
    size_t size;
    uint8_t key_type;
    int id;
    char *name;
    uint8_t *png_data;
} RmPngEntry;

static RmImageEntry *g_img_entries = NULL;
static size_t g_img_count = 0;
static uint8_t *g_merge_buf = NULL;

static RmPngEntry *g_png_entries = NULL;
static size_t g_png_count = 0;

static const char *const k_obj_save_fields[12] =
    {
        "ox", "oy", "oz",
        "cx", "cy", "cz",
        "rx", "ry", "rz",
        "alpha", "zoom", "aspect"};

static char *dup_str_local(const char *s)
{
    size_t n;
    char *p;
    if (!s)
    {
        return NULL;
    }
    n = strlen(s);
    p = (char *)rm_malloc(n + 1u);
    if (p)
    {
        memcpy(p, s, n + 1u);
    }
    return p;
}

static uint8_t *encode_png_mem(const uint8_t *bgra, int w, int h, size_t *out_size)
{
    GpBitmap *bmp = NULL;
    IStream *stream = NULL;
    uint8_t *out = NULL;

    *out_size = 0;
    if (!bgra || w <= 0 || h <= 0 || !ensure_gdiplus())
    {
        return NULL;
    }

    if (CreateStreamOnHGlobal(NULL, TRUE, &stream) != S_OK || !stream)
    {
        return NULL;
    }

    if (GdipCreateBitmapFromScan0(w, h, w * 4, GDIP_PIXELFORMAT_32BPP_ARGB, (BYTE *)bgra, &bmp) == GDIP_OK && bmp)
    {
        if (GdipSaveImageToStream((GpImage *)bmp, stream, &k_clsid_png, NULL) == GDIP_OK)
        {
            STATSTG st;
            HGLOBAL hg = NULL;
            memset(&st, 0, sizeof(st));
            if (IStream_Stat(stream, &st, STATFLAG_NONAME) == S_OK && st.cbSize.QuadPart > 0 &&
                GetHGlobalFromStream(stream, &hg) == S_OK && hg)
            {
                size_t sz = (size_t)st.cbSize.QuadPart;
                const void *src = GlobalLock(hg);
                if (src)
                {
                    out = (uint8_t *)rm_malloc(sz);
                    if (out)
                    {
                        memcpy(out, src, sz);
                        *out_size = sz;
                    }
                    GlobalUnlock(hg);
                }
            }
        }
        GdipDisposeImage((GpImage *)bmp);
    }

    IStream_Release(stream);
    return out;
}

static uint8_t *decode_png_mem(const uint8_t *png_data, size_t png_size, int *out_w, int *out_h)
{
    HGLOBAL hg;
    void *mem;
    IStream *stream = NULL;
    GpBitmap *bmp = NULL;
    uint8_t *bgra = NULL;

    if (!png_data || png_size == 0 || !ensure_gdiplus())
    {
        return NULL;
    }

    hg = GlobalAlloc(GMEM_MOVEABLE, png_size);
    if (!hg)
    {
        return NULL;
    }
    mem = GlobalLock(hg);
    if (!mem)
    {
        GlobalFree(hg);
        return NULL;
    }
    memcpy(mem, png_data, png_size);
    GlobalUnlock(hg);

    if (CreateStreamOnHGlobal(hg, TRUE, &stream) != S_OK || !stream)
    {
        GlobalFree(hg);
        return NULL;
    }

    if (GdipCreateBitmapFromStream(stream, &bmp) == GDIP_OK && bmp)
    {
        UINT uw = 0;
        UINT uh = 0;
        if (GdipGetImageWidth((GpImage *)bmp, &uw) == GDIP_OK &&
            GdipGetImageHeight((GpImage *)bmp, &uh) == GDIP_OK &&
            uw > 0 && uh > 0)
        {
            GpRect rect;
            GpBitmapData bd;
            rect.X = 0;
            rect.Y = 0;
            rect.Width = (INT)uw;
            rect.Height = (INT)uh;
            memset(&bd, 0, sizeof(bd));

            if (GdipBitmapLockBits(bmp, &rect, GDIP_LOCKMODE_READ, GDIP_PIXELFORMAT_32BPP_ARGB, &bd) == GDIP_OK)
            {
                bgra = (uint8_t *)rm_malloc((size_t)uw * (size_t)uh * 4u);
                if (bgra)
                {
                    UINT y;
                    for (y = 0; y < uh; y++)
                    {
                        const uint8_t *srow = (const uint8_t *)bd.Scan0 + (ptrdiff_t)y * (ptrdiff_t)bd.Stride;
                        memcpy(bgra + (size_t)y * (size_t)uw * 4u, srow, (size_t)uw * 4u);
                    }
                    *out_w = (int)uw;
                    *out_h = (int)uh;
                }
                GdipBitmapUnlockBits(bmp, &bd);
            }
        }
        GdipDisposeImage((GpImage *)bmp);
    }

    IStream_Release(stream);
    return bgra;
}

static RmImageEntry *find_img_entry(uint8_t key_type, int id, const char *name, size_t *out_idx)
{
    size_t i;
    for (i = 0; i < g_img_count; i++)
    {
        if (g_img_entries[i].key_type != key_type)
        {
            continue;
        }
        if (key_type == 0)
        {
            if (g_img_entries[i].id == id)
            {
                if (out_idx)
                {
                    *out_idx = i;
                }
                return &g_img_entries[i];
            }
        }
        else if (g_img_entries[i].name && name && strcmp(g_img_entries[i].name, name) == 0)
        {
            if (out_idx)
            {
                *out_idx = i;
            }
            return &g_img_entries[i];
        }
    }
    return NULL;
}

static int store_img_entry(uint8_t key_type, int id, const char *name,
                           uint32_t w, uint32_t h, uint8_t *owned_pixels)
{
    size_t idx = 0;
    RmImageEntry *existing = find_img_entry(key_type, id, name, &idx);
    RmImageEntry *grown;
    size_t ins_pos;
    char *name_copy = NULL;

    if (existing)
    {
        rm_free(existing->pixels);
        existing->w = w;
        existing->h = h;
        existing->pixels = owned_pixels;
        return 1;
    }

    if (key_type == 1)
    {
        name_copy = dup_str_local(name);
        if (!name_copy)
        {
            rm_free(owned_pixels);
            return 0;
        }
    }

    grown = (RmImageEntry *)rm_realloc(g_img_entries, (g_img_count + 1u) * sizeof(RmImageEntry));
    if (!grown)
    {
        rm_free(name_copy);
        rm_free(owned_pixels);
        return 0;
    }
    g_img_entries = grown;

    if (key_type == 0)
    {
        ins_pos = 0;
        while (ins_pos < g_img_count &&
               g_img_entries[ins_pos].key_type == 0 &&
               g_img_entries[ins_pos].id <= id)
        {
            ins_pos++;
        }
    }
    else
    {
        ins_pos = g_img_count;
    }

    if (ins_pos < g_img_count)
    {
        memmove(&g_img_entries[ins_pos + 1u],
                &g_img_entries[ins_pos],
                (g_img_count - ins_pos) * sizeof(RmImageEntry));
    }

    g_img_entries[ins_pos].w = w;
    g_img_entries[ins_pos].h = h;
    g_img_entries[ins_pos].key_type = key_type;
    g_img_entries[ins_pos].id = (key_type == 0) ? id : 0;
    g_img_entries[ins_pos].name = name_copy;
    g_img_entries[ins_pos].pixels = owned_pixels;
    g_img_count++;
    return 1;
}

static RmPngEntry *find_png_entry(uint8_t key_type, int id, const char *name)
{
    size_t i;
    for (i = 0; i < g_png_count; i++)
    {
        if (g_png_entries[i].key_type != key_type)
        {
            continue;
        }
        if (key_type == 0)
        {
            if (g_png_entries[i].id == id)
            {
                return &g_png_entries[i];
            }
        }
        else if (g_png_entries[i].name && name && strcmp(g_png_entries[i].name, name) == 0)
        {
            return &g_png_entries[i];
        }
    }
    return NULL;
}

static int store_png_entry(uint8_t key_type, int id, const char *name,
                           uint8_t *owned_png, size_t png_size)
{
    RmPngEntry *existing = find_png_entry(key_type, id, name);
    RmPngEntry *grown;
    size_t ins_pos;
    char *name_copy = NULL;

    if (existing)
    {
        rm_free(existing->png_data);
        existing->size = png_size;
        existing->png_data = owned_png;
        return 1;
    }

    if (key_type == 1)
    {
        name_copy = dup_str_local(name);
        if (!name_copy)
        {
            rm_free(owned_png);
            return 0;
        }
    }

    grown = (RmPngEntry *)rm_realloc(g_png_entries, (g_png_count + 1u) * sizeof(RmPngEntry));
    if (!grown)
    {
        rm_free(name_copy);
        rm_free(owned_png);
        return 0;
    }
    g_png_entries = grown;

    if (key_type == 0)
    {
        ins_pos = 0;
        while (ins_pos < g_png_count &&
               g_png_entries[ins_pos].key_type == 0 &&
               g_png_entries[ins_pos].id <= id)
        {
            ins_pos++;
        }
    }
    else
    {
        ins_pos = g_png_count;
    }

    if (ins_pos < g_png_count)
    {
        memmove(&g_png_entries[ins_pos + 1u],
                &g_png_entries[ins_pos],
                (g_png_count - ins_pos) * sizeof(RmPngEntry));
    }

    g_png_entries[ins_pos].size = png_size;
    g_png_entries[ins_pos].key_type = key_type;
    g_png_entries[ins_pos].id = (key_type == 0) ? id : 0;
    g_png_entries[ins_pos].name = name_copy;
    g_png_entries[ins_pos].png_data = owned_png;
    g_png_count++;
    return 1;
}

static void put_pixels_to_obj(lua_State *L, void *pixels, int w, int h, int preserve_props)
{
    int i;

    lua_settop(L, 0);
    lua_getfield(L, LUA_GLOBALSINDEX, "obj");

    if (preserve_props)
    {
        for (i = 0; i < 12; i++)
        {
            lua_getfield(L, 1, k_obj_save_fields[i]);
        }
        rm_update_obj_size(L, w, h, 1);
        for (i = 11; i >= 0; i--)
        {
            lua_setfield(L, 1, k_obj_save_fields[i]);
        }
    }
    else
    {
        rm_update_obj_size(L, w, h, 1);
    }

    lua_getfield(L, 1, "putpixeldata");
    lua_pushlightuserdata(L, pixels);
    lua_call(L, 1, 0);
    lua_settop(L, 0);
}

static int push_pixel_result(lua_State *L, uint32_t px, int plus_mode)
{
    lua_settop(L, 0);
    if (!plus_mode)
    {
        double a = (double)((float)(px >> 24) / 255.0f);
        lua_pushinteger(L, (lua_Integer)(px & 0x00ffffffu));
        lua_pushnumber(L, a);
        return 2;
    }
    lua_pushinteger(L, (lua_Integer)((px >> 16) & 0xffu));
    lua_pushinteger(L, (lua_Integer)((px >> 8) & 0xffu));
    lua_pushinteger(L, (lua_Integer)(px & 0xffu));
    lua_pushinteger(L, (lua_Integer)((px >> 24) & 0xffu));
    return 4;
}

static int ret_bool(lua_State *L, int val)
{
    lua_settop(L, 0);
    lua_pushboolean(L, val);
    return 1;
}

static int parse_bank_key(lua_State *L, int idx, uint8_t *out_type, int *out_id, char **out_name)
{
    int t = lua_type(L, idx);
    *out_type = 0;
    *out_id = 0;
    *out_name = NULL;

    if (t == LUA_TNUMBER)
    {
        *out_type = 0;
        *out_id = (int)lua_tointeger(L, idx);
        return 1;
    }
    if (t == LUA_TSTRING)
    {
        const char *s = lua_tolstring(L, idx, NULL);
        if (!s)
        {
            return 0;
        }
        *out_type = 1;
        *out_name = dup_str_local(s);
        return *out_name != NULL;
    }
    return 0;
}

int __cdecl lua_rikky_image(lua_State *L)
{
    static const int k_num_cmds[8] = {'w', 'r', 'c', 'g', 'm', 'i', 'p', 'u'};
    int t1 = lua_type(L, 1);
    int cmd = 0;
    uint8_t key_type = 0;
    int key_id = 0;
    char *key_name = NULL;

    if (t1 == LUA_TNUMBER)
    {
        int n = (int)lua_tointeger(L, 1);
        if (n >= 0 && n <= 7)
        {
            cmd = k_num_cmds[n];
        }
        else if (n >= 100 && n <= 107)
        {
            cmd = k_num_cmds[n - 100] + 0x100;
        }
        else
        {
            return ret_bool(L, 0);
        }
    }
    else if (t1 == LUA_TSTRING)
    {
        size_t slen = 0;
        const char *s = lua_tolstring(L, 1, &slen);
        if (!s || slen == 0)
        {
            return ret_bool(L, 0);
        }
        cmd = (unsigned char)s[0];
        if (slen == 2u && s[1] == '+')
        {
            cmd += 0x100;
        }
    }
    else
    {
        return ret_bool(L, 0);
    }

    // commands that do not require an entry key at arg 2
    if (cmd == ('c' + 0x100))
    {
        size_t i;
        for (i = 0; i < g_img_count; i++)
        {
            rm_free(g_img_entries[i].name);
            rm_free(g_img_entries[i].pixels);
        }
        rm_free(g_img_entries);
        g_img_entries = NULL;
        g_img_count = 0;
        return ret_bool(L, 1);
    }

    if (cmd == 'g')
    {
        if (lua_type(L, 2) == LUA_TNUMBER && lua_tointeger(L, 2) >= 1)
        {
            int want = (int)lua_tointeger(L, 2);
            int found = 0;
            int cand = 0;
            size_t idx = 0;

            lua_settop(L, 0);
            lua_createtable(L, 0, 0);
            while (found < want)
            {
                while (idx < g_img_count &&
                       g_img_entries[idx].key_type == 0 &&
                       g_img_entries[idx].id < cand)
                {
                    idx++;
                }
                if (idx < g_img_count &&
                    g_img_entries[idx].key_type == 0 &&
                    g_img_entries[idx].id == cand)
                {
                    idx++;
                }
                else
                {
                    found++;
                    lua_pushinteger(L, cand);
                    lua_rawseti(L, -2, found);
                }
                cand++;
            }
            return 1;
        }
        else
        {
            int cand = 0;
            size_t idx = 0;
            while (idx < g_img_count &&
                   g_img_entries[idx].key_type == 0 &&
                   g_img_entries[idx].id < cand)
            {
                idx++;
            }
            while (idx < g_img_count &&
                   g_img_entries[idx].key_type == 0 &&
                   g_img_entries[idx].id == cand)
            {
                cand++;
                idx++;
            }
            lua_settop(L, 0);
            lua_pushinteger(L, cand);
            return 1;
        }
    }

    if (cmd == ('g' + 0x100))
    {
        size_t i;
        int out_i = 0;
        lua_settop(L, 0);
        lua_createtable(L, 0, 0);
        for (i = 0; i < g_img_count; i++)
        {
            if (g_img_entries[i].key_type == 0)
            {
                out_i++;
                lua_pushinteger(L, g_img_entries[i].id);
                lua_rawseti(L, -2, out_i);
            }
        }
        for (i = 0; i < g_img_count; i++)
        {
            if (g_img_entries[i].key_type == 1 && g_img_entries[i].name)
            {
                out_i++;
                lua_pushstring(L, g_img_entries[i].name);
                lua_rawseti(L, -2, out_i);
            }
        }
        return 1;
    }

    // 'u' / 'u+' direct lightuserdata pixel read overload
    if ((cmd == 'u' || cmd == ('u' + 0x100)) &&
        (lua_type(L, 2) == LUA_TLIGHTUSERDATA || lua_type(L, 2) == LUA_TUSERDATA))
    {
        int top = lua_gettop(L);
        const uint32_t *base = (const uint32_t *)lua_touserdata(L, 2);
        if (!base)
        {
            return ret_bool(L, 0);
        }
        if (top == 5)
        {
            if (lua_type(L, 3) == LUA_TNUMBER &&
                lua_type(L, 4) == LUA_TNUMBER &&
                lua_type(L, 5) == LUA_TNUMBER)
            {
                int idx = (int)lua_tointeger(L, 3);
                int w = (int)lua_tointeger(L, 4);
                int h = (int)lua_tointeger(L, 5);
                if (w >= 1 && h >= 1 && idx >= 0 && idx < w * h)
                {
                    return push_pixel_result(L, base[idx], cmd != 'u');
                }
            }
            return ret_bool(L, 0);
        }
        if (top == 6)
        {
            if (lua_type(L, 3) == LUA_TNUMBER &&
                lua_type(L, 4) == LUA_TNUMBER &&
                lua_type(L, 5) == LUA_TNUMBER &&
                lua_type(L, 6) == LUA_TNUMBER)
            {
                int x = (int)lua_tointeger(L, 3);
                int y = (int)lua_tointeger(L, 4);
                int w = (int)lua_tointeger(L, 5);
                int h = (int)lua_tointeger(L, 6);
                if (w >= 1 && h >= 1 && x >= 0 && x < w && y >= 0 && y < h)
                {
                    return push_pixel_result(L, base[(size_t)y * (size_t)w + (size_t)x], cmd != 'u');
                }
            }
            return ret_bool(L, 0);
        }
        return ret_bool(L, 0);
    }

    if (!parse_bank_key(L, 2, &key_type, &key_id, &key_name))
    {
        return ret_bool(L, 0);
    }

    if (cmd == 'w' || cmd == ('w' + 0x100))
    {
        uint8_t *pixels = NULL;
        int w = 0;
        int h = 0;

        if (key_type == 1 && key_name[0] == '\0')
        {
            rm_free(key_name);
            return ret_bool(L, 0);
        }

        if (cmd == 'w' && lua_type(L, 3) == LUA_TSTRING)
        {
            char *src_opt = dup_str_local(lua_tolstring(L, 3, NULL));
            int is_tb = 0;
            if (src_opt)
            {
                rm_str_tolower(src_opt);
                is_tb = (strcmp(src_opt, "tempbuffer") == 0);
                rm_free(src_opt);
            }
            lua_settop(L, 0);
            if (is_tb)
            {
                pixels = rm_read_exedit_bgra(L, 2, &w, &h);
                if (!pixels || w <= 0 || h <= 0)
                {
                    rm_free(pixels);
                    rm_free(key_name);
                    return ret_bool(L, 0);
                }
                {
                    int ok = store_img_entry(key_type, key_id, key_name, (uint32_t)w, (uint32_t)h, pixels);
                    rm_free(key_name);
                    return ret_bool(L, ok);
                }
            }
        }

        lua_settop(L, 0);
        lua_getfield(L, LUA_GLOBALSINDEX, "obj");
        if (cmd == ('w' + 0x100))
        {
            double gv_alpha;
            double obj_alpha;
            float factor;
            const uint32_t *src;
            size_t count;
            size_t i;

            lua_getfield(L, 1, "getvalue");
            lua_pushstring(L, "alpha");
            lua_call(L, 1, 1);
            gv_alpha = lua_tonumber(L, -1);
            lua_pop(L, 1);

            lua_getfield(L, 1, "alpha");
            obj_alpha = lua_tonumber(L, -1);
            lua_pop(L, 1);
            factor = (float)(gv_alpha * obj_alpha);

            lua_getfield(L, 1, "getpixeldata");
            lua_call(L, 0, 3);
            src = (const uint32_t *)lua_touserdata(L, -3);
            w = (int)lua_tointeger(L, -2);
            h = (int)lua_tointeger(L, -1);
            if (!src || w <= 0 || h <= 0)
            {
                rm_free(key_name);
                return ret_bool(L, 0);
            }

            count = (size_t)w * (size_t)h;
            pixels = (uint8_t *)rm_malloc(count * 4u);
            if (!pixels)
            {
                rm_free(key_name);
                return ret_bool(L, 0);
            }
            for (i = 0; i < count; i++)
            {
                uint32_t px = src[i];
                int a = (int)((float)(px >> 24) * factor);
                if (a < 0)
                {
                    a = 0;
                }
                else if (a > 255)
                {
                    a = 255;
                }
                ((uint32_t *)pixels)[i] = (px & 0x00ffffffu) | ((uint32_t)a << 24);
            }
        }
        else
        {
            const void *src;
            size_t bytes;

            lua_getfield(L, 1, "getpixeldata");
            lua_call(L, 0, 3);
            src = lua_touserdata(L, -3);
            w = (int)lua_tointeger(L, -2);
            h = (int)lua_tointeger(L, -1);
            if (!src || w <= 0 || h <= 0)
            {
                rm_free(key_name);
                return ret_bool(L, 0);
            }

            bytes = (size_t)w * (size_t)h * 4u;
            pixels = (uint8_t *)rm_malloc(bytes);
            if (!pixels)
            {
                rm_free(key_name);
                return ret_bool(L, 0);
            }
            memcpy(pixels, src, bytes);
        }

        {
            int ok = store_img_entry(key_type, key_id, key_name, (uint32_t)w, (uint32_t)h, pixels);
            rm_free(key_name);
            return ret_bool(L, ok);
        }
    }

    if (cmd == 'r' || cmd == ('r' + 0x100))
    {
        RmImageEntry *ent = find_img_entry(key_type, key_id, key_name, NULL);
        rm_free(key_name);
        if (!ent || !ent->pixels)
        {
            return ret_bool(L, 0);
        }
        put_pixels_to_obj(L, ent->pixels, (int)ent->w, (int)ent->h, cmd == ('r' + 0x100));
        return ret_bool(L, 1);
    }

    if (cmd == 'c')
    {
        size_t idx = 0;
        RmImageEntry *ent = find_img_entry(key_type, key_id, key_name, &idx);
        rm_free(key_name);
        if (!ent)
        {
            return ret_bool(L, 0);
        }
        rm_free(ent->name);
        rm_free(ent->pixels);
        if (g_img_count == 1u)
        {
            rm_free(g_img_entries);
            g_img_entries = NULL;
            g_img_count = 0;
        }
        else
        {
            if (idx + 1u < g_img_count)
            {
                memmove(&g_img_entries[idx],
                        &g_img_entries[idx + 1u],
                        (g_img_count - idx - 1u) * sizeof(RmImageEntry));
            }
            g_img_count--;
        }
        return ret_bool(L, 1);
    }

    if (cmd == 'i' || cmd == ('i' + 0x100))
    {
        RmImageEntry *ent = find_img_entry(key_type, key_id, key_name, NULL);
        rm_free(key_name);
        if (!ent)
        {
            return ret_bool(L, 0);
        }
        lua_settop(L, 0);
        if (cmd == 'i')
        {
            lua_pushlightuserdata(L, ent->pixels);
        }
        else
        {
            lua_createtable(L, 0, 0);
        }
        lua_pushinteger(L, (lua_Integer)ent->w);
        lua_pushinteger(L, (lua_Integer)ent->h);
        return 3;
    }

    if (cmd == 'm' || cmd == ('m' + 0x100))
    {
        uint8_t k2_type = 0;
        int k2_id = 0;
        char *k2_name = NULL;
        int dx;
        int dy;
        RmImageEntry *e1;
        RmImageEntry *e2;
        int w1;
        int h1;
        int w2;
        int h2;
        int sx0;
        int sx1;
        int sy0;
        int sy1;
        size_t bytes1;
        int sx;
        int sy;

        if (!parse_bank_key(L, 3, &k2_type, &k2_id, &k2_name))
        {
            rm_free(key_name);
            return ret_bool(L, 0);
        }
        dx = (int)lua_tointeger(L, 4);
        dy = (int)lua_tointeger(L, 5);
        lua_settop(L, 0);

        e1 = find_img_entry(key_type, key_id, key_name, NULL);
        e2 = find_img_entry(k2_type, k2_id, k2_name, NULL);
        rm_free(key_name);
        rm_free(k2_name);
        if (!e1 || !e2 || !e1->pixels || !e2->pixels)
        {
            return ret_bool(L, 0);
        }

        w1 = (int)e1->w;
        h1 = (int)e1->h;
        w2 = (int)e2->w;
        h2 = (int)e2->h;
        bytes1 = (size_t)w1 * (size_t)h1 * 4u;

        rm_free(g_merge_buf);
        g_merge_buf = (uint8_t *)rm_malloc(bytes1);
        if (!g_merge_buf)
        {
            return ret_bool(L, 0);
        }
        memcpy(g_merge_buf, e1->pixels, bytes1);

        sx0 = (dx < 0) ? -dx : 0;
        sx1 = (dx + w2 > w1) ? (w1 - dx) : w2;
        sy0 = (dy < 0) ? -dy : 0;
        sy1 = (dy + h2 > h1) ? (h1 - dy) : h2;

        for (sy = sy0; sy < sy1; sy++)
        {
            int ty = dy + sy;
            const uint32_t *srow = (const uint32_t *)e2->pixels + (size_t)sy * (size_t)w2;
            uint32_t *drow = (uint32_t *)g_merge_buf + (size_t)ty * (size_t)w1;
            for (sx = sx0; sx < sx1; sx++)
            {
                int tx = dx + sx;
                uint32_t spx = srow[sx];
                uint32_t dpx = drow[tx];
                float a2 = (float)(spx >> 24) / 255.0f;
                float a1 = (float)(dpx >> 24) / 255.0f;
                float wt2 = (2.0f - a1) * a2;
                float wt1 = (1.0f - a2) * a1;
                float wsum = wt1 + wt2;

                if (wsum > 0.0f)
                {
                    uint32_t ob = (uint32_t)(uint8_t)(int)(((float)(dpx & 0xffu) * wt1 +
                                                            (float)(spx & 0xffu) * wt2) /
                                                           wsum);
                    uint32_t og = (uint32_t)(uint8_t)(int)(((float)((dpx >> 8) & 0xffu) * wt1 +
                                                            (float)((spx >> 8) & 0xffu) * wt2) /
                                                           wsum);
                    uint32_t or_ = (uint32_t)(uint8_t)(int)(((float)((dpx >> 16) & 0xffu) * wt1 +
                                                             (float)((spx >> 16) & 0xffu) * wt2) /
                                                            wsum);
                    int oa = (int)((a2 + wt1) * 255.0f);
                    if (oa < 0)
                    {
                        oa = 0;
                    }
                    else if (oa > 255)
                    {
                        oa = 255;
                    }
                    drow[tx] = ob | (og << 8) | (or_ << 16) | ((uint32_t)oa << 24);
                }
            }
        }

        if (cmd == 'm')
        {
            lua_pushlightuserdata(L, g_merge_buf);
            lua_pushinteger(L, w1);
            lua_pushinteger(L, h1);
            return 3;
        }

        put_pixels_to_obj(L, g_merge_buf, w1, h1, 1);
        return ret_bool(L, 1);
    }

    if (cmd == 'p' || cmd == ('p' + 0x100))
    {
        int t3 = lua_type(L, 3);
        uint32_t w = 0;
        uint32_t h = 0;
        const void *src_ptr = NULL;
        uint8_t *copy_px;
        size_t bytes;
        int ok;

        if (key_type == 1 && key_name[0] == '\0')
        {
            rm_free(key_name);
            return ret_bool(L, 0);
        }
        if (cmd == ('p' + 0x100) && find_img_entry(key_type, key_id, key_name, NULL) != NULL)
        {
            rm_free(key_name);
            return ret_bool(L, 0);
        }

        if (t3 == LUA_TLIGHTUSERDATA || t3 == LUA_TUSERDATA)
        {
            int iw;
            int ih;
            if (lua_type(L, 4) != LUA_TNUMBER || lua_type(L, 5) != LUA_TNUMBER)
            {
                rm_free(key_name);
                return ret_bool(L, 0);
            }
            iw = (int)lua_tointeger(L, 4);
            ih = (int)lua_tointeger(L, 5);
            src_ptr = lua_touserdata(L, 3);
            if (!src_ptr || iw <= 0 || ih <= 0)
            {
                rm_free(key_name);
                return ret_bool(L, 0);
            }
            w = (uint32_t)iw;
            h = (uint32_t)ih;
        }
        else
        {
            uint8_t sk_type = 0;
            int sk_id = 0;
            char *sk_name = NULL;
            RmImageEntry *src_ent;

            if (!parse_bank_key(L, 3, &sk_type, &sk_id, &sk_name))
            {
                rm_free(key_name);
                return ret_bool(L, 0);
            }
            src_ent = find_img_entry(sk_type, sk_id, sk_name, NULL);
            rm_free(sk_name);
            if (!src_ent || !src_ent->pixels)
            {
                rm_free(key_name);
                return ret_bool(L, 0);
            }
            w = src_ent->w;
            h = src_ent->h;
            src_ptr = src_ent->pixels;
        }

        bytes = (size_t)w * (size_t)h * 4u;
        copy_px = (uint8_t *)rm_malloc(bytes);
        if (!copy_px)
        {
            rm_free(key_name);
            return ret_bool(L, 0);
        }
        memcpy(copy_px, src_ptr, bytes);
        ok = store_img_entry(key_type, key_id, key_name, w, h, copy_px);
        rm_free(key_name);
        return ret_bool(L, ok);
    }

    if (cmd == 'u' || cmd == ('u' + 0x100))
    {
        int top = lua_gettop(L);
        RmImageEntry *ent = find_img_entry(key_type, key_id, key_name, NULL);
        const uint32_t *px_arr;
        int w;
        int h;

        rm_free(key_name);
        if (!ent || !ent->pixels)
        {
            return ret_bool(L, 0);
        }
        px_arr = (const uint32_t *)ent->pixels;
        w = (int)ent->w;
        h = (int)ent->h;

        if (top == 3 && lua_type(L, 3) == LUA_TNUMBER)
        {
            int idx = (int)lua_tointeger(L, 3);
            if (idx >= 0 && idx < w * h)
            {
                return push_pixel_result(L, px_arr[idx], cmd != 'u');
            }
        }
        else if (top == 4 && lua_type(L, 3) == LUA_TNUMBER && lua_type(L, 4) == LUA_TNUMBER)
        {
            int x = (int)lua_tointeger(L, 3);
            int y = (int)lua_tointeger(L, 4);
            if (x >= 0 && x < w && y >= 0 && y < h)
            {
                return push_pixel_result(L, px_arr[(size_t)y * (size_t)w + (size_t)x], cmd != 'u');
            }
        }
        return ret_bool(L, 0);
    }

    if (cmd == 'Q')
    {
        const uint8_t *src;
        int w;
        int h;
        size_t png_sz = 0;
        uint8_t *png_bytes;
        int ok;

        if (key_type == 1 && key_name[0] == '\0')
        {
            rm_free(key_name);
            return ret_bool(L, 0);
        }

        lua_settop(L, 0);
        lua_getfield(L, LUA_GLOBALSINDEX, "obj");
        lua_getfield(L, 1, "getpixeldata");
        lua_call(L, 0, 3);
        src = (const uint8_t *)lua_touserdata(L, -3);
        w = (int)lua_tointeger(L, -2);
        h = (int)lua_tointeger(L, -1);
        if (!src || w <= 0 || h <= 0)
        {
            rm_free(key_name);
            return ret_bool(L, 0);
        }

        png_bytes = encode_png_mem(src, w, h, &png_sz);
        if (!png_bytes || png_sz == 0)
        {
            rm_free(png_bytes);
            rm_free(key_name);
            return ret_bool(L, 0);
        }

        ok = store_png_entry(key_type, key_id, key_name, png_bytes, png_sz);
        rm_free(key_name);
        return ret_bool(L, ok);
    }

    if (cmd == 'Z')
    {
        RmPngEntry *ent = find_png_entry(key_type, key_id, key_name);
        uint8_t *decoded;
        int w = 0;
        int h = 0;

        rm_free(key_name);
        if (!ent || !ent->png_data || ent->size == 0)
        {
            return ret_bool(L, 0);
        }

        decoded = decode_png_mem(ent->png_data, ent->size, &w, &h);
        if (!decoded || w <= 0 || h <= 0)
        {
            rm_free(decoded);
            return ret_bool(L, 0);
        }

        put_pixels_to_obj(L, decoded, w, h, 0);
        rm_free(decoded);
        return ret_bool(L, 1);
    }

    rm_free(key_name);
    return ret_bool(L, 0);
}
