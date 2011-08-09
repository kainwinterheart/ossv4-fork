/*
 * Purpose: Solaris compatible partial DDI interface for OSS/Linux
 */

#define COPYING10 Copyright (C) Hannu Savolainen and Dev Mazumdar 2000-2006. All rights reserved.
#ifndef NULL
#define NULL 0
#endif

typedef int ddi_iblock_cookie_t;
typedef int kmutex_t;
typedef int cred_t;

typedef int ddi_acc_handle_t;
typedef int kcondvar_t;
typedef int ddi_dma_handle_t;
typedef int ddi_dma_cookie_t;
typedef int ddi_dma_win_t;
typedef int ddi_dma_seg_t;
typedef int offset_t;
typedef int ddi_info_cmd_t;
typedef int ddi_attach_cmd_t;
typedef int ddi_detach_cmd_t;

#include <stdint.h>

typedef struct _ddi_dma_attr_t
{
#define DMA_ATTR_V0 0
  int a, b, c, d, e, f, g, h, i, j, k, l, m, n;
} ddi_dma_attr_t;

struct pollhead
{
  int dummy;
};
