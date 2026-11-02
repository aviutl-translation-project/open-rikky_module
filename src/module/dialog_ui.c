#define COBJMACROS
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "rikky_module_internal.h"

typedef struct
{
    char **items;
    int count;
} RmListSlot;

typedef struct
{
    HWND edit_hwnd;
    RmListSlot *slot;
} RmListDlgCtx;

typedef struct
{
    HWND edit_hwnd;
    const uint32_t *custom_palette;
} RmColorDlgCtx;

// embedded binary dialog templates from orig .rsrc (100, 200, 300, 400)
static const __declspec(align(4)) uint8_t k_dlg_template_100[206] =
{
    0xc0, 0x00, 0xc8, 0x80, 0x00, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x78, 0x00,
    0x3e, 0x00, 0x00, 0x00, 0x00, 0x00, 0xd5, 0x30, 0xa9, 0x30, 0xf3, 0x30, 0xc8, 0x30, 0x78, 0x90,
    0x9e, 0x62, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x50, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x33, 0x00, 0x26, 0x00, 0x0a, 0x00, 0x01, 0x00, 0xff, 0xff, 0x80, 0x00, 0x4f, 0x00,
    0x4b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x50, 0x00, 0x00, 0x00, 0x00,
    0x27, 0x00, 0x33, 0x00, 0x26, 0x00, 0x0a, 0x00, 0x02, 0x00, 0xff, 0xff, 0x80, 0x00, 0x43, 0x00,
    0x41, 0x00, 0x4e, 0x00, 0x43, 0x00, 0x45, 0x00, 0x4c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x03, 0x00, 0x01, 0x50, 0x00, 0x00, 0x00, 0x00, 0x4d, 0x00, 0x33, 0x00, 0x26, 0x00, 0x0a, 0x00,
    0x2d, 0x01, 0xff, 0xff, 0x80, 0x00, 0x26, 0x7e, 0xf8, 0x66, 0x4d, 0x30, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x50, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x29, 0x00, 0x76, 0x00, 0x0a, 0x00,
    0x2e, 0x01, 0xff, 0xff, 0x82, 0x00, 0x42, 0x30, 0x20, 0x00, 0x41, 0x00, 0x20, 0x00, 0x31, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0xa0, 0x50, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x78, 0x00, 0x28, 0x00, 0x2c, 0x01, 0xff, 0xff, 0x83, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static const __declspec(align(4)) uint8_t k_dlg_template_200[138] =
{
    0xc0, 0x00, 0xc8, 0x80, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x78, 0x00,
    0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0xea, 0x30, 0xb9, 0x30, 0xc8, 0x30, 0x78, 0x90, 0x9e, 0x62,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x50, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x0a, 0x00, 0x3b, 0x00, 0x0a, 0x00, 0x01, 0x00, 0xff, 0xff, 0x80, 0x00, 0x4f, 0x00,
    0x4b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x50, 0x00, 0x00, 0x00, 0x00,
    0x3c, 0x00, 0x0a, 0x00, 0x3b, 0x00, 0x0a, 0x00, 0x02, 0x00, 0xff, 0xff, 0x80, 0x00, 0x43, 0x00,
    0x41, 0x00, 0x4e, 0x00, 0x43, 0x00, 0x45, 0x00, 0x4c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x43, 0x00, 0x00, 0x50, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x78, 0x00, 0x32, 0x00,
    0x2c, 0x01, 0xff, 0xff, 0x85, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static const __declspec(align(4)) uint8_t k_dlg_template_300[118] =
{
    0xc0, 0x00, 0xc8, 0x80, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0xaa, 0x00,
    0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 0xab, 0x30, 0xb9, 0x30, 0xbf, 0x30, 0xe0, 0x30, 0xd1, 0x30,
    0xe9, 0x30, 0xe1, 0x30, 0xfc, 0x30, 0xbf, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x01, 0x50, 0x00, 0x00, 0x00, 0x00, 0x2c, 0x00, 0x0a, 0x00, 0x28, 0x00, 0x0a, 0x00,
    0x01, 0x00, 0xff, 0xff, 0x80, 0x00, 0x4f, 0x00, 0x4b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x01, 0x50, 0x00, 0x00, 0x00, 0x00, 0x56, 0x00, 0x0a, 0x00, 0x28, 0x00, 0x0a, 0x00,
    0x02, 0x00, 0xff, 0xff, 0x80, 0x00, 0x43, 0x00, 0x41, 0x00, 0x4e, 0x00, 0x43, 0x00, 0x45, 0x00,
    0x4c, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static const __declspec(align(4)) uint8_t k_dlg_template_400[1218] =
{
    0xc0, 0x00, 0xc8, 0x80, 0x00, 0x00, 0x00, 0x00, 0x22, 0x00, 0x00, 0x00, 0x00, 0x00, 0xd3, 0x00,
    0x86, 0x00, 0x00, 0x00, 0x00, 0x00, 0x72, 0x82, 0xc0, 0x30, 0xa4, 0x30, 0xa2, 0x30, 0xed, 0x30,
    0xb0, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x43, 0x00, 0x20, 0x50, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0xd3, 0x00, 0x32, 0x00, 0x90, 0x01, 0xff, 0xff, 0x85, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x50, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x78, 0x00,
    0x28, 0x00, 0x0a, 0x00, 0x01, 0x00, 0xff, 0xff, 0x80, 0x00, 0x4f, 0x00, 0x4b, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x50, 0x00, 0x00, 0x00, 0x00, 0x2c, 0x00, 0x78, 0x00,
    0x28, 0x00, 0x0a, 0x00, 0x02, 0x00, 0xff, 0xff, 0x80, 0x00, 0x43, 0x00, 0x41, 0x00, 0x4e, 0x00,
    0x43, 0x00, 0x45, 0x00, 0x4c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x50,
    0x00, 0x00, 0x00, 0x00, 0x56, 0x00, 0x78, 0x00, 0x28, 0x00, 0x0a, 0x00, 0x91, 0x01, 0xff, 0xff,
    0x80, 0x00, 0xdc, 0x88, 0x72, 0x82, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x50,
    0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x78, 0x00, 0x28, 0x00, 0x0a, 0x00, 0x92, 0x01, 0xff, 0xff,
    0x80, 0x00, 0xcd, 0x53, 0xfe, 0x5b, 0x72, 0x82, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x50,
    0x00, 0x00, 0x00, 0x00, 0xaa, 0x00, 0x78, 0x00, 0x28, 0x00, 0x0a, 0x00, 0x93, 0x01, 0xff, 0xff,
    0x80, 0x00, 0xb9, 0x30, 0xdd, 0x30, 0xa4, 0x30, 0xc8, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x02, 0x50, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x0c, 0x00, 0x09, 0x00, 0x0a, 0x00,
    0x9a, 0x01, 0xff, 0xff, 0x82, 0x00, 0x52, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x50,
    0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x16, 0x00, 0x09, 0x00, 0x0a, 0x00, 0x9b, 0x01, 0xff, 0xff,
    0x82, 0x00, 0x47, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x50, 0x00, 0x00, 0x00, 0x00,
    0x03, 0x00, 0x20, 0x00, 0x09, 0x00, 0x0a, 0x00, 0x9c, 0x01, 0xff, 0xff, 0x82, 0x00, 0x42, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x50, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x2a, 0x00,
    0x09, 0x00, 0x0a, 0x00, 0x9d, 0x01, 0xff, 0xff, 0x82, 0x00, 0x48, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x02, 0x50, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x34, 0x00, 0x09, 0x00, 0x0a, 0x00,
    0x9e, 0x01, 0xff, 0xff, 0x82, 0x00, 0x53, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x50,
    0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x3e, 0x00, 0x09, 0x00, 0x0a, 0x00, 0x9f, 0x01, 0xff, 0xff,
    0x82, 0x00, 0x56, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x50, 0x00, 0x00, 0x00, 0x00,
    0x03, 0x00, 0x48, 0x00, 0x09, 0x00, 0x0a, 0x00, 0xa0, 0x01, 0xff, 0xff, 0x82, 0x00, 0x23, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x02, 0x20, 0x83, 0x50, 0x00, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x0a, 0x00,
    0x1f, 0x00, 0x0a, 0x00, 0xa4, 0x01, 0xff, 0xff, 0x81, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x02, 0x20, 0x81, 0x50, 0x00, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x14, 0x00, 0x1f, 0x00, 0x0a, 0x00,
    0xa5, 0x01, 0xff, 0xff, 0x81, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x20, 0x81, 0x50,
    0x00, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x1e, 0x00, 0x1f, 0x00, 0x0a, 0x00, 0xa6, 0x01, 0xff, 0xff,
    0x81, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x20, 0x81, 0x50, 0x00, 0x00, 0x00, 0x00,
    0x0b, 0x00, 0x28, 0x00, 0x1f, 0x00, 0x0a, 0x00, 0xa7, 0x01, 0xff, 0xff, 0x81, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x02, 0x20, 0x81, 0x50, 0x00, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x32, 0x00,
    0x1f, 0x00, 0x0a, 0x00, 0xa8, 0x01, 0xff, 0xff, 0x81, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x02, 0x20, 0x81, 0x50, 0x00, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x3c, 0x00, 0x1f, 0x00, 0x0a, 0x00,
    0xa9, 0x01, 0xff, 0xff, 0x81, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x81, 0x50,
    0x00, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x46, 0x00, 0x1f, 0x00, 0x0a, 0x00, 0xaa, 0x01, 0xff, 0xff,
    0x81, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x03, 0x50, 0x00, 0x00, 0x00, 0x00,
    0x33, 0x00, 0x0a, 0x00, 0x07, 0x00, 0x0a, 0x00, 0xae, 0x01, 0xff, 0xff, 0x80, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x01, 0x50, 0x00, 0x00, 0x00, 0x00, 0x33, 0x00, 0x14, 0x00,
    0x07, 0x00, 0x0a, 0x00, 0xaf, 0x01, 0xff, 0xff, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x04, 0x00, 0x01, 0x50, 0x00, 0x00, 0x00, 0x00, 0x33, 0x00, 0x1e, 0x00, 0x07, 0x00, 0x0a, 0x00,
    0xb0, 0x01, 0xff, 0xff, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x01, 0x50,
    0x00, 0x00, 0x00, 0x00, 0x33, 0x00, 0x28, 0x00, 0x07, 0x00, 0x0a, 0x00, 0xb1, 0x01, 0xff, 0xff,
    0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x01, 0x50, 0x00, 0x00, 0x00, 0x00,
    0x33, 0x00, 0x32, 0x00, 0x07, 0x00, 0x0a, 0x00, 0xb2, 0x01, 0xff, 0xff, 0x80, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x01, 0x50, 0x00, 0x00, 0x00, 0x00, 0x33, 0x00, 0x3c, 0x00,
    0x07, 0x00, 0x0a, 0x00, 0xb3, 0x01, 0xff, 0xff, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x04, 0x00, 0x01, 0x50, 0x00, 0x00, 0x00, 0x00, 0x33, 0x00, 0x46, 0x00, 0x07, 0x00, 0x0a, 0x00,
    0xb4, 0x01, 0xff, 0xff, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x50,
    0x00, 0x00, 0x00, 0x00, 0x29, 0x00, 0x0a, 0x00, 0x0a, 0x00, 0x09, 0x00, 0xb8, 0x01, 0x4d, 0x00,
    0x53, 0x00, 0x43, 0x00, 0x54, 0x00, 0x4c, 0x00, 0x53, 0x00, 0x5f, 0x00, 0x55, 0x00, 0x50, 0x00,
    0x44, 0x00, 0x4f, 0x00, 0x57, 0x00, 0x4e, 0x00, 0x33, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x50, 0x00, 0x00, 0x00, 0x00, 0x29, 0x00, 0x14, 0x00,
    0x0a, 0x00, 0x09, 0x00, 0xb9, 0x01, 0x4d, 0x00, 0x53, 0x00, 0x43, 0x00, 0x54, 0x00, 0x4c, 0x00,
    0x53, 0x00, 0x5f, 0x00, 0x55, 0x00, 0x50, 0x00, 0x44, 0x00, 0x4f, 0x00, 0x57, 0x00, 0x4e, 0x00,
    0x33, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x50,
    0x00, 0x00, 0x00, 0x00, 0x29, 0x00, 0x1e, 0x00, 0x0a, 0x00, 0x09, 0x00, 0xba, 0x01, 0x4d, 0x00,
    0x53, 0x00, 0x43, 0x00, 0x54, 0x00, 0x4c, 0x00, 0x53, 0x00, 0x5f, 0x00, 0x55, 0x00, 0x50, 0x00,
    0x44, 0x00, 0x4f, 0x00, 0x57, 0x00, 0x4e, 0x00, 0x33, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x50, 0x00, 0x00, 0x00, 0x00, 0x29, 0x00, 0x28, 0x00,
    0x0a, 0x00, 0x09, 0x00, 0xbb, 0x01, 0x4d, 0x00, 0x53, 0x00, 0x43, 0x00, 0x54, 0x00, 0x4c, 0x00,
    0x53, 0x00, 0x5f, 0x00, 0x55, 0x00, 0x50, 0x00, 0x44, 0x00, 0x4f, 0x00, 0x57, 0x00, 0x4e, 0x00,
    0x33, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x50,
    0x00, 0x00, 0x00, 0x00, 0x29, 0x00, 0x32, 0x00, 0x0a, 0x00, 0x09, 0x00, 0xbc, 0x01, 0x4d, 0x00,
    0x53, 0x00, 0x43, 0x00, 0x54, 0x00, 0x4c, 0x00, 0x53, 0x00, 0x5f, 0x00, 0x55, 0x00, 0x50, 0x00,
    0x44, 0x00, 0x4f, 0x00, 0x57, 0x00, 0x4e, 0x00, 0x33, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x50, 0x00, 0x00, 0x00, 0x00, 0x29, 0x00, 0x3c, 0x00,
    0x0a, 0x00, 0x09, 0x00, 0xbd, 0x01, 0x4d, 0x00, 0x53, 0x00, 0x43, 0x00, 0x54, 0x00, 0x4c, 0x00,
    0x53, 0x00, 0x5f, 0x00, 0x55, 0x00, 0x50, 0x00, 0x44, 0x00, 0x4f, 0x00, 0x57, 0x00, 0x4e, 0x00,
    0x33, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x50,
    0x00, 0x00, 0x00, 0x00, 0x29, 0x00, 0x46, 0x00, 0x0a, 0x00, 0x09, 0x00, 0xbe, 0x01, 0x4d, 0x00,
    0x53, 0x00, 0x43, 0x00, 0x54, 0x00, 0x4c, 0x00, 0x53, 0x00, 0x5f, 0x00, 0x55, 0x00, 0x50, 0x00,
    0x44, 0x00, 0x4f, 0x00, 0x57, 0x00, 0x4e, 0x00, 0x33, 0x00, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00,
};

// shift-jis UI strings
static const char k_file_filter[] = "All files {*.*}\0*.*\0";
static const char k_file_title[] = "\x83\x74\x83\x40\x83\x43\x83\x8b\x82\xf0\x91\x49\x91\xf0";
static const char k_fold_title[] =
    "\x83\x74\x83\x48\x83\x8b\x83\x5f\x82\xf0\x91\x49\x91\xf0\x82\xb5\x82\xc4\x82\xad\x82\xbe\x82\xb3\x82\xa2";
static const char k_font_sample[] = "\x82\xa0 A 1";
static const char k_fullwidth_space[] = "\x81\x40";
static const char k_pal_original[] = "*\x83\x49\x83\x8a\x83\x57\x83\x69\x83\x8b";
static const char k_eyedropper_prompt[] =
    "\x90\x46\x82\xf0\x83\x4e\x83\x8a\x83\x62\x83\x4e\x82\xb5\x82\xc4\x82\xad\x82\xbe\x82\xb3\x82\xa2";
static const char k_color_dlg_title[] = "\x90\x46\x83\x5f\x83\x43\x83\x41\x83\x8d\x83\x4f";
static const char k_param_dlg_title[] = "\x83\x70\x83\x89\x83\x81\x81\x5b\x83\x5e\x90\xdd\x92\xe8";

// module-static dialog state
static uint8_t g_color_mode = 3;
static WNDPROC g_orig_param_dlg_proc = NULL;
static uint32_t g_active_palette[32];
static uint8_t g_color_updating = 0;
static int8_t g_last_palette_sel = 0;
static uint32_t g_custom_palettes[16][32];
static RmListSlot g_list_slots[16];
static uint8_t g_slot_flags[16];
static int g_param_dlg_slot = 0;
static clock_t g_eyedropper_start_clock = 0;
static int g_color_drag_mode = 0;
static const uint32_t *g_color_custom_pal_ptr = NULL;
static int g_color_box_pos[2] = { 120, 61 };
static int g_color_bar_x = 120;
static HBRUSH g_swatch_brushes[32];
static HFONT g_font_preview_hfont = NULL;

static INT_PTR run_dialog_box(UINT res_id, const void *tmpl, HWND parent, DLGPROC proc, LPARAM lparam)
{
    HMODULE hinst = GetModuleHandleA("rikky_module");
    if (hinst && FindResourceA(hinst, MAKEINTRESOURCEA(res_id), (LPCSTR)RT_DIALOG))
    {
        return DialogBoxParamA(hinst, MAKEINTRESOURCEA(res_id), parent, proc, lparam);
    }
    return DialogBoxIndirectParamA(hinst, (LPCDLGTEMPLATEA)tmpl, parent, proc, lparam);
}

static int is_sjis_lead(unsigned char c)
{
    return (unsigned char)(c + 0x5fu) > 0x3eu && (signed char)c < 0;
}

static size_t escape_sjis_string(char *buf, size_t len, const char *esc_map)
{
    int pass;
    for (pass = 0; pass < 5; pass++)
    {
        size_t i = 0;
        while (i < len)
        {
            unsigned char c = (unsigned char)buf[i];
            if (is_sjis_lead(c))
            {
                i += 2;
                continue;
            }
            if ((char)c == esc_map[pass])
            {
                memmove(buf + i + 2, buf + i + 1, len - i);
                buf[i] = '\\';
                buf[i + 1] = esc_map[pass + 5];
                len++;
                i += 2;
            }
            else
            {
                i++;
            }
        }
    }
    return len;
}

static int serialize_lua_table(lua_State *L, char *out_buf, int pos, const char *esc_map)
{
    int has_prev = 0;
    out_buf[pos++] = '{';
    lua_pushnil(L);

    while (lua_next(L, -2) != 0)
    {
        int key_type = lua_type(L, -2);
        int val_type;

        if (key_type == LUA_TSTRING)
        {
            const char *kstr = lua_tostring(L, -2);
            size_t klen = kstr ? strlen(kstr) : 0;
            if (has_prev == 1)
            {
                out_buf[pos++] = ',';
            }
            if (klen > 0)
            {
                memmove(out_buf + pos, kstr, klen);
                pos += (int)klen;
            }
            out_buf[pos++] = '=';
            has_prev = 0;
        }
        else
        {
            double kd = lua_tonumber(L, -2);
            if (floor(kd) == ceil(kd))
            {
                int ki = (int)lua_tointeger(L, -2);
                if (ki <= 0)
                {
                    char kbuf[64];
                    int klen;
                    if (has_prev == 1)
                    {
                        out_buf[pos++] = ',';
                    }
                    klen = sprintf(kbuf, "[%d]=", ki);
                    memcpy(out_buf + pos, kbuf, (size_t)klen);
                    pos += klen;
                    has_prev = 0;
                }
            }
            else
            {
                char kbuf[64];
                int klen;
                if (has_prev == 1)
                {
                    out_buf[pos++] = ',';
                }
                klen = sprintf(kbuf, "[%g]=", kd);
                memcpy(out_buf + pos, kbuf, (size_t)klen);
                pos += klen;
                has_prev = 0;
            }
        }

        val_type = lua_type(L, -1);
        if (val_type == LUA_TNUMBER)
        {
            double vd = lua_tonumber(L, -1);
            char vbuf[64];
            int vlen;
            if (floor(vd) == ceil(vd))
            {
                int vi = (int)lua_tointeger(L, -1);
                if (has_prev == 0)
                {
                    vlen = sprintf(vbuf, "%d", vi);
                    has_prev = 1;
                }
                else
                {
                    vlen = sprintf(vbuf, ",%d", vi);
                }
            }
            else
            {
                if (has_prev == 0)
                {
                    vlen = sprintf(vbuf, "%g", vd);
                    has_prev = 1;
                }
                else
                {
                    vlen = sprintf(vbuf, ",%g", vd);
                }
            }
            memcpy(out_buf + pos, vbuf, (size_t)vlen);
            pos += vlen;
        }
        else if (val_type == LUA_TBOOLEAN)
        {
            int bv = lua_toboolean(L, -1);
            const char *bstr;
            size_t blen;
            if (bv == 0)
            {
                bstr = (has_prev == 0) ? "false" : ",false";
            }
            else
            {
                bstr = (has_prev == 0) ? "true" : ",true";
            }
            has_prev = 1;
            blen = strlen(bstr);
            memcpy(out_buf + pos, bstr, blen);
            pos += (int)blen;
        }
        else if (val_type == LUA_TSTRING)
        {
            const char *s = lua_tostring(L, -1);
            char sbuf[1024];
            size_t slen = s ? strlen(s) : 0;
            if (slen > 511)
            {
                slen = 511;
            }
            if (s && slen > 0)
            {
                memcpy(sbuf, s, slen);
            }
            sbuf[slen] = '\0';
            slen = escape_sjis_string(sbuf, slen, esc_map);
            if (has_prev == 0)
            {
                out_buf[pos++] = '\"';
                has_prev = 1;
            }
            else
            {
                out_buf[pos++] = ',';
                out_buf[pos++] = '\"';
            }
            memcpy(out_buf + pos, sbuf, slen);
            pos += (int)slen;
            out_buf[pos++] = '\"';
        }
        else if (val_type == LUA_TTABLE)
        {
            if (has_prev == 0)
            {
                has_prev = 1;
            }
            else
            {
                out_buf[pos++] = ',';
            }
            pos = serialize_lua_table(L, out_buf, pos, esc_map);
        }

        lua_remove(L, -1);
    }

    out_buf[pos++] = '}';
    return pos;
}

static void set_dialog_param_impl(lua_State *L, int is_cs)
{
    if (rm_check_exedit_ver() && lua_type(L, 1) == LUA_TSTRING)
    {
        const char *key = lua_tostring(L, 1);
        char orig_buf[514];
        uintptr_t *p_base;
        char *dialog_str;
        size_t orig_len;
        size_t key_len;
        size_t search_off = 0;

        memset(orig_buf, 0, sizeof(orig_buf));
        p_base = (uintptr_t *)RikkyGetMemoryData(is_cs ? 0x87 : 0x8d);
        if (!p_base || !*p_base || !key)
        {
            lua_settop(L, 0);
            lua_pushboolean(L, 0);
            return;
        }

        dialog_str = (char *)(*p_base + 0x104u);
        strncpy(orig_buf, dialog_str, 512);
        orig_buf[512] = '\0';
        orig_len = strlen(orig_buf);
        key_len = strlen(key);

        while (search_off <= orig_len)
        {
            char *hit = strstr(orig_buf + search_off, key);
            size_t pos;
            if (!hit)
            {
                break;
            }
            pos = (size_t)(hit - orig_buf) + key_len;
            if (pos < orig_len)
            {
                int state = 0;
                size_t val_start = 0;
                size_t i = pos;
                while (i < orig_len)
                {
                    if (orig_buf[i] != ' ')
                    {
                        if (orig_buf[i] == '=')
                        {
                            size_t j = i + 1;
                            while (j < orig_len && orig_buf[j] == '\x14')
                            {
                                j++;
                            }
                            state = 1;
                            val_start = j;
                        }
                        else
                        {
                            state = 2;
                        }
                        break;
                    }
                    i++;
                }

                if (state == 1)
                {
                    char out_buf[2048];
                    char suffix_buf[520];
                    char val_buf[1024];
                    const char esc_map[10] = { '\\', '\"', '\'', '\n', '\r', '\\', '\"', '\'', 'n', 'r' };
                    size_t k;
                    int val_type;
                    size_t out_len;

                    memcpy(out_buf, orig_buf, val_start);
                    out_buf[val_start] = '\0';
                    suffix_buf[0] = '\0';

                    for (k = val_start; k < orig_len; k++)
                    {
                        if (orig_buf[k] == '\0')
                        {
                            suffix_buf[0] = '\0';
                        }
                        else if (orig_buf[k] == ';')
                        {
                            memcpy(suffix_buf, orig_buf + k, orig_len - k + 1);
                            break;
                        }
                    }

                    val_buf[0] = '\0';
                    val_type = lua_type(L, 2);
                    switch (val_type)
                    {
                    case -1:
                    case LUA_TNIL:
                        strcpy(val_buf, "nil");
                        break;
                    case LUA_TBOOLEAN:
                        strcpy(val_buf, lua_toboolean(L, 2) ? "true" : "false");
                        break;
                    case LUA_TNUMBER:
                    {
                        double d = lua_tonumber(L, 2);
                        if (floor(d) == ceil(d))
                        {
                            const char *fmt = "%d";
                            if (lua_type(L, 3) == LUA_TNUMBER && lua_tointeger(L, 3) == 1)
                            {
                                fmt = "0x%x";
                            }
                            sprintf(val_buf, fmt, (int)lua_tointeger(L, 2));
                        }
                        else
                        {
                            sprintf(val_buf, "%g", d);
                        }
                        break;
                    }
                    case LUA_TSTRING:
                    {
                        const char *raw = lua_tostring(L, 2);
                        char sbuf[1024];
                        size_t slen = raw ? strlen(raw) : 0;
                        if (slen >= 512)
                        {
                            memcpy(sbuf, raw, 512);
                            slen = 512;
                        }
                        else if (raw)
                        {
                            strcpy(sbuf, raw);
                        }
                        sbuf[slen] = '\0';
                        slen = escape_sjis_string(sbuf, slen, esc_map);
                        sbuf[slen] = '\0';
                        sprintf(val_buf, "\"%s\"", sbuf);
                        break;
                    }
                    case LUA_TTABLE:
                    {
                        int end_pos;
                        lua_pushvalue(L, 2);
                        end_pos = serialize_lua_table(L, val_buf, 0, esc_map);
                        val_buf[end_pos] = '\0';
                        lua_pop(L, 1);
                        break;
                    }
                    default:
                        break;
                    }

                    strcat(out_buf, val_buf);
                    strcat(out_buf, suffix_buf);
                    out_len = strlen(out_buf);
                    if (out_len < 255)
                    {
                        memcpy(dialog_str, out_buf, out_len + 1);
                    }
                    else
                    {
                        out_buf[254] = '\0';
                        memcpy(dialog_str, out_buf, 255);
                    }

                    lua_pushvalue(L, 2);
                    lua_setfield(L, LUA_GLOBALSINDEX, key);
                    lua_settop(L, 0);
                    lua_pushboolean(L, 1);
                    return;
                }
            }
            search_off = (size_t)(hit - orig_buf) + 1;
        }
    }

    lua_settop(L, 0);
    lua_pushboolean(L, 0);
}

static void format_selected_path(char *path, int basename_only)
{
    if (basename_only == 0)
    {
        char tmp[520];
        char *slash = strchr(path, '\\');
        while (slash != NULL)
        {
            strcpy(tmp, slash + 1);
            slash[0] = '\\';
            slash[1] = '\\';
            slash[2] = '\0';
            strcat(path, tmp);
            slash = strchr(slash + 2, '\\');
        }
    }
    else
    {
        size_t len = strlen(path);
        int i = (int)len - 1;
        while (i >= 0 && path[i] != '\\')
        {
            i--;
        }
        if (i >= 0)
        {
            memmove(path, path + i + 1, len - (size_t)i);
        }
    }
}

static void open_file_dialog(HWND owner, HWND edit_hwnd, int basename_only)
{
    char file_buf[520];
    OPENFILENAMEA ofn;

    memset(file_buf, 0, sizeof(file_buf));
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(OPENFILENAMEA);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = k_file_filter;
    ofn.lpstrFile = file_buf;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = k_file_title;
    ofn.Flags = OFN_FILEMUSTEXIST;

    if (GetOpenFileNameA(&ofn))
    {
        format_selected_path(file_buf, basename_only);
        SetWindowTextA(edit_hwnd, file_buf);
    }
}

static void open_folder_dialog(HWND owner, HWND edit_hwnd, int basename_only)
{
    char path_buf[520];
    IMalloc *p_malloc = NULL;
    BROWSEINFOA bi;
    LPITEMIDLIST pidl;

    memset(path_buf, 0, sizeof(path_buf));
    SHGetMalloc(&p_malloc);

    memset(&bi, 0, sizeof(bi));
    bi.hwndOwner = owner;
    bi.pszDisplayName = path_buf;
    bi.lpszTitle = k_fold_title;
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

    pidl = SHBrowseForFolderA(&bi);
    if (pidl != NULL)
    {
        if (SHGetPathFromIDListA(pidl, path_buf))
        {
            format_selected_path(path_buf, basename_only);
            SetWindowTextA(edit_hwnd, path_buf);
        }
        if (p_malloc != NULL)
        {
            IMalloc_Free(p_malloc, pidl);
        }
    }
    if (p_malloc != NULL)
    {
        IMalloc_Release(p_malloc);
    }
}

static int CALLBACK enum_font_fam_proc(const LOGFONTA *lf, const TEXTMETRICA *tm, DWORD font_type, LPARAM lparam)
{
    HWND list_hwnd = (HWND)lparam;
    LRESULT count;
    int idx;
    char item_text[128];

    (void)tm;
    (void)font_type;

    if (lf->lfFaceName[0] == '@')
    {
        return 1;
    }

    count = SendMessageA(list_hwnd, LB_GETCOUNT, 0, 0);
    for (idx = (int)count - 1; idx >= 0; idx--)
    {
        item_text[0] = '\0';
        SendMessageA(list_hwnd, LB_GETTEXT, (WPARAM)idx, (LPARAM)item_text);
        if (strcmp(lf->lfFaceName, item_text) == 0)
        {
            return 1;
        }
    }

    SendMessageA(list_hwnd, LB_ADDSTRING, 0, (LPARAM)lf->lfFaceName);
    return 1;
}

static void set_font_preview_face(HWND dlg_hwnd, const char *face_name)
{
    HFONT new_font = CreateFontA(0, 0, 0, 0, FW_NORMAL, 0, 0, 0, SHIFTJIS_CHARSET, 0, 0, 0, 0, face_name);
    HWND preview_hwnd = GetDlgItem(dlg_hwnd, 302);
    SendMessageA(preview_hwnd, WM_SETFONT, (WPARAM)new_font, 0);
    if (g_font_preview_hfont != NULL)
    {
        DeleteObject(g_font_preview_hfont);
    }
    g_font_preview_hfont = new_font;
}

static void init_font_dialog(HWND dlg_hwnd, HWND edit_hwnd)
{
    WPARAM dlg_font = (WPARAM)SendMessageA(edit_hwnd, WM_GETFONT, 0, 0);
    HWND list_hwnd = GetDlgItem(dlg_hwnd, 300);
    LOGFONTA lf;
    HDC hdc;
    char cur_name[260];
    char item_name[268];
    LRESULT count;
    LRESULT i;
    LRESULT sel = 0;

    SendMessageA(list_hwnd, WM_SETFONT, dlg_font, 0);
    SendMessageA(GetDlgItem(dlg_hwnd, 301), WM_SETFONT, dlg_font, 0);
    SendMessageA(GetDlgItem(dlg_hwnd, IDOK), WM_SETFONT, dlg_font, 0);
    SendMessageA(GetDlgItem(dlg_hwnd, IDCANCEL), WM_SETFONT, dlg_font, 0);

    memset(&lf, 0, sizeof(lf));
    lf.lfCharSet = DEFAULT_CHARSET;
    hdc = GetDC(dlg_hwnd);
    EnumFontFamiliesExA(hdc, &lf, enum_font_fam_proc, (LPARAM)list_hwnd, 0);
    ReleaseDC(dlg_hwnd, hdc);

    cur_name[0] = '\0';
    GetWindowTextA(edit_hwnd, cur_name, 256);
    if (cur_name[0] == '@')
    {
        memmove(cur_name, cur_name + 1, strlen(cur_name));
        SendMessageA(GetDlgItem(dlg_hwnd, 301), BM_SETCHECK, BST_CHECKED, 0);
    }

    count = SendMessageA(list_hwnd, LB_GETCOUNT, 0, 0);
    item_name[0] = '\0';
    if (count > 0)
    {
        int matched = 0;
        for (i = 0; i < count; i++)
        {
            SendMessageA(list_hwnd, LB_GETTEXT, (WPARAM)i, (LPARAM)item_name);
            if (strcmp(cur_name, item_name) == 0)
            {
                sel = i;
                matched = 1;
                break;
            }
        }
        if (!matched)
        {
            sel = 0;
            SendMessageA(list_hwnd, LB_GETTEXT, 0, (LPARAM)item_name);
        }
    }

    set_font_preview_face(dlg_hwnd, item_name);
    SendMessageA(list_hwnd, LB_SETCURSEL, (WPARAM)sel, 0);
    SetFocus(list_hwnd);
}

static char *accept_font_dialog(HWND dlg_hwnd)
{
    char *res = (char *)rm_calloc(256, 1);
    HWND list_hwnd;
    LRESULT sel;

    if (!res)
    {
        return NULL;
    }

    list_hwnd = GetDlgItem(dlg_hwnd, 300);
    sel = SendMessageA(list_hwnd, LB_GETCURSEL, 0, 0);
    if (sel != LB_ERR)
    {
        SendMessageA(list_hwnd, LB_GETTEXT, (WPARAM)sel, (LPARAM)res);
    }
    if (SendMessageA(GetDlgItem(dlg_hwnd, 301), BM_GETCHECK, 0, 0) == BST_CHECKED)
    {
        memmove(res + 1, res, strlen(res) + 1);
        res[0] = '@';
    }
    return res;
}

static INT_PTR CALLBACK font_dlg_proc(HWND dlg_hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    if (msg == WM_INITDIALOG)
    {
        init_font_dialog(dlg_hwnd, (HWND)lparam);
        return 0;
    }
    if (msg == WM_COMMAND)
    {
        UINT id = LOWORD(wparam);
        if (id == 300)
        {
            UINT code = HIWORD(wparam);
            if (code == LBN_SELCHANGE || code == LBN_DBLCLK)
            {
                HWND list_hwnd = GetDlgItem(dlg_hwnd, 300);
                HWND preview_hwnd = GetDlgItem(dlg_hwnd, 302);
                LRESULT sel = SendMessageA(list_hwnd, LB_GETCURSEL, 0, 0);
                char face[268];
                face[0] = '\0';
                if (sel != LB_ERR)
                {
                    SendMessageA(list_hwnd, LB_GETTEXT, (WPARAM)sel, (LPARAM)face);
                }
                set_font_preview_face(dlg_hwnd, face);
                SetWindowTextA(preview_hwnd, k_font_sample);
            }
            return 0;
        }
        if (id == IDOK)
        {
            char *res = accept_font_dialog(dlg_hwnd);
            if (g_font_preview_hfont != NULL)
            {
                DeleteObject(g_font_preview_hfont);
                g_font_preview_hfont = NULL;
            }
            EndDialog(dlg_hwnd, (INT_PTR)res);
            return 1;
        }
        if (id != IDCANCEL)
        {
            return 0;
        }
    }
    else if (msg != WM_CLOSE)
    {
        return 0;
    }

    if (g_font_preview_hfont != NULL)
    {
        DeleteObject(g_font_preview_hfont);
        g_font_preview_hfont = NULL;
    }
    EndDialog(dlg_hwnd, 0);
    return 1;
}

static void init_list_dialog(HWND dlg_hwnd, const RmListDlgCtx *ctx)
{
    WPARAM dlg_font = (WPARAM)SendMessageA(ctx->edit_hwnd, WM_GETFONT, 0, 0);
    HWND combo_hwnd = GetDlgItem(dlg_hwnd, 300);
    char cur_text[268];
    int sel = 0;
    int i;

    SendMessageA(combo_hwnd, WM_SETFONT, dlg_font, 0);
    SendMessageA(GetDlgItem(dlg_hwnd, IDOK), WM_SETFONT, dlg_font, 0);
    SendMessageA(GetDlgItem(dlg_hwnd, IDCANCEL), WM_SETFONT, dlg_font, 0);

    cur_text[0] = '\0';
    GetWindowTextA(ctx->edit_hwnd, cur_text, 256);

    if (ctx->slot && ctx->slot->count > 0 && ctx->slot->items)
    {
        for (i = 0; i < ctx->slot->count; i++)
        {
            const char *item = ctx->slot->items[i] ? ctx->slot->items[i] : "";
            SendMessageA(combo_hwnd, CB_ADDSTRING, 0, (LPARAM)item);
            if (cur_text[0] != '\0' && strcmp(cur_text, item) == 0)
            {
                sel = i;
            }
        }
    }

    SendMessageA(combo_hwnd, CB_SETCURSEL, (WPARAM)sel, 0);
    SetFocus(combo_hwnd);
}

static INT_PTR CALLBACK list_dlg_proc(HWND dlg_hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    if (msg == WM_INITDIALOG)
    {
        init_list_dialog(dlg_hwnd, (const RmListDlgCtx *)lparam);
        return 0;
    }
    if (msg == WM_COMMAND)
    {
        UINT id = LOWORD(wparam);
        if (id == IDOK)
        {
            HWND combo_hwnd = GetDlgItem(dlg_hwnd, 300);
            LRESULT sel = SendMessageA(combo_hwnd, CB_GETCURSEL, 0, 0);
            EndDialog(dlg_hwnd, (INT_PTR)sel);
            return 1;
        }
        if (id != IDCANCEL)
        {
            return 0;
        }
    }
    else if (msg != WM_CLOSE)
    {
        return 0;
    }

    EndDialog(dlg_hwnd, -1);
    return 1;
}

static void load_exedit_default_palette(void)
{
    const uint32_t *ex_pal = (const uint32_t *)RikkyGetMemoryData(0x75);
    int i;
    if (!ex_pal)
    {
        memset(g_active_palette, 0, sizeof(g_active_palette));
        return;
    }
    for (i = 0; i < 32; i++)
    {
        uint32_t c = ex_pal[i];
        g_active_palette[i] = ((c & 0xffu) << 16) | ((c >> 16) & 0xffu) | (c & 0xff00u);
    }
}

static int get_color_palette_edit_dir(char *out_dir)
{
    HMODULE hmod = GetModuleHandleA("color_palette.auf");
    size_t len;
    int pos;

    if (!hmod)
    {
        return 0;
    }
    out_dir[0] = '\0';
    GetModuleFileNameA(hmod, out_dir, MAX_PATH);
    len = strlen(out_dir);
    pos = (int)len - 1;
    while (pos >= 0 && out_dir[pos] != '\\')
    {
        pos--;
    }
    if (pos >= 0)
    {
        strcpy(out_dir + pos + 1, "color_palette_edit");
    }
    return 1;
}

static void load_palette_cpe(const char *pal_name, const uint32_t *custom_pal)
{
    char name_copy[272];
    char edit_dir[300];
    char file_path[560];
    char *star;
    FILE *fp;
    int b0;
    char *text_buf = NULL;

    if (pal_name[0] == '*')
    {
        if (custom_pal)
        {
            memcpy(g_active_palette, custom_pal, 32 * sizeof(uint32_t));
        }
        return;
    }

    if (strcmp(pal_name, "DEFAULT") == 0 || !get_color_palette_edit_dir(edit_dir))
    {
        load_exedit_default_palette();
        return;
    }

    strncpy(name_copy, pal_name, 268);
    name_copy[268] = '\0';
    star = strchr(name_copy + 1, '*');
    if (!star)
    {
        sprintf(file_path, "%s\\%s.cpe", edit_dir, name_copy);
    }
    else
    {
        *star = '\0';
        sprintf(file_path, "%s\\%s\\%s.cpe", edit_dir, star + 1, name_copy);
    }

    fp = fopen(file_path, "rb");
    if (!fp)
    {
        load_exedit_default_palette();
        return;
    }

    b0 = fgetc(fp);
    if (b0 == 0xfe)
    {
        int b1 = fgetc(fp);
        if (b1 == 0xff)
        {
            long fsize;
            size_t wcount = 0;
            wchar_t *wbuf;
            int hi;
            fseek(fp, 0, SEEK_END);
            fsize = ftell(fp);
            wbuf = (wchar_t *)rm_malloc((size_t)(fsize + 4));
            if (!wbuf)
            {
                fclose(fp);
                load_exedit_default_palette();
                return;
            }
            fseek(fp, 2, SEEK_SET);
            while ((hi = fgetc(fp)) != EOF)
            {
                int lo = fgetc(fp);
                if (lo == EOF)
                {
                    lo = 0;
                }
                wbuf[wcount++] = (wchar_t)(((unsigned short)hi << 8) | (unsigned short)lo);
            }
            wbuf[wcount] = L'\0';
            fclose(fp);
            text_buf = rm_wide_to_sjis_html(NULL, wbuf);
            rm_free(wbuf);
        }
    }
    else if (b0 == 0xff)
    {
        int b1 = fgetc(fp);
        if (b1 == 0xfe)
        {
            long fsize;
            size_t wcount = 0;
            wchar_t *wbuf;
            int lo;
            fseek(fp, 0, SEEK_END);
            fsize = ftell(fp);
            wbuf = (wchar_t *)rm_malloc((size_t)(fsize + 4));
            if (!wbuf)
            {
                fclose(fp);
                load_exedit_default_palette();
                return;
            }
            fseek(fp, 2, SEEK_SET);
            while ((lo = fgetc(fp)) != EOF)
            {
                int hi = fgetc(fp);
                if (hi == EOF)
                {
                    hi = 0;
                }
                wbuf[wcount++] = (wchar_t)(((unsigned short)hi << 8) | (unsigned short)lo);
            }
            wbuf[wcount] = L'\0';
            fclose(fp);
            text_buf = rm_wide_to_sjis_html(NULL, wbuf);
            rm_free(wbuf);
        }
    }
    else if (b0 == 0xef)
    {
        int b1 = fgetc(fp);
        int b2 = (b1 == 0xbb) ? fgetc(fp) : -1;
        if (b1 == 0xbb && b2 == 0xbf)
        {
            long fsize;
            size_t ulen = 0;
            char *ubuf;
            int ch;
            int wlen;
            wchar_t *wbuf;

            fseek(fp, 0, SEEK_END);
            fsize = ftell(fp);
            ubuf = (char *)rm_malloc((size_t)(fsize + 4));
            if (!ubuf)
            {
                fclose(fp);
                load_exedit_default_palette();
                return;
            }
            fseek(fp, 3, SEEK_SET);
            while ((ch = fgetc(fp)) != EOF)
            {
                ubuf[ulen++] = (char)ch;
            }
            ubuf[ulen] = '\0';
            fclose(fp);

            wlen = MultiByteToWideChar(CP_UTF8, 0, ubuf, (int)(ulen + 1), NULL, 0);
            wbuf = (wchar_t *)rm_malloc((size_t)wlen * sizeof(wchar_t) + 2);
            if (!wbuf)
            {
                rm_free(ubuf);
                load_exedit_default_palette();
                return;
            }
            MultiByteToWideChar(CP_UTF8, 0, ubuf, (int)(ulen + 1), wbuf, wlen);
            rm_free(ubuf);
            text_buf = rm_wide_to_sjis_html(NULL, wbuf);
            rm_free(wbuf);
        }
    }

    if (!text_buf)
    {
        long fsize;
        size_t slen = 0;
        int ch;
        fseek(fp, 0, SEEK_END);
        fsize = ftell(fp);
        if (fsize < 0)
        {
            fclose(fp);
            load_exedit_default_palette();
            return;
        }
        text_buf = (char *)rm_malloc((size_t)(fsize + 4));
        if (!text_buf)
        {
            fclose(fp);
            load_exedit_default_palette();
            return;
        }
        fseek(fp, 0, SEEK_SET);
        while ((ch = fgetc(fp)) != EOF)
        {
            text_buf[slen++] = (char)ch;
        }
        text_buf[slen] = '\0';
        fclose(fp);
    }

    // strip tabs, carriage returns, ascii spaces, and full-width spaces
    {
        size_t i = 0;
        size_t slen = strlen(text_buf);
        char *grown;

        while (i < slen)
        {
            unsigned char c = (unsigned char)text_buf[i];
            if ((c & 0xfbu) == 9u || c == 0x20u)
            {
                memmove(text_buf + i, text_buf + i + 1, slen - i);
                slen--;
            }
            else if (strncmp(text_buf + i, k_fullwidth_space, 2) == 0)
            {
                memmove(text_buf + i, text_buf + i + 2, slen - i - 1);
                slen -= 2;
            }
            else
            {
                i++;
            }
        }

        grown = (char *)rm_realloc(text_buf, slen + 2);
        if (grown)
        {
            int row = 0;
            char *line_ptr = grown;
            grown[slen + 1] = '\0';

            for (;;)
            {
                char *nl = strchr(line_ptr, '\n');
                int done = 0;
                int col = 0;
                int slot = row;

                if (!nl)
                {
                    nl = line_ptr + strlen(line_ptr);
                    done = 1;
                }
                else if (row == 7)
                {
                    done = 1;
                }
                *nl = ',';

                while (line_ptr <= nl && col < 4)
                {
                    char *comma = strchr(line_ptr, ',');
                    char *semi = strchr(line_ptr, ';');
                    char hex_tok[64];

                    if (!semi || comma <= semi)
                    {
                        *comma = '\0';
                    }
                    else
                    {
                        *semi = '\0';
                    }

                    if (slot < 32 && line_ptr[0] != '\0')
                    {
                        snprintf(hex_tok, sizeof(hex_tok), "0x%s", line_ptr);
                        if (strlen(hex_tok) > 2)
                        {
                            g_active_palette[slot] = (uint32_t)strtol(hex_tok, NULL, 0);
                        }
                    }

                    col++;
                    slot += 8;
                    line_ptr = comma + 1;
                }

                if (done)
                {
                    break;
                }
                row++;
                line_ptr = nl + 1;
            }

            rm_free(grown);
            return;
        }
        rm_free(text_buf);
    }

    load_exedit_default_palette();
}

static uint32_t read_dlg_hex_color(HWND dlg_hwnd)
{
    char buf[260];
    buf[0] = '\0';
    GetWindowTextA(GetDlgItem(dlg_hwnd, 0x1aa), buf, 256);
    return (uint32_t)strtol(buf, NULL, 16);
}

static void read_dlg_hue_fallback(HWND dlg_hwnd, double *out_h)
{
    char buf[260];
    buf[0] = '\0';
    GetWindowTextA(GetDlgItem(dlg_hwnd, 0x1a7), buf, 256);
    *out_h = (double)atoi(buf);
}

static void rgb_to_hsv_keep_hue(HWND dlg_hwnd, uint32_t rgb, int *out_r, int *out_g, int *out_b,
                                double *out_h, double *out_s, double *out_v)
{
    *out_r = (int)((rgb >> 16) & 0xffu);
    *out_g = (int)((rgb >> 8) & 0xffu);
    *out_b = (int)(rgb & 0xffu);
    RGB2HSV(*out_r, *out_g, *out_b, out_h, out_s, out_v);
    if (*out_h == 0.0 && *out_s == 0.0)
    {
        read_dlg_hue_fallback(dlg_hwnd, out_h);
    }
}

static void update_color_cursors(int *bar_x, int *box_pos, uint32_t rgb, int r, int g, int b,
                                 double h, double s, double v)
{
    double y_val;
    switch (g_color_mode)
    {
    case 0:
        *bar_x = (int)lrint(ceil((double)((float)r * 179.0f / 255.0f)) + 120.0);
        box_pos[0] = (int)lrint(ceil((double)g * 179.0 / 255.0) + 120.0);
        y_val = (1.0 - (double)b / 255.0) * 179.0;
        break;
    case 1:
        *bar_x = (int)lrint(ceil((double)((float)g * 179.0f / 255.0f)) + 120.0);
        box_pos[0] = (int)lrint(ceil((double)b * 179.0 / 255.0) + 120.0);
        y_val = (1.0 - (double)r / 255.0) * 179.0;
        break;
    case 2:
        *bar_x = (int)lrint(ceil((double)((float)b * 179.0f / 255.0f)) + 120.0);
        box_pos[0] = (int)lrint(ceil((double)r * 179.0 / 255.0) + 120.0);
        y_val = (1.0 - (double)g / 255.0) * 179.0;
        break;
    case 3:
        *bar_x = (int)lrint(ceil(h * 179.0 / 360.0) + 120.0);
        box_pos[0] = (int)lrint(ceil(s * 179.0 / 100.0) + 120.0);
        y_val = (1.0 - v / 100.0) * 179.0;
        break;
    case 4:
        *bar_x = (int)lrint(ceil(s * 179.0 / 100.0) + 120.0);
        box_pos[0] = (int)lrint(ceil(v * 179.0 / 100.0) + 120.0);
        y_val = (1.0 - h / 360.0) * 179.0;
        break;
    case 5:
        *bar_x = (int)lrint(ceil(v * 179.0 / 100.0) + 120.0);
        box_pos[0] = (int)lrint(ceil(s * 179.0 / 100.0) + 120.0);
        y_val = (1.0 - h / 360.0) * 179.0;
        break;
    case 6:
        *bar_x = (int)lrint(ceil((double)((float)rgb * 179.0f / 16777215.0f)) + 120.0);
        return;
    default:
        return;
    }
    box_pos[1] = (int)lrint(ceil(y_val) + 61.0);
}

static void set_color_edit_texts(HWND dlg_hwnd, uint32_t rgb, int r, int g, int b, int h, int s, int v)
{
    char buf[64];
    g_color_updating = 1;
    sprintf(buf, "%06X", rgb & 0xffffffu);
    SetWindowTextA(GetDlgItem(dlg_hwnd, 0x1aa), buf);
    sprintf(buf, "%d", r);
    SetWindowTextA(GetDlgItem(dlg_hwnd, 0x1a4), buf);
    sprintf(buf, "%d", g);
    SetWindowTextA(GetDlgItem(dlg_hwnd, 0x1a5), buf);
    sprintf(buf, "%d", b);
    SetWindowTextA(GetDlgItem(dlg_hwnd, 0x1a6), buf);
    sprintf(buf, "%d", h);
    SetWindowTextA(GetDlgItem(dlg_hwnd, 0x1a7), buf);
    sprintf(buf, "%d", s);
    SetWindowTextA(GetDlgItem(dlg_hwnd, 0x1a8), buf);
    sprintf(buf, "%d", v);
    SetWindowTextA(GetDlgItem(dlg_hwnd, 0x1a9), buf);
    g_color_updating = 0;
}

static void invalidate_color_bar(HWND dlg_hwnd)
{
    RECT rc = { 0x77, 0x1a, 0x12d, 0x38 };
    InvalidateRect(dlg_hwnd, &rc, FALSE);
}

static void invalidate_color_box(HWND dlg_hwnd)
{
    RECT rc = { 0x77, 0x3c, 0x12d, 0xf2 };
    InvalidateRect(dlg_hwnd, &rc, FALSE);
}

static void invalidate_color_preview(HWND dlg_hwnd)
{
    RECT rc = { 0x1e, 0xc0, 0x50, 0xf2 };
    InvalidateRect(dlg_hwnd, &rc, FALSE);
}

static void invalidate_color_all(HWND dlg_hwnd)
{
    invalidate_color_bar(dlg_hwnd);
    invalidate_color_box(dlg_hwnd);
    invalidate_color_preview(dlg_hwnd);
}

static void cleanup_swatch_brushes(void)
{
    int i;
    for (i = 0; i < 32; i++)
    {
        if (g_swatch_brushes[i] != NULL)
        {
            DeleteObject(g_swatch_brushes[i]);
            g_swatch_brushes[i] = NULL;
        }
    }
}

static const uint32_t *init_color_dialog(HWND dlg_hwnd, const RmColorDlgCtx *ctx, int *bar_x, int *box_pos)
{
    HWND edit_hwnd = ctx->edit_hwnd;
    const uint32_t *custom_pal = ctx->custom_palette;
    char text_buf[268];
    uint32_t rgb;
    WPARAM dlg_font;
    HWND combo_hwnd;
    char edit_dir[300];
    HINSTANCE hinst;
    int col_x;
    int base_id;
    int id;
    int r, g, b;
    double h = 0.0, s = 0.0, v = 0.0;

    cleanup_swatch_brushes();
    text_buf[0] = '\0';
    GetWindowTextA(edit_hwnd, text_buf, 256);
    rgb = (uint32_t)strtol(text_buf, NULL, 0) & 0xffffffu;
    dlg_font = (WPARAM)SendMessageA(edit_hwnd, WM_GETFONT, 0, 0);

    combo_hwnd = GetDlgItem(dlg_hwnd, 400);
    if (custom_pal != NULL)
    {
        SendMessageA(combo_hwnd, CB_ADDSTRING, 0, (LPARAM)k_pal_original);
    }
    SendMessageA(combo_hwnd, CB_ADDSTRING, 0, (LPARAM)"DEFAULT");

    if (get_color_palette_edit_dir(edit_dir))
    {
        char pattern[340];
        WIN32_FIND_DATAA ffd;
        HANDLE hfind;

        snprintf(pattern, sizeof(pattern), "%s\\*", edit_dir);
        hfind = FindFirstFileA(pattern, &ffd);
        if (hfind != INVALID_HANDLE_VALUE)
        {
            do
            {
                const char *fname = ffd.cFileName;
                if (strcmp(fname, ".") == 0 || strcmp(fname, "..") == 0)
                {
                    continue;
                }
                if ((ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
                {
                    size_t nlen = strlen(fname);
                    if (nlen >= 4 && strcmp(fname + nlen - 4, ".cpe") == 0)
                    {
                        char stem[268];
                        memcpy(stem, fname, nlen - 4);
                        stem[nlen - 4] = '\0';
                        SendMessageA(combo_hwnd, CB_ADDSTRING, 0, (LPARAM)stem);
                    }
                }
                else
                {
                    char sub_pattern[560];
                    WIN32_FIND_DATAA ffd2;
                    HANDLE hfind2;

                    snprintf(sub_pattern, sizeof(sub_pattern), "%s\\%s\\*", edit_dir, fname);
                    hfind2 = FindFirstFileA(sub_pattern, &ffd2);
                    if (hfind2 != INVALID_HANDLE_VALUE)
                    {
                        do
                        {
                            const char *fname2 = ffd2.cFileName;
                            if (strcmp(fname2, ".") == 0 || strcmp(fname2, "..") == 0)
                            {
                                continue;
                            }
                            if ((ffd2.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
                            {
                                size_t nlen2 = strlen(fname2);
                                if (nlen2 >= 4 && strcmp(fname2 + nlen2 - 4, ".cpe") == 0)
                                {
                                    char stem2[268];
                                    char combined[540];
                                    memcpy(stem2, fname2, nlen2 - 4);
                                    stem2[nlen2 - 4] = '\0';
                                    snprintf(combined, sizeof(combined), "%s*%s", stem2, fname);
                                    SendMessageA(combo_hwnd, CB_ADDSTRING, 0, (LPARAM)combined);
                                }
                            }
                        } while (FindNextFileA(hfind2, &ffd2));
                        FindClose(hfind2);
                    }
                }
            } while (FindNextFileA(hfind, &ffd));
            FindClose(hfind);
        }
    }

    if (g_last_palette_sel == 0)
    {
        SendMessageA(combo_hwnd, CB_SETCURSEL, 0, 0);
        GetWindowTextA(combo_hwnd, text_buf, 256);
        g_last_palette_sel = (custom_pal == NULL) ? 1 : -1;
    }
    else
    {
        int v_sel = (int)g_last_palette_sel;
        if (v_sel < 0)
        {
            if (custom_pal != NULL)
            {
                SendMessageA(combo_hwnd, CB_SETCURSEL, (WPARAM)(~v_sel), 0);
                GetWindowTextA(combo_hwnd, text_buf, 256);
            }
            else
            {
                int idx = -v_sel - 2;
                if (idx == -1)
                {
                    g_last_palette_sel = (int8_t)-2;
                    idx = 0;
                }
                SendMessageA(combo_hwnd, CB_SETCURSEL, (WPARAM)idx, 0);
                GetWindowTextA(combo_hwnd, text_buf, 256);
                g_last_palette_sel = (int8_t)(~g_last_palette_sel);
            }
        }
        else if (custom_pal == NULL)
        {
            SendMessageA(combo_hwnd, CB_SETCURSEL, (WPARAM)(v_sel - 1), 0);
            GetWindowTextA(combo_hwnd, text_buf, 256);
        }
        else
        {
            SendMessageA(combo_hwnd, CB_SETCURSEL, (WPARAM)v_sel, 0);
            GetWindowTextA(combo_hwnd, text_buf, 256);
            g_last_palette_sel = (int8_t)(~g_last_palette_sel);
        }
    }

    hinst = (HINSTANCE)GetWindowLongPtrA(dlg_hwnd, GWLP_HINSTANCE);
    col_x = 0x13c;
    for (base_id = 500; base_id < 532; base_id += 8)
    {
        int row_y = 0x1e;
        int cur_id = base_id;
        while (row_y != 0xe6)
        {
            CreateWindowExA(0, "STATIC", "", WS_CHILD | WS_VISIBLE | SS_SUNKEN,
                            col_x, row_y, 20, 20, dlg_hwnd, (HMENU)(uintptr_t)cur_id, hinst, NULL);
            cur_id++;
            row_y += 25;
        }
        col_x += 25;
    }

    load_palette_cpe(text_buf, custom_pal);

    for (id = 400; id < 404; id++)
    {
        SendMessageA(GetDlgItem(dlg_hwnd, id), WM_SETFONT, dlg_font, 0);
    }
    for (id = 0x19a; id < 0x1a1; id++)
    {
        SendMessageA(GetDlgItem(dlg_hwnd, id), WM_SETFONT, dlg_font, 0);
        SendMessageA(GetDlgItem(dlg_hwnd, id + 10), WM_SETFONT, dlg_font, 0);
        SendMessageA(GetDlgItem(dlg_hwnd, id + 20), WM_SETFONT, dlg_font, 0);
    }
    SendMessageA(GetDlgItem(dlg_hwnd, IDOK), WM_SETFONT, dlg_font, 0);
    SendMessageA(GetDlgItem(dlg_hwnd, IDCANCEL), WM_SETFONT, dlg_font, 0);
    SendMessageA(GetDlgItem(dlg_hwnd, (int)g_color_mode + 0x1ae), BM_SETCHECK, BST_CHECKED, 0);

    rgb_to_hsv_keep_hue(dlg_hwnd, rgb, &r, &g, &b, &h, &s, &v);
    set_color_edit_texts(dlg_hwnd, rgb, r, g, b,
                         (int)lrint(ceil(h)), (int)lrint(ceil(s)), (int)lrint(ceil(v)));
    update_color_cursors(bar_x, box_pos, rgb, r, g, b, h, s, v);
    return custom_pal;
}

static void paint_color_dialog(HWND dlg_hwnd, const int *bar_x, const int *box_pos)
{
    uint32_t rgb = read_dlg_hex_color(dlg_hwnd);
    int r, g, b;
    double h = 0.0, s = 0.0, v = 0.0;
    PAINTSTRUCT ps;
    HDC hdc;
    HDC mem_dc;
    HBITMAP bmp;
    HGDIOBJ old_bmp;
    HPEN pen;
    HGDIOBJ old_pen;
    HBRUSH brush;
    HGDIOBJ old_brush;
    COLORREF cur_cref;
    int cx, cy;
    int x, y;

    rgb_to_hsv_keep_hue(dlg_hwnd, rgb, &r, &g, &b, &h, &s, &v);
    cur_cref = (COLORREF)(((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r);

    hdc = BeginPaint(dlg_hwnd, &ps);
    mem_dc = CreateCompatibleDC(hdc);
    bmp = CreateCompatibleBitmap(hdc, 0xb6, 0x1e);
    old_bmp = SelectObject(mem_dc, bmp);
    Rectangle(mem_dc, 0, 0, 0xb6, 0x1e);

    switch (g_color_mode)
    {
    case 0:
        for (x = 0; x < 180; x++)
        {
            COLORREF c = (x == 0) ? 0 : (COLORREF)lrint(ceil((double)((float)x / 179.0f * 255.0f)));
            for (y = 1; y < 29; y++)
            {
                SetPixel(mem_dc, x + 1, y, c);
            }
        }
        break;
    case 1:
        for (x = 0; x < 180; x++)
        {
            uint32_t gv = (x == 0) ? 0u : (uint32_t)lrint(ceil((double)((float)x / 179.0f * 255.0f)));
            COLORREF c = (COLORREF)(gv << 8);
            for (y = 1; y < 29; y++)
            {
                SetPixel(mem_dc, x + 1, y, c);
            }
        }
        break;
    case 2:
        for (x = 0; x < 180; x++)
        {
            uint32_t bv = (x == 0) ? 0u : (uint32_t)lrint(ceil((double)((float)x / 179.0f * 255.0f)));
            COLORREF c = (COLORREF)(bv << 16);
            for (y = 1; y < 29; y++)
            {
                SetPixel(mem_dc, x + 1, y, c);
            }
        }
        break;
    case 3:
        for (x = 0; x < 180; x++)
        {
            int cr = 0, cg = 0, cb = 0;
            COLORREF c;
            HSV2RGB((double)((float)x * 360.0f / 179.0f), 100.0, 100.0, &cr, &cg, &cb);
            c = (COLORREF)(((uint32_t)cb << 16) | ((uint32_t)cg << 8) | (uint32_t)cr);
            for (y = 1; y < 29; y++)
            {
                SetPixel(mem_dc, x + 1, y, c);
            }
        }
        break;
    case 4:
    case 5:
        for (x = 0; x < 180; x++)
        {
            uint32_t lv = (x == 0) ? 0u : (uint32_t)lrint(ceil((double)((float)x / 179.0f * 255.0f)));
            COLORREF c = (COLORREF)((lv << 16) | (lv << 8) | lv);
            for (y = 1; y < 29; y++)
            {
                SetPixel(mem_dc, x + 1, y, c);
            }
        }
        break;
    case 6:
        for (x = 0; x < 180; x++)
        {
            COLORREF c = (x == 0) ? 0 : (COLORREF)lrint(ceil((double)((float)x / 179.0f * 16777215.0f)));
            for (y = 1; y < 29; y++)
            {
                SetPixel(mem_dc, x + 1, y, c);
            }
        }
        break;
    default:
        break;
    }

    cx = (int)(short)((short)(*bar_x) - 0x77);
    pen = CreatePen(PS_SOLID, 1, 0xffffff);
    old_pen = SelectObject(mem_dc, pen);
    MoveToEx(mem_dc, cx, 0, NULL);
    LineTo(mem_dc, cx, 29);
    SelectObject(mem_dc, old_pen);
    DeleteObject(pen);

    pen = CreatePen(PS_SOLID, 1, 0x000000);
    old_pen = SelectObject(mem_dc, pen);
    MoveToEx(mem_dc, cx - 1, 0, NULL);
    LineTo(mem_dc, cx - 1, 29);
    MoveToEx(mem_dc, cx + 1, 0, NULL);
    LineTo(mem_dc, cx + 1, 29);
    SelectObject(mem_dc, old_pen);
    DeleteObject(pen);

    BitBlt(hdc, 0x77, 0x1a, 0xb6, 0x1e, mem_dc, 0, 0, SRCCOPY);
    SelectObject(mem_dc, old_bmp);
    DeleteObject(bmp);

    bmp = CreateCompatibleBitmap(hdc, 0xb6, 0xb6);
    old_bmp = SelectObject(mem_dc, bmp);
    Rectangle(mem_dc, 0, 0, 0xb6, 0xb6);

    switch (g_color_mode)
    {
    case 0:
        for (x = 0; x < 180; x++)
        {
            uint32_t gv = (x == 0) ? 0u : (uint32_t)lrint(ceil((double)((float)x * 255.0f / 179.0f)));
            for (y = 0; y < 180; y++)
            {
                uint32_t bv = (y == 0) ? 255u : (uint32_t)lrint(ceil((double)((1.0f - (float)y / 179.0f) * 255.0f)));
                SetPixel(mem_dc, x + 1, y + 1, (COLORREF)((bv << 16) | (gv << 8) | (uint32_t)r));
            }
        }
        break;
    case 1:
        for (x = 0; x < 180; x++)
        {
            uint32_t bv = (x == 0) ? 0u : (uint32_t)lrint(ceil((double)((float)x * 255.0f / 179.0f)));
            for (y = 0; y < 180; y++)
            {
                uint32_t rv = (y == 0) ? 255u : (uint32_t)lrint(ceil((double)((1.0f - (float)y / 179.0f) * 255.0f)));
                SetPixel(mem_dc, x + 1, y + 1, (COLORREF)((bv << 16) | ((uint32_t)g << 8) | rv));
            }
        }
        break;
    case 2:
        for (x = 0; x < 180; x++)
        {
            uint32_t rv = (x == 0) ? 0u : (uint32_t)lrint(ceil((double)((float)x * 255.0f / 179.0f)));
            for (y = 0; y < 180; y++)
            {
                uint32_t gv = (y == 0) ? 255u : (uint32_t)lrint(ceil((double)((1.0f - (float)y / 179.0f) * 255.0f)));
                SetPixel(mem_dc, x + 1, y + 1, (COLORREF)(((uint32_t)b << 16) | (gv << 8) | rv));
            }
        }
        break;
    case 3:
        for (x = 0; x < 180; x++)
        {
            double cs = (double)((float)x * 100.0f / 179.0f);
            for (y = 0; y < 180; y++)
            {
                int cr = 0, cg = 0, cb = 0;
                double cv = (double)((1.0f - (float)y / 179.0f) * 100.0f);
                HSV2RGB(h, cs, cv, &cr, &cg, &cb);
                SetPixel(mem_dc, x + 1, y + 1, (COLORREF)(((uint32_t)cb << 16) | ((uint32_t)cg << 8) | (uint32_t)cr));
            }
        }
        break;
    case 4:
        for (x = 0; x < 180; x++)
        {
            double cv = (double)((float)x * 100.0f / 179.0f);
            for (y = 0; y < 180; y++)
            {
                int cr = 0, cg = 0, cb = 0;
                double ch = (double)((1.0f - (float)y / 179.0f) * 360.0f);
                HSV2RGB(ch, s, cv, &cr, &cg, &cb);
                SetPixel(mem_dc, x + 1, y + 1, (COLORREF)(((uint32_t)cb << 16) | ((uint32_t)cg << 8) | (uint32_t)cr));
            }
        }
        break;
    case 5:
        for (x = 0; x < 180; x++)
        {
            double cs = (double)((float)x * 100.0f / 179.0f);
            for (y = 0; y < 180; y++)
            {
                int cr = 0, cg = 0, cb = 0;
                double ch = (double)((1.0f - (float)y / 179.0f) * 360.0f);
                HSV2RGB(ch, cs, v, &cr, &cg, &cb);
                SetPixel(mem_dc, x + 1, y + 1, (COLORREF)(((uint32_t)cb << 16) | ((uint32_t)cg << 8) | (uint32_t)cr));
            }
        }
        break;
    case 6:
        brush = CreateSolidBrush(cur_cref);
        old_brush = SelectObject(mem_dc, brush);
        Rectangle(mem_dc, 0, 0, 0xb6, 0xb6);
        SelectObject(mem_dc, old_brush);
        DeleteObject(brush);
        break;
    default:
        break;
    }

    cx = (int)(short)((short)box_pos[0] - 0x77);
    cy = (int)(short)((short)box_pos[1] - 0x3c);
    pen = CreatePen(PS_SOLID, 1, 0x000000);
    old_pen = SelectObject(mem_dc, pen);
    Arc(mem_dc, cx - 5, cy - 5, cx + 5, cy + 5, cx - 5, cy - 5, cx - 5, cy - 5);
    SelectObject(mem_dc, old_pen);
    DeleteObject(pen);

    pen = CreatePen(PS_SOLID, 1, 0xffffff);
    old_pen = SelectObject(mem_dc, pen);
    Arc(mem_dc, cx - 4, cy - 4, cx + 4, cy + 4, cx - 4, cy - 4, cx - 4, cy - 4);
    SelectObject(mem_dc, old_pen);
    DeleteObject(pen);

    BitBlt(hdc, 0x77, 0x3c, 0xb6, 0xb6, mem_dc, 0, 0, SRCCOPY);
    SelectObject(mem_dc, old_bmp);
    DeleteObject(bmp);

    bmp = CreateCompatibleBitmap(hdc, 0x32, 0x32);
    old_bmp = SelectObject(mem_dc, bmp);
    brush = CreateSolidBrush(cur_cref);
    old_brush = SelectObject(mem_dc, brush);
    Rectangle(mem_dc, 0, 0, 0x32, 0x32);
    SelectObject(mem_dc, old_brush);
    DeleteObject(brush);
    BitBlt(hdc, 0x1e, 0xc0, 0x32, 0x32, mem_dc, 0, 0, SRCCOPY);
    SelectObject(mem_dc, old_bmp);
    DeleteObject(bmp);

    DeleteDC(mem_dc);
    EndPaint(dlg_hwnd, &ps);
}

static void apply_complementary_color(HWND dlg_hwnd, int *bar_x, int *box_pos)
{
    uint32_t c = read_dlg_hex_color(dlg_hwnd);
    int r = (int)((c >> 16) & 0xffu);
    int g = (int)((c >> 8) & 0xffu);
    int b = (int)(c & 0xffu);
    int hi = r > g ? r : g;
    int lo = r < g ? r : g;
    int sum, nr, ng, nb;
    uint32_t nrgb;
    double h = 0.0, s = 0.0, v = 0.0;

    if (b > hi)
    {
        hi = b;
    }
    if (b < lo)
    {
        lo = b;
    }
    sum = hi + lo;
    nr = sum - r;
    ng = sum - g;
    nb = sum - b;
    nrgb = ((uint32_t)nr << 16) | ((uint32_t)ng << 8) | (uint32_t)nb;

    RGB2HSV(nr, ng, nb, &h, &s, &v);
    if (h == 0.0 && s == 0.0)
    {
        read_dlg_hue_fallback(dlg_hwnd, &h);
    }
    set_color_edit_texts(dlg_hwnd, nrgb, nr, ng, nb,
                         (int)lrint(ceil(h)), (int)lrint(ceil(s)), (int)lrint(ceil(v)));
    update_color_cursors(bar_x, box_pos, nrgb, nr, ng, nb, h, s, v);
    invalidate_color_all(dlg_hwnd);
}

static void apply_invert_color(HWND dlg_hwnd, int *bar_x, int *box_pos)
{
    uint32_t c = read_dlg_hex_color(dlg_hwnd);
    int nr = 255 - (int)((c >> 16) & 0xffu);
    int ng = 255 - (int)((c >> 8) & 0xffu);
    int nb = 255 - (int)(c & 0xffu);
    uint32_t nrgb = ((uint32_t)nr << 16) | ((uint32_t)ng << 8) | (uint32_t)nb;
    double h = 0.0, s = 0.0, v = 0.0;

    RGB2HSV(nr, ng, nb, &h, &s, &v);
    if (h == 0.0 && s == 0.0)
    {
        read_dlg_hue_fallback(dlg_hwnd, &h);
    }
    set_color_edit_texts(dlg_hwnd, nrgb, nr, ng, nb,
                         (int)lrint(ceil(h)), (int)lrint(ceil(s)), (int)lrint(ceil(v)));
    update_color_cursors(bar_x, box_pos, nrgb, nr, ng, nb, h, s, v);
    invalidate_color_all(dlg_hwnd);
}

static void select_color_mode_radio(HWND dlg_hwnd, int ctrl_id, int *bar_x, int *box_pos)
{
    int id;
    char buf[260];
    int r, g, b, h, s, v;
    uint32_t rgb;

    for (id = 0x1ae; id < 0x1b5; id++)
    {
        SendMessageA(GetDlgItem(dlg_hwnd, id), BM_SETCHECK, (WPARAM)(id == ctrl_id), 0);
    }
    g_color_mode = (uint8_t)(ctrl_id - 0x1ae);

    GetWindowTextA(GetDlgItem(dlg_hwnd, 0x1a4), buf, 256);
    r = atoi(buf);
    GetWindowTextA(GetDlgItem(dlg_hwnd, 0x1a5), buf, 256);
    g = atoi(buf);
    GetWindowTextA(GetDlgItem(dlg_hwnd, 0x1a6), buf, 256);
    b = atoi(buf);
    GetWindowTextA(GetDlgItem(dlg_hwnd, 0x1a7), buf, 256);
    h = atoi(buf);
    GetWindowTextA(GetDlgItem(dlg_hwnd, 0x1a8), buf, 256);
    s = atoi(buf);
    GetWindowTextA(GetDlgItem(dlg_hwnd, 0x1a9), buf, 256);
    v = atoi(buf);
    rgb = read_dlg_hex_color(dlg_hwnd);

    update_color_cursors(bar_x, box_pos, rgb, r, g, b, (double)h, (double)s, (double)v);
    invalidate_color_bar(dlg_hwnd);
    invalidate_color_box(dlg_hwnd);
}

static int clamp_int_range(int val, int lo, int hi)
{
    if (val < lo)
    {
        return lo;
    }
    if (val > hi)
    {
        return hi;
    }
    return val;
}

static int read_clamped_dlg_int(HWND dlg_hwnd, int ctrl_id, int lo, int hi)
{
    char buf[260];
    buf[0] = '\0';
    GetWindowTextA(GetDlgItem(dlg_hwnd, ctrl_id), buf, 256);
    return clamp_int_range(atoi(buf), lo, hi);
}

static void update_color_from_drag(HWND dlg_hwnd, const int *bar_x, const int *box_pos, int drag_kind)
{
    int from_box = (drag_kind != 1);
    int r = 0, g = 0, b = 0;
    double h = 0.0, s = 0.0, v = 0.0;
    uint32_t rgb;
    char buf[64];

    if (g_color_mode > 6)
    {
        invalidate_color_all(dlg_hwnd);
        return;
    }

    if (g_color_mode <= 2)
    {
        if (g_color_mode == 0)
        {
            if (from_box)
            {
                g = (int)lrint(((float)(box_pos[0] - 0x78) / 179.0f) * 255.0f);
                b = (int)lrint(255.0f - ((float)(box_pos[1] - 0x3d) / 179.0f) * 255.0f);
                r = read_clamped_dlg_int(dlg_hwnd, 0x1a4, 0, 255);
            }
            else
            {
                r = (int)lrint(((float)(*bar_x - 0x78) / 179.0f) * 255.0f);
                g = read_clamped_dlg_int(dlg_hwnd, 0x1a5, 0, 255);
                b = read_clamped_dlg_int(dlg_hwnd, 0x1a6, 0, 255);
            }
        }
        else if (g_color_mode == 1)
        {
            if (from_box)
            {
                b = (int)lrint(((float)(box_pos[0] - 0x78) / 179.0f) * 255.0f);
                r = (int)lrint(255.0f - ((float)(box_pos[1] - 0x3d) / 179.0f) * 255.0f);
                g = read_clamped_dlg_int(dlg_hwnd, 0x1a5, 0, 255);
            }
            else
            {
                g = (int)lrint(((float)(*bar_x - 0x78) / 179.0f) * 255.0f);
                b = read_clamped_dlg_int(dlg_hwnd, 0x1a6, 0, 255);
                r = read_clamped_dlg_int(dlg_hwnd, 0x1a4, 0, 255);
            }
        }
        else
        {
            if (from_box)
            {
                r = (int)lrint(((float)(box_pos[0] - 0x78) / 179.0f) * 255.0f);
                g = (int)lrint(255.0f - ((float)(box_pos[1] - 0x3d) / 179.0f) * 255.0f);
                b = read_clamped_dlg_int(dlg_hwnd, 0x1a6, 0, 255);
            }
            else
            {
                b = (int)lrint(((float)(*bar_x - 0x78) / 179.0f) * 255.0f);
                r = read_clamped_dlg_int(dlg_hwnd, 0x1a4, 0, 255);
                g = read_clamped_dlg_int(dlg_hwnd, 0x1a5, 0, 255);
            }
        }

        RGB2HSV(r, g, b, &h, &s, &v);
        if (h == 0.0 && s == 0.0)
        {
            read_dlg_hue_fallback(dlg_hwnd, &h);
        }
        rgb = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        set_color_edit_texts(dlg_hwnd, rgb, r, g, b,
                             (int)lrint(ceil(h)), (int)lrint(ceil(s)), (int)lrint(ceil(v)));
        invalidate_color_all(dlg_hwnd);
        return;
    }

    if (g_color_mode == 6)
    {
        if (from_box)
        {
            invalidate_color_box(dlg_hwnd);
            return;
        }
        rgb = (uint32_t)lrint(((float)(*bar_x - 0x78) / 179.0f) * 16777215.0f) & 0xffffffu;
        rgb_to_hsv_keep_hue(dlg_hwnd, rgb, &r, &g, &b, &h, &s, &v);
        set_color_edit_texts(dlg_hwnd, rgb, r, g, b,
                             (int)lrint(ceil(h)), (int)lrint(ceil(s)), (int)lrint(ceil(v)));
        invalidate_color_all(dlg_hwnd);
        return;
    }

    if (g_color_mode == 3)
    {
        if (from_box)
        {
            s = (double)(((float)(box_pos[0] - 0x78) / 179.0f) * 100.0f);
            v = (double)(100.0f - ((float)(box_pos[1] - 0x3d) / 179.0f) * 100.0f);
            h = (double)read_clamped_dlg_int(dlg_hwnd, 0x1a7, 0, 360);
        }
        else
        {
            h = (double)(((float)(*bar_x - 0x78) / 179.0f) * 360.0f);
            s = (double)read_clamped_dlg_int(dlg_hwnd, 0x1a8, 0, 100);
            v = (double)read_clamped_dlg_int(dlg_hwnd, 0x1a9, 0, 100);
        }
    }
    else if (g_color_mode == 4)
    {
        if (from_box)
        {
            v = (double)(((float)(box_pos[0] - 0x78) / 179.0f) * 100.0f);
            h = (double)(360.0f - ((float)(box_pos[1] - 0x3d) / 179.0f) * 360.0f);
            s = (double)read_clamped_dlg_int(dlg_hwnd, 0x1a8, 0, 100);
        }
        else
        {
            s = (double)(((float)(*bar_x - 0x78) / 179.0f) * 100.0f);
            v = (double)read_clamped_dlg_int(dlg_hwnd, 0x1a9, 0, 100);
            h = (double)read_clamped_dlg_int(dlg_hwnd, 0x1a7, 0, 360);
        }
    }
    else
    {
        if (from_box)
        {
            s = (double)(((float)(box_pos[0] - 0x78) / 179.0f) * 100.0f);
            h = (double)(360.0f - ((float)(box_pos[1] - 0x3d) / 179.0f) * 360.0f);
            v = (double)read_clamped_dlg_int(dlg_hwnd, 0x1a9, 0, 100);
        }
        else
        {
            v = (double)(((float)(*bar_x - 0x78) / 179.0f) * 100.0f);
            s = (double)read_clamped_dlg_int(dlg_hwnd, 0x1a8, 0, 100);
            h = (double)read_clamped_dlg_int(dlg_hwnd, 0x1a7, 0, 360);
        }
    }

    g_color_updating = 1;
    if (g_color_mode == 3)
    {
        if (from_box)
        {
            sprintf(buf, "%d", (int)lrint(ceil(s)));
            SetWindowTextA(GetDlgItem(dlg_hwnd, 0x1a8), buf);
            sprintf(buf, "%d", (int)lrint(ceil(v)));
            SetWindowTextA(GetDlgItem(dlg_hwnd, 0x1a9), buf);
        }
        else
        {
            sprintf(buf, "%d", (int)lrint(ceil(h)));
            SetWindowTextA(GetDlgItem(dlg_hwnd, 0x1a7), buf);
        }
    }
    else if (g_color_mode == 4)
    {
        if (from_box)
        {
            sprintf(buf, "%d", (int)lrint(ceil(v)));
            SetWindowTextA(GetDlgItem(dlg_hwnd, 0x1a9), buf);
            sprintf(buf, "%d", (int)lrint(ceil(h)));
            SetWindowTextA(GetDlgItem(dlg_hwnd, 0x1a7), buf);
        }
        else
        {
            sprintf(buf, "%d", (int)lrint(ceil(s)));
            SetWindowTextA(GetDlgItem(dlg_hwnd, 0x1a8), buf);
        }
    }
    else
    {
        if (from_box)
        {
            sprintf(buf, "%d", (int)lrint(ceil(s)));
            SetWindowTextA(GetDlgItem(dlg_hwnd, 0x1a8), buf);
            sprintf(buf, "%d", (int)lrint(ceil(h)));
            SetWindowTextA(GetDlgItem(dlg_hwnd, 0x1a7), buf);
        }
        else
        {
            sprintf(buf, "%d", (int)lrint(ceil(v)));
            SetWindowTextA(GetDlgItem(dlg_hwnd, 0x1a9), buf);
        }
    }

    HSV2RGB(h, s, v, &r, &g, &b);
    rgb = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
    sprintf(buf, "%d", r);
    SetWindowTextA(GetDlgItem(dlg_hwnd, 0x1a4), buf);
    sprintf(buf, "%d", g);
    SetWindowTextA(GetDlgItem(dlg_hwnd, 0x1a5), buf);
    sprintf(buf, "%d", b);
    SetWindowTextA(GetDlgItem(dlg_hwnd, 0x1a6), buf);
    sprintf(buf, "%06X", rgb);
    SetWindowTextA(GetDlgItem(dlg_hwnd, 0x1aa), buf);
    g_color_updating = 0;

    invalidate_color_all(dlg_hwnd);
}

static int handle_color_lbuttondown(HWND dlg_hwnd, LPARAM lparam, int *bar_x, int *box_pos)
{
    short mx = (short)LOWORD(lparam);
    short my = (short)HIWORD(lparam);
    int col_x;
    int col_base;

    if ((unsigned short)(mx - 0x78) < 180u)
    {
        if ((unsigned short)(my - 0x1b) < 29u)
        {
            SetCapture(dlg_hwnd);
            *bar_x = (int)mx;
            update_color_from_drag(dlg_hwnd, bar_x, box_pos, 1);
            return 1;
        }
        if ((unsigned short)(my - 0x3d) < 180u)
        {
            SetCapture(dlg_hwnd);
            box_pos[0] = (int)mx;
            box_pos[1] = (int)my;
            update_color_from_drag(dlg_hwnd, bar_x, box_pos, 2);
            return 2;
        }
        return 0;
    }

    for (col_x = 0x13c, col_base = 0; col_x < 0x1a0; col_x += 25, col_base += 8)
    {
        if (mx >= col_x && mx < col_x + 20)
        {
            int row;
            for (row = 0; row < 8; row++)
            {
                int row_y = 0x1e + row * 25;
                if ((unsigned short)(my - row_y) < 20u)
                {
                    uint32_t rgb = g_active_palette[col_base + row] & 0xffffffu;
                    int r, g, b;
                    double h = 0.0, s = 0.0, v = 0.0;
                    rgb_to_hsv_keep_hue(dlg_hwnd, rgb, &r, &g, &b, &h, &s, &v);
                    set_color_edit_texts(dlg_hwnd, rgb, r, g, b,
                                         (int)lrint(ceil(h)), (int)lrint(ceil(s)), (int)lrint(ceil(v)));
                    update_color_cursors(bar_x, box_pos, rgb, r, g, b, h, s, v);
                    invalidate_color_all(dlg_hwnd);
                    return 0;
                }
            }
        }
    }

    return 0;
}

static INT_PTR CALLBACK color_dlg_proc(HWND dlg_hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    if (msg == WM_INITDIALOG)
    {
        g_color_drag_mode = 0;
        g_color_custom_pal_ptr = init_color_dialog(dlg_hwnd, (const RmColorDlgCtx *)lparam,
                                                   &g_color_bar_x, g_color_box_pos);
        return 0;
    }

    if (msg == WM_PAINT)
    {
        paint_color_dialog(dlg_hwnd, &g_color_bar_x, g_color_box_pos);
        return 0;
    }

    if (msg == WM_CTLCOLORSTATIC)
    {
        int ctrl_id = GetDlgCtrlID((HWND)lparam);
        if ((unsigned int)(ctrl_id - 500) < 32u)
        {
            int idx = ctrl_id - 500;
            uint32_t c = g_active_palette[idx];
            COLORREF cref = (COLORREF)(((c & 0xffu) << 16) | ((c >> 16) & 0xffu) | (c & 0xff00u));
            if (g_swatch_brushes[idx] != NULL)
            {
                DeleteObject(g_swatch_brushes[idx]);
            }
            g_swatch_brushes[idx] = CreateSolidBrush(cref);
            return (INT_PTR)g_swatch_brushes[idx];
        }
        return 0;
    }

    if (msg == WM_LBUTTONDOWN)
    {
        g_color_drag_mode = handle_color_lbuttondown(dlg_hwnd, lparam, &g_color_bar_x, g_color_box_pos);
        return 0;
    }

    if (msg == WM_MOUSEMOVE)
    {
        short mx, my;
        if (g_color_drag_mode == 0)
        {
            return 0;
        }
        mx = (short)LOWORD(lparam);
        mx = (short)clamp_int_range((int)mx, 120, 299);
        if (g_color_drag_mode == 1)
        {
            g_color_bar_x = (int)mx;
        }
        else if (g_color_drag_mode == 2)
        {
            my = (short)HIWORD(lparam);
            g_color_box_pos[0] = (int)mx;
            g_color_box_pos[1] = clamp_int_range((int)my, 0x3d, 0xf0);
        }
        update_color_from_drag(dlg_hwnd, &g_color_bar_x, g_color_box_pos, g_color_drag_mode);
        return 0;
    }

    if (msg == WM_LBUTTONUP)
    {
        if (g_color_drag_mode != 0)
        {
            ReleaseCapture();
            g_color_drag_mode = 0;
        }
        return 0;
    }

    if (msg == WM_TIMER)
    {
        if (GetAsyncKeyState(VK_ESCAPE) < 0)
        {
            KillTimer(dlg_hwnd, 0x193);
            SetWindowTextA(dlg_hwnd, k_color_dlg_title);
            SendMessageA(GetDlgItem(dlg_hwnd, 0x193), BM_SETSTATE, 0, 0);
            return 0;
        }
        if (GetKeyState(VK_LBUTTON) < 0 && clock() > g_eyedropper_start_clock + 50)
        {
            POINT pt;
            HWND desk_hwnd;
            HDC desk_dc;
            COLORREF px;
            int r, g, b;
            uint32_t rgb;
            double h = 0.0, s = 0.0, v = 0.0;

            KillTimer(dlg_hwnd, 0x193);
            SetWindowTextA(dlg_hwnd, k_color_dlg_title);
            SendMessageA(GetDlgItem(dlg_hwnd, 0x193), BM_SETSTATE, 0, 0);

            GetCursorPos(&pt);
            desk_hwnd = GetDesktopWindow();
            desk_dc = GetDC(desk_hwnd);
            px = GetPixel(desk_dc, pt.x, pt.y);
            ReleaseDC(desk_hwnd, desk_dc);

            r = (int)(px & 0xffu);
            g = (int)((px >> 8) & 0xffu);
            b = (int)((px >> 16) & 0xffu);
            rgb = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;

            RGB2HSV(r, g, b, &h, &s, &v);
            if (h == 0.0 && s == 0.0)
            {
                read_dlg_hue_fallback(dlg_hwnd, &h);
            }
            set_color_edit_texts(dlg_hwnd, rgb, r, g, b,
                                 (int)lrint(ceil(h)), (int)lrint(ceil(s)), (int)lrint(ceil(v)));
            update_color_cursors(&g_color_bar_x, g_color_box_pos, rgb, r, g, b, h, s, v);
            invalidate_color_all(dlg_hwnd);
        }
        return 0;
    }

    if (msg == WM_NOTIFY)
    {
        const NMUPDOWN *nmud = (const NMUPDOWN *)lparam;
        if (nmud && nmud->hdr.code == UDN_DELTAPOS)
        {
            UINT id = (UINT)wparam;
            char buf[64];
            if (id >= 0x1b8 && id <= 0x1ba)
            {
                HWND edit_hwnd = GetDlgItem(dlg_hwnd, (int)id - 20);
                int val;
                SetFocus(edit_hwnd);
                SendMessageA(edit_hwnd, EM_SETSEL, 0, 0);
                GetWindowTextA(edit_hwnd, buf, sizeof(buf));
                val = clamp_int_range(atoi(buf) - nmud->iDelta, 0, 255);
                sprintf(buf, "%d", val);
                SetWindowTextA(edit_hwnd, buf);
            }
            else if (id >= 0x1bb && id <= 0x1bd)
            {
                HWND edit_hwnd = GetDlgItem(dlg_hwnd, (int)id - 20);
                int hi = (id == 0x1bb) ? 360 : 100;
                int val;
                SetFocus(edit_hwnd);
                SendMessageA(edit_hwnd, EM_SETSEL, 0, 0);
                GetWindowTextA(edit_hwnd, buf, sizeof(buf));
                val = clamp_int_range(atoi(buf) - nmud->iDelta, 0, hi);
                sprintf(buf, "%d", val);
                SetWindowTextA(edit_hwnd, buf);
            }
            else if (id == 0x1be)
            {
                HWND edit_hwnd = GetDlgItem(dlg_hwnd, 0x1aa);
                int val;
                SetFocus(edit_hwnd);
                SendMessageA(edit_hwnd, EM_SETSEL, 0, 0);
                GetWindowTextA(edit_hwnd, buf, sizeof(buf));
                val = clamp_int_range((int)strtol(buf, NULL, 16) - nmud->iDelta, 0, 0xffffff);
                sprintf(buf, "%06X", val);
                SetWindowTextA(edit_hwnd, buf);
            }
        }
        return 0;
    }

    if (msg == WM_COMMAND)
    {
        UINT id = LOWORD(wparam);
        UINT code = HIWORD(wparam);
        char buf[260];

        if (id == IDOK)
        {
            uint32_t res = read_dlg_hex_color(dlg_hwnd) & 0xffffffu;
            KillTimer(dlg_hwnd, 0x193);
            cleanup_swatch_brushes();
            EndDialog(dlg_hwnd, (INT_PTR)res);
            return 1;
        }
        if (id == IDCANCEL)
        {
            KillTimer(dlg_hwnd, 0x193);
            cleanup_swatch_brushes();
            EndDialog(dlg_hwnd, -1);
            return 1;
        }
        if (id == 400)
        {
            if (code == CBN_SELCHANGE)
            {
                HWND combo_hwnd = (HWND)lparam;
                LRESULT sel = SendMessageA(combo_hwnd, CB_GETCURSEL, 0, 0);
                buf[0] = '\0';
                GetWindowTextA(combo_hwnd, buf, 256);
                g_last_palette_sel = (int8_t)(sel + 1);
                if (g_color_custom_pal_ptr != NULL)
                {
                    g_last_palette_sel = (int8_t)(-g_last_palette_sel);
                }
                load_palette_cpe(buf, g_color_custom_pal_ptr);
                InvalidateRect(dlg_hwnd, NULL, FALSE);
            }
            return 0;
        }
        if (id == 0x191)
        {
            apply_complementary_color(dlg_hwnd, &g_color_bar_x, g_color_box_pos);
            return 0;
        }
        if (id == 0x192)
        {
            apply_invert_color(dlg_hwnd, &g_color_bar_x, g_color_box_pos);
            return 0;
        }
        if (id == 0x193)
        {
            SetWindowTextA(dlg_hwnd, k_eyedropper_prompt);
            SendMessageA(GetDlgItem(dlg_hwnd, 0x193), BM_SETSTATE, 1, 0);
            SetTimer(dlg_hwnd, 0x193, 10, NULL);
            g_eyedropper_start_clock = clock();
            return 0;
        }
        if (id >= 0x1ae && id <= 0x1b4)
        {
            select_color_mode_radio(dlg_hwnd, (int)id, &g_color_bar_x, g_color_box_pos);
            return 0;
        }
        if (id >= 0x1a4 && id <= 0x1a6)
        {
            uint32_t base_rgb, rgb;
            int comp, r, g, b;
            double h = 0.0, s = 0.0, v = 0.0;
            if (code != EN_CHANGE || g_color_updating)
            {
                return 0;
            }
            base_rgb = read_dlg_hex_color(dlg_hwnd);
            buf[0] = '\0';
            GetWindowTextA((HWND)lparam, buf, 256);
            comp = clamp_int_range(atoi(buf), 0, 255);
            if (id == 0x1a4)
            {
                rgb = ((uint32_t)comp << 16) | (base_rgb & 0x00ffffu);
            }
            else if (id == 0x1a5)
            {
                rgb = ((uint32_t)comp << 8) | (base_rgb & 0xff00ffu);
            }
            else
            {
                rgb = (uint32_t)comp | (base_rgb & 0xffff00u);
            }
            rgb_to_hsv_keep_hue(dlg_hwnd, rgb, &r, &g, &b, &h, &s, &v);
            g_color_updating = 1;
            sprintf(buf, "%d", (int)lrint(ceil(h)));
            SetWindowTextA(GetDlgItem(dlg_hwnd, 0x1a7), buf);
            sprintf(buf, "%d", (int)lrint(ceil(s)));
            SetWindowTextA(GetDlgItem(dlg_hwnd, 0x1a8), buf);
            sprintf(buf, "%d", (int)lrint(ceil(v)));
            SetWindowTextA(GetDlgItem(dlg_hwnd, 0x1a9), buf);
            sprintf(buf, "%06X", rgb);
            SetWindowTextA(GetDlgItem(dlg_hwnd, 0x1aa), buf);
            g_color_updating = 0;
            update_color_cursors(&g_color_bar_x, g_color_box_pos, rgb, r, g, b, h, s, v);
            invalidate_color_all(dlg_hwnd);
            return 0;
        }
        if (id >= 0x1a7 && id <= 0x1a9)
        {
            double h, s, v;
            int r = 0, g = 0, b = 0;
            uint32_t rgb;
            if (code != EN_CHANGE || g_color_updating)
            {
                return 0;
            }
            GetWindowTextA(GetDlgItem(dlg_hwnd, 0x1a7), buf, 256);
            h = (double)atoi(buf);
            GetWindowTextA(GetDlgItem(dlg_hwnd, 0x1a8), buf, 256);
            s = (double)atoi(buf);
            GetWindowTextA(GetDlgItem(dlg_hwnd, 0x1a9), buf, 256);
            v = (double)atoi(buf);
            if (id == 0x1a7)
            {
                if (h < 0.0) h = 0.0;
                else if (h > 360.0) h = 360.0;
            }
            else if (id == 0x1a8)
            {
                if (s < 0.0) s = 0.0;
                else if (s > 100.0) s = 100.0;
            }
            else
            {
                if (v < 0.0) v = 0.0;
                else if (v > 100.0) v = 100.0;
            }
            HSV2RGB(h, s, v, &r, &g, &b);
            rgb = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
            g_color_updating = 1;
            sprintf(buf, "%d", r);
            SetWindowTextA(GetDlgItem(dlg_hwnd, 0x1a4), buf);
            sprintf(buf, "%d", g);
            SetWindowTextA(GetDlgItem(dlg_hwnd, 0x1a5), buf);
            sprintf(buf, "%d", b);
            SetWindowTextA(GetDlgItem(dlg_hwnd, 0x1a6), buf);
            sprintf(buf, "%06X", rgb);
            SetWindowTextA(GetDlgItem(dlg_hwnd, 0x1aa), buf);
            g_color_updating = 0;
            update_color_cursors(&g_color_bar_x, g_color_box_pos, rgb, r, g, b, h, s, v);
            invalidate_color_all(dlg_hwnd);
            return 0;
        }
        if (id == 0x1aa)
        {
            uint32_t rgb;
            int r, g, b;
            double h = 0.0, s = 0.0, v = 0.0;
            if (code != EN_CHANGE || g_color_updating)
            {
                return 0;
            }
            rgb = read_dlg_hex_color(dlg_hwnd) & 0xffffffu;
            rgb_to_hsv_keep_hue(dlg_hwnd, rgb, &r, &g, &b, &h, &s, &v);
            g_color_updating = 1;
            sprintf(buf, "%d", r);
            SetWindowTextA(GetDlgItem(dlg_hwnd, 0x1a4), buf);
            sprintf(buf, "%d", g);
            SetWindowTextA(GetDlgItem(dlg_hwnd, 0x1a5), buf);
            sprintf(buf, "%d", b);
            SetWindowTextA(GetDlgItem(dlg_hwnd, 0x1a6), buf);
            sprintf(buf, "%d", (int)lrint(ceil(h)));
            SetWindowTextA(GetDlgItem(dlg_hwnd, 0x1a7), buf);
            sprintf(buf, "%d", (int)lrint(ceil(s)));
            SetWindowTextA(GetDlgItem(dlg_hwnd, 0x1a8), buf);
            sprintf(buf, "%d", (int)lrint(ceil(v)));
            SetWindowTextA(GetDlgItem(dlg_hwnd, 0x1a9), buf);
            g_color_updating = 0;
            update_color_cursors(&g_color_bar_x, g_color_box_pos, rgb, r, g, b, h, s, v);
            invalidate_color_all(dlg_hwnd);
            return 0;
        }
        return 0;
    }

    if (msg == WM_CLOSE)
    {
        KillTimer(dlg_hwnd, 0x193);
        cleanup_swatch_brushes();
        EndDialog(dlg_hwnd, -1);
        return 1;
    }

    return 0;
}

static void open_color_dialog(HWND owner, HWND edit_hwnd, const uint32_t *custom_pal)
{
    RmColorDlgCtx ctx;
    INT_PTR res;

    ctx.edit_hwnd = edit_hwnd;
    ctx.custom_palette = custom_pal;
    res = run_dialog_box(400, k_dlg_template_400, owner, color_dlg_proc, (LPARAM)&ctx);
    if ((uintptr_t)res < 0x1000000u)
    {
        char out_hex[64];
        sprintf(out_hex, "0x%06x", (unsigned int)res);
        SetWindowTextA(edit_hwnd, out_hex);
    }
}


static void eval_lua_expr(lua_State *L)
{
    lua_getfield(L, LUA_GLOBALSINDEX, "loadstring");
    lua_getfield(L, LUA_GLOBALSINDEX, "string");
    lua_getfield(L, -1, "format");
    lua_pushstring(L, "return %s");
    lua_pushvalue(L, -5);
    lua_call(L, 2, 1);
    lua_remove(L, -2);
    lua_call(L, 1, 1);
    if (lua_type(L, -1) == LUA_TFUNCTION)
    {
        lua_call(L, 0, 1);
    }
}

static INT_PTR accept_custom_param_dialog(HWND dlg_hwnd, int slot)
{
    lua_State **p_exL = (lua_State **)RikkyGetMemoryData(0x96);
    lua_State *exL = p_exL ? *p_exL : NULL;
    int top0;
    int count;
    const char *id_str;
    int found = 0;
    int i;

    if (!exL)
    {
        return 0;
    }

    top0 = lua_gettop(exL);
    lua_getfield(exL, LUA_GLOBALSINDEX, "Rikky_Dialog_Infomation");
    lua_rawgeti(exL, -1, slot);
    lua_getfield(exL, -1, "number");
    count = (int)lua_tointeger(exL, -1);
    lua_getfield(exL, -2, "ID");
    id_str = lua_tostring(exL, -1);
    if (!id_str)
    {
        lua_settop(exL, top0);
        return 0;
    }

    lua_getfield(exL, LUA_GLOBALSINDEX, "param_Rikky_custom_dialog");
    if (lua_type(exL, -1) != LUA_TTABLE)
    {
        lua_settop(exL, top0);
        return 0;
    }

    lua_pushnil(exL);
    while (lua_next(exL, -2) != 0)
    {
        const char *k = (lua_type(exL, -2) == LUA_TSTRING) ? lua_tostring(exL, -2) : NULL;
        if (k && strcmp(k, id_str) == 0)
        {
            found = 1;
            break;
        }
        lua_pop(exL, 1);
    }

    if (!found)
    {
        lua_settop(exL, top0);
        return 0;
    }

    // remove key, leaving value table on top and slot_info at -5
    lua_remove(exL, -2);

    for (i = 0; i < count; i++)
    {
        int edit_id = count + 100 + i;
        int ptype;

        lua_rawgeti(exL, -5, i);
        lua_getfield(exL, -1, "type");
        ptype = (int)lua_tointeger(exL, -1);

        switch (ptype)
        {
        case 0:
        {
            HWND ctrl = GetDlgItem(dlg_hwnd, edit_id);
            int len = GetWindowTextLengthA(ctrl);
            char *buf = (char *)rm_malloc((size_t)len + 1u);
            if (buf)
            {
                GetWindowTextA(ctrl, buf, len + 1);
                lua_pushstring(exL, buf);
                lua_setfield(exL, -3, "default");
                lua_pushstring(exL, buf);
                lua_rawseti(exL, -4, i);
                rm_free(buf);
            }
            break;
        }
        case 1:
        case 8:
        {
            int num_id = (ptype == 1) ? (count + edit_id) : edit_id;
            HWND ctrl = GetDlgItem(dlg_hwnd, num_id);
            int len = GetWindowTextLengthA(ctrl);
            char *buf = (char *)rm_malloc((size_t)len + 1u);
            if (buf)
            {
                GetWindowTextA(ctrl, buf, len + 1);
                lua_getfield(exL, LUA_GLOBALSINDEX, "tonumber");
                lua_pushstring(exL, buf);
                lua_call(exL, 1, 1);
                if (lua_type(exL, -1) == LUA_TNUMBER)
                {
                    lua_pop(exL, 1);
                    lua_pushstring(exL, buf);
                    lua_setfield(exL, -3, "default");
                    lua_pushstring(exL, buf);
                    lua_rawseti(exL, -4, i);
                }
                else
                {
                    lua_pop(exL, 1);
                }
                rm_free(buf);
            }
            break;
        }
        case 2:
        {
            HWND chk = GetDlgItem(dlg_hwnd, count * 3 + edit_id);
            LRESULT checked = SendMessageA(chk, BM_GETCHECK, 0, 0);
            const char *bstr = (checked != 0) ? "true" : "false";
            lua_pushstring(exL, bstr);
            lua_setfield(exL, -3, "default");
            lua_pushstring(exL, bstr);
            lua_rawseti(exL, -4, i);
            break;
        }
        case 3:
        case 5:
        case 6:
        case 7:
        {
            HWND ctrl = GetDlgItem(dlg_hwnd, edit_id);
            int len = GetWindowTextLengthA(ctrl);
            char *buf = (char *)rm_malloc((size_t)len + 3u);
            if (buf)
            {
                size_t slen;
                buf[0] = '\"';
                GetWindowTextA(ctrl, buf + 1, len + 1);
                slen = strlen(buf + 1);
                buf[slen + 1] = '\"';
                buf[slen + 2] = '\0';
                lua_pushstring(exL, buf);
                lua_setfield(exL, -3, "default");
                lua_pushstring(exL, buf);
                lua_rawseti(exL, -4, i);
                rm_free(buf);
            }
            break;
        }
        case 4:
        {
            HWND ctrl = GetDlgItem(dlg_hwnd, edit_id);
            int len = GetWindowTextLengthA(ctrl);
            char *buf = (char *)rm_malloc((size_t)len + 3u);
            if (buf)
            {
                size_t slen;
                buf[0] = '{';
                GetWindowTextA(ctrl, buf + 1, len + 1);
                slen = strlen(buf + 1);
                buf[slen + 1] = '}';
                buf[slen + 2] = '\0';
                lua_pushstring(exL, buf);
                lua_setfield(exL, -3, "default");
                lua_pushstring(exL, buf);
                lua_rawseti(exL, -4, i);
                rm_free(buf);
            }
            break;
        }
        default:
            break;
        }

        lua_pop(exL, 2);
    }

    lua_settop(exL, top0);
    return 1;
}

static void init_custom_param_dialog(HWND dlg_hwnd, int slot)
{
    lua_State **p_exL = (lua_State **)RikkyGetMemoryData(0x96);
    lua_State *exL = p_exL ? *p_exL : NULL;
    int top0;
    int count;
    int extra_h;
    int col_w;
    HINSTANCE hinst;
    HWND parent_hwnd;
    WPARAM hfont;
    RECT rc;
    POINT pt;
    int btn_id;
    int i;

    if (!exL)
    {
        return;
    }

    top0 = lua_gettop(exL);
    lua_getfield(exL, LUA_GLOBALSINDEX, "Rikky_Dialog_Infomation");
    lua_rawgeti(exL, -1, slot);
    lua_getfield(exL, -1, "number");
    count = (int)lua_tointeger(exL, -1);
    lua_pop(exL, 1);

    hinst = (HINSTANCE)(LONG_PTR)GetWindowLongPtrA(dlg_hwnd, GWLP_HINSTANCE);
    parent_hwnd = GetParent(dlg_hwnd);
    hfont = (WPARAM)SendMessageA(parent_hwnd, WM_GETFONT, 0, 0);
    SendMessageA(dlg_hwnd, WM_SETFONT, hfont, 0);

    GetWindowRect(dlg_hwnd, &rc);
    extra_h = count * 20;
    SetWindowPos(dlg_hwnd, NULL, 0, 0, rc.right - rc.left, (rc.bottom - rc.top) + extra_h,
                 SWP_NOMOVE | SWP_NOZORDER);

    GetClientRect(dlg_hwnd, &rc);
    col_w = rc.right / 3;

    for (btn_id = 1; btn_id <= 2; btn_id++)
    {
        HWND btn = GetDlgItem(dlg_hwnd, btn_id);
        GetWindowRect(btn, &rc);
        pt.x = rc.left;
        pt.y = rc.top;
        ScreenToClient(dlg_hwnd, &pt);
        SetWindowPos(btn, NULL, pt.x, pt.y + extra_h, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        SendMessageA(btn, WM_SETFONT, hfont, 0);
    }

    for (i = 0; i < count; i++)
    {
        int label_id = 100 + i;
        int edit_id = 100 + count + i;
        int y = label_id * 20 - 1999;
        int left_w = col_w - 10;
        int right_w = col_w * 2 - 10;
        const char *name_str;
        const char *def_str;
        int ptype;
        HWND left_ctrl;
        HWND right_ctrl = NULL;

        lua_rawgeti(exL, -1, i);
        lua_getfield(exL, -1, "name");
        name_str = lua_tostring(exL, -1);
        left_ctrl = CreateWindowExA(0, "STATIC", name_str ? name_str : "", 0x50000201u,
                                    10, y, left_w, 20, dlg_hwnd, (HMENU)(INT_PTR)label_id,
                                    hinst, NULL);

        lua_getfield(exL, -2, "type");
        lua_getfield(exL, -3, "default");
        ptype = (int)lua_tointeger(exL, -2);
        def_str = lua_tostring(exL, -1);

        switch (ptype)
        {
        case 0:
            right_ctrl = CreateWindowExA(0, "EDIT", def_str ? def_str : "", 0x50800084u,
                                         col_w, y, right_w, 20, dlg_hwnd,
                                         (HMENU)(INT_PTR)edit_id, hinst, NULL);
            break;

        case 1:
        {
            int scale;
            int val_is_num;
            int scaled_val = 0;
            int scaled_up;
            int scaled_down;
            int track_id = 100 + count * 3 + i;
            HWND track_hwnd;

            eval_lua_expr(exL);
            lua_getfield(exL, -5, "check");
            scale = (int)lua_tointeger(exL, -1);
            val_is_num = (lua_type(exL, -2) == LUA_TNUMBER);
            if (val_is_num)
            {
                scaled_val = (int)lrint((double)scale * lua_tonumber(exL, -2));
            }
            lua_getfield(exL, -6, "downlimit");
            lua_getfield(exL, -7, "uplimit");
            scaled_up = (int)lrint(lua_tonumber(exL, -1) * (double)scale);
            scaled_down = (int)lrint(lua_tonumber(exL, -2) * (double)scale);

            track_hwnd = CreateWindowExA(0, "msctls_trackbar32", "Slider1", 0x50000010u,
                                         col_w, y, (int)lrint((double)col_w * 1.25), 20,
                                         dlg_hwnd, (HMENU)(INT_PTR)track_id, hinst, NULL);
            SendMessageA(track_hwnd, TBM_SETRANGEMAX, 0, (LPARAM)(scaled_up - scaled_down));
            SendMessageA(track_hwnd, TBM_SETRANGEMIN, 1, 0);
            SendMessageA(track_hwnd, TBM_SETLINESIZE, 0, 1);
            SendMessageA(track_hwnd, TBM_SETPAGESIZE, 0, 1);
            if (val_is_num)
            {
                LPARAM pos = 0;
                if (scaled_val >= scaled_down)
                {
                    if (scaled_val >= scaled_up)
                    {
                        scaled_val = scaled_up;
                    }
                    pos = (LPARAM)(scaled_val - scaled_down);
                }
                SendMessageA(track_hwnd, TBM_SETPOS, 1, pos);
            }

            right_ctrl = CreateWindowExA(0, "EDIT", def_str ? def_str : "", 0x50800084u,
                                         (int)lrint((double)col_w * 2.25), y,
                                         (int)lrint((double)col_w * 0.75 - 10.0), 20,
                                         dlg_hwnd, (HMENU)(INT_PTR)(track_id - count),
                                         hinst, NULL);
            lua_pop(exL, 4);
            break;
        }

        case 2:
        {
            WPARAM checked;
            eval_lua_expr(exL);
            right_ctrl = CreateWindowExA(0, "BUTTON", NULL, 0x50000003u, col_w, y, 20, 20,
                                         dlg_hwnd, (HMENU)(INT_PTR)(100 + count * 4 + i),
                                         hinst, NULL);
            checked = (WPARAM)lua_toboolean(exL, -1);
            SendMessageA(right_ctrl, BM_SETCHECK, checked, 0);
            lua_pop(exL, 1);
            break;
        }

        case 5:
        case 6:
        case 7:
            ShowWindow(left_ctrl, SW_HIDE);
            left_ctrl = CreateWindowExA(0, "BUTTON", name_str ? name_str : "", 0x50000f00u,
                                        10, y, left_w, 20, dlg_hwnd,
                                        (HMENU)(INT_PTR)(100 + count * ptype + i),
                                        hinst, NULL);
            // fall through to strip quotes for edit text
        case 3:
        case 4:
        {
            size_t dlen = def_str ? strlen(def_str) : 0;
            char *inner = (char *)rm_malloc(dlen + 1u);
            if (inner)
            {
                if (dlen >= 2)
                {
                    memcpy(inner, def_str + 1, dlen - 2);
                    inner[dlen - 2] = '\0';
                }
                else
                {
                    inner[0] = '\0';
                }
                right_ctrl = CreateWindowExA(0, "EDIT", inner, 0x50800084u, col_w, y,
                                             right_w, 20, dlg_hwnd,
                                             (HMENU)(INT_PTR)edit_id, hinst, NULL);
                rm_free(inner);
            }
            break;
        }

        case 8:
            ShowWindow(left_ctrl, SW_HIDE);
            left_ctrl = CreateWindowExA(0, "BUTTON", name_str ? name_str : "", 0x50000f00u,
                                        10, y, left_w, 20, dlg_hwnd,
                                        (HMENU)(INT_PTR)(100 + count * 8 + i),
                                        hinst, NULL);
            right_ctrl = CreateWindowExA(0, "EDIT", def_str ? def_str : "", 0x50800084u,
                                         col_w, y, right_w, 20, dlg_hwnd,
                                         (HMENU)(INT_PTR)edit_id, hinst, NULL);
            break;

        default:
            break;
        }

        lua_pop(exL, 4);
        SendMessageA(left_ctrl, WM_SETFONT, hfont, 0);
        if (right_ctrl)
        {
            SendMessageA(right_ctrl, WM_SETFONT, hfont, 0);
        }
    }

    lua_settop(exL, top0);
}

static void custom_param_command(HWND dlg_hwnd, int slot, WPARAM wparam)
{
    UINT ctrl_id = (UINT)LOWORD(wparam);
    WORD code = HIWORD(wparam);
    lua_State **p_exL = (lua_State **)RikkyGetMemoryData(0x96);
    lua_State *exL = p_exL ? *p_exL : NULL;
    int top0;
    int count;

    if (!exL)
    {
        return;
    }

    top0 = lua_gettop(exL);
    lua_getfield(exL, LUA_GLOBALSINDEX, "Rikky_Dialog_Infomation");
    lua_rawgeti(exL, -1, slot);
    lua_getfield(exL, -1, "number");
    count = (int)lua_tointeger(exL, -1);

    if (count > 0)
    {
        UINT base5 = (UINT)(100 + count * 5);
        UINT base6 = (UINT)(100 + count * 6);
        UINT base7 = (UINT)(100 + count * 7);
        UINT base8 = (UINT)(100 + count * 8);
        UINT base9 = (UINT)(100 + count * 9);
        UINT base2 = (UINT)(100 + count * 2);
        UINT base3 = (UINT)(100 + count * 3);

        if (ctrl_id >= base5 && ctrl_id < base6)
        {
            int idx = (int)(ctrl_id - base5);
            HWND edit_hwnd = GetDlgItem(dlg_hwnd, 100 + count + idx);
            open_file_dialog(dlg_hwnd, edit_hwnd, 0);
        }
        else if (ctrl_id >= base6 && ctrl_id < base7)
        {
            int idx = (int)(ctrl_id - base6);
            HWND edit_hwnd = GetDlgItem(dlg_hwnd, 100 + count + idx);
            open_folder_dialog(dlg_hwnd, edit_hwnd, 0);
        }
        else if (ctrl_id >= base7 && ctrl_id < base8)
        {
            int idx = (int)(ctrl_id - base7);
            HWND edit_hwnd = GetDlgItem(dlg_hwnd, 100 + count + idx);
            char *font_name = (char *)run_dialog_box(100, k_dlg_template_100, dlg_hwnd,
                                                     font_dlg_proc, (LPARAM)edit_hwnd);
            if (font_name)
            {
                SetWindowTextA(edit_hwnd, font_name);
                rm_free(font_name);
            }
        }
        else if (ctrl_id >= base8 && ctrl_id < base9)
        {
            int idx = (int)(ctrl_id - base8);
            HWND edit_hwnd = GetDlgItem(dlg_hwnd, 100 + count + idx);
            open_color_dialog(dlg_hwnd, edit_hwnd, NULL);
        }
        else if (ctrl_id >= base2 && ctrl_id < base3 && code == EN_KILLFOCUS)
        {
            int idx = (int)(ctrl_id - base2);
            HWND edit_hwnd = GetDlgItem(dlg_hwnd, (int)ctrl_id);
            int len = GetWindowTextLengthA(edit_hwnd);
            char *buf = (char *)rm_malloc((size_t)len + 1u);
            if (buf)
            {
                GetWindowTextA(edit_hwnd, buf, len + 1);
                lua_getfield(exL, LUA_GLOBALSINDEX, "tonumber");
                lua_pushstring(exL, buf);
                lua_call(exL, 1, 1);
                if (lua_type(exL, -1) == LUA_TNUMBER)
                {
                    double val = lua_tonumber(exL, -1);
                    double scale_d, down_d, up_d, clamped, snapped;
                    int scale_i;
                    const char *formatted;
                    HWND track_hwnd;

                    lua_rawgeti(exL, -3, idx);
                    lua_getfield(exL, -1, "uplimit");
                    lua_getfield(exL, -2, "downlimit");
                    lua_getfield(exL, -3, "check");
                    scale_d = lua_tonumber(exL, -1);
                    down_d = lua_tonumber(exL, -2);
                    up_d = lua_tonumber(exL, -3);

                    if (val < down_d)
                    {
                        clamped = down_d;
                    }
                    else if (val > up_d)
                    {
                        clamped = up_d;
                    }
                    else
                    {
                        clamped = val;
                    }

                    scale_i = (int)lrint(scale_d);
                    snapped = (double)lrint(clamped * (double)scale_i) / (double)scale_i;

                    lua_getfield(exL, LUA_GLOBALSINDEX, "tostring");
                    lua_pushnumber(exL, snapped);
                    lua_call(exL, 1, 1);
                    formatted = lua_tostring(exL, -1);
                    if (formatted)
                    {
                        SetWindowTextA(edit_hwnd, formatted);
                    }

                    track_hwnd = GetDlgItem(dlg_hwnd, 100 + count * 3 + idx);
                    SendMessageA(track_hwnd, TBM_SETPOS, 1,
                                 (LPARAM)lrint((double)scale_i * (snapped - down_d)));
                }
                rm_free(buf);
            }
        }
    }

    lua_settop(exL, top0);
}

static void custom_param_hscroll(HWND dlg_hwnd, int slot, HWND track_hwnd, int scroll_code)
{
    lua_State **p_exL = (lua_State **)RikkyGetMemoryData(0x96);
    lua_State *exL = p_exL ? *p_exL : NULL;
    int top0;
    int count;
    int ctrl_id;
    int idx;
    double down_d;
    int scale;
    LRESULT pos;
    char text_buf[140];

    if (!exL)
    {
        return;
    }

    top0 = lua_gettop(exL);
    lua_getfield(exL, LUA_GLOBALSINDEX, "Rikky_Dialog_Infomation");
    lua_rawgeti(exL, -1, slot);
    lua_getfield(exL, -1, "number");
    count = (int)lua_tointeger(exL, -1);
    ctrl_id = GetDlgCtrlID(track_hwnd);

    if ((unsigned int)(scroll_code - 2) > 3u ||
        (((1u << (scroll_code - 2)) & 0xbu) == 0u) ||
        count < 1)
    {
        lua_settop(exL, top0);
        return;
    }

    idx = ctrl_id - (100 + count * 3);
    if (idx < 0 || idx >= count)
    {
        lua_settop(exL, top0);
        return;
    }

    lua_rawgeti(exL, -2, idx);
    lua_getfield(exL, -1, "uplimit");
    (void)lua_tonumber(exL, -1);
    lua_getfield(exL, -2, "downlimit");
    down_d = lua_tonumber(exL, -1);
    lua_getfield(exL, -3, "check");
    scale = (int)lua_tointeger(exL, -1);

    pos = SendMessageA(track_hwnd, TBM_GETPOS, 0, 0);
    sprintf(text_buf, "%g",
            (double)lrint((double)pos + down_d * (double)scale) / (double)scale);
    SetWindowTextA(GetDlgItem(dlg_hwnd, 100 + count * 2 + idx), text_buf);
    lua_settop(exL, top0);
}

static INT_PTR CALLBACK custom_param_dlg_proc(HWND dlg_hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    if (msg == WM_INITDIALOG)
    {
        g_param_dlg_slot = (int)lparam;
        init_custom_param_dialog(dlg_hwnd, g_param_dlg_slot);
        return 0;
    }

    if (msg == WM_HSCROLL)
    {
        custom_param_hscroll(dlg_hwnd, g_param_dlg_slot, (HWND)lparam, (int)LOWORD(wparam));
        return 0;
    }

    if (msg == WM_COMMAND)
    {
        UINT id = (UINT)LOWORD(wparam);
        if (id == IDOK)
        {
            EndDialog(dlg_hwnd, accept_custom_param_dialog(dlg_hwnd, g_param_dlg_slot));
            return 1;
        }
        if (id == IDCANCEL)
        {
            EndDialog(dlg_hwnd, -1);
            return 1;
        }
        custom_param_command(dlg_hwnd, g_param_dlg_slot, wparam);
        return 0;
    }

    if (msg == WM_CLOSE)
    {
        EndDialog(dlg_hwnd, -1);
        return 1;
    }

    return 0;
}

static void handle_exedit_param_btn(HWND dlg_hwnd, int ctrl_id)
{
    int slot = ctrl_id - 301;
    uint8_t flag;
    HWND edit_hwnd;
    int high_flag;

    if (slot < 0 || slot >= 16)
    {
        return;
    }

    flag = g_slot_flags[slot];
    edit_hwnd = GetDlgItem(dlg_hwnd, ctrl_id - 130);
    high_flag = ((flag >> 4) != 0) ? 1 : 0;

    switch (flag & 0x0fu)
    {
    case 1:
        open_file_dialog(dlg_hwnd, edit_hwnd, high_flag);
        break;

    case 2:
        open_folder_dialog(dlg_hwnd, edit_hwnd, high_flag);
        break;

    case 3:
    {
        char *font_name = (char *)run_dialog_box(100, k_dlg_template_100, dlg_hwnd,
                                                 font_dlg_proc, (LPARAM)edit_hwnd);
        if (font_name)
        {
            SetWindowTextA(edit_hwnd, font_name);
            rm_free(font_name);
        }
        break;
    }

    case 4:
    {
        RmListDlgCtx ctx;
        INT_PTR sel;

        ctx.edit_hwnd = edit_hwnd;
        ctx.slot = &g_list_slots[slot];
        sel = run_dialog_box(200, k_dlg_template_200, dlg_hwnd, list_dlg_proc, (LPARAM)&ctx);
        if (sel != -1 && sel >= 0 && sel < g_list_slots[slot].count)
        {
            if (high_flag == 0)
            {
                SetWindowTextA(edit_hwnd, g_list_slots[slot].items[sel]);
            }
            else
            {
                char num_buf[64];
                sprintf(num_buf, "%d", (int)sel + 1);
                SetWindowTextA(edit_hwnd, num_buf);
            }
        }
        break;
    }

    case 5:
        run_dialog_box(300, k_dlg_template_300, dlg_hwnd, custom_param_dlg_proc, (LPARAM)slot);
        break;

    case 6:
        open_color_dialog(dlg_hwnd, edit_hwnd, high_flag ? g_custom_palettes[slot] : NULL);
        break;

    default:
        break;
    }
}

static void toggle_exedit_checkbox(HWND edit_hwnd, HWND chk_hwnd)
{
    LRESULT cur = SendMessageA(chk_hwnd, BM_GETCHECK, 0, 0);
    SendMessageA(chk_hwnd, BM_SETCHECK, (WPARAM)(cur == 0), 0);
    SetWindowTextA(edit_hwnd, (cur == 0) ? "1" : "0");
}

static void clone_custom_param_id(HWND edit_hwnd, int slot)
{
    char old_id[268];
    char new_id[16];
    lua_State **p_exL = (lua_State **)RikkyGetMemoryData(0x96);
    lua_State *exL = p_exL ? *p_exL : NULL;
    int top0;
    int count;
    int salt = 0x61;
    int i;

    if (!exL || !edit_hwnd)
    {
        return;
    }

    old_id[0] = '\0';
    GetWindowTextA(edit_hwnd, old_id, 256);

    top0 = lua_gettop(exL);
    lua_getfield(exL, LUA_GLOBALSINDEX, "Rikky_Dialog_Infomation");
    if (lua_type(exL, -1) != LUA_TTABLE)
    {
        lua_settop(exL, top0);
        return;
    }

    lua_rawgeti(exL, -1, slot);
    if (lua_type(exL, -1) != LUA_TTABLE)
    {
        lua_settop(exL, top0);
        return;
    }

    lua_getfield(exL, -1, "number");
    count = (int)lua_tointeger(exL, -1);
    lua_pop(exL, 1);

    lua_getfield(exL, LUA_GLOBALSINDEX, "param_Rikky_custom_dialog");
    if (lua_type(exL, -1) != LUA_TTABLE)
    {
        lua_settop(exL, top0);
        return;
    }

    for (;;)
    {
        unsigned int r0, r1, r2, r3, r4;
        time_t now = time(NULL);

        srand((unsigned int)((int)now + salt));
        r0 = (unsigned int)rand();
        r1 = (unsigned int)rand();
        r2 = (unsigned int)rand();
        r3 = (unsigned int)rand();
        r4 = (unsigned int)rand();
        sprintf(new_id, "%02X%02X%02X%02X%02X",
                r4 & 0xffu, r0 & 0xffu, r2 & 0xffu, r1 & 0xffu, r3 & 0xffu);

        if (strcmp(new_id, old_id) != 0)
        {
            int existing_type;
            lua_getfield(exL, -1, new_id);
            existing_type = lua_type(exL, -1);
            lua_pop(exL, 1);
            if (existing_type != LUA_TTABLE)
            {
                break;
            }
        }
        salt += 0x61;
    }

    // copy saved parameter entries from old_id (or slot defaults) into param_Rikky_custom_dialog[new_id]
    lua_getfield(exL, -1, old_id);
    lua_createtable(exL, 0, 0);
    if (lua_type(exL, -2) == LUA_TTABLE)
    {
        for (i = 0; i < count; i++)
        {
            lua_rawgeti(exL, -2, i);
            lua_rawseti(exL, -2, i);
        }
    }
    else
    {
        for (i = 0; i < count; i++)
        {
            lua_rawgeti(exL, -4, i);
            if (lua_type(exL, -1) == LUA_TTABLE)
            {
                lua_getfield(exL, -1, "default");
                lua_rawseti(exL, -3, i);
            }
            lua_pop(exL, 1);
        }
    }
    lua_setfield(exL, -3, new_id);
    lua_pop(exL, 1);

    // update active slot ID before SetWindowTextA triggers EN_CHANGE
    lua_pushstring(exL, new_id);
    lua_setfield(exL, -3, "ID");
    lua_settop(exL, top0);

    SetWindowTextA(edit_hwnd, new_id);
}

static LRESULT CALLBACK exedit_param_dlg_subclass_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    if (msg == WM_COMMAND)
    {
        UINT ctrl_id = (UINT)LOWORD(wparam);
        if (ctrl_id >= 301u && ctrl_id <= 316u)
        {
            handle_exedit_param_btn(hwnd, (int)ctrl_id);
        }
        else if (ctrl_id >= 501u && ctrl_id <= 516u)
        {
            HWND edit_hwnd = GetDlgItem(hwnd, (int)(ctrl_id - 330u));
            toggle_exedit_checkbox(edit_hwnd, (HWND)lparam);
        }
        else if (ctrl_id >= 701u && ctrl_id <= 716u)
        {
            HWND edit_hwnd = GetDlgItem(hwnd, (int)(ctrl_id - 530u));
            clone_custom_param_id(edit_hwnd, (int)(ctrl_id - 701u));
        }
    }

    return CallWindowProcA(g_orig_param_dlg_proc, hwnd, msg, wparam, lparam);
}

static void free_list_slot(RmListSlot *slot)
{
    int i;
    if (!slot || !slot->items)
    {
        return;
    }
    for (i = 0; i < slot->count; i++)
    {
        rm_free(slot->items[i]);
        slot->items[i] = NULL;
    }
    rm_free(slot->items);
    slot->items = NULL;
    slot->count = 0;
}

static void install_exedit_controls(lua_State *L, int kind, int is_cs)
{
    int *p_flag7d;
    int *p_dlg_target;
    int *p_cur_target;
    HWND *p_dlg_hwnd;
    int dlg_target;
    int cur_target;
    HWND dlg_hwnd;
    char title_buf[256];
    int arg;
    int top;

    if (!rm_check_exedit_ver())
    {
        return;
    }

    p_flag7d = (int *)RikkyGetMemoryData(0x7d);
    if (p_flag7d)
    {
        *p_flag7d = 1;
    }

    p_dlg_target = (int *)RikkyGetMemoryData(0xa0);
    dlg_target = p_dlg_target ? *p_dlg_target : 0;
    if (RikkyGetMemoryData(0xa5) == NULL)
    {
        (void)RikkyGetMemoryData(0xa6);
    }

    p_cur_target = (int *)RikkyGetMemoryData(is_cs ? 0x9a : 0x99);
    cur_target = p_cur_target ? *p_cur_target : 0;

    if (dlg_target == 0)
    {
        memset(g_slot_flags, 0, sizeof(g_slot_flags));
        g_orig_param_dlg_proc = NULL;
        return;
    }

    if (dlg_target != cur_target)
    {
        return;
    }

    p_dlg_hwnd = (HWND *)RikkyGetMemoryData(0xa1);
    dlg_hwnd = p_dlg_hwnd ? *p_dlg_hwnd : NULL;
    if (!dlg_hwnd)
    {
        return;
    }

    title_buf[0] = '\0';
    GetWindowTextA(dlg_hwnd, title_buf, 256);
    if (strcmp(title_buf, k_param_dlg_title) != 0)
    {
        return;
    }

    arg = 1;
    top = lua_gettop(L);
    while (arg <= top)
    {
        int slot;
        int next_arg;
        int has_custom_pal = 0;

        if (lua_type(L, arg) != LUA_TNUMBER)
        {
            arg++;
            continue;
        }

        slot = (int)lrint(lua_tonumber(L, arg));
        next_arg = arg + 1;
        if (next_arg <= top && lua_type(L, next_arg) == LUA_TSTRING)
        {
            next_arg++;
        }

        if (kind == 3)
        {
            if (next_arg <= top && lua_type(L, next_arg) == LUA_TTABLE)
            {
                int tbl_idx = next_arg;
                next_arg++;

                if ((unsigned int)(slot - 1) < 16u)
                {
                    RmListSlot *ls = &g_list_slots[slot - 1];
                    int count = (int)lua_objlen(L, tbl_idx);
                    int k;

                    free_list_slot(ls);
                    if (count <= 0)
                    {
                        arg = next_arg;
                        continue;
                    }

                    ls->items = (char **)rm_malloc((size_t)count * sizeof(char *));
                    if (!ls->items)
                    {
                        arg = next_arg;
                        continue;
                    }
                    ls->count = count;

                    for (k = 0; k < count; k++)
                    {
                        const char *item_str;
                        size_t item_len;

                        lua_rawgeti(L, tbl_idx, k + 1);
                        item_str = lua_tostring(L, -1);
                        if (!item_str)
                        {
                            item_str = "";
                        }
                        item_len = strlen(item_str);
                        ls->items[k] = (char *)rm_malloc(item_len + 1u);
                        if (!ls->items[k])
                        {
                            lua_pop(L, 1);
                            ls->count = k;
                            free_list_slot(ls);
                            break;
                        }
                        memcpy(ls->items[k], item_str, item_len + 1u);
                        lua_pop(L, 1);
                    }

                    if (!ls->items)
                    {
                        arg = next_arg;
                        continue;
                    }
                }
            }
        }
        else if (kind == 5)
        {
            if (next_arg <= top && lua_type(L, next_arg) == LUA_TTABLE)
            {
                int tbl_idx = next_arg;
                next_arg++;

                if ((unsigned int)(slot - 1) < 16u)
                {
                    int k;
                    for (k = 1; k <= 32; k++)
                    {
                        lua_rawgeti(L, tbl_idx, k);
                        g_custom_palettes[slot - 1][k - 1] = (uint32_t)lua_tointeger(L, -1);
                        lua_pop(L, 1);
                    }
                    has_custom_pal = 1;
                }
            }
        }

        if ((unsigned int)(slot - 1) < 16u)
        {
            if (kind == 6)
            {
                if (GetDlgItem(dlg_hwnd, slot + 500) == NULL)
                {
                    HWND edit_hwnd = GetDlgItem(dlg_hwnd, slot + 0xaa);
                    char edit_buf[264];
                    RECT rc;
                    POINT pt;
                    int h;
                    HINSTANCE hinst;
                    HWND chk_hwnd;
                    int is_one = 0;

                    edit_buf[0] = '\0';
                    GetWindowTextA(edit_hwnd, edit_buf, 261);
                    GetWindowRect(edit_hwnd, &rc);
                    pt.x = rc.left;
                    pt.y = rc.top;
                    h = rc.bottom - rc.top;
                    ScreenToClient(dlg_hwnd, &pt);
                    hinst = (HINSTANCE)(LONG_PTR)GetWindowLongPtrA(dlg_hwnd, GWLP_HINSTANCE);
                    chk_hwnd = CreateWindowExA(0, "BUTTON", "", 0x50000002u,
                                               pt.x, pt.y, 18, h, dlg_hwnd,
                                               (HMENU)(INT_PTR)(slot + 500), hinst, NULL);

                    if (edit_buf[0] != '-')
                    {
                        int parsed;
                        if (strlen(edit_buf) >= 3 && edit_buf[0] == '0' && edit_buf[1] == 'x')
                        {
                            parsed = (int)strtol(edit_buf, NULL, 0);
                        }
                        else
                        {
                            parsed = atoi(edit_buf);
                        }
                        if (parsed == 1)
                        {
                            is_one = 1;
                        }
                    }

                    SendMessageA(chk_hwnd, BM_SETCHECK, is_one ? 1 : 0, 0);
                    SetWindowTextA(edit_hwnd, is_one ? "1" : "0");
                    (void)GetWindowLongPtrA(edit_hwnd, GWL_STYLE);
                    SendMessageA(edit_hwnd, EM_SETREADONLY, 1, 0);
                    SetWindowPos(edit_hwnd, NULL, pt.x + 18, pt.y,
                                 (rc.right - rc.left) - 18, h,
                                 SWP_NOZORDER | SWP_FRAMECHANGED);

                    g_slot_flags[slot - 1] = 7;
                    if (!g_orig_param_dlg_proc)
                    {
                        g_orig_param_dlg_proc = (WNDPROC)(LONG_PTR)SetWindowLongPtrA(
                            dlg_hwnd, GWLP_WNDPROC, (LONG_PTR)exedit_param_dlg_subclass_proc);
                    }
                }
            }
            else if (GetDlgItem(dlg_hwnd, slot + 300) == NULL)
            {
                HWND lbl_hwnd = GetDlgItem(dlg_hwnd, slot + 0x82);
                char label_buf[264];
                RECT rc;
                POINT pt;
                int w, h;
                WPARAM hfont;
                HINSTANCE hinst;
                HWND btn_hwnd;

                label_buf[0] = '\0';
                label_buf[1] = '\0';
                GetWindowTextA(lbl_hwnd, label_buf, 261);
                GetWindowRect(lbl_hwnd, &rc);
                pt.x = rc.left;
                pt.y = rc.top;
                w = rc.right - rc.left;
                h = rc.bottom - rc.top;
                hfont = (WPARAM)SendMessageA(lbl_hwnd, WM_GETFONT, 0, 0);
                ScreenToClient(dlg_hwnd, &pt);
                ShowWindow(lbl_hwnd, SW_HIDE);
                hinst = (HINSTANCE)(LONG_PTR)GetWindowLongPtrA(dlg_hwnd, GWLP_HINSTANCE);
                btn_hwnd = CreateWindowExA(0, "BUTTON", label_buf, 0x50000f00u,
                                           pt.x - 4, pt.y - 2, w + 4, h + 4, dlg_hwnd,
                                           (HMENU)(INT_PTR)(slot + 300), hinst, NULL);
                SendMessageA(btn_hwnd, WM_SETFONT, hfont, 0);

                if (kind == 5)
                {
                    g_slot_flags[slot - 1] = (uint8_t)((has_custom_pal ? 0x10u : 0u) | 6u);
                }
                else
                {
                    uint8_t star = (label_buf[0] == '*' ||
                                    (label_buf[0] == '$' && label_buf[1] == '*'))
                                       ? 0x10u
                                       : 0u;
                    g_slot_flags[slot - 1] = (uint8_t)(star | (uint8_t)(kind + 1));
                }

                if (!g_orig_param_dlg_proc)
                {
                    g_orig_param_dlg_proc = (WNDPROC)(LONG_PTR)SetWindowLongPtrA(
                        dlg_hwnd, GWLP_WNDPROC, (LONG_PTR)exedit_param_dlg_subclass_proc);
                }
            }
        }

        arg = next_arg;
    }
}

static void ensure_datasave_param_entry(lua_State *exL)
{
    int len;
    int i;
    int exists = 0;

    rm_ensure_datasave_registered(exL);

    lua_getfield(exL, LUA_GLOBALSINDEX, "Rikky_datasave_Table");
    if (lua_type(exL, -1) != LUA_TTABLE)
    {
        lua_pop(exL, 1);
        lua_createtable(exL, 0, 0);
        lua_setfield(exL, LUA_GLOBALSINDEX, "Rikky_datasave_Table");
        lua_getfield(exL, LUA_GLOBALSINDEX, "Rikky_datasave_Table");
    }

    len = (int)lua_objlen(exL, -1);
    for (i = 1; i <= len; i++)
    {
        const char *entry;
        lua_rawgeti(exL, -1, i);
        entry = lua_tostring(exL, -1);
        if (entry && strcmp(entry, "param_Rikky_custom_dialog") == 0)
        {
            lua_pop(exL, 1);
            exists = 1;
            break;
        }
        lua_pop(exL, 1);
    }

    if (!exists)
    {
        lua_pushstring(exL, "param_Rikky_custom_dialog");
        lua_rawseti(exL, -2, len + 1);
    }
    lua_pop(exL, 1);

    lua_getfield(exL, LUA_GLOBALSINDEX, "param_Rikky_custom_dialog");
    if (lua_type(exL, -1) != LUA_TTABLE)
    {
        lua_pop(exL, 1);
        lua_createtable(exL, 0, 0);
        lua_setfield(exL, LUA_GLOBALSINDEX, "param_Rikky_custom_dialog");
        lua_getfield(exL, LUA_GLOBALSINDEX, "param_Rikky_custom_dialog");
    }
}

static int strip_tag_suffix(char *name, const char *tag)
{
    char *hit = strstr(name, tag);
    if (hit)
    {
        *hit = '\0';
        return 1;
    }
    return 0;
}

static int parse_precision_suffix(char *name)
{
    char *hit = strstr(name, "*0.");
    char c;
    int pos;
    int digits = 0;

    if (!hit)
    {
        return 0;
    }

    c = hit[3];
    if (c == '\0')
    {
        return 0;
    }

    if (c == '1')
    {
        pos = 3;
    }
    else if (c == '0')
    {
        pos = 3;
        for (;;)
        {
            pos++;
            c = hit[pos];
            if (c == '\0')
            {
                return 0;
            }
            if (c == '1')
            {
                break;
            }
            if (c != '0')
            {
                return 0;
            }
        }
    }
    else
    {
        return 0;
    }

    if (hit[pos + 1] == '\0')
    {
        int capped = (pos < 6) ? pos : 5;
        digits = capped - 2;
        *hit = '\0';
    }

    return digits;
}

static int custom_parameter_impl(lua_State *L, int is_cs)
{
    const char *raw_id = lua_tostring(L, 1);
    int slot = (int)lua_tointeger(L, 2);
    size_t desc_len;
    int param_count;
    char id_buf[272];
    int *p_flag7d;
    int *p_dlg_target;
    int dlg_target;
    lua_State **p_exL;
    lua_State *exL;
    int p;

    if (!raw_id || lua_type(L, 3) != LUA_TTABLE)
    {
        return 0;
    }

    desc_len = lua_objlen(L, 3);
    if (desc_len == 0 || (desc_len & 3u) != 0 || (unsigned int)(slot - 1) > 15u)
    {
        return 0;
    }

    if (!rm_check_exedit_ver())
    {
        return 0;
    }

    strncpy(id_buf, raw_id, sizeof(id_buf) - 1u);
    id_buf[sizeof(id_buf) - 1u] = '\0';

    lua_remove(L, 1);
    lua_remove(L, 1);
    lua_settop(L, 1);

    p_flag7d = (int *)RikkyGetMemoryData(0x7d);
    if (p_flag7d)
    {
        *p_flag7d = 1;
    }

    p_dlg_target = (int *)RikkyGetMemoryData(0xa0);
    dlg_target = p_dlg_target ? *p_dlg_target : 0;
    if (RikkyGetMemoryData(0xa5) == NULL)
    {
        (void)RikkyGetMemoryData(0xa6);
    }

    param_count = (int)(desc_len >> 2);
    p_exL = (lua_State **)RikkyGetMemoryData(0x96);
    exL = p_exL ? *p_exL : NULL;
    if (!exL)
    {
        return 0;
    }

    if (L == exL)
    {
        int *p_cur_target = (int *)RikkyGetMemoryData(is_cs ? 0x9a : 0x99);
        int cur_target = p_cur_target ? *p_cur_target : 0;
        HWND dlg_hwnd = NULL;
        int use_active_dlg = 0;

        ensure_datasave_param_entry(L);

        if (dlg_target == 0)
        {
            g_orig_param_dlg_proc = NULL;
            memset(g_slot_flags, 0, sizeof(g_slot_flags));
        }
        else if (dlg_target == cur_target)
        {
            HWND *p_dlg_hwnd = (HWND *)RikkyGetMemoryData(0xa1);
            char title_buf[256];
            dlg_hwnd = p_dlg_hwnd ? *p_dlg_hwnd : NULL;
            if (dlg_hwnd)
            {
                title_buf[0] = '\0';
                GetWindowTextA(dlg_hwnd, title_buf, 256);
                if (strcmp(title_buf, k_param_dlg_title) == 0)
                {
                    use_active_dlg = 1;
                }
            }
        }

        if (use_active_dlg)
        {
            int slot_idx = slot - 1;

            if (id_buf[0] == '\0')
            {
                uintptr_t stride = (uintptr_t)RikkyGetMemoryData(0x6d);
                uintptr_t *p_base66 = (uintptr_t *)RikkyGetMemoryData(0x66);
                uintptr_t base66 = p_base66 ? *p_base66 : 0;
                int s1 = base66 ? (*(int *)(base66 + stride - 8u) * 0x67) : 0;
                int s2 = base66 ? (*(int *)(base66 + stride - 4u) * 0x35 + (int)base66) : 0;
                int s3 = base66 ? (*(int *)(base66 + 8u) * 0x6b) : 0;
                int salt = 0;
                int existing_type;

                do
                {
                    unsigned int r0, r1, r2, r3, r4;
                    time_t t0 = time(NULL);
                    srand((unsigned int)((int)t0 + s2 + s1 + s3 + salt));
                    r0 = (unsigned int)rand();
                    r1 = (unsigned int)rand();
                    t0 = time(NULL);
                    srand((unsigned int)((int)t0 * 11 + s2 + salt));
                    r2 = (unsigned int)rand();
                    t0 = time(NULL);
                    srand((unsigned int)((int)t0 * 13 + s1 + (int)base66 + salt));
                    r3 = (unsigned int)rand();
                    t0 = time(NULL);
                    srand((unsigned int)((int)t0 * 17 + (int)base66 + s3 + salt));
                    salt += 0x61;
                    r4 = (unsigned int)rand();

                    sprintf(id_buf, "%02X%02X%02X%02X%02X",
                            r4 & 0xffu, r0 & 0xffu, r2 & 0xffu, r1 & 0xffu, r3 & 0xffu);
                    lua_getfield(L, -1, id_buf);
                    existing_type = lua_type(L, -1);
                    lua_pop(L, 1);
                } while (existing_type == LUA_TTABLE);

                SetWindowTextA(GetDlgItem(dlg_hwnd, slot + 0xaa), id_buf);
            }

            lua_getfield(L, -1, id_buf);
            if (lua_type(L, -1) != LUA_TTABLE)
            {
                lua_pop(L, 1);
                lua_createtable(L, 0, 0);
                for (p = 0; p < param_count; p++)
                {
                    lua_rawgeti(L, 1, p * 4 + 2);
                    lua_rawseti(L, -2, p);
                }
                lua_setfield(L, -2, id_buf);
                lua_getfield(L, -1, id_buf);
            }

            lua_getfield(L, LUA_GLOBALSINDEX, "Rikky_Dialog_Infomation");
            if (lua_type(L, -1) != LUA_TTABLE)
            {
                lua_pop(L, 1);
                lua_createtable(L, 0, 0);
                lua_setfield(L, LUA_GLOBALSINDEX, "Rikky_Dialog_Infomation");
                lua_getfield(L, LUA_GLOBALSINDEX, "Rikky_Dialog_Infomation");
            }

            lua_rawgeti(L, -1, slot_idx);
            if (lua_type(L, -1) != LUA_TTABLE)
            {
                lua_pop(L, 1);
                lua_createtable(L, 0, 0);
                lua_rawseti(L, -2, slot_idx);
                lua_rawgeti(L, -1, slot_idx);
            }

            lua_pushinteger(L, param_count);
            lua_setfield(L, -2, "number");
            lua_pushstring(L, id_buf);
            lua_setfield(L, -2, "ID");

            for (p = 0; p < param_count; p++)
            {
                int base = p * 4;
                int item_idx;
                const char *raw_name;
                char name_buf[272];
                int def_type;
                int ptype;

                lua_createtable(L, 0, 0);
                item_idx = lua_gettop(L);

                // item_idx + 1: def_expr, item_idx + 2: def_val
                lua_rawgeti(L, 1, base + 2);
                eval_lua_expr(L);

                // item_idx + 3: saved_expr, item_idx + 4: saved_val
                lua_rawgeti(L, 3, p);
                eval_lua_expr(L);

                lua_rawgeti(L, 1, base + 1);
                raw_name = lua_tostring(L, -1);
                if (raw_name)
                {
                    strncpy(name_buf, raw_name, sizeof(name_buf) - 1u);
                    name_buf[sizeof(name_buf) - 1u] = '\0';
                }
                else
                {
                    name_buf[0] = '\0';
                }
                lua_pop(L, 1);

                def_type = lua_type(L, item_idx + 2);
                if (def_type == LUA_TSTRING)
                {
                    if (strip_tag_suffix(name_buf, "*file"))
                    {
                        ptype = 5;
                    }
                    else if (strip_tag_suffix(name_buf, "*folder"))
                    {
                        ptype = 6;
                    }
                    else if (strip_tag_suffix(name_buf, "*font"))
                    {
                        ptype = 7;
                    }
                    else
                    {
                        ptype = 3;
                    }
                    lua_pushstring(L, name_buf);
                    lua_setfield(L, item_idx, "name");
                }
                else if (def_type == LUA_TTABLE)
                {
                    ptype = 4;
                    lua_pushstring(L, name_buf);
                    lua_setfield(L, item_idx, "name");
                }
                else if (def_type == LUA_TBOOLEAN)
                {
                    ptype = 2;
                    lua_pushstring(L, name_buf);
                    lua_setfield(L, item_idx, "name");
                }
                else if (strip_tag_suffix(name_buf, "*color"))
                {
                    ptype = 8;
                    lua_pushstring(L, name_buf);
                    lua_setfield(L, item_idx, "name");
                }
                else
                {
                    int digits = parse_precision_suffix(name_buf);
                    lua_pushstring(L, name_buf);
                    lua_setfield(L, item_idx, "name");

                    lua_rawgeti(L, 1, base + 3);
                    lua_rawgeti(L, 1, base + 4);
                    if (def_type == LUA_TNUMBER &&
                        lua_type(L, -2) == LUA_TNUMBER &&
                        lua_type(L, -1) == LUA_TNUMBER &&
                        lua_tonumber(L, -1) > lua_tonumber(L, -2))
                    {
                        int scale = 1;
                        int d;
                        for (d = 0; d < digits; d++)
                        {
                            scale *= 10;
                        }
                        lua_pushinteger(L, scale);
                        lua_setfield(L, item_idx, "check");
                        lua_setfield(L, item_idx, "uplimit");
                        lua_setfield(L, item_idx, "downlimit");
                        ptype = 1;
                    }
                    else
                    {
                        lua_pop(L, 2);
                        ptype = 0;
                    }
                }

                lua_pushinteger(L, ptype);
                lua_setfield(L, item_idx, "type");
                lua_pushvalue(L, item_idx + 3);
                lua_setfield(L, item_idx, "default");

                lua_settop(L, item_idx);
                lua_rawseti(L, -2, p);
            }

            // pop slot_info and Rikky_Dialog_Infomation, leaving val_tbl on top
            lua_pop(L, 2);

            if (GetDlgItem(dlg_hwnd, slot + 300) == NULL)
            {
                HWND lbl_hwnd = GetDlgItem(dlg_hwnd, slot + 0x82);
                HWND edit_hwnd;
                char label_buf[272];
                RECT rc;
                POINT pt;
                int w, h;
                WPARAM hfont;
                HINSTANCE hinst;
                HWND btn_hwnd;
                HWND c_btn_hwnd;

                label_buf[0] = '\0';
                GetWindowTextA(lbl_hwnd, label_buf, 261);
                GetWindowRect(lbl_hwnd, &rc);
                w = rc.right - rc.left;
                h = rc.bottom - rc.top;
                pt.x = rc.left;
                pt.y = rc.top;
                hfont = (WPARAM)SendMessageA(lbl_hwnd, WM_GETFONT, 0, 0);
                ScreenToClient(dlg_hwnd, &pt);
                ShowWindow(lbl_hwnd, SW_HIDE);
                hinst = (HINSTANCE)(LONG_PTR)GetWindowLongPtrA(dlg_hwnd, GWLP_HINSTANCE);
                btn_hwnd = CreateWindowExA(0, "BUTTON", label_buf, 0x50000f00u,
                                           pt.x - 4, pt.y - 2, w + 4, h + 4, dlg_hwnd,
                                           (HMENU)(INT_PTR)(slot + 300), hinst, NULL);
                SendMessageA(btn_hwnd, WM_SETFONT, hfont, 0);

                edit_hwnd = GetDlgItem(dlg_hwnd, slot + 0xaa);
                GetWindowRect(edit_hwnd, &rc);
                h = rc.bottom - rc.top;
                pt.x = rc.right - h;
                pt.y = rc.top;
                ScreenToClient(dlg_hwnd, &pt);
                SendMessageA(edit_hwnd, EM_SETREADONLY, 1, 0);
                SetWindowPos(edit_hwnd, NULL, 0, 0, (rc.right - rc.left) - h, h,
                             SWP_NOMOVE | SWP_NOZORDER | SWP_FRAMECHANGED);
                c_btn_hwnd = CreateWindowExA(0, "BUTTON", "C", 0x50000f00u,
                                             pt.x, pt.y, h, h, dlg_hwnd,
                                             (HMENU)(INT_PTR)(slot + 700), hinst, NULL);
                SendMessageA(c_btn_hwnd, WM_SETFONT, hfont, 0);

                g_slot_flags[slot_idx] = 5;
                if (!g_orig_param_dlg_proc)
                {
                    g_orig_param_dlg_proc = (WNDPROC)(LONG_PTR)SetWindowLongPtrA(
                        dlg_hwnd, GWLP_WNDPROC, (LONG_PTR)exedit_param_dlg_subclass_proc);
                }
            }
        }
        else if (id_buf[0] == '\0')
        {
            lua_createtable(L, 0, 0);
            for (p = 0; p < param_count; p++)
            {
                lua_rawgeti(L, 1, p * 4 + 2);
                lua_rawseti(L, -2, p);
            }
        }
        else
        {
            lua_getfield(L, -1, id_buf);
            if (lua_type(L, -1) != LUA_TTABLE)
            {
                lua_pop(L, 1);
                lua_createtable(L, 0, 0);
                for (p = 0; p < param_count; p++)
                {
                    lua_rawgeti(L, 1, p * 4 + 2);
                    lua_rawseti(L, -2, p);
                }
                lua_setfield(L, -2, id_buf);
                lua_getfield(L, -1, id_buf);
            }
        }

        lua_insert(L, 1);
        lua_settop(L, 1);
    }
    else
    {
        int ex_top0 = lua_gettop(exL);
        int copied = 0;

        ensure_datasave_param_entry(exL);
        lua_createtable(L, 0, 0);

        if (id_buf[0] != '\0')
        {
            lua_getfield(exL, -1, id_buf);
            if (lua_type(exL, -1) == LUA_TTABLE)
            {
                for (p = 0; p < param_count; p++)
                {
                    const char *val_str;
                    lua_rawgeti(exL, -1, p);
                    val_str = lua_tostring(exL, -1);
                    lua_pushstring(L, val_str ? val_str : "");
                    lua_rawseti(L, -2, p);
                    lua_pop(exL, 1);
                }
                copied = 1;
            }
        }

        if (!copied)
        {
            for (p = 0; p < param_count; p++)
            {
                lua_rawgeti(L, 1, p * 4 + 2);
                lua_rawseti(L, -2, p);
            }
        }

        lua_settop(exL, ex_top0);
        lua_insert(L, 1);
        lua_settop(L, 1);
    }

    for (p = 0; p < param_count; p++)
    {
        lua_rawgeti(L, 1, p);
        eval_lua_expr(L);
        lua_remove(L, -2);
    }

    lua_remove(L, 1);
    return param_count;
}

int __cdecl lua_rikky_trackbarwrite(lua_State *L)
{
    int *p_sel_obj = (int *)RikkyGetMemoryData(0x6b);
    uint32_t *p_cur_target = (uint32_t *)RikkyGetMemoryData(0x99);
    int sel_obj = p_sel_obj ? *p_sel_obj : -1;
    uint32_t cur_target = p_cur_target ? *p_cur_target : 0;
    int updated = 0;

    if (sel_obj != -1 && (cur_target & 0xffffu) == (uint32_t)(sel_obj + 1))
    {
        uintptr_t obj_stride = (uintptr_t)RikkyGetMemoryData(0x6d);
        uintptr_t *p_obj_table = (uintptr_t *)RikkyGetMemoryData(0x6c);
        uintptr_t obj_ptr = obj_stride * (uintptr_t)sel_obj + (p_obj_table ? *p_obj_table : 0);
        int track_idx = (int)lua_tointeger(L, 1);
        int new_raw;
        int dlg_track_off;
        int obj_track_off;
        uint32_t filt_idx;
        int abs_track;
        int *p_obj_val;
        int *p_dlg_val;
        uintptr_t exedit_ver;

        if (track_idx > 3)
        {
            track_idx = 3;
        }
        if (track_idx < 0)
        {
            track_idx = 0;
        }

        new_raw = (int)lrint(floor(lua_tonumber(L, 2) * 100.0));
        if (lua_gettop(L) >= 3 && lua_type(L, 3) == LUA_TBOOLEAN && lua_toboolean(L, 3) != 0)
        {
            dlg_track_off = 0xa40;
            obj_track_off = 0x1f8;
        }
        else
        {
            dlg_track_off = 0x18;
            obj_track_off = 0xf8;
        }

        filt_idx = (cur_target >> 16) & 0xffu;
        abs_track = (int)*(int16_t *)(obj_ptr + 0x58u + filt_idx * 0x0cu) + track_idx;
        p_obj_val = (int *)(obj_ptr + (uintptr_t)obj_track_off + (uintptr_t)abs_track * 4u);

        exedit_ver = (uintptr_t)RikkyGetMemoryData(100);
        if (exedit_ver >= 0x23u)
        {
            uintptr_t *p_be = (uintptr_t *)RikkyGetMemoryData(0xbe);
            p_dlg_val = (int *)(*p_be + (uintptr_t)dlg_track_off + (uintptr_t)abs_track * 0x28u);
        }
        else if (exedit_ver >= 0x1eu)
        {
            uintptr_t be_addr = (uintptr_t)RikkyGetMemoryData(0xbe);
            p_dlg_val = (int *)(be_addr + (uintptr_t)(dlg_track_off - 0x18) + (uintptr_t)abs_track * 0x28u);
        }
        else
        {
            uintptr_t be_addr = (uintptr_t)RikkyGetMemoryData(0xbe);
            int legacy_off = (dlg_track_off == 0x18) ? 0x18 : 0xa20;
            p_dlg_val = (int *)(be_addr + (uintptr_t)(legacy_off - 0x18) + (uintptr_t)abs_track * 0x28u);
        }

        if (*p_obj_val != new_raw || *p_dlg_val != new_raw)
        {
            char field_name[20];

            *p_obj_val = new_raw;
            if (*p_dlg_val != new_raw)
            {
                HWND *p_exedit_dlg = (HWND *)RikkyGetMemoryData(0x76);
                *p_dlg_val = new_raw;
                if (p_exedit_dlg && *p_exedit_dlg && IsWindowVisible(*p_exedit_dlg))
                {
                    SendMessageA(*p_exedit_dlg, WM_COMMAND, 0x45c, 0);
                }
            }

            lua_getfield(L, LUA_GLOBALSINDEX, "obj");
            lua_pushnumber(L, (double)new_raw * 0.01);
            sprintf(field_name, "track%d", track_idx);
            lua_setfield(L, -2, field_name);
            lua_pop(L, 1);
            updated = 1;
        }
    }

    lua_pushboolean(L, updated);
    return 1;
}

int __cdecl lua_rikky_file(lua_State *L)
{
    install_exedit_controls(L, 0, 0);
    return 0;
}

int __cdecl lua_rikky_fileCS(lua_State *L)
{
    install_exedit_controls(L, 0, 1);
    return 0;
}

int __cdecl lua_rikky_fold(lua_State *L)
{
    install_exedit_controls(L, 1, 0);
    return 0;
}

int __cdecl lua_rikky_foldCS(lua_State *L)
{
    install_exedit_controls(L, 1, 1);
    return 0;
}

int __cdecl lua_rikky_font(lua_State *L)
{
    install_exedit_controls(L, 2, 0);
    return 0;
}

int __cdecl lua_rikky_fontCS(lua_State *L)
{
    install_exedit_controls(L, 2, 1);
    return 0;
}

int __cdecl lua_rikky_list(lua_State *L)
{
    install_exedit_controls(L, 3, 0);
    return 0;
}

int __cdecl lua_rikky_listCS(lua_State *L)
{
    install_exedit_controls(L, 3, 1);
    return 0;
}

int __cdecl lua_rikky_colordialog(lua_State *L)
{
    install_exedit_controls(L, 5, 0);
    return 0;
}

int __cdecl lua_rikky_colordialogCS(lua_State *L)
{
    install_exedit_controls(L, 5, 1);
    return 0;
}

int __cdecl lua_rikky_checkbox(lua_State *L)
{
    install_exedit_controls(L, 6, 0);
    return 0;
}

int __cdecl lua_rikky_checkboxCS(lua_State *L)
{
    install_exedit_controls(L, 6, 1);
    return 0;
}

int __cdecl lua_rikky_parameter(lua_State *L)
{
    return custom_parameter_impl(L, 0);
}

int __cdecl lua_rikky_parameterCS(lua_State *L)
{
    return custom_parameter_impl(L, 1);
}

int __cdecl lua_rikky_setdialogparam(lua_State *L)
{
    set_dialog_param_impl(L, 0);
    return 1;
}

int __cdecl lua_rikky_setdialogparamCS(lua_State *L)
{
    set_dialog_param_impl(L, 1);
    return 1;
}
