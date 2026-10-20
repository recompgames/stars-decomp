/* USER.LoadBitmap (stars-9gm.3): loads an RT_BITMAP resource of the program as a bitmap
 * compatible with the screen (1-bpp DIBs become monochrome bitmaps, as in Windows 3.1). The
 * DIB is drawn into the new bitmap with StretchDIBits, so colours map through the system
 * palette exactly as a DIB drawn to the screen would. No harness dependency. */
#include "runtime/gdi.h"
#include "runtime/kernel.h"
#include "runtime/user.h"

#define RES_BITMAP 2

static uint32_t rd32le(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

HBITMAP16 LoadBitmap(HANDLE16 hInstance, FARPTR lpBitmapName) {
  HANDLE16 hr, hd;
  FARPTR p;
  uint32_t size, hdr, ncol = 0, entry;
  int32_t w, h;
  int bpp;
  const uint8_t *d;
  HDC16 hscreen, hmem;
  HBITMAP16 hbm = 0, hold;
  if (!hInstance) return 0; /* system (OBM_) bitmaps are not part of the loaded program */
  hr = FindResource(hInstance, lpBitmapName, (FARPTR)RES_BITMAP);
  if (!hr) return 0;
  size = SizeofResource(hInstance, hr);
  hd = LoadResource(hInstance, hr);
  if (!hd) return 0;
  p = LockResource(hd);
  if (!p || size < 12) goto out;
  d = (const uint8_t *)LIN(p);
  hdr = rd32le(d);
  if (hdr == 12) {
    w = d[4] | (d[5] << 8);
    h = d[6] | (d[7] << 8);
    bpp = d[10] | (d[11] << 8);
    entry = 3;
  } else if (hdr >= 40 && size >= 40) {
    w = (int32_t)rd32le(d + 4);
    h = (int32_t)rd32le(d + 8);
    bpp = d[14] | (d[15] << 8);
    entry = 4;
    if (rd32le(d + 16) != 0) goto out; /* compressed DIBs are not supported */
  } else {
    goto out;
  }
  if (h < 0) h = -h;
  if (w <= 0 || h <= 0) goto out;
  if (bpp <= 8) {
    ncol = 1u << bpp;
    if (hdr >= 40 && rd32le(d + 32) && rd32le(d + 32) < ncol) ncol = rd32le(d + 32);
  }
  hscreen = GetDC(0);
  if (!hscreen) goto out;
  if (bpp == 1) {
    HDC16 hm0 = CreateCompatibleDC(hscreen); /* a memory DC holds the 1x1 monochrome bitmap */
    hbm = hm0 ? CreateCompatibleBitmap(hm0, (int16_t)w, (int16_t)h) : 0;
    if (hm0) DeleteDC(hm0);
  } else {
    hbm = CreateCompatibleBitmap(hscreen, (int16_t)w, (int16_t)h);
  }
  hmem = CreateCompatibleDC(hscreen);
  if (hbm && hmem) {
    hold = (HBITMAP16)SelectObject(hmem, hbm);
    StretchDIBits(hmem, 0, 0, (int16_t)w, (int16_t)h, 0, 0, (int16_t)w, (int16_t)h,
                  MK_FP(FP_SEG(p), (uint16_t)(FP_OFF(p) + hdr + ncol * entry)), p, 0, 0x00CC0020u /* SRCCOPY */);
    SelectObject(hmem, hold);
  }
  if (hmem) DeleteDC(hmem);
  ReleaseDC(0, hscreen);
out:
  GlobalUnlock(hd);
  FreeResource(hd);
  return hbm;
}
