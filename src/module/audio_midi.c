#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rikky_module_internal.h"

// radix-2 decimation-in-frequency fft
static void fft_dif(double *re, double *im, int n, int bits)
{
    double w_step = 3.141592653589793 / (double)n;
    int len = n;
    int stage;
    int i;
    int j;

    for (stage = 0; stage < bits; stage++)
    {
        int half = len / 2;
        w_step *= 2.0;
        for (j = 0; j < half; j++)
        {
            double ang = (double)j * w_step;
            double wr = cos(ang);
            double wi = sin(ang);
            int k;
            for (k = j; k < n; k += len)
            {
                int kh = k + half;
                double dr = re[k] - re[kh];
                double di = im[k] - im[kh];
                re[k] += re[kh];
                im[k] += im[kh];
                re[kh] = dr * wr - di * wi;
                im[kh] = dr * wi + di * wr;
            }
        }
        len = half;
    }

    j = n / 2;
    for (i = 1; i < n - 1; i++)
    {
        int bit;
        if (i < j)
        {
            double tr = re[i];
            double ti = im[i];
            re[i] = re[j];
            im[i] = im[j];
            re[j] = tr;
            im[j] = ti;
        }
        bit = n / 2;
        while (bit <= j)
        {
            j -= bit;
            bit /= 2;
        }
        j += bit;
    }
}

static void copy_lower_arg(lua_State *L, int idx, char *buf, size_t buf_sz)
{
    const char *s = lua_tolstring(L, idx, NULL);
    if (!s || buf_sz == 0)
    {
        if (buf_sz > 0)
        {
            buf[0] = '\0';
        }
        return;
    }
    strncpy(buf, s, buf_sz - 1);
    buf[buf_sz - 1] = '\0';
    rm_str_tolower(buf);
}

static void push_fft_spectrum_table(
    lua_State *L,
    int tbl_idx,
    const double *re,
    const double *im,
    int fft_n,
    uint32_t audio_rate,
    uint32_t bin_lo,
    uint32_t bin_hi,
    uint32_t band_cnt)
{
    double step = 1.0 / (double)(band_cnt + 1u);
    double df = (double)audio_rate / (double)fft_n;
    double half_df = df * 0.5;
    double log_lo = log10(half_df + (double)(int)bin_lo * df);
    double log_hi = log10(half_df + (double)(int)bin_hi * df);
    double span = log_hi - log_lo;
    double inv_span;
    double scale = 2.0 / (double)fft_n;
    uint32_t k = bin_lo;
    double p0;
    double p1;
    double m0;
    double m1;
    uint32_t band;

    if (span == 0.0)
    {
        return;
    }
    inv_span = 1.0 / span;
    p0 = 0.0;
    m0 = hypot(re[k], im[k]) * scale;
    if ((int)(k + 1u) < fft_n)
    {
        p1 = (log10(half_df + (double)(int)(k + 1u) * df) - log_lo) * inv_span;
        m1 = hypot(re[k + 1u], im[k + 1u]) * scale;
    }
    else
    {
        p1 = 1.0;
        m1 = m0;
    }

    for (band = 1; band <= band_cnt; band++)
    {
        double pos = (double)band * step;
        double s;
        double w;
        int val;

        while (pos > p1 && k + 1u < bin_hi && (int)(k + 2u) < fft_n)
        {
            k++;
            p0 = p1;
            m0 = m1;
            p1 = (log10(half_df + (double)(int)(k + 1u) * df) - log_lo) * inv_span;
            m1 = hypot(re[k + 1u], im[k + 1u]) * scale;
        }

        if (p1 > p0)
        {
            s = (pos - p0) / (p1 - p0);
            if (s < 0.0)
            {
                s = 0.0;
            }
            else if (s > 1.0)
            {
                s = 1.0;
            }
            w = (3.0 - 2.0 * s) * s * s;
            val = (int)lrint(m0 + (m1 - m0) * w);
        }
        else
        {
            val = (int)lrint(m0);
        }

        lua_pushinteger(L, val);
        lua_rawseti(L, tbl_idx, (int)band);
    }
}

int __cdecl lua_rikky_audiobuffer(lua_State *L)
{
    uintptr_t editp;
    int (__cdecl *get_audio_filtered)(void *, int, void *);
    int top;
    int frame;
    int max_frame;
    int mode = 0;
    int want_stereo = 1;
    int force_mono = 0;
    int div_num = -1;
    int fft_bits = 11;
    int fft_n = 2048;
    int freq_lo = 0;
    int freq_hi = 0;
    int cur_frame = 0;
    uint32_t max_total_smp;
    uint32_t audio_rate;
    int16_t *raw_buf;
    uint32_t got_smp;
    uint32_t work_len;
    int is_stereo;
    int stereo_out;
    int16_t *ch_l;
    int16_t *ch_r = NULL;
    char sbuf[32];

    if (!rm_check_aviutl_ver())
    {
        return 0;
    }

    editp = *(uintptr_t *)RikkyGetMemoryData(3);
    get_audio_filtered = (int (__cdecl *)(void *, int, void *))RikkyGetMemoryData(0x17);

    top = lua_gettop(L);
    if (top == 0)
    {
        lua_pushinteger(L, (lua_Integer)(*(uint8_t *)(editp + 0x3fa)));
        lua_pushinteger(L, *(int *)(editp + (uintptr_t)RikkyGetMemoryData(0x12)));
        return 2;
    }

    frame = (int)lua_tointeger(L, 1);
    max_frame = *(int *)(editp + (uintptr_t)RikkyGetMemoryData(7)) - 1;

    if (lua_type(L, 2) == LUA_TSTRING)
    {
        copy_lower_arg(L, 2, sbuf, sizeof(sbuf));
        if (strcmp(sbuf, "spectrum") == 0 || strcmp(sbuf, "s") == 0)
        {
            mode = 1;
        }
        else if (strcmp(sbuf, "decibel") == 0 || strcmp(sbuf, "db") == 0)
        {
            mode = 2;
        }
        else if (strcmp(sbuf, "fourier") == 0 || strcmp(sbuf, "f") == 0)
        {
            mode = 3;
        }
        else
        {
            mode = 0;
        }
    }

    if (lua_type(L, 3) == LUA_TSTRING)
    {
        copy_lower_arg(L, 3, sbuf, sizeof(sbuf));
        if (strcmp(sbuf, "absolute") != 0 && strcmp(sbuf, "abs") != 0 && strcmp(sbuf, "a") != 0)
        {
            lua_getglobal(L, "obj");
            lua_getfield(L, -1, "frame");
            frame += (int)lua_tointeger(L, -1);
            lua_pop(L, 2);
        }
    }
    else
    {
        lua_getglobal(L, "obj");
        lua_getfield(L, -1, "frame");
        frame += (int)lua_tointeger(L, -1);
        lua_pop(L, 2);
    }

    if (lua_type(L, 4) == LUA_TSTRING)
    {
        copy_lower_arg(L, 4, sbuf, sizeof(sbuf));
        if (strcmp(sbuf, "monaural") == 0 || strcmp(sbuf, "mono") == 0 || strcmp(sbuf, "m") == 0)
        {
            want_stereo = 0;
            force_mono = 1;
        }
    }

    if (lua_type(L, 5) == LUA_TNUMBER)
    {
        div_num = (int)lua_tointeger(L, 5);
        if (div_num < 0)
        {
            div_num = 0;
        }
    }

    if (mode != 0)
    {
        if (lua_type(L, 6) == LUA_TNUMBER)
        {
            int k = (int)lua_tointeger(L, 6);
            int n = 0x800 << ((uint32_t)(k - 1) & 0x1fu);
            if (n < 0x400 || k < 0)
            {
                fft_bits = 10;
                fft_n = 0x400;
            }
            else if (n <= 0x10000 && k <= 6)
            {
                fft_bits = k + 10;
                fft_n = n;
            }
            else
            {
                fft_bits = 16;
                fft_n = 0x10000;
            }
        }
        if (lua_type(L, 7) == LUA_TTABLE)
        {
            lua_rawgeti(L, 7, 1);
            freq_lo = (int)lua_tointeger(L, -1);
            lua_rawgeti(L, 7, 2);
            freq_hi = (int)lua_tointeger(L, -1);
            lua_pop(L, 2);
        }
    }

    if (frame >= 0)
    {
        cur_frame = (frame > max_frame) ? max_frame : frame;
        if (cur_frame < 0)
        {
            cur_frame = 0;
        }
    }

    max_total_smp = *(uint32_t *)(editp + (uintptr_t)RikkyGetMemoryData(0x11));
    audio_rate = *(uint32_t *)(editp + (uintptr_t)RikkyGetMemoryData(0x12));
    if (max_total_smp == 0)
    {
        max_total_smp = (uint32_t)(*(uint8_t *)(editp + 0x3fa)) *
                        (uint32_t)get_audio_filtered((void *)editp, 0, NULL);
    }
    if (max_total_smp == 0)
    {
        lua_pushnil(L);
        return 1;
    }

    raw_buf = (int16_t *)rm_malloc((size_t)max_total_smp * sizeof(int16_t));
    if (!raw_buf)
    {
        lua_pushnil(L);
        return 1;
    }

    got_smp = (uint32_t)get_audio_filtered((void *)editp, cur_frame, raw_buf);
    work_len = (mode != 0 && fft_n > (int)got_smp) ? (uint32_t)fft_n : got_smp;
    is_stereo = (got_smp < max_total_smp) ? 1 : 0;
    stereo_out = (want_stereo && is_stereo) ? 1 : 0;

    ch_l = (int16_t *)rm_malloc((size_t)(work_len > 0 ? work_len : 1u) * sizeof(int16_t));
    if (!ch_l)
    {
        rm_free(raw_buf);
        lua_pushnil(L);
        return 1;
    }
    if (stereo_out)
    {
        ch_r = (int16_t *)rm_malloc((size_t)(work_len > 0 ? work_len : 1u) * sizeof(int16_t));
        if (!ch_r)
        {
            rm_free(ch_l);
            rm_free(raw_buf);
            lua_pushnil(L);
            return 1;
        }
    }

    lua_settop(L, 0);

    if (mode == 0)
    {
        uint32_t j;
        uint32_t cnt = (div_num >= 0 && div_num <= (int)got_smp) ? (uint32_t)div_num : got_smp;

        for (j = 0; j < got_smp; j++)
        {
            if (is_stereo)
            {
                if (force_mono)
                {
                    ch_l[j] = (int16_t)lrint(((float)raw_buf[j * 2] + (float)raw_buf[j * 2 + 1]) * 0.5f);
                }
                else
                {
                    ch_l[j] = raw_buf[j * 2];
                    ch_r[j] = raw_buf[j * 2 + 1];
                }
            }
            else
            {
                ch_l[j] = raw_buf[j];
            }
        }

        lua_newtable(L);
        if (stereo_out)
        {
            lua_newtable(L);
            for (j = 0; j < cnt; j++)
            {
                lua_pushnumber(L, (double)ch_l[j]);
                lua_rawseti(L, 1, (int)(j + 1u));
                lua_pushnumber(L, (double)ch_r[j]);
                lua_rawseti(L, 2, (int)(j + 1u));
            }
        }
        else
        {
            for (j = 0; j < cnt; j++)
            {
                lua_pushnumber(L, (double)ch_l[j]);
                lua_rawseti(L, 1, (int)(j + 1u));
            }
        }

        rm_free(raw_buf);
        rm_free(ch_l);
        if (ch_r)
        {
            rm_free(ch_r);
        }
        return stereo_out ? 2 : 1;
    }
    else
    {
        uint32_t dst = 0;
        uint32_t rem = work_len;
        uint32_t cur_got = got_smp;
        int nyquist = (int)lrint((float)audio_rate * 0.5f);
        double *re_l;
        double *im_l;
        double *re_r = NULL;
        double *im_r = NULL;
        double win_step;
        int i;
        uint32_t half_n;
        uint32_t bin_hi;
        uint32_t bin_lo;

        while (rem > 0)
        {
            uint32_t take;
            uint32_t j;

            if (cur_got == 0)
            {
                memset(ch_l + dst, 0, (size_t)rem * sizeof(int16_t));
                if (stereo_out)
                {
                    memset(ch_r + dst, 0, (size_t)rem * sizeof(int16_t));
                }
                break;
            }

            take = (rem < cur_got) ? rem : cur_got;
            for (j = 0; j < take; j++)
            {
                if (is_stereo)
                {
                    if (force_mono)
                    {
                        ch_l[dst + j] = (int16_t)lrint(((float)raw_buf[j * 2] + (float)raw_buf[j * 2 + 1]) * 0.5f);
                    }
                    else
                    {
                        ch_l[dst + j] = raw_buf[j * 2];
                        ch_r[dst + j] = raw_buf[j * 2 + 1];
                    }
                }
                else
                {
                    ch_l[dst + j] = raw_buf[j];
                }
            }

            dst += take;
            rem -= take;
            if (rem == 0)
            {
                break;
            }

            cur_frame++;
            if (cur_frame > max_frame || cur_frame < 0)
            {
                memset(raw_buf, 0, (size_t)max_total_smp * sizeof(int16_t));
                cur_got = (got_smp > 0) ? got_smp : rem;
            }
            else
            {
                cur_got = (uint32_t)get_audio_filtered((void *)editp, cur_frame, raw_buf);
            }
        }

        rm_free(raw_buf);

        if (freq_hi < 0)
        {
            freq_hi = 0;
        }
        else if (freq_hi > nyquist)
        {
            freq_hi = nyquist;
        }
        if (freq_lo < 0)
        {
            freq_lo = 0;
        }
        else if (freq_lo > freq_hi)
        {
            freq_lo = freq_hi;
        }

        re_l = (double *)rm_malloc((size_t)fft_n * sizeof(double));
        im_l = (double *)rm_calloc((size_t)fft_n, sizeof(double));
        if (!re_l || !im_l)
        {
            rm_free(re_l);
            rm_free(im_l);
            rm_free(ch_l);
            rm_free(ch_r);
            lua_pushnil(L);
            return 1;
        }
        if (stereo_out)
        {
            re_r = (double *)rm_malloc((size_t)fft_n * sizeof(double));
            im_r = (double *)rm_calloc((size_t)fft_n, sizeof(double));
            if (!re_r || !im_r)
            {
                rm_free(re_l);
                rm_free(im_l);
                rm_free(re_r);
                rm_free(im_r);
                rm_free(ch_l);
                rm_free(ch_r);
                lua_pushnil(L);
                return 1;
            }
        }

        win_step = 6.283185307179586 / (double)fft_n;
        if (stereo_out)
        {
            for (i = 0; i < fft_n; i++)
            {
                double w = (1.0 - cos((double)i * win_step)) * 0.5;
                re_l[i] = (double)ch_l[i] * w;
                re_r[i] = (double)ch_r[i] * w;
            }
            fft_dif(re_l, im_l, fft_n, fft_bits);
            fft_dif(re_r, im_r, fft_n, fft_bits);
        }
        else
        {
            for (i = 0; i < fft_n; i++)
            {
                double w = (1.0 - cos((double)i * win_step)) * 0.5;
                re_l[i] = (double)ch_l[i] * w;
            }
            fft_dif(re_l, im_l, fft_n, fft_bits);
        }

        rm_free(ch_l);
        rm_free(ch_r);

        half_n = (uint32_t)(fft_n >> 1);
        bin_hi = (freq_hi == 0) ? half_n : (uint32_t)lrint(((double)freq_hi / (double)audio_rate) * (double)fft_n);
        bin_lo = (uint32_t)lrint(((double)freq_lo / (double)audio_rate) * (double)fft_n);
        if (bin_hi > half_n)
        {
            bin_hi = half_n;
        }
        if (bin_lo > bin_hi)
        {
            bin_lo = bin_hi;
        }

        lua_newtable(L);
        if (stereo_out)
        {
            lua_newtable(L);
        }

        if (mode == 1)
        {
            if (bin_lo != bin_hi && div_num != 0)
            {
                uint32_t band_cnt = (div_num >= 0 && div_num <= (int)got_smp) ? (uint32_t)div_num : half_n;
                if (band_cnt > 0)
                {
                    push_fft_spectrum_table(L, 1, re_l, im_l, fft_n, audio_rate, bin_lo, bin_hi, band_cnt);
                    if (stereo_out)
                    {
                        push_fft_spectrum_table(L, 2, re_r, im_r, fft_n, audio_rate, bin_lo, bin_hi, band_cnt);
                    }
                }
            }
        }
        else if (mode == 2)
        {
            uint32_t count = bin_hi - bin_lo;
            double inv_n = (1.0 / (double)fft_n) * 0.0001220703125;
            uint32_t idx;
            for (idx = 1; idx <= count; idx++)
            {
                uint32_t k = bin_lo + idx - 1u;
                int m_l = (int)lrint(hypot(re_l[k], im_l[k]));
                if (m_l == 0)
                {
                    lua_pushnil(L);
                }
                else
                {
                    lua_pushnumber(L, 20.0 * log10((double)m_l * inv_n));
                }
                lua_rawseti(L, 1, (int)idx);

                if (stereo_out)
                {
                    int m_r = (int)lrint(hypot(re_r[k], im_r[k]));
                    if (m_r == 0)
                    {
                        lua_pushnil(L);
                    }
                    else
                    {
                        lua_pushnumber(L, 20.0 * log10((double)m_r * inv_n));
                    }
                    lua_rawseti(L, 2, (int)idx);
                }
            }
        }
        else if (mode == 3)
        {
            uint32_t count = bin_hi - bin_lo;
            double scale = 4.0 / (double)fft_n;
            uint32_t idx;
            for (idx = 1; idx <= count; idx++)
            {
                uint32_t k = bin_lo + idx - 1u;
                lua_pushinteger(L, (int)lrint(hypot(re_l[k], im_l[k]) * scale));
                lua_rawseti(L, 1, (int)idx);
                if (stereo_out)
                {
                    lua_pushinteger(L, (int)lrint(hypot(re_r[k], im_r[k]) * scale));
                    lua_rawseti(L, 2, (int)idx);
                }
            }
        }

        rm_free(re_l);
        rm_free(im_l);
        rm_free(re_r);
        rm_free(im_r);
        return stereo_out ? 2 : 1;
    }
}

