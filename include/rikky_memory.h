#pragma once
#ifndef RIKKY_MEMORY_H
#define RIKKY_MEMORY_H

#include <stdint.h>
#include "aviutl_sdk.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (__cdecl *RikkyExitCallback)(void *ctx2);
typedef void (__cdecl *RikkyLoadCallback)(void *data, uint32_t size, void *ctx3);
typedef void (__cdecl *RikkyResetCallback)(void *ctx3);
typedef int (__cdecl *RikkySaveCallback)(void *buf, int *inout_size, void *ctx3);

int __cdecl CloseAviHandle(AVI_FILE_HANDLE afh);
void *__cdecl FileArea(const uint8_t *bgra, int w, int h, int seed_x, int seed_y,
                       int mode, uint32_t threshold, void **out_coords, int *out_bbox4);
AVI_FILE_HANDLE __cdecl GetAviHandle(const char *path, FILE_INFO *fip);
const char *__cdecl GetObjectName(int effect_idx, uint32_t *out_flag, int *out_canon_idx);
uint32_t __cdecl GetObjectProcessIndex(int return_index_mode);
void __cdecl HSV2RGB(double h, double s, double v, int *r, int *g, int *b);
void __cdecl RGB2HSV(int r, int g, int b, double *h, double *s, double *v);
int __cdecl RikkyExitRegister(RikkyExitCallback cb, int user_data);
void *__cdecl RikkyGetMemoryData(int id);
int __cdecl RikkyLoadRegister(const char *key, RikkyLoadCallback load_cb, uint32_t load_user,
                              RikkyResetCallback reset_cb, uint32_t reset_user);
int __cdecl RikkySaveRegister(const char *key, RikkySaveCallback save_cb, uint32_t save_user);
int __cdecl RikkyBackupRegister_impl(FILTER *fp);

#ifdef __cplusplus
}
#endif

#endif // RIKKY_MEMORY_H