typedef struct
{
    char *path;
    uint32_t pad0;
    double offset;
    double volume;
    double speed;
    double pan;
    uint8_t loop;
    uint8_t pad1[7];
} RmSoundRegEntry;

int __cdecl lua_rikky_soundregister(lua_State *L)
{
    int top = lua_gettop(L);

    if (top == 0)
    {
        int8_t *p = (int8_t *)RikkyGetMemoryData(0xce);
        lua_pushboolean(L, p ? (int)*p : 0);
        return 1;
    }

    if (lua_type(L, 1) == LUA_TSTRING)
    {
        const char *path = lua_tolstring(L, 1, NULL);
        if (path && path[0] != '\0')
        {
            if (top == 1)
            {
                FILE_INFO fi;
                AVI_FILE_HANDLE afh;
                memset(&fi, 0, sizeof(fi));
                afh = GetAviHandle(path, &fi);
                if (afh != NULL)
                {
                    CloseAviHandle(afh);
                    lua_pushnumber(L, (double)fi.audio_n / (double)fi.audio_rate);
                    return 1;
                }
                lua_pushboolean(L, 0);
                return 1;
            }
            else
            {
                double offset = lua_tonumber(L, 2);
                double vol = lua_tonumber(L, 3);
                double speed = lua_tonumber(L, 4);
                uint8_t *st = (uint8_t *)RikkyGetMemoryData(200);

                if (vol > 0.0 && speed > 0.0 && st && st[0] != 0)
                {
                    int count = *(int *)(st + 4);
                    void *old_entries = *(void **)(st + 8);
                    size_t path_len = strlen(path) + 1u;
                    char *path_copy = (char *)rm_malloc(path_len);
                    RmSoundRegEntry *entries;
                    double pan;
                    uint8_t loop_flag = 0;

                    if (!path_copy)
                    {
                        lua_pushboolean(L, 0);
                        return 1;
                    }

                    entries = (RmSoundRegEntry *)rm_realloc(
                        old_entries,
                        (size_t)(count + 1) * sizeof(RmSoundRegEntry));
                    if (!entries)
                    {
                        rm_free(path_copy);
                        lua_pushboolean(L, 0);
                        return 1;
                    }

                    memcpy(path_copy, path, path_len);
                    pan = lua_tonumber(L, 5);
                    if (pan > 100.0)
                    {
                        pan = 100.0;
                    }
                    else if (pan < -100.0)
                    {
                        pan = -100.0;
                    }
                    if (lua_type(L, 6) == LUA_TBOOLEAN)
                    {
                        loop_flag = (uint8_t)lua_toboolean(L, 6);
                    }

                    memset(&entries[count], 0, sizeof(RmSoundRegEntry));
                    entries[count].path = path_copy;
                    entries[count].offset = offset;
                    entries[count].volume = vol * 0.01;
                    entries[count].speed = speed * 0.01;
                    entries[count].pan = pan;
                    entries[count].loop = loop_flag;

                    *(void **)(st + 8) = entries;
                    *(int *)(st + 4) = count + 1;
                    lua_pushboolean(L, 1);
                    return 1;
                }

                lua_pushboolean(L, 0);
                return 1;
            }
        }
    }

    lua_pushnil(L);
    return 1;
}

typedef struct
{
    int len;
    int16_t *pcm;
} RmWaveEntry;

int __cdecl lua_rikky_wave(lua_State *L)
{
    uintptr_t editp;
    uintptr_t p_rate;
    int audio_rate;
    uint16_t audio_ch;
    int *p_vid;
    int max_len;
    int len;
    uint8_t *st;
    int count;
    int16_t *pcm;
    RmWaveEntry *entries;
    int i;

    if (lua_type(L, 1) != LUA_TTABLE)
    {
        lua_pushboolean(L, 0);
        return 1;
    }

    editp = *(uintptr_t *)RikkyGetMemoryData(3);
    p_rate = editp + (uintptr_t)RikkyGetMemoryData(0x12);
    audio_rate = *(int *)p_rate;
    audio_ch = *(uint16_t *)(p_rate - 2);
    p_vid = (int *)(editp + (uintptr_t)RikkyGetMemoryData(0x1d));

    max_len = (int)lrint(ceil(
        ((double)p_vid[1] * (double)audio_rate * (double)audio_ch) / (double)p_vid[0]));

    len = (int)lua_objlen(L, 1);
    if (len <= 0)
    {
        lua_pushboolean(L, 0);
        return 1;
    }

    st = (uint8_t *)RikkyGetMemoryData(0xc9);
    if (!st || st[0] == 0)
    {
        lua_pushboolean(L, 0);
        return 1;
    }

    if (len > max_len)
    {
        len = max_len;
    }

    pcm = (int16_t *)rm_malloc((size_t)len * sizeof(int16_t));
    if (!pcm)
    {
        lua_pushboolean(L, 0);
        return 1;
    }

    count = *(int *)(st + 4);
    entries = (RmWaveEntry *)rm_realloc(*(void **)(st + 8), (size_t)(count + 1) * sizeof(RmWaveEntry));
    if (!entries)
    {
        rm_free(pcm);
        lua_pushboolean(L, 0);
        return 1;
    }

    for (i = 1; i <= len; i++)
    {
        int v;
        lua_rawgeti(L, 1, i);
        v = (int)lua_tointeger(L, -1);
        lua_pop(L, 1);
        if (v > 32767)
        {
            v = 32767;
        }
        else if (v < -32768)
        {
            v = -32768;
        }
        pcm[i - 1] = (int16_t)v;
    }

    entries[count].len = len;
    entries[count].pcm = pcm;
    *(void **)(st + 8) = entries;
    *(int *)(st + 4) = count + 1;

    lua_pushboolean(L, 1);
    return 1;
}

static volatile double g_midi_progress = 0.0;
static volatile char g_midi_abort = 0;

static LRESULT CALLBACK midi_progress_wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_CREATE:
        SetTimer(hwnd, 1, 10, NULL);
        break;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        RECT rc;
        HDC hdc = BeginPaint(hwnd, &ps);
        HBRUSH br = CreateSolidBrush(0x00ff00);
        HGDIOBJ old_obj = SelectObject(hdc, br);
        GetClientRect(hwnd, &rc);
        Rectangle(hdc, 0, 0, (int)lrint((double)rc.right * g_midi_progress * 0.01), rc.bottom);
        SelectObject(hdc, old_obj);
        DeleteObject(br);
        EndPaint(hwnd, &ps);
        break;
    }
    case WM_TIMER:
        if (g_midi_progress >= 100.0)
        {
            KillTimer(hwnd, 1);
            DestroyWindow(hwnd);
            return 0;
        }
        InvalidateRect(hwnd, NULL, FALSE);
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static DWORD WINAPI midi_progress_thread_proc(LPVOID param)
{
    HINSTANCE hinst = GetModuleHandleA("rikky_module.dll");
    WNDCLASSA wc;
    HWND hwnd;
    MSG msg;

    (void)param;
    memset(&wc, 0, sizeof(wc));
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = midi_progress_wnd_proc;
    wc.hInstance = hinst;
    wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    wc.lpszClassName = "PROGRESS";
    RegisterClassA(&wc);

    hwnd = CreateWindowExA(
        0,
        "PROGRESS",
        "midi\x8f\x88\x97\x9d\x92\x86",
        0x10ca0000,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        300,
        60,
        NULL,
        NULL,
        hinst,
        NULL);

    if (!hwnd)
    {
        HWND *ph = (HWND *)RikkyGetMemoryData(0x0e);
        MessageBoxA(
            ph ? *ph : NULL,
            "\x90\x69\x92\xbb\x95\x5c\x8e\xa6\x82\xc9\x8e\xb8\x94\x73\x82\xb5\x82\xdc\x82\xb5\x82\xbd\n"
            "midi\x8f\x88\x97\x9d\x82\xaa\x8f\x49\x82\xed\x82\xe9\x82\xdc\x82\xc5\x82\xb5\x82\xce\x82\xe7\x82\xad\x82\xa8\x91\xd2\x82\xbf\x82\xad\x82\xbe\x82\xb3\x82\xa2",
            "midi",
            MB_OK);
    }
    else
    {
        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);
        while (GetMessageA(&msg, NULL, 0, 0) > 0)
        {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        if (g_midi_progress < 100.0)
        {
            g_midi_abort = 1;
        }
    }

    UnregisterClassA("PROGRESS", hinst);
    return 1;
}

typedef struct
{
    uint32_t step;
    uint32_t tempo;
} SmfBpmEvent;

typedef struct
{
    uint32_t step;
    int numer;
    int denom;
} SmfRhythmEvent;

typedef struct
{
    uint32_t step;
    int sf;
    int mi;
} SmfScaleEvent;

typedef struct
{
    uint32_t start;
    uint32_t end;
    int pitch;
    int vel;
} SmfNote;

typedef struct
{
    uint32_t step;
    int val;
} SmfExpr;

typedef struct
{
    uint32_t key;
    uint8_t active[256];
    uint32_t start_step[256];
    uint8_t start_vel[256];
    SmfNote *notes;
    size_t note_cnt;
    size_t note_cap;
    SmfExpr *exprs;
    size_t expr_cnt;
    size_t expr_cap;
} SmfChanTrack;

static void smf_free_all(
    SmfBpmEvent *bpms,
    SmfRhythmEvent *rhythms,
    SmfScaleEvent *scales,
    SmfChanTrack *chans,
    size_t chan_cnt)
{
    size_t i;
    rm_free(bpms);
    rm_free(rhythms);
    rm_free(scales);
    if (chans)
    {
        for (i = 0; i < chan_cnt; i++)
        {
            rm_free(chans[i].notes);
            rm_free(chans[i].exprs);
        }
        rm_free(chans);
    }
}

static SmfChanTrack *smf_get_chan(SmfChanTrack **chans, size_t *cnt, size_t *cap, uint32_t key)
{
    size_t i;
    for (i = 0; i < *cnt; i++)
    {
        if ((*chans)[i].key == key)
        {
            return &(*chans)[i];
        }
    }
    if (*cnt == *cap)
    {
        size_t new_cap = (*cap == 0) ? 16u : (*cap + 16u);
        SmfChanTrack *p = (SmfChanTrack *)rm_realloc(*chans, new_cap * sizeof(SmfChanTrack));
        if (!p)
        {
            return NULL;
        }
        *chans = p;
        *cap = new_cap;
    }
    memset(&(*chans)[*cnt], 0, sizeof(SmfChanTrack));
    (*chans)[*cnt].key = key;
    (*cnt)++;
    return &(*chans)[*cnt - 1u];
}

static int smf_add_note_event(SmfChanTrack *ct, uint32_t step, int is_on, uint8_t pitch, uint8_t vel)
{
    if (!ct->active[pitch])
    {
        if (is_on)
        {
            ct->active[pitch] = 1;
            ct->start_step[pitch] = step;
            ct->start_vel[pitch] = vel;
        }
        return 1;
    }

    if (ct->start_step[pitch] != step)
    {
        uint32_t s = ct->start_step[pitch];
        size_t pos;
        if (ct->note_cnt == ct->note_cap)
        {
            size_t new_cap = (ct->note_cap == 0) ? 64u : (ct->note_cap * 2u);
            SmfNote *p = (SmfNote *)rm_realloc(ct->notes, new_cap * sizeof(SmfNote));
            if (!p)
            {
                return 0;
            }
            ct->notes = p;
            ct->note_cap = new_cap;
        }
        pos = ct->note_cnt;
        while (pos > 0 && s < ct->notes[pos - 1u].start)
        {
            ct->notes[pos] = ct->notes[pos - 1u];
            pos--;
        }
        ct->notes[pos].start = s;
        ct->notes[pos].end = step;
        ct->notes[pos].pitch = (int)pitch;
        ct->notes[pos].vel = (int)ct->start_vel[pitch];
        ct->note_cnt++;
    }

    if (!is_on)
    {
        ct->active[pitch] = 0;
    }
    else
    {
        ct->start_step[pitch] = step;
        ct->start_vel[pitch] = vel;
    }
    return 1;
}

static int smf_add_expr_event(SmfChanTrack *ct, uint32_t step, int val)
{
    if (ct->expr_cnt == 0 && step != 0)
    {
        SmfExpr *p = (SmfExpr *)rm_realloc(ct->exprs, 16u * sizeof(SmfExpr));
        if (!p)
        {
            return 0;
        }
        ct->exprs = p;
        ct->expr_cap = 16u;
        ct->exprs[0].step = 0;
        ct->exprs[0].val = 127;
        ct->expr_cnt = 1u;
    }
    if (ct->expr_cnt == ct->expr_cap)
    {
        size_t new_cap = (ct->expr_cap == 0) ? 16u : (ct->expr_cap * 2u);
        SmfExpr *p = (SmfExpr *)rm_realloc(ct->exprs, new_cap * sizeof(SmfExpr));
        if (!p)
        {
            return 0;
        }
        ct->exprs = p;
        ct->expr_cap = new_cap;
    }
    ct->exprs[ct->expr_cnt].step = step;
    ct->exprs[ct->expr_cnt].val = val;
    ct->expr_cnt++;
    return 1;
}

static int smf_convert_to_dat(const char *mid_path, const char *dat_path)
{
    FILE *fp;
    uint8_t hdr[14];
    uint32_t ntrks;
    uint32_t division;
    uint32_t max_step = 0;
    SmfBpmEvent *bpms = NULL;
    size_t bpm_cnt = 0;
    size_t bpm_cap = 0;
    SmfRhythmEvent *rhythms = NULL;
    size_t rhy_cnt = 0;
    size_t rhy_cap = 0;
    SmfScaleEvent *scales = NULL;
    size_t scl_cnt = 0;
    size_t scl_cap = 0;
    SmfChanTrack *chans = NULL;
    size_t chan_cnt = 0;
    size_t chan_cap = 0;
    HANDLE hthread;
    DWORD tid = 0;
    uint32_t trk;
    FILE *out_fp;
    size_t i;
    size_t active_tracks = 0;
    size_t written_tracks = 0;
    int has_notes = 0;
    int max_note = 72;
    int min_note = 72;

    fp = fopen(mid_path, "rb");
    if (!fp)
    {
        return 0;
    }

    if (fread(hdr, 1, 14, fp) != 14 ||
        memcmp(hdr, "MThd", 4) != 0 ||
        hdr[4] != 0 || hdr[5] != 0 || hdr[6] != 0 || hdr[7] != 6 ||
        (hdr[12] & 0x80) != 0)
    {
        fclose(fp);
        return 0;
    }

    ntrks = ((uint32_t)hdr[10] << 8) | (uint32_t)hdr[11];
    division = ((uint32_t)hdr[12] << 8) | (uint32_t)hdr[13];

    chan_cap = (size_t)ntrks * 16u;
    if (chan_cap > 0)
    {
        chans = (SmfChanTrack *)rm_calloc(chan_cap, sizeof(SmfChanTrack));
        if (!chans)
        {
            fclose(fp);
            return 0;
        }
    }

    g_midi_abort = 0;
    g_midi_progress = 0.0;
    hthread = CreateThread(NULL, 0, midi_progress_thread_proc, NULL, 0, &tid);
    if (hthread)
    {
        CloseHandle(hthread);
    }

    for (trk = 0; trk < ntrks; trk++)
    {
        uint8_t thdr[8];
        uint32_t trk_len;
        uint32_t pos = 0;
        uint32_t step = 0;
        uint8_t run_status = 0;

        if (fread(thdr, 1, 8, fp) != 8)
        {
            break;
        }
        trk_len = ((uint32_t)thdr[4] << 24) |
                  ((uint32_t)thdr[5] << 16) |
                  ((uint32_t)thdr[6] << 8) |
                  (uint32_t)thdr[7];

        while (pos < trk_len)
        {
            uint32_t delta = 0;
            int ch_byte;
            uint8_t status;
            uint8_t hi;
            uint8_t lo;

            if (g_midi_abort != 0)
            {
                fclose(fp);
                smf_free_all(bpms, rhythms, scales, chans, chan_cnt);
                return 0;
            }

            if (trk_len > 0)
            {
                g_midi_progress = (double)((((float)trk + (float)pos / (float)trk_len) / (float)ntrks) * 100.0f);
            }

            for (;;)
            {
                ch_byte = fgetc(fp);
                if (ch_byte == EOF)
                {
                    pos = trk_len;
                    break;
                }
                pos++;
                if ((ch_byte & 0x80) == 0)
                {
                    delta = (delta << 7) | (uint32_t)(ch_byte & 0x7f);
                    break;
                }
                delta = (delta << 7) | (uint32_t)(ch_byte & 0x7f);
            }
            if (ch_byte == EOF)
            {
                break;
            }

            step += delta;
            ch_byte = fgetc(fp);
            if (ch_byte == EOF)
            {
                break;
            }
            pos++;
            status = (uint8_t)ch_byte;

            if ((status & 0x80u) != 0)
            {
                hi = status >> 4;
                lo = status & 0x0fu;
                if (hi == 8 || hi == 9)
                {
                    int n_b = fgetc(fp);
                    int v_b = fgetc(fp);
                    SmfChanTrack *ct;
                    int is_on;
                    pos += 2u;
                    run_status = status;
                    if (n_b == EOF || v_b == EOF)
                    {
                        break;
                    }
                    ct = smf_get_chan(&chans, &chan_cnt, &chan_cap, (trk << 8) | (uint32_t)lo);
                    is_on = (hi == 9 && (v_b & 0xff) != 0) ? 1 : 0;
                    if (!ct || !smf_add_note_event(ct, step, is_on, (uint8_t)n_b, (uint8_t)(is_on ? v_b : 0)))
                    {
                        g_midi_progress = 100.0;
                        fclose(fp);
                        smf_free_all(bpms, rhythms, scales, chans, chan_cnt);
                        return 0;
                    }
                    if (step > max_step)
                    {
                        max_step = step;
                    }
                }
                else if (hi == 0x0a || hi == 0x0e)
                {
                    fseek(fp, 2, SEEK_CUR);
                    pos += 2u;
                    run_status = status;
                }
                else if (hi == 0x0b)
                {
                    int cc_b = fgetc(fp);
                    int val_b = fgetc(fp);
                    pos += 2u;
                    run_status = status;
                    if (cc_b == EOF || val_b == EOF)
                    {
                        break;
                    }
                    if ((cc_b & 0xff) == 11)
                    {
                        SmfChanTrack *ct = smf_get_chan(&chans, &chan_cnt, &chan_cap, (trk << 8) | (uint32_t)lo);
                        if (!ct || !smf_add_expr_event(ct, step, val_b & 0xff))
                        {
                            g_midi_progress = 100.0;
                            fclose(fp);
                            smf_free_all(bpms, rhythms, scales, chans, chan_cnt);
                            return 0;
                        }
                        if (step > max_step)
                        {
                            max_step = step;
                        }
                    }
                }
                else if (hi == 0x0c || hi == 0x0d)
                {
                    fseek(fp, 1, SEEK_CUR);
                    pos += 1u;
                    run_status = status;
                }
                else
                {
                    if (lo == 0x0f)
                    {
                        int mtype = fgetc(fp);
                        pos++;
                        if (mtype == 0x58)
                        {
                            uint8_t mb[4];
                            fseek(fp, 1, SEEK_CUR);
                            if (fread(mb, 1, 4, fp) != 4)
                            {
                                break;
                            }
                            pos += 5u;
                            if (rhy_cnt == rhy_cap)
                            {
                                size_t nc = (rhy_cap == 0) ? 16u : (rhy_cap * 2u);
                                SmfRhythmEvent *p = (SmfRhythmEvent *)rm_realloc(rhythms, nc * sizeof(SmfRhythmEvent));
                                if (!p)
                                {
                                    g_midi_progress = 100.0;
                                    fclose(fp);
                                    smf_free_all(bpms, rhythms, scales, chans, chan_cnt);
                                    return 0;
                                }
                                rhythms = p;
                                rhy_cap = nc;
                            }
                            rhythms[rhy_cnt].step = step;
                            rhythms[rhy_cnt].numer = (int)mb[0];
                            rhythms[rhy_cnt].denom = 1 << (mb[1] & 0x1fu);
                            rhy_cnt++;
                            if (step > max_step)
                            {
                                max_step = step;
                            }
                        }
                        else if (mtype == 0x59)
                        {
                            uint8_t mb[2];
                            size_t sp;
                            int dup = 0;
                            fseek(fp, 1, SEEK_CUR);
                            if (fread(mb, 1, 2, fp) != 2)
                            {
                                break;
                            }
                            pos += 3u;
                            for (sp = 0; sp < scl_cnt; sp++)
                            {
                                if (scales[sp].step == step)
                                {
                                    dup = 1;
                                    break;
                                }
                                if (scales[sp].step > step)
                                {
                                    break;
                                }
                            }
                            if (!dup)
                            {
                                if (scl_cnt == scl_cap)
                                {
                                    size_t nc = (scl_cap == 0) ? 16u : (scl_cap * 2u);
                                    SmfScaleEvent *p = (SmfScaleEvent *)rm_realloc(scales, nc * sizeof(SmfScaleEvent));
                                    if (!p)
                                    {
                                        g_midi_progress = 100.0;
                                        fclose(fp);
                                        smf_free_all(bpms, rhythms, scales, chans, chan_cnt);
                                        return 0;
                                    }
                                    scales = p;
                                    scl_cap = nc;
                                }
                                if (sp < scl_cnt)
                                {
                                    memmove(&scales[sp + 1u], &scales[sp], (scl_cnt - sp) * sizeof(SmfScaleEvent));
                                }
                                scales[sp].step = step;
                                scales[sp].sf = (int)(int8_t)mb[0];
                                scales[sp].mi = (int)mb[1];
                                scl_cnt++;
                            }
                            if (step > max_step)
                            {
                                max_step = step;
                            }
                        }
                        else if (mtype == 0x51)
                        {
                            uint8_t mb[3];
                            fseek(fp, 1, SEEK_CUR);
                            if (fread(mb, 1, 3, fp) != 3)
                            {
                                break;
                            }
                            pos += 4u;
                            if (bpm_cnt == bpm_cap)
                            {
                                size_t nc = (bpm_cap == 0) ? 16u : (bpm_cap * 2u);
                                SmfBpmEvent *p = (SmfBpmEvent *)rm_realloc(bpms, nc * sizeof(SmfBpmEvent));
                                if (!p)
                                {
                                    g_midi_progress = 100.0;
                                    fclose(fp);
                                    smf_free_all(bpms, rhythms, scales, chans, chan_cnt);
                                    return 0;
                                }
                                bpms = p;
                                bpm_cap = nc;
                            }
                            bpms[bpm_cnt].step = step;
                            bpms[bpm_cnt].tempo = ((uint32_t)mb[0] << 16) | ((uint32_t)mb[1] << 8) | (uint32_t)mb[2];
                            bpm_cnt++;
                            if (step > max_step)
                            {
                                max_step = step;
                            }
                        }
                        else
                        {
                            uint32_t mlen = 0;
                            for (;;)
                            {
                                int lb = fgetc(fp);
                                if (lb == EOF)
                                {
                                    pos = trk_len;
                                    break;
                                }
                                pos++;
                                mlen = (mlen << 7) | (uint32_t)(lb & 0x7f);
                                if ((lb & 0x80) == 0)
                                {
                                    break;
                                }
                            }
                            fseek(fp, (long)mlen, SEEK_CUR);
                            pos += mlen;
                        }
                    }
                    else if (lo == 0x00 || lo == 0x07)
                    {
                        uint32_t slen = 0;
                        for (;;)
                        {
                            int lb = fgetc(fp);
                            if (lb == EOF)
                            {
                                pos = trk_len;
                                break;
                            }
                            pos++;
                            slen = (slen << 7) | (uint32_t)(lb & 0x7f);
                            if ((lb & 0x80) == 0)
                            {
                                break;
                            }
                        }
                        fseek(fp, (long)slen, SEEK_CUR);
                        pos += slen;
                    }
                }
            }
            else
            {
                hi = run_status >> 4;
                lo = run_status & 0x0fu;
                if (hi == 8 || hi == 9)
                {
                    int v_b = fgetc(fp);
                    SmfChanTrack *ct;
                    int is_on;
                    pos++;
                    if (v_b == EOF)
                    {
                        break;
                    }
                    ct = smf_get_chan(&chans, &chan_cnt, &chan_cap, (trk << 8) | (uint32_t)lo);
                    is_on = (hi == 9 && (v_b & 0xff) != 0) ? 1 : 0;
                    if (!ct || !smf_add_note_event(ct, step, is_on, status, (uint8_t)(is_on ? v_b : 0)))
                    {
                        g_midi_progress = 100.0;
                        fclose(fp);
                        smf_free_all(bpms, rhythms, scales, chans, chan_cnt);
                        return 0;
                    }
                    if (step > max_step)
                    {
                        max_step = step;
                    }
                }
                else if (hi == 0x0a || hi == 0x0e)
                {
                    fseek(fp, 1, SEEK_CUR);
                    pos++;
                }
                else if (hi == 0x0b)
                {
                    if (status == 11)
                    {
                        int val_b = fgetc(fp);
                        SmfChanTrack *ct;
                        pos++;
                        if (val_b == EOF)
                        {
                            break;
                        }
                        ct = smf_get_chan(&chans, &chan_cnt, &chan_cap, (trk << 8) | (uint32_t)lo);
                        if (!ct || !smf_add_expr_event(ct, step, val_b & 0xff))
                        {
                            g_midi_progress = 100.0;
                            fclose(fp);
                            smf_free_all(bpms, rhythms, scales, chans, chan_cnt);
                            return 0;
                        }
                        if (step > max_step)
                        {
                            max_step = step;
                        }
                    }
                    else
                    {
                        fseek(fp, 1, SEEK_CUR);
                        pos++;
                    }
                }
            }
        }
    }

    g_midi_progress = 100.0;
    fclose(fp);

    out_fp = fopen(dat_path, "w");
    if (!out_fp)
    {
        smf_free_all(bpms, rhythms, scales, chans, chan_cnt);
        return 0;
    }

    fwrite("return {\nbpm={\n", 1, 15, out_fp);
    if (bpm_cnt == 0)
    {
        fprintf(out_fp, "{%d,%d}", 0, 500000);
    }
    else
    {
        int wrote = 0;
        for (i = 0; i < bpm_cnt; i++)
        {
            if (i == 0 && bpms[0].step != 0)
            {
                fprintf(out_fp, "{0,%d}", 500000);
                wrote = 1;
            }
            if (wrote)
            {
                fputc(',', out_fp);
            }
            fprintf(out_fp, "{%u,%u}", bpms[i].step, bpms[i].tempo);
            wrote = 1;
        }
    }

    fwrite("\n},\nrhythm={\n", 1, 13, out_fp);
    if (rhy_cnt == 0)
    {
        fprintf(out_fp, "{%d,%d,%d}", 0, 4, 4);
    }
    else
    {
        int wrote = 0;
        for (i = 0; i < rhy_cnt; i++)
        {
            if (i == 0 && rhythms[0].step != 0)
            {
                fprintf(out_fp, "{0,%d,%d}", 4, 4);
                wrote = 1;
            }
            if (wrote)
            {
                fputc(',', out_fp);
            }
            fprintf(out_fp, "{%u,%d,%d}", rhythms[i].step, rhythms[i].numer, rhythms[i].denom);
            wrote = 1;
        }
    }

    fwrite("\n},\nscale={\n", 1, 12, out_fp);
    if (scl_cnt == 0)
    {
        fprintf(out_fp, "{%d,%d,%d}", 0, 0, 0);
    }
    else
    {
        if (scales[0].step > 0)
        {
            fprintf(out_fp, "{0,%d,%d},", 0, 0);
        }
        for (i = 0; i + 1u < scl_cnt; i++)
        {
            fprintf(out_fp, "{%u,%d,%d},", scales[i].step, scales[i].sf, scales[i].mi);
        }
        fprintf(out_fp, "{%u,%d,%d}", scales[scl_cnt - 1u].step, scales[scl_cnt - 1u].sf, scales[scl_cnt - 1u].mi);
    }

    fwrite("\n},\nevent={\n", 1, 12, out_fp);
    for (i = 0; i < chan_cnt; i++)
    {
        if (chans[i].note_cnt > 0)
        {
            active_tracks++;
        }
    }

    if (active_tracks == 0)
    {
        fprintf(out_fp, "{\n{%u,72,1,1}\n", division * 4u);
        max_note = 72;
        min_note = 72;
        active_tracks = 1u;
    }
    else
    {
        for (i = 0; i < chan_cnt; i++)
        {
            SmfChanTrack *ct = &chans[i];
            size_t j;
            if (ct->note_cnt == 0)
            {
                continue;
            }
            fwrite("{\nnote={\n", 1, 9, out_fp);
            for (j = 0; j < ct->note_cnt; j++)
            {
                int p = ct->notes[j].pitch;
                if (!has_notes)
                {
                    max_note = p;
                    min_note = p;
                    has_notes = 1;
                }
                else
                {
                    if (p > max_note)
                    {
                        max_note = p;
                    }
                    if (p < min_note)
                    {
                        min_note = p;
                    }
                }
                if (j > 0)
                {
                    fputc(',', out_fp);
                }
                fprintf(
                    out_fp,
                    "{%d,%u,%u,%g}",
                    p,
                    ct->notes[j].start,
                    ct->notes[j].end,
                    (double)((float)ct->notes[j].vel / 100.0f));
            }
            fputc('\n', out_fp);
            fwrite("},\nexpression={\n", 1, 16, out_fp);
            for (j = 0; j < ct->expr_cnt; j++)
            {
                if (j > 0)
                {
                    fprintf(out_fp, ",{%u,%d}", ct->exprs[j].step, ct->exprs[j].val);
                }
                else
                {
                    fprintf(out_fp, "{%u,%d}", ct->exprs[j].step, ct->exprs[j].val);
                }
            }
            fwrite("\n}\n}", 1, 4, out_fp);
            written_tracks++;
            if (written_tracks == active_tracks)
            {
                fputc('\n', out_fp);
            }
            else
            {
                fwrite(",\n", 1, 2, out_fp);
            }
        }
    }

    fwrite("},\ninfo={\n", 1, 10, out_fp);
    fprintf(out_fp, "%d,%d,%u,%u,%u\n}\n}\n", max_note, min_note, (uint32_t)active_tracks, division, max_step);
    fclose(out_fp);

    smf_free_all(bpms, rhythms, scales, chans, chan_cnt);
    return 1;
}

static char *build_dat_path(const char *mid_path)
{
    size_t len = strlen(mid_path);
    const char *dot = strrchr(mid_path, '.');
    size_t stem_len = dot ? (size_t)(dot - mid_path) : ((len >= 3u) ? (len - 3u) : len);
    char *dat_path = (char *)rm_malloc(stem_len + 5u);
    if (!dat_path)
    {
        return NULL;
    }
    memcpy(dat_path, mid_path, stem_len);
    if (dot)
    {
        memcpy(dat_path + stem_len, ".dat", 5u);
    }
    else
    {
        memcpy(dat_path + stem_len, "dat", 4u);
    }
    return dat_path;
}

int __cdecl lua_rikky_midiconverter(lua_State *L)
{
    const char *mid_path;
    char *dat_path;
    FILE *fp;

    if (lua_gettop(L) <= 0 || lua_type(L, 1) != LUA_TSTRING)
    {
        lua_pushboolean(L, 0);
        return 1;
    }

    mid_path = lua_tolstring(L, 1, NULL);
    if (!mid_path || mid_path[0] == '\0')
    {
        lua_pushboolean(L, 0);
        return 1;
    }

    dat_path = build_dat_path(mid_path);
    if (!dat_path)
    {
        lua_pushboolean(L, 0);
        return 1;
    }

    fp = fopen(dat_path, "r");
    if (fp)
    {
        fclose(fp);
    }
    else if (!smf_convert_to_dat(mid_path, dat_path))
    {
        rm_free(dat_path);
        lua_pushboolean(L, 0);
        return 1;
    }

    rm_free(dat_path);
    lua_pushboolean(L, 1);
    return 1;
}

// midiconverterEx table methods and helpers

static int format_note_name(int note, char *out)
{
    static const char *const names[12] =
    {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };
    if (note < 0 || note > 127)
    {
        return 0;
    }
    sprintf(out, "%s%d", names[note % 12], note / 12 - 1);
    return 1;
}

static void format_scale_key(int mi, int sf, char *out)
{
    static const char maj_notes[7] = { 'C', 'G', 'D', 'A', 'E', 'B', 'F' };
    static const char min_notes[7] = { 'A', 'E', 'B', 'F', 'C', 'G', 'D' };
    int idx = sf + 7;
    int mod = ((idx % 7) + 7) % 7;

    if (mi == 0)
    {
        out[0] = maj_notes[mod];
        if (idx < 6)
        {
            out[1] = 'b';
            out[2] = '\0';
        }
        else if (idx >= 13)
        {
            out[1] = '#';
            out[2] = '\0';
        }
        else
        {
            out[1] = '\0';
        }
    }
    else
    {
        out[0] = min_notes[mod];
        if (idx < 3)
        {
            out[1] = 'b';
            out[2] = 'm';
            out[3] = '\0';
        }
        else if (idx >= 10)
        {
            out[1] = '#';
            out[2] = 'm';
            out[3] = '\0';
        }
        else
        {
            out[1] = 'm';
            out[2] = '\0';
        }
    }
}

static void format_scale_name(int mi, int sf, char *out)
{
    static const char *const maj_jp[7] =
    {
        "\x83\x6e", "\x83\x67", "\x83\x6a", "\x83\x43", "\x83\x7a", "\x83\x8d", "\x83\x77"
    };
    static const char *const min_jp[7] =
    {
        "\x83\x43", "\x83\x7a", "\x83\x8d", "\x83\x77", "\x83\x6e", "\x83\x67", "\x83\x6a"
    };
    int idx = sf + 7;
    int mod = ((idx % 7) + 7) % 7;
    const char *acc = "";

    if (mi == 0)
    {
        if (idx < 6)
        {
            acc = "\x95\xcf";
        }
        else if (idx > 12)
        {
            acc = "\x89\x64";
        }
        sprintf(out, "%s%s\x92\xb7\x92\xb2", acc, maj_jp[mod]);
    }
    else
    {
        if (idx < 3)
        {
            acc = "\x95\xcf";
        }
        else if (idx > 9)
        {
            acc = "\x89\x64";
        }
        sprintf(out, "%s%s\x92\x5a\x92\xb2", acc, min_jp[mod]);
    }
}

static int midi_time_to_step(lua_State *L, double t, int delta)
{
    int len;
    int k;
    double s_step;
    double tempo;
    double s_time;
    double steps_f;

    lua_getfield(L, 1, "bpm");
    len = (int)lua_objlen(L, -1);
    if (len <= 0)
    {
        lua_pop(L, 1);
        return 0;
    }

    if (t < 0.0)
    {
        lua_rawgeti(L, -1, 1);
        lua_rawgeti(L, -1, 2);
        tempo = lua_tonumber(L, -1);
        lua_pop(L, 3);
        steps_f = floor((double)delta * (-t / tempo) * 1000000.0 + 0.5);
        return -(int)lrint(steps_f);
    }

    for (k = 1; k < len; k++)
    {
        double t0;
        double t1;
        lua_rawgeti(L, -1, k);
        lua_rawgeti(L, -1, 3);
        t0 = lua_tonumber(L, -1);
        lua_rawgeti(L, -2, 4);
        t1 = lua_tonumber(L, -1);
        if (t0 <= t && t < t1)
        {
            lua_rawgeti(L, -3, 2);
            tempo = lua_tonumber(L, -1);
            lua_rawgeti(L, -4, 1);
            s_step = lua_tonumber(L, -1);
            lua_pop(L, 6);
            steps_f = floor((double)delta * ((t - t0) / tempo) * 1000000.0);
            return (int)lrint(steps_f + s_step);
        }
        lua_pop(L, 3);
    }

    lua_rawgeti(L, -1, len);
    lua_rawgeti(L, -1, 1);
    s_step = lua_tonumber(L, -1);
    lua_rawgeti(L, -2, 2);
    tempo = lua_tonumber(L, -1);
    lua_rawgeti(L, -3, 3);
    s_time = lua_tonumber(L, -1);
    lua_pop(L, 5);
    steps_f = floor((double)delta * ((t - s_time) / tempo) * 1000000.0);
    return (int)lrint(steps_f + s_step);
}

static double midi_step_to_time(lua_State *L, double step, int delta)
{
    int len;
    int k;
    double s0;
    double tempo;
    double t0;

    lua_getfield(L, 1, "bpm");
    len = (int)lua_objlen(L, -1);
    if (len <= 0)
    {
        lua_pop(L, 1);
        return 0.0;
    }

    if ((float)step < 0.0f)
    {
        lua_rawgeti(L, -1, 1);
        lua_rawgeti(L, -1, 2);
        tempo = lua_tonumber(L, -1);
        lua_pop(L, 3);
        return ((step / (double)delta) * tempo) / 1000000.0;
    }

    for (k = 1; k < len; k++)
    {
        double s1;
        lua_rawgeti(L, -1, k);
        lua_rawgeti(L, -1, 1);
        s0 = lua_tonumber(L, -1);
        lua_rawgeti(L, -3, k + 1);
        lua_rawgeti(L, -1, 1);
        s1 = lua_tonumber(L, -1);
        if (s0 <= step && step < s1)
        {
            lua_rawgeti(L, -4, 2);
            tempo = lua_tonumber(L, -1);
            lua_rawgeti(L, -5, 3);
            t0 = lua_tonumber(L, -1);
            lua_pop(L, 7);
            return (((step - s0) / (double)delta) * tempo) / 1000000.0 + t0;
        }
        lua_pop(L, 4);
    }

    lua_rawgeti(L, -1, len);
    lua_rawgeti(L, -1, 1);
    s0 = lua_tonumber(L, -1);
    lua_rawgeti(L, -2, 2);
    tempo = lua_tonumber(L, -1);
    lua_rawgeti(L, -3, 3);
    t0 = lua_tonumber(L, -1);
    lua_pop(L, 5);
    return (((step - s0) / (double)delta) * tempo) / 1000000.0 + t0;
}

static void midi_step_to_bar_vals(
    lua_State *L,
    double step,
    int delta,
    double *out_bar,
    double *out_beat,
    double *out_prog)
{
    int len;
    int k;
    double bar_acc = 0.0;
    double s0;
    double bar_len;
    double denom;
    double bars_in;
    double beat_total;
    double beat_int;

    lua_getfield(L, 1, "rhythm");
    len = (int)lua_objlen(L, -1);
    if (len <= 0)
    {
        lua_pop(L, 1);
        *out_bar = 1.0;
        *out_beat = 1.0;
        *out_prog = 0.0;
        return;
    }

    if ((float)step < 0.0f)
    {
        lua_rawgeti(L, -1, 1);
        lua_rawgeti(L, -1, 4);
        bar_len = lua_tonumber(L, -1);
        lua_rawgeti(L, -2, 3);
        denom = lua_tonumber(L, -1);
        lua_pop(L, 4);
        bars_in = floor(-step / bar_len);
        beat_total = (((step + bar_len + bar_len * bars_in) / (double)delta) * denom) * 0.25;
        beat_int = floor(beat_total);
        *out_bar = bars_in;
        *out_beat = beat_int + 1.0;
        *out_prog = beat_total - beat_int;
        return;
    }

    for (k = 1; k < len; k++)
    {
        double s1;
        double bcnt;
        lua_rawgeti(L, -1, k);
        lua_rawgeti(L, -1, 1);
        s0 = lua_tonumber(L, -1);
        lua_rawgeti(L, -3, k + 1);
        lua_rawgeti(L, -1, 1);
        s1 = lua_tonumber(L, -1);
        if (s0 <= step && step < s1)
        {
            lua_rawgeti(L, -4, 4);
            bar_len = lua_tonumber(L, -1);
            lua_rawgeti(L, -5, 3);
            denom = lua_tonumber(L, -1);
            lua_pop(L, 7);
            bars_in = floor((step - s0) / bar_len);
            beat_total = ((((step - s0) - bars_in * bar_len) / (double)delta) * denom) * 0.25;
            beat_int = floor(beat_total);
            *out_bar = bar_acc + bars_in + 1.0;
            *out_beat = beat_int + 1.0;
            *out_prog = beat_total - beat_int;
            return;
        }
        lua_rawgeti(L, -4, 5);
        bcnt = lua_tonumber(L, -1);
        bar_acc += bcnt;
        lua_pop(L, 5);
    }

    lua_rawgeti(L, -1, len);
    lua_rawgeti(L, -1, 1);
    s0 = lua_tonumber(L, -1);
    lua_rawgeti(L, -2, 4);
    bar_len = lua_tonumber(L, -1);
    lua_rawgeti(L, -3, 3);
    denom = lua_tonumber(L, -1);
    lua_pop(L, 5);

    bars_in = floor((step - s0) / bar_len);
    beat_total = ((((step - s0) - bars_in * bar_len) / (double)delta) * denom) * 0.25;
    beat_int = floor(beat_total);
    *out_bar = bar_acc + bars_in + 1.0;
    *out_beat = beat_int + 1.0;
    *out_prog = beat_total - beat_int;
}

static double midi_bar_to_step(lua_State *L, double bar, double beat, double prog, int delta)
{
    int len;
    int k;
    int bar_sum = 0;
    double step_acc = 0.0;
    double bar_len;
    int numer;
    int denom;
    double beat_off;

    lua_getfield(L, 1, "rhythm");
    len = (int)lua_objlen(L, -1);
    if (len <= 0)
    {
        lua_pop(L, 1);
        return 0.0;
    }

    if (bar < 1.0)
    {
        lua_rawgeti(L, -1, 1);
        lua_rawgeti(L, -1, 4);
        bar_len = lua_tonumber(L, -1);
        lua_rawgeti(L, -2, 2);
        numer = (int)lua_tointeger(L, -1);
        lua_rawgeti(L, -3, 3);
        denom = (int)lua_tointeger(L, -1);
        lua_pop(L, 5);
        beat_off = (beat >= 1.0 && beat <= (double)numer) ? (beat - 1.0) : 0.0;
        return -floor((-bar_len * bar) + (double)delta * ((beat_off * 4.0) / (double)denom + prog));
    }

    for (k = 1; k < len; k++)
    {
        int bcnt;
        double prev_bars;
        lua_rawgeti(L, -1, k);
        lua_rawgeti(L, -1, 5);
        bcnt = (int)lua_tointeger(L, -1);
        prev_bars = (double)bar_sum;
        bar_sum += bcnt;
        if (prev_bars < bar && bar <= (double)bar_sum)
        {
            int blen_i;
            lua_rawgeti(L, -2, 4);
            blen_i = (int)lua_tointeger(L, -1);
            lua_rawgeti(L, -3, 2);
            numer = (int)lua_tointeger(L, -1);
            lua_rawgeti(L, -4, 3);
            denom = (int)lua_tointeger(L, -1);
            lua_pop(L, 6);
            if (beat < 1.0)
            {
                beat_off = 0.0;
            }
            else if (beat <= (double)numer)
            {
                beat_off = beat - 1.0;
            }
            else
            {
                beat_off = (double)numer - 1.0;
            }
            return floor(
                (double)blen_i * ((bar - prev_bars) - 1.0) +
                step_acc +
                (double)delta * ((beat_off * 4.0) / (double)denom + prog));
        }
        lua_rawgeti(L, -3, k + 1);
        lua_rawgeti(L, -1, 1);
        step_acc = lua_tonumber(L, -1);
        lua_pop(L, 4);
    }

    lua_rawgeti(L, -1, len);
    lua_rawgeti(L, -1, 1);
    step_acc = lua_tonumber(L, -1);
    lua_rawgeti(L, -2, 4);
    bar_len = (double)(int)lua_tointeger(L, -1);
    lua_rawgeti(L, -3, 2);
    numer = (int)lua_tointeger(L, -1);
    lua_rawgeti(L, -4, 3);
    denom = (int)lua_tointeger(L, -1);
    lua_pop(L, 6);

    if (beat < 1.0)
    {
        beat_off = 0.0;
    }
    else if (beat > (double)numer)
    {
        beat_off = (double)numer - 1.0;
    }
    else
    {
        beat_off = beat - 1.0;
    }
    return floor(
        bar_len * ((bar - (double)bar_sum) - 1.0) +
        step_acc +
        (double)delta * prog +
        ((beat_off * 4.0) / (double)denom) * (double)delta);
}

static int midi_find_rhythm_at_step(lua_State *L, int rhy_tbl_idx, double step, int start_idx)
{
    int len = (int)lua_objlen(L, rhy_tbl_idx);
    int k = start_idx;

    lua_rawgeti(L, rhy_tbl_idx, k);
    while (k < len)
    {
        double s0;
        double s1;
        lua_rawgeti(L, rhy_tbl_idx, k + 1);
        lua_rawgeti(L, -2, 1);
        s0 = lua_tonumber(L, -1);
        lua_rawgeti(L, -2, 1);
        s1 = lua_tonumber(L, -1);
        if (s0 <= step && step < s1)
        {
            lua_pop(L, 3);
            return k;
        }
        lua_pop(L, 2);
        lua_remove(L, -2);
        k++;
    }
    return len;
}

static int midi_find_scale_idx(lua_State *L, double step)
{
    int len;
    int k;

    lua_getfield(L, 1, "scale");
    len = (int)lua_objlen(L, -1);
    if (len <= 1 || step < 0.0)
    {
        lua_pop(L, 1);
        return 1;
    }
    for (k = 1; k < len; k++)
    {
        double s0;
        double s1;
        lua_rawgeti(L, -1, k);
        lua_rawgeti(L, -1, 1);
        s0 = lua_tonumber(L, -1);
        lua_rawgeti(L, -3, k + 1);
        lua_rawgeti(L, -1, 1);
        s1 = lua_tonumber(L, -1);
        lua_pop(L, 4);
        if (s0 <= step && step < s1)
        {
            lua_pop(L, 1);
            return k;
        }
    }
    lua_pop(L, 1);
    return len;
}

static void midi_read_now_state(
    lua_State *L,
    double *out_time,
    int *out_delta,
    int *out_tracks,
    int *out_type_range,
    double *out_down,
    double *out_up)
{
    double t;
    double down_v;

    lua_getfield(L, 1, "offsettime");
    t = lua_tonumber(L, -1);
    lua_getglobal(L, "obj");
    lua_getfield(L, -1, "time");
    t += lua_tonumber(L, -1);
    lua_pop(L, 3);

    lua_getfield(L, 1, "info");
    lua_rawgeti(L, -1, 4);
    *out_delta = (int)lrint(lua_tonumber(L, -1));
    if (out_tracks)
    {
        lua_rawgeti(L, -2, 3);
        *out_tracks = (int)lua_tointeger(L, -1);
        lua_pop(L, 3);
    }
    else
    {
        lua_pop(L, 2);
    }

    lua_getfield(L, 1, "type_range");
    *out_type_range = (int)lrint(lua_tonumber(L, -1));
    lua_pop(L, 1);

    lua_getfield(L, 1, "down_range");
    down_v = lua_tonumber(L, -1);
    lua_pop(L, 1);
    if (down_v == 0.0)
    {
        lua_getfield(L, 1, "donw_range");
        if (lua_type(L, -1) == LUA_TNUMBER)
        {
            down_v = lua_tonumber(L, -1);
        }
        lua_pop(L, 1);
    }
    *out_down = down_v;

    lua_getfield(L, 1, "up_range");
    *out_up = lua_tonumber(L, -1);
    lua_pop(L, 1);

    *out_time = t;
}

static void midi_compute_step_window(
    lua_State *L,
    double now_time,
    int delta,
    int type_range,
    double down_range,
    double up_range,
    double *out_cur_step,
    double *out_low_step,
    double *out_high_step)
{
    int cur_s = midi_time_to_step(L, now_time, delta);
    *out_cur_step = (double)cur_s;

    if (type_range == 0)
    {
        *out_low_step = (double)cur_s;
        *out_high_step = (double)cur_s;
    }
    else if (type_range == 1)
    {
        *out_low_step = (double)midi_time_to_step(L, now_time - down_range, delta);
        *out_high_step = (double)midi_time_to_step(L, now_time + up_range, delta);
    }
    else if (type_range == 2)
    {
        *out_low_step = (double)cur_s - down_range;
        *out_high_step = (double)cur_s + up_range;
    }
    else if (type_range == 3)
    {
        double bar;
        double beat;
        double prog;
        midi_step_to_bar_vals(L, (double)cur_s, delta, &bar, &beat, &prog);
        *out_low_step = midi_bar_to_step(L, bar - down_range, beat, prog, delta);
        *out_high_step = midi_bar_to_step(L, bar + up_range, beat, prog, delta);
    }
    else
    {
        *out_low_step = (double)cur_s;
        *out_high_step = (double)cur_s;
    }
}

static int __cdecl midi_m_set_offset_time(lua_State *L)
{
    double old_v;
    lua_getfield(L, 1, "offsettime");
    old_v = lua_tonumber(L, -1);
    lua_pop(L, 1);
    if (lua_type(L, 2) == LUA_TNUMBER)
    {
        lua_pushnumber(L, lua_tonumber(L, 2));
    }
    else
    {
        lua_pushnumber(L, 0.0);
    }
    lua_setfield(L, 1, "offsettime");
    lua_pushnumber(L, old_v);
    return 1;
}

static int __cdecl midi_m_set_up_range(lua_State *L)
{
    double old_v;
    lua_getfield(L, 1, "up_range");
    old_v = lua_tonumber(L, -1);
    lua_pop(L, 1);
    if (lua_type(L, 2) == LUA_TNUMBER)
    {
        double v = lua_tonumber(L, 2);
        lua_pushnumber(L, (v >= 0.0) ? v : 0.0);
    }
    else
    {
        lua_pushnumber(L, 0.0);
    }
    lua_setfield(L, 1, "up_range");
    lua_pushnumber(L, old_v);
    return 1;
}

static int __cdecl midi_m_set_down_range(lua_State *L)
{
    double old_v;
    lua_getfield(L, 1, "down_range");
    old_v = lua_tonumber(L, -1);
    lua_pop(L, 1);
    if (lua_type(L, 2) == LUA_TNUMBER)
    {
        double v = lua_tonumber(L, 2);
        lua_pushnumber(L, (v >= 0.0) ? v : 0.0);
    }
    else
    {
        lua_pushnumber(L, 0.0);
    }
    lua_setfield(L, 1, "down_range");
    lua_pushnumber(L, old_v);
    return 1;
}

static int __cdecl midi_m_set_type_range(lua_State *L)
{
    int old_v;
    lua_getfield(L, 1, "type_range");
    old_v = (int)lrint(lua_tonumber(L, -1));
    lua_pop(L, 1);
    if (lua_type(L, 2) == LUA_TNUMBER)
    {
        int v = (int)lua_tointeger(L, 2);
        if ((uint32_t)(v - 1) > 2u)
        {
            v = 0;
        }
        lua_pushinteger(L, v);
    }
    else
    {
        lua_pushnumber(L, 0.0);
    }
    lua_setfield(L, 1, "type_range");
    lua_pushinteger(L, old_v);
    return 1;
}

static int __cdecl midi_m_note_num_to_key_name(lua_State *L)
{
    int n = (int)lrint(lua_tonumber(L, 2));
    char buf[16];
    if (!format_note_name(n, buf))
    {
        lua_pushnil(L);
        return 1;
    }
    lua_pushstring(L, buf);
    return 1;
}

static int __cdecl midi_m_key_name_to_note_num(lua_State *L)
{
    const uint8_t *s;
    uint32_t ch;
    int semi;
    int pos;
    uint32_t stop_idx;
    size_t slen;
    int sign_mul;
    int oct_base;

    if (lua_type(L, 2) != LUA_TSTRING)
    {
        lua_pushnil(L);
        return 1;
    }
    s = (const uint8_t *)lua_tolstring(L, 2, NULL);
    if (!s || s[0] == '\0')
    {
        lua_pushnil(L);
        return 1;
    }

    ch = s[0];
    if (ch > 0x47u)
    {
        ch -= 0x20u;
    }
    if (ch - 0x41u >= 7u)
    {
        lua_pushnil(L);
        return 1;
    }

    if ((int)ch < 0x43)
    {
        ch += 14u;
    }
    semi = (int)ch - 0x43;
    switch (semi)
    {
    case 1:
        semi = (int)ch - 0x42;
        break;
    case 2:
    case 3:
        semi = (int)ch - 0x41;
        break;
    case 4:
        semi = (int)ch - 0x40;
        break;
    case 5:
        semi = (int)ch - 0x3f;
        break;
    case 6:
        semi = (int)ch - 0x3e;
        break;
    default:
        break;
    }

    if (s[1] == '#')
    {
        semi++;
        pos = 2;
        stop_idx = 1u;
    }
    else
    {
        pos = 1;
        stop_idx = 0u;
    }

    if (s[pos] == '-')
    {
        stop_idx = (uint32_t)pos;
    }
    sign_mul = (s[pos] != '-') ? 1 : -1;
    slen = strlen((const char *)s);

    if ((int)stop_idx < (int)(slen - 1u))
    {
        const uint8_t *p = s + (slen - 1u);
        int oct = 0;
        int mul = sign_mul;
        do
        {
            oct += ((int)*p - '0') * mul;
            mul *= 10;
            p--;
        } while (p != s + stop_idx);
        oct_base = (oct + 1) * 12;
    }
    else
    {
        oct_base = 12;
    }

    if ((uint32_t)(semi + oct_base) < 128u)
    {
        lua_pushnumber(L, (double)(semi + oct_base));
        return 1;
    }
    lua_pushnil(L);
    return 1;
}

static int midi_get_info_elem(lua_State *L, int idx)
{
    lua_getfield(L, 1, "info");
    lua_rawgeti(L, -1, idx);
    return 1;
}

static int __cdecl midi_m_get_max_note_num(lua_State *L)
{
    return midi_get_info_elem(L, 1);
}

static int __cdecl midi_m_get_min_note_num(lua_State *L)
{
    return midi_get_info_elem(L, 2);
}

static int __cdecl midi_m_get_track_num(lua_State *L)
{
    return midi_get_info_elem(L, 3);
}

static int __cdecl midi_m_get_delta_time(lua_State *L)
{
    return midi_get_info_elem(L, 4);
}

static int __cdecl midi_m_get_midi_length(lua_State *L)
{
    return midi_get_info_elem(L, 5);
}

static int __cdecl midi_m_get_max_note_name(lua_State *L)
{
    int n;
    char buf[16];
    lua_getfield(L, 1, "info");
    lua_rawgeti(L, -1, 1);
    n = (int)lrint(lua_tonumber(L, -1));
    if (!format_note_name(n, buf))
    {
        lua_pushnil(L);
        return 1;
    }
    lua_pushstring(L, buf);
    return 1;
}

static int __cdecl midi_m_get_min_note_name(lua_State *L)
{
    int n;
    char buf[16];
    lua_getfield(L, 1, "info");
    lua_rawgeti(L, -1, 2);
    n = (int)lrint(lua_tonumber(L, -1));
    if (!format_note_name(n, buf))
    {
        lua_pushnil(L);
        return 1;
    }
    lua_pushstring(L, buf);
    return 1;
}

static int __cdecl midi_m_step_to_time(lua_State *L)
{
    int step = (int)lrint(lua_tonumber(L, 2));
    int delta;
    lua_getfield(L, 1, "info");
    lua_rawgeti(L, -1, 4);
    delta = (int)lrint(lua_tonumber(L, -1));
    lua_pop(L, 2);
    lua_pushnumber(L, midi_step_to_time(L, (double)step, delta));
    return 1;
}

static int __cdecl midi_m_time_to_step(lua_State *L)
{
    double t = lua_tonumber(L, 2);
    int delta;
    lua_getfield(L, 1, "info");
    lua_rawgeti(L, -1, 4);
    delta = (int)lrint(lua_tonumber(L, -1));
    lua_pop(L, 2);
    lua_pushnumber(L, (double)midi_time_to_step(L, t, delta));
    return 1;
}

static int __cdecl midi_m_step_to_bar(lua_State *L)
{
    int step = (int)lua_tointeger(L, 2);
    int delta;
    double bar;
    double beat;
    double prog;
    lua_getfield(L, 1, "info");
    lua_rawgeti(L, -1, 4);
    delta = (int)lrint(lua_tonumber(L, -1));
    lua_pop(L, 2);
    midi_step_to_bar_vals(L, (double)step, delta, &bar, &beat, &prog);
    lua_pushnumber(L, bar);
    lua_pushnumber(L, beat);
    lua_pushnumber(L, prog);
    return 3;
}

static int __cdecl midi_m_bar_to_step(lua_State *L)
{
    int delta;
    double bar = (double)(int)lrint(lua_tonumber(L, 2));
    double beat = (double)(int)lrint(lua_tonumber(L, 3));
    double prog = lua_tonumber(L, 4);
    if (prog < 0.0)
    {
        prog = 0.0;
    }
    else if (prog > 1.0)
    {
        prog = 1.0;
    }
    lua_getfield(L, 1, "info");
    lua_rawgeti(L, -1, 4);
    delta = (int)lrint(lua_tonumber(L, -1));
    lua_pop(L, 2);
    lua_pushnumber(L, midi_bar_to_step(L, bar, beat, prog, delta));
    return 1;
}

static int __cdecl midi_m_get_now_scale_key(lua_State *L)
{
    double now_time;
    int delta;
    int type_range;
    double down_r;
    double up_r;
    double cur_s;
    double low_s;
    double high_s;
    char buf[16];

    midi_read_now_state(L, &now_time, &delta, NULL, &type_range, &down_r, &up_r);
    midi_compute_step_window(L, now_time, delta, type_range, down_r, up_r, &cur_s, &low_s, &high_s);

    if (type_range == 0)
    {
        int idx = midi_find_scale_idx(L, cur_s);
        int sf;
        int mi;
        lua_getfield(L, 1, "scale");
        lua_rawgeti(L, -1, idx);
        lua_rawgeti(L, -1, 2);
        sf = (int)lua_tointeger(L, -1);
        lua_rawgeti(L, -2, 3);
        mi = (int)lua_tointeger(L, -1);
        format_scale_key(mi, sf, buf);
        lua_pushstring(L, buf);
        return 1;
    }
    else
    {
        int i_lo = midi_find_scale_idx(L, low_s);
        int i_hi = midi_find_scale_idx(L, high_s);
        int k;
        int out_i = 1;
        lua_getfield(L, 1, "scale");
        lua_newtable(L);
        for (k = i_lo; k <= i_hi; k++, out_i++)
        {
            int sf;
            int mi;
            lua_newtable(L);
            lua_rawgeti(L, -3, k);
            lua_rawgeti(L, -1, 1);
            lua_setfield(L, -3, "step");
            lua_rawgeti(L, -1, 2);
            sf = (int)lua_tointeger(L, -1);
            lua_rawgeti(L, -2, 3);
            mi = (int)lua_tointeger(L, -1);
            lua_pop(L, 3);
            format_scale_key(mi, sf, buf);
            lua_pushstring(L, buf);
            lua_setfield(L, -2, "key");
            lua_rawseti(L, -2, out_i);
        }
        return 1;
    }
}

static int __cdecl midi_m_get_now_scale_name(lua_State *L)
{
    double now_time;
    int delta;
    int type_range;
    double down_r;
    double up_r;
    double cur_s;
    double low_s;
    double high_s;
    char buf[32];

    midi_read_now_state(L, &now_time, &delta, NULL, &type_range, &down_r, &up_r);
    midi_compute_step_window(L, now_time, delta, type_range, down_r, up_r, &cur_s, &low_s, &high_s);

    if (type_range == 0)
    {
        int idx = midi_find_scale_idx(L, cur_s);
        int sf;
        int mi;
        lua_getfield(L, 1, "scale");
        lua_rawgeti(L, -1, idx);
        lua_rawgeti(L, -1, 2);
        sf = (int)lua_tointeger(L, -1);
        lua_rawgeti(L, -2, 3);
        mi = (int)lua_tointeger(L, -1);
        format_scale_name(mi, sf, buf);
        lua_pushstring(L, buf);
        return 1;
    }
    else
    {
        int i_lo = midi_find_scale_idx(L, low_s);
        int i_hi = midi_find_scale_idx(L, high_s);
        int k;
        int out_i = 1;
        lua_getfield(L, 1, "scale");
        lua_newtable(L);
        for (k = i_lo; k <= i_hi; k++, out_i++)
        {
            int sf;
            int mi;
            lua_newtable(L);
            lua_rawgeti(L, -3, k);
            lua_rawgeti(L, -1, 1);
            lua_setfield(L, -3, "step");
            lua_rawgeti(L, -1, 2);
            sf = (int)lua_tointeger(L, -1);
            lua_rawgeti(L, -2, 3);
            mi = (int)lua_tointeger(L, -1);
            lua_pop(L, 3);
            format_scale_name(mi, sf, buf);
            lua_pushstring(L, buf);
            lua_setfield(L, -2, "name");
            lua_rawseti(L, -2, out_i);
        }
        return 1;
    }
}

static int __cdecl midi_m_get_now_step_num(lua_State *L)
{
    double now_time;
    int delta;
    int type_range;
    double down_r;
    double up_r;
    double cur_s;
    double low_s;
    double high_s;

    midi_read_now_state(L, &now_time, &delta, NULL, &type_range, &down_r, &up_r);
    midi_compute_step_window(L, now_time, delta, type_range, down_r, up_r, &cur_s, &low_s, &high_s);

    if (type_range == 0)
    {
        lua_pushnumber(L, cur_s);
        return 1;
    }

    lua_newtable(L);
    lua_pushnumber(L, low_s);
    lua_setfield(L, -2, "low_step");
    lua_pushnumber(L, high_s);
    lua_setfield(L, -2, "high_step");
    lua_pushnumber(L, cur_s);
    lua_setfield(L, -2, "step");
    return 1;
}

static int __cdecl midi_m_get_now_bar(lua_State *L)
{
    double now_time;
    int delta;
    int type_range;
    double down_r;
    double up_r;
    int cur_s;
    double lo_bar = 1.0;
    double lo_beat = 1.0;
    double lo_prog = 0.0;
    double hi_bar = 1.0;
    double hi_beat = 1.0;
    double hi_prog = 0.0;

    midi_read_now_state(L, &now_time, &delta, NULL, &type_range, &down_r, &up_r);
    cur_s = midi_time_to_step(L, now_time, delta);

    if (type_range == 0)
    {
        midi_step_to_bar_vals(L, (double)cur_s, delta, &lo_bar, &lo_beat, &lo_prog);
        lua_pushnumber(L, lo_bar);
        lua_pushnumber(L, lo_beat);
        lua_pushnumber(L, lo_prog);
        return 3;
    }

    if (type_range == 1)
    {
        double ls = (double)midi_time_to_step(L, now_time - down_r, delta);
        double hs = (double)midi_time_to_step(L, now_time + up_r, delta);
        midi_step_to_bar_vals(L, ls, delta, &lo_bar, &lo_beat, &lo_prog);
        midi_step_to_bar_vals(L, hs, delta, &hi_bar, &hi_beat, &hi_prog);
    }
    else if (type_range == 2)
    {
        midi_step_to_bar_vals(L, (double)cur_s - down_r, delta, &lo_bar, &lo_beat, &lo_prog);
        midi_step_to_bar_vals(L, (double)cur_s + up_r, delta, &hi_bar, &hi_beat, &hi_prog);
    }
    else if (type_range == 3)
    {
        double bar;
        double beat;
        double prog;
        midi_step_to_bar_vals(L, (double)cur_s, delta, &bar, &beat, &prog);
        lo_bar = bar - down_r;
        lo_beat = beat;
        lo_prog = prog;
        hi_bar = bar + up_r;
        hi_beat = beat;
        hi_prog = prog;
    }

    lua_newtable(L);
    lua_newtable(L);
    lua_newtable(L);
    lua_pushnumber(L, lo_bar);
    lua_setfield(L, -4, "low_bar");
    lua_pushnumber(L, lo_beat);
    lua_setfield(L, -3, "low_beat");
    lua_pushnumber(L, lo_prog);
    lua_setfield(L, -2, "low_prog");
    lua_pushnumber(L, hi_bar);
    lua_setfield(L, -4, "high_bar");
    lua_pushnumber(L, hi_beat);
    lua_setfield(L, -3, "high_beat");
    lua_pushnumber(L, hi_prog);
    lua_setfield(L, -2, "high_prog");
    return 3;
}

static int __cdecl midi_m_get_now_note(lua_State *L)
{
    double now_time;
    int delta;
    int tracks;
    int type_range;
    double down_r;
    double up_r;
    double cur_s;
    double low_s;
    double high_s;
    int t;

    midi_read_now_state(L, &now_time, &delta, &tracks, &type_range, &down_r, &up_r);
    midi_compute_step_window(L, now_time, delta, type_range, down_r, up_r, &cur_s, &low_s, &high_s);

    lua_getfield(L, 1, "event");
    lua_newtable(L);

    for (t = 1; t <= tracks; t++)
    {
        int nlen;
        int i;
        int out_i = 0;

        lua_rawgeti(L, -2, t);
        lua_getfield(L, -1, "note");
        nlen = (int)lua_objlen(L, -1);
        lua_newtable(L);

        for (i = 1; i <= nlen; i++)
        {
            double s_start;
            double s_end;
            double c;

            lua_rawgeti(L, -2, i);
            lua_rawgeti(L, -1, 2);
            s_start = lua_tonumber(L, -1);
            lua_pop(L, 1);

            if (s_start > high_s)
            {
                lua_pop(L, 1);
                break;
            }

            c = (s_start < low_s) ? low_s : s_start;
            lua_rawgeti(L, -1, 3);
            s_end = lua_tonumber(L, -1);
            lua_pop(L, 1);

            if (c < s_end && low_s <= c && c <= high_s)
            {
                out_i++;
                lua_newtable(L);
                lua_rawgeti(L, -2, 1);
                lua_rawseti(L, -2, 1);
                lua_rawgeti(L, -2, 2);
                lua_rawseti(L, -2, 2);
                lua_rawgeti(L, -2, 3);
                lua_rawseti(L, -2, 3);
                lua_rawgeti(L, -2, 4);
                lua_rawseti(L, -2, 4);
                lua_rawseti(L, -3, out_i);
            }
            lua_pop(L, 1);
        }

        lua_rawseti(L, -4, t);
        lua_pop(L, 2);
    }

    return 1;
}

static int __cdecl midi_m_get_now_expression(lua_State *L)
{
    double now_time;
    int delta;
    int tracks;
    int type_range;
    double down_r;
    double up_r;
    double cur_s;
    double low_s;
    double high_s;
    int t;

    midi_read_now_state(L, &now_time, &delta, &tracks, &type_range, &down_r, &up_r);
    midi_compute_step_window(L, now_time, delta, type_range, down_r, up_r, &cur_s, &low_s, &high_s);

    lua_getfield(L, 1, "event");
    lua_newtable(L);

    for (t = 1; t <= tracks; t++)
    {
        int elen;
        int out_i = 1;

        lua_rawgeti(L, -2, t);
        lua_getfield(L, -1, "expression");
        elen = (int)lua_objlen(L, -1);
        lua_newtable(L);

        if (elen > 0)
        {
            double s0;
            double v0;
            int k;

            lua_rawgeti(L, -2, 1);
            lua_rawgeti(L, -1, 1);
            s0 = lua_tonumber(L, -1);
            lua_rawgeti(L, -2, 2);
            v0 = lua_tonumber(L, -1);
            lua_pop(L, 3);

            if (elen > 1 && s0 <= high_s)
            {
                for (k = 2; k <= elen; k++)
                {
                    double s1;
                    double v1;
                    double c = (s0 < low_s) ? low_s : s0;

                    lua_rawgeti(L, -2, k);
                    lua_rawgeti(L, -1, 1);
                    s1 = lua_tonumber(L, -1);
                    lua_rawgeti(L, -2, 2);
                    v1 = lua_tonumber(L, -1);
                    lua_pop(L, 3);

                    if (c < s1 && low_s <= c && c <= high_s)
                    {
                        lua_newtable(L);
                        lua_pushnumber(L, s0);
                        lua_rawseti(L, -2, 1);
                        lua_pushnumber(L, v0);
                        lua_rawseti(L, -2, 2);
                        lua_rawseti(L, -2, out_i);
                        out_i++;
                    }

                    s0 = s1;
                    v0 = v1;
                    if (s0 > high_s)
                    {
                        break;
                    }
                }
            }

            if (s0 < high_s)
            {
                lua_newtable(L);
                lua_pushnumber(L, s0);
                lua_rawseti(L, -2, 1);
                lua_pushnumber(L, v0);
                lua_rawseti(L, -2, 2);
                lua_rawseti(L, -2, out_i);
            }
        }

        lua_rawseti(L, -4, t);
        lua_pop(L, 2);
    }

    return 1;
}

static int midi_find_bpm_time_idx(lua_State *L, int bpm_tbl_idx, int len, double t, int start_k)
{
    int k;
    if (t < 0.0 || len <= 1)
    {
        return 1;
    }
    for (k = start_k; k < len; k++)
    {
        double t0;
        double t1;
        lua_rawgeti(L, bpm_tbl_idx, k);
        lua_rawgeti(L, -1, 3);
        t0 = lua_tonumber(L, -1);
        lua_rawgeti(L, -2, 4);
        t1 = lua_tonumber(L, -1);
        lua_pop(L, 3);
        if (t0 <= t && t < t1)
        {
            return k;
        }
    }
    return len;
}

static int midi_find_bpm_step_idx(lua_State *L, int bpm_tbl_idx, int len, double step)
{
    int k;
    if (len <= 1)
    {
        return 1;
    }
    for (k = len; k >= 2; k--)
    {
        double s;
        lua_rawgeti(L, bpm_tbl_idx, k);
        lua_rawgeti(L, -1, 1);
        s = lua_tonumber(L, -1);
        lua_pop(L, 2);
        if (s <= step)
        {
            return k;
        }
    }
    return 1;
}

static int __cdecl midi_m_get_now_bpm(lua_State *L)
{
    double now_time;
    int delta;
    int type_range;
    double down_r;
    double up_r;
    int bpm_idx;
    int len;
    int i_lo;
    int i_hi;
    int k;
    int out_i = 1;

    midi_read_now_state(L, &now_time, &delta, NULL, &type_range, &down_r, &up_r);

    lua_getfield(L, 1, "bpm");
    bpm_idx = lua_gettop(L);
    len = (int)lua_objlen(L, bpm_idx);

    if (type_range == 0)
    {
        int idx = midi_find_bpm_time_idx(L, bpm_idx, len, now_time, 1);
        lua_rawgeti(L, bpm_idx, idx);
        lua_newtable(L);
        lua_rawgeti(L, -2, 2);
        lua_setfield(L, -2, "tempo");
        lua_rawgeti(L, -2, 5);
        lua_setfield(L, -2, "bpm");
        return 1;
    }

    if (type_range == 1)
    {
        i_lo = midi_find_bpm_time_idx(L, bpm_idx, len, now_time - down_r, 1);
        i_hi = midi_find_bpm_time_idx(L, bpm_idx, len, now_time + up_r, i_lo);
    }
    else
    {
        double cur_s;
        double low_s;
        double high_s;
        midi_compute_step_window(L, now_time, delta, type_range, down_r, up_r, &cur_s, &low_s, &high_s);
        i_lo = midi_find_bpm_step_idx(L, bpm_idx, len, low_s);
        i_hi = midi_find_bpm_step_idx(L, bpm_idx, len, high_s);
    }

    lua_newtable(L);
    for (k = i_lo; k <= i_hi; k++, out_i++)
    {
        lua_rawgeti(L, bpm_idx, k);
        lua_newtable(L);
        lua_rawgeti(L, -2, 1);
        lua_setfield(L, -2, "step");
        lua_rawgeti(L, -2, 2);
        lua_setfield(L, -2, "tempo");
        lua_rawgeti(L, -2, 5);
        lua_setfield(L, -2, "bpm");
        lua_rawseti(L, -3, out_i);
        lua_pop(L, 1);
    }
    return 1;
}

int __cdecl lua_rikky_midiconverterEx(lua_State *L)
{
    static const luaL_Reg methods[] =
    {
        { "SetOffsetTime", midi_m_set_offset_time },
        { "NoteNumber2KeyName", midi_m_note_num_to_key_name },
        { "KeyName2NoteNumber", midi_m_key_name_to_note_num },
        { "GetMaxNoteNumber", midi_m_get_max_note_num },
        { "GetMaxNoteName", midi_m_get_max_note_name },
        { "GetMinNoteNumber", midi_m_get_min_note_num },
        { "GetMinNoteName", midi_m_get_min_note_name },
        { "GetTrackNum", midi_m_get_track_num },
        { "GetDeltaTime", midi_m_get_delta_time },
        { "GetMidiLength", midi_m_get_midi_length },
        { "GetNowScaleKey", midi_m_get_now_scale_key },
        { "GetNowScaleName", midi_m_get_now_scale_name },
        { "GetNowStepNum", midi_m_get_now_step_num },
        { "GetNowBar", midi_m_get_now_bar },
        { "GetNowNote", midi_m_get_now_note },
        { "GetNowExpression", midi_m_get_now_expression },
        { "GetNowBPM", midi_m_get_now_bpm },
        { "Step2Time", midi_m_step_to_time },
        { "Time2Step", midi_m_time_to_step },
        { "Step2Bar", midi_m_step_to_bar },
        { "Bar2Step", midi_m_bar_to_step },
        { "SetUprange", midi_m_set_up_range },
        { "SetDownrange", midi_m_set_down_range },
        { "SetTyperange", midi_m_set_type_range },
        { NULL, NULL }
    };
    const char *mid_path;
    char *dat_path;
    FILE *fp;
    int delta;
    int bpm_len;
    int rhy_len;
    int scl_len;
    int k;
    char bpm_buf[32];

    if (lua_gettop(L) <= 1 || lua_type(L, 1) != LUA_TTABLE || lua_type(L, 2) != LUA_TSTRING)
    {
        lua_pushboolean(L, 0);
        return 1;
    }

    lua_settop(L, 2);
    mid_path = lua_tolstring(L, 2, NULL);
    if (!mid_path || mid_path[0] == '\0')
    {
        lua_pushboolean(L, 0);
        return 1;
    }

    dat_path = build_dat_path(mid_path);
    if (!dat_path)
    {
        lua_pushboolean(L, 0);
        return 1;
    }

    fp = fopen(dat_path, "r");
    if (fp)
    {
        fclose(fp);
    }
    else if (!smf_convert_to_dat(mid_path, dat_path))
    {
        rm_free(dat_path);
        lua_pushboolean(L, 0);
        return 1;
    }

    if (luaL_loadfile(L, dat_path) != 0)
    {
        rm_free(dat_path);
        lua_pop(L, 1);
        lua_pushboolean(L, 0);
        return 1;
    }
    rm_free(dat_path);

    lua_call(L, 0, 1);

    lua_pushnumber(L, 0.0);
    lua_setfield(L, 1, "offsettime");
    lua_pushnumber(L, 0.0);
    lua_setfield(L, 1, "up_range");
    lua_pushnumber(L, 0.0);
    lua_setfield(L, 1, "down_range");
    lua_pushnumber(L, 0.0);
    lua_setfield(L, 1, "type_range");

    lua_getfield(L, 3, "info");
    lua_rawgeti(L, -1, 4);
    delta = (int)lua_tointeger(L, -1);
    lua_pop(L, 1);
    lua_setfield(L, 1, "info");

    // build self.bpm at stack index 5 from dat.bpm at stack index 4
    lua_getfield(L, 3, "bpm");
    bpm_len = (int)lua_objlen(L, -1);
    lua_newtable(L);
    if (bpm_len > 0)
    {
        double cur_step;
        double cur_tempo;
        double cur_time = 0.0;

        lua_rawgeti(L, 4, 1);
        lua_rawgeti(L, -1, 1);
        cur_step = lua_tonumber(L, -1);
        lua_rawgeti(L, -2, 2);
        cur_tempo = lua_tonumber(L, -1);
        lua_pop(L, 3);

        for (k = 1; k < bpm_len; k++)
        {
            double next_step;
            double next_tempo;
            double end_time;

            lua_rawgeti(L, 4, k + 1);
            lua_rawgeti(L, -1, 1);
            next_step = lua_tonumber(L, -1);
            lua_rawgeti(L, -2, 2);
            next_tempo = lua_tonumber(L, -1);
            lua_pop(L, 3);

            end_time = (((next_step - cur_step) / (double)delta) * cur_tempo) / 1000000.0 + cur_time;

            lua_newtable(L);
            lua_pushnumber(L, cur_step);
            lua_rawseti(L, -2, 1);
            lua_pushnumber(L, cur_tempo);
            lua_rawseti(L, -2, 2);
            lua_pushnumber(L, cur_time);
            lua_rawseti(L, -2, 3);
            lua_pushnumber(L, end_time);
            lua_rawseti(L, -2, 4);
            sprintf(bpm_buf, "%0.2f", 60000000.0 / cur_tempo);
            lua_pushstring(L, bpm_buf);
            lua_rawseti(L, -2, 5);
            lua_rawseti(L, 5, k);

            cur_step = next_step;
            cur_tempo = next_tempo;
            cur_time = end_time;
        }

        lua_newtable(L);
        lua_pushnumber(L, cur_step);
        lua_rawseti(L, -2, 1);
        lua_pushnumber(L, cur_tempo);
        lua_rawseti(L, -2, 2);
        lua_pushnumber(L, cur_time);
        lua_rawseti(L, -2, 3);
        lua_pushnil(L);
        lua_rawseti(L, -2, 4);
        sprintf(bpm_buf, "%0.2f", 60000000.0 / cur_tempo);
        lua_pushstring(L, bpm_buf);
        lua_rawseti(L, -2, 5);
        lua_rawseti(L, 5, bpm_len);
    }
    lua_setfield(L, 1, "bpm");
    lua_pop(L, 1);

    // build self.rhythm at stack index 5 from dat.rhythm at stack index 4
    lua_getfield(L, 3, "rhythm");
    rhy_len = (int)lua_objlen(L, -1);
    lua_newtable(L);
    if (rhy_len > 0)
    {
        double cur_step;
        double cur_num;
        double cur_den;

        lua_rawgeti(L, 4, 1);
        lua_rawgeti(L, -1, 1);
        cur_step = lua_tonumber(L, -1);
        lua_rawgeti(L, -2, 2);
        cur_num = lua_tonumber(L, -1);
        lua_rawgeti(L, -3, 3);
        cur_den = lua_tonumber(L, -1);
        lua_pop(L, 4);

        for (k = 1; k < rhy_len; k++)
        {
            double next_raw_step;
            double next_num;
            double next_den;
            double bar_len = (4.0 / cur_den) * (double)delta * cur_num;
            double bar_cnt;
            double next_aligned;

            lua_rawgeti(L, 4, k + 1);
            lua_rawgeti(L, -1, 1);
            next_raw_step = lua_tonumber(L, -1);
            lua_rawgeti(L, -2, 2);
            next_num = lua_tonumber(L, -1);
            lua_rawgeti(L, -3, 3);
            next_den = lua_tonumber(L, -1);
            lua_pop(L, 4);

            bar_cnt = floor((next_raw_step - cur_step) / bar_len);
            next_aligned = cur_step + bar_cnt * bar_len;

            lua_newtable(L);
            lua_pushnumber(L, cur_step);
            lua_rawseti(L, -2, 1);
            lua_pushnumber(L, cur_num);
            lua_rawseti(L, -2, 2);
            lua_pushnumber(L, cur_den);
            lua_rawseti(L, -2, 3);
            lua_pushnumber(L, bar_len);
            lua_rawseti(L, -2, 4);
            lua_pushnumber(L, bar_cnt);
            lua_rawseti(L, -2, 5);
            lua_rawseti(L, 5, k);

            cur_step = next_aligned;
            cur_num = next_num;
            cur_den = next_den;
        }

        lua_newtable(L);
        lua_pushnumber(L, cur_step);
        lua_rawseti(L, -2, 1);
        lua_pushnumber(L, cur_num);
        lua_rawseti(L, -2, 2);
        lua_pushnumber(L, cur_den);
        lua_rawseti(L, -2, 3);
        lua_pushnumber(L, (4.0 / cur_den) * (double)delta * cur_num);
        lua_rawseti(L, -2, 4);
        lua_pushnil(L);
        lua_rawseti(L, -2, 5);
        lua_rawseti(L, 5, rhy_len);
    }

    // build self.scale at stack index 7 using built rhythm at index 5 and dat.scale at index 6
    lua_getfield(L, 3, "scale");
    scl_len = (int)lua_objlen(L, -1);
    lua_newtable(L);
    if (scl_len > 0 && rhy_len > 0)
    {
        int rhy_cursor = 1;
        for (k = 1; k <= scl_len; k++)
        {
            double raw_step;
            double r_start;
            double r_bar_len;
            double bar_idx;

            lua_rawgeti(L, 6, k);
            lua_rawgeti(L, -1, 1);
            raw_step = lua_tonumber(L, -1);
            rhy_cursor = midi_find_rhythm_at_step(L, 5, raw_step, rhy_cursor);
            lua_rawgeti(L, -1, 1);
            r_start = lua_tonumber(L, -1);
            lua_rawgeti(L, -2, 4);
            r_bar_len = lua_tonumber(L, -1);

            bar_idx = floor((raw_step - r_start) / r_bar_len);

            lua_newtable(L);
            lua_pushnumber(L, r_start + bar_idx * r_bar_len);
            lua_rawseti(L, -2, 1);
            lua_rawgeti(L, -6, 2);
            lua_rawseti(L, -2, 2);
            lua_rawgeti(L, -6, 3);
            lua_rawseti(L, -2, 3);
            lua_rawseti(L, 7, k);

            lua_settop(L, 7);
        }
    }
    lua_setfield(L, 1, "scale");
    lua_pop(L, 1);

    lua_setfield(L, 1, "rhythm");
    lua_pop(L, 1);

    lua_getfield(L, 3, "event");
    lua_setfield(L, 1, "event");

    for (k = 0; methods[k].name != NULL; k++)
    {
        lua_pushcclosure(L, methods[k].func, 0);
        lua_setfield(L, 1, methods[k].name);
    }

    lua_pushboolean(L, 1);
    return 1;
}
