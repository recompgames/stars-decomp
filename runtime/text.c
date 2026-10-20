/* Fonts and text output (runtime/text.h), task stars-0z4.5. Behaviour is specified from the
 * Windows 3.1 SDK documentation; there are no reference captures (docs/PLAN.md decision 9), so
 * every pixel result here is UNVERIFIED against Windows 3.1. Runtime state that changes during
 * a call (the font objects) lives in the arena through gdi_obj_alloc; the font set itself is
 * rebuilt from const data by text_init and holds host pointers, so it is never stored in the arena. */
#include "runtime/text.h"

#include <ctype.h>
#include <string.h>

#include "runtime/fontdata.h"
#include "runtime/gdi_internal.h"
#include "runtime/mem.h"

#define MAX_FONTS 64
#define MAX_TEXT 4096
#define MAX_LINES 512

static FntFont g_fonts[MAX_FONTS];
static int g_nfonts;

typedef struct FontObj {
  uint8_t lf[LOGFONT16_SIZE];
  int16_t idx;
  uint8_t underline, strikeout;
  uint8_t italic; /* lfItalic, or an " Italic" face-name suffix */
} FontObj;

/* ---------------------------------------------------------------- font set and mapping */

int text_register_font(const uint8_t *fnt, size_t size) {
  if (g_nfonts >= MAX_FONTS) return -10;
  int rc = fnt_parse(fnt, size, &g_fonts[g_nfonts]);
  if (rc < 0) return rc;
  return g_nfonts++;
}

int text_font_count(void) { return g_nfonts; }
const FntFont *text_font_info(int index) { return (index >= 0 && index < g_nfonts) ? &g_fonts[index] : NULL; }

static int ieq(const char *a, const char *b) {
  for (; *a && *b; a++, b++)
    if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
  return *a == *b;
}

static int char_height(const FntFont *f) { return (int)f->pix_height - (int)f->internal_leading; }

static int map_logfont(const uint8_t *lf, bool *italic_out) {
  if (g_nfonts == 0) return -1;
  char face[LF_FACENAME_LEN + 1];
  memcpy(face, lf + LF_FACENAME, LF_FACENAME_LEN);
  face[LF_FACENAME_LEN] = 0;
  const char *family = "System"; /* unknown face: System */
  bool bold_alias = false, italic_alias = false;
  /* The game's face names are "Arial", "Arial Bold", "Arial Italic", "Arial Bold Italic". When
   * the full name is not a known family, strip a trailing " Bold", " Italic" or " Bold Italic"
   * and look the rest up; Bold wants the bold weight class. */
  char base[LF_FACENAME_LEN + 1];
  strcpy(base, face);
  bool known = false;
  for (int i = 0; i < g_nfonts; i++)
    if (ieq(g_fonts[i].face, face)) known = true;
  if (!known) {
    size_t n = strlen(base);
    if (n > 7 && ieq(base + n - 7, " Italic")) {
      base[n -= 7] = 0;
      if (n > 5 && ieq(base + n - 5, " Bold")) {
        base[n -= 5] = 0;
        bold_alias = true;
      }
      italic_alias = true;
    } else if (n > 5 && ieq(base + n - 5, " Bold")) {
      base[n - 5] = 0;
      bold_alias = true;
    }
  }
  if (ieq(base, "Helv") || ieq(base, "Helvetica") || ieq(base, "MS Sans")) {
    family = "MS Sans Serif";
  } else {
    for (int i = 0; i < g_nfonts; i++) /* exact face of a built-in or registered font */
      if (ieq(g_fonts[i].face, base)) {
        family = g_fonts[i].face;
        break;
      }
  }
  if (italic_out) *italic_out = italic_alias;
  int16_t height = (int16_t)(lf[LF_HEIGHT] | (lf[LF_HEIGHT + 1] << 8));
  uint16_t weight = (uint16_t)(lf[LF_WEIGHT] | (lf[LF_WEIGHT + 1] << 8));
  bool want_bold = weight >= 600 || bold_alias;

  /* Candidates: the family, in the wanted weight class when it has one. */
  int pass_bold[2] = {want_bold, !want_bold};
  for (int pass = 0; pass < 3; pass++) {
    int best = -1, smallest = -1;
    for (int i = 0; i < g_nfonts; i++) {
      const FntFont *f = &g_fonts[i];
      if (!ieq(f->face, family)) continue;
      if (pass < 2 && ((f->weight >= 600) != (pass_bold[pass] != 0))) continue;
      int req_ok;
      int size = height > 0 ? (int)f->pix_height : char_height(f);
      int want = height > 0 ? height : (height < 0 ? -height : 13);
      req_ok = size <= want;
      if (smallest < 0 || size < (height > 0 ? (int)g_fonts[smallest].pix_height : char_height(&g_fonts[smallest])))
        smallest = i;
      if (req_ok) {
        int bsize = best < 0 ? -1 : (height > 0 ? (int)g_fonts[best].pix_height : char_height(&g_fonts[best]));
        if (best < 0 || size > bsize) best = i;
      }
    }
    if (best >= 0) return best;
    if (smallest >= 0) return smallest;
  }
  return 0; /* family absent: System is font 0 */
}

int text_map_logfont(const uint8_t *lf) { return map_logfont(lf, NULL); }

/* ---------------------------------------------------------------- GDI objects */

static FontObj *fobj_of(uint16_t h) { return gdi_obj_get(h, GDI_OBJ_FONT); }

static uint16_t select_font(HDC16 hdc, uint16_t hfont) {
  DCObj *dc = gdi_dc_get(hdc);
  if (!dc || !fobj_of(hfont)) return 0;
  uint16_t old = dc->hfont;
  if (old == hfont) return old;
  gdi_obj_ref(old, -1);
  gdi_obj_ref(hfont, 1);
  dc->hfont = hfont;
  return old;
}

static uint16_t make_font(const uint8_t *lf, int idx, bool italic_alias) {
  uint16_t h = gdi_obj_alloc(GDI_OBJ_FONT, sizeof(FontObj));
  FontObj *fo = gdi_obj_get(h, GDI_OBJ_FONT);
  if (!fo) return 0;
  memcpy(fo->lf, lf, LOGFONT16_SIZE);
  fo->idx = (int16_t)idx;
  fo->italic = lf[LF_ITALIC] || italic_alias;
  fo->underline = lf[LF_UNDERLINE];
  fo->strikeout = lf[LF_STRIKEOUT];
  return h;
}

int text_init(void) {
  g_nfonts = 0;
  for (unsigned i = 0; i < fontdata_count; i++)
    if (text_register_font(fontdata_blobs[i].data, fontdata_blobs[i].size) < 0) return -1;
  gdi_set_font_hook(select_font);
  /* Stock fonts. No fixed-pitch look-alike is built in: every fixed stock font is System
   * (UNVERIFIED). ANSI_VAR_FONT is MS Sans Serif (font 1), the rest System (font 0). */
  static const struct {
    int id, idx;
  } stock[] = {{OEM_FIXED_FONT, 0}, {ANSI_FIXED_FONT, 0}, {ANSI_VAR_FONT, 1},
               {SYSTEM_FONT, 0},    {DEVICE_DEFAULT_FONT, 0}, {SYSTEM_FIXED_FONT, 0}};
  for (unsigned i = 0; i < sizeof stock / sizeof stock[0]; i++) {
    uint8_t lf[LOGFONT16_SIZE] = {0};
    const char *name = stock[i].idx == 1 ? "MS Sans Serif" : "System";
    strncpy((char *)lf + LF_FACENAME, name, LF_FACENAME_LEN - 1);
    uint16_t h = make_font(lf, stock[i].idx, false);
    if (!h) return -1;
    gdi_set_stock(stock[i].id, h);
  }
  return 0;
}

HFONT16 CreateFontIndirect(FARPTR lplf) {
  if (!lplf || !mem_check(lplf, LOGFONT16_SIZE)) return 0;
  uint8_t lf[LOGFONT16_SIZE];
  memcpy(lf, LIN(lplf), LOGFONT16_SIZE);
  lf[LF_FACENAME + LF_FACENAME_LEN - 1] = 0;
  bool italic = false;
  int idx = map_logfont(lf, &italic);
  if (idx < 0) return 0;
  return make_font(lf, idx, italic);
}

/* ---------------------------------------------------------------- helpers */

typedef struct TextCtx {
  const FntFont *f;
  const FontObj *fo;
  int underline_row, strike_row;
} TextCtx;

static bool text_ctx(HDC16 hdc, TextCtx *c) {
  DCObj *dc = gdi_dc_get(hdc);
  if (!dc) return false;
  const FontObj *fo = fobj_of(dc->hfont);
  int idx = fo ? fo->idx : 0;
  if (idx < 0 || idx >= g_nfonts) idx = 0;
  c->f = &g_fonts[idx];
  c->fo = fo;
  int h = c->f->pix_height;
  c->underline_row = c->f->ascent + 1 < h ? c->f->ascent + 1 : h - 1;
  c->strike_row = h / 2;
  return true;
}

static void w16(uint8_t *p, int v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
}

/* Copies up to cap bytes of a far string: count >= 0 exact, count < 0 up to the NUL. */
static int get_str(FARPTR str, int count, uint8_t *buf, int cap) {
  if (!str) return 0;
  int n = 0;
  if (count < 0) {
    while (n < cap && mem_check(str + (uint32_t)n, 1)) {
      uint8_t c = *(uint8_t *)LIN(str + (uint32_t)n);
      if (!c) break;
      buf[n++] = c;
    }
    return n;
  }
  if (count > cap) count = cap;
  if (count && !mem_check(str, (uint32_t)count)) return 0;
  if (count) memcpy(buf, LIN(str), (size_t)count);
  return count;
}

static void put(HDC16 hdc, const RECT16 *clip, int x, int y, uint8_t idx) {
  if (clip && (x < clip->left || x >= clip->right || y < clip->top || y >= clip->bottom)) return;
  gdi_dc_put_pixel(hdc, (int16_t)x, (int16_t)y, idx);
}

/* Draws a run of characters with its top-left at (x, y). dx (host array, may be NULL) overrides
 * the advances; ul (may be NULL) marks mnemonic characters. Returns the total advance. */
static int draw_run(HDC16 hdc, const TextCtx *c, int x, int y, const uint8_t *s, int n, const int16_t *dx,
                    const uint8_t *ul, const RECT16 *clip) {
  const FntFont *f = c->f;
  uint8_t fg = gdi_dc_color_index(hdc, GetTextColor(hdc));
  bool opaque = GetBkMode(hdc) == OPAQUE;
  uint8_t bg = gdi_dc_color_index(hdc, GetBkColor(hdc));
  bool under = c->fo && c->fo->underline, strike = c->fo && c->fo->strikeout;
  int pen = x;
  for (int i = 0; i < n; i++) {
    int gw = fnt_glyph_width(f, s[i]);
    int adv = dx ? dx[i] : gw;
    bool blank = dx && s[i] == '\t'; /* an expanded tab is a gap, not the default glyph */
    if (blank) gw = 0;
    int cw = adv > gw ? adv : gw;
    bool u = under || (ul && ul[i]);
    for (int row = 0; row < f->pix_height; row++)
      for (int col = 0; col < cw; col++) {
        bool on = (!blank && fnt_glyph_pixel(f, s[i], col, row)) || (u && row == c->underline_row && col < adv) ||
                  (strike && row == c->strike_row && col < adv);
        if (on)
          put(hdc, clip, pen + col, y + row, fg);
        else if (opaque)
          put(hdc, clip, pen + col, y + row, bg);
      }
    pen += adv;
  }
  return pen - x;
}

static int run_width(const FntFont *f, const uint8_t *s, int n) {
  int w = 0;
  for (int i = 0; i < n; i++) w += fnt_glyph_width(f, s[i]);
  return w;
}

/* ---------------------------------------------------------------- GDI text API */

uint32_t GetTextExtent(HDC16 hdc, FARPTR str, int16_t count) {
  TextCtx c;
  if (!text_ctx(hdc, &c) || count < 0) return 0;
  uint8_t buf[MAX_TEXT];
  int n = get_str(str, count, buf, MAX_TEXT);
  return (uint32_t)(uint16_t)run_width(c.f, buf, n) | ((uint32_t)c.f->pix_height << 16);
}

uint16_t GetTextMetrics(HDC16 hdc, FARPTR lptm) {
  TextCtx c;
  if (!text_ctx(hdc, &c) || !lptm || !mem_check(lptm, TEXTMETRIC16_SIZE)) return 0;
  const FntFont *f = c.f;
  uint8_t *p = LIN(lptm);
  memset(p, 0, TEXTMETRIC16_SIZE);
  w16(p + 0, f->pix_height);
  w16(p + 2, f->ascent);
  w16(p + 4, f->pix_height - f->ascent);
  w16(p + 6, f->internal_leading);
  w16(p + 8, f->external_leading);
  w16(p + 10, f->avg_width);
  w16(p + 12, f->max_width);
  w16(p + 14, f->weight);
  p[16] = (uint8_t)(f->italic || (c.fo && c.fo->italic));
  p[17] = (uint8_t)(c.fo && c.fo->underline);
  p[18] = (uint8_t)(c.fo && c.fo->strikeout);
  p[19] = f->first;
  p[20] = f->last;
  p[21] = f->default_char;
  p[22] = f->break_char;
  p[23] = f->pitch_family;
  p[24] = f->charset;
  w16(p + 25, 0);                 /* tmOverhang */
  w16(p + 27, f->horiz_res);      /* tmDigitizedAspectX */
  w16(p + 29, f->vert_res);       /* tmDigitizedAspectY */
  return 1;
}

static void fill_rect(HDC16 hdc, const RECT16 *r, uint8_t idx) {
  for (int y = r->top; y < r->bottom; y++)
    for (int x = r->left; x < r->right; x++) gdi_dc_put_pixel(hdc, (int16_t)x, (int16_t)y, idx);
}

uint16_t ExtTextOut(HDC16 hdc, int16_t x, int16_t y, uint16_t options, FARPTR rect, FARPTR str,
                    uint16_t count, FARPTR dx) {
  TextCtx c;
  if (!text_ctx(hdc, &c)) return 0;
  RECT16 r;
  bool have_rect = rect && mem_check(rect, sizeof r);
  if (have_rect) memcpy(&r, LIN(rect), sizeof r);
  if (have_rect && (options & ETO_OPAQUE)) fill_rect(hdc, &r, gdi_dc_color_index(hdc, GetBkColor(hdc)));
  uint8_t buf[MAX_TEXT];
  int n = get_str(str, count, buf, MAX_TEXT);
  int16_t adv[MAX_TEXT];
  const int16_t *dxp = NULL;
  if (dx && n && mem_check(dx, (uint32_t)n * 2)) {
    memcpy(adv, LIN(dx), (size_t)n * 2);
    dxp = adv;
  }
  draw_run(hdc, &c, x, y, buf, n, dxp, NULL, (have_rect && (options & ETO_CLIPPED)) ? &r : NULL);
  return 1;
}

uint16_t TextOut(HDC16 hdc, int16_t x, int16_t y, FARPTR str, int16_t count) {
  TextCtx c;
  if (!text_ctx(hdc, &c) || count < 0) return 0;
  uint8_t buf[MAX_TEXT];
  int n = get_str(str, count, buf, MAX_TEXT);
  draw_run(hdc, &c, x, y, buf, n, NULL, NULL, NULL);
  return 1;
}

/* ---------------------------------------------------------------- DrawText */

typedef struct Line {
  int start, end; /* [start, end) in the display string */
} Line;

int16_t DrawText(HDC16 hdc, FARPTR str, int16_t count, FARPTR rect, uint16_t flags) {
  TextCtx c;
  if (!text_ctx(hdc, &c) || !rect || !mem_check(rect, 8)) return 0;
  const FntFont *f = c.f;
  RECT16 rc;
  memcpy(&rc, LIN(rect), sizeof rc);

  uint8_t src[MAX_TEXT];
  int sn = get_str(str, count, src, MAX_TEXT);
  /* Display string: drop CR (and LF when single line), process the '&' mnemonic prefix. */
  static uint8_t disp[MAX_TEXT], ul[MAX_TEXT];
  int dn = 0;
  bool single = (flags & DT_SINGLELINE) != 0;
  for (int i = 0; i < sn; i++) {
    uint8_t ch = src[i];
    if (ch == '\r' || (single && ch == '\n')) continue;
    if (ch == '&' && !(flags & DT_NOPREFIX)) {
      if (i + 1 >= sn) continue;
      if (src[i + 1] == '&') {
        i++; /* "&&" is a literal ampersand */
      } else {
        i++;
        if (src[i] == '\r' || (single && src[i] == '\n')) continue;
        disp[dn] = src[i];
        ul[dn++] = 1;
        continue;
      }
      ch = '&';
    }
    disp[dn] = ch;
    ul[dn++] = 0;
  }

  int maxw = rc.right - rc.left;
  int tabw = 8 * f->avg_width;
  if ((flags & DT_TABSTOP) && ((flags >> 8) & 0xff)) tabw = ((flags >> 8) & 0xff) * f->avg_width;
  if (tabw <= 0) tabw = 8;
  bool expand = (flags & DT_EXPANDTABS) != 0;

  /* Per-character advance given the pen position inside the line. */
#define ADV(ch, pen) ((expand && (ch) == '\t') ? (tabw - ((pen) % tabw)) : fnt_glyph_width(f, (ch)))

  static Line lines[MAX_LINES];
  int nl = 0;
  int s = 0;
  while ((s < dn || nl == 0) && nl < MAX_LINES) {
    int i = s, pen = 0, lastspace = -1, end = -1, next = -1;
    for (; i < dn; i++) {
      uint8_t ch = disp[i];
      if (ch == '\n' && !single) {
        end = i;
        next = i + 1;
        break;
      }
      int a = ADV(ch, pen);
      if ((flags & DT_WORDBREAK) && !single && pen + a > maxw && i > s) {
        if (lastspace >= s) {
          end = lastspace;
          next = lastspace + 1;
        } else {
          end = i;
          next = i;
        }
        break;
      }
      if (ch == ' ') lastspace = i;
      pen += a;
    }
    if (end < 0) {
      end = dn;
      next = dn;
    }
    lines[nl].start = s;
    lines[nl].end = end;
    nl++;
    if (next <= s && next >= dn) break;
    s = next;
    if (s >= dn) break;
  }

  int lh = f->pix_height + ((flags & DT_EXTERNALLEADING) ? f->external_leading : 0);
  int total_h = nl * lh;
  int widths[MAX_LINES], wmax = 0;
  for (int l = 0; l < nl; l++) {
    int pen = 0;
    for (int i = lines[l].start; i < lines[l].end; i++) pen += ADV(disp[i], pen);
    widths[l] = pen;
    if (pen > wmax) wmax = pen;
  }

  if (flags & DT_CALCRECT) {
    if (!((flags & DT_WORDBREAK) && !single) || wmax > maxw) rc.right = (int16_t)(rc.left + wmax);
    rc.bottom = (int16_t)(rc.top + total_h);
    memcpy(LIN(rect), &rc, sizeof rc);
    return (int16_t)total_h;
  }

  int y = rc.top;
  if (single) {
    int rch = rc.bottom - rc.top;
    if (flags & DT_VCENTER)
      y = rc.top + (rch - total_h) / 2;
    else if (flags & DT_BOTTOM)
      y = rc.bottom - total_h;
  }
  const RECT16 *clip = (flags & DT_NOCLIP) ? NULL : &rc;
  for (int l = 0; l < nl; l++) {
    int x = rc.left;
    if (flags & DT_CENTER)
      x = rc.left + (maxw - widths[l]) / 2;
    else if (flags & DT_RIGHT)
      x = rc.right - widths[l];
    if (!expand) {
      draw_run(hdc, &c, x, y, disp + lines[l].start, lines[l].end - lines[l].start, NULL, ul + lines[l].start, clip);
    } else {
      /* Expand tabs: draw each tab as a gap via explicit advances. */
      int16_t adv[MAX_TEXT];
      int pen = 0, n = lines[l].end - lines[l].start;
      for (int i = 0; i < n; i++) {
        uint8_t ch = disp[lines[l].start + i];
        adv[i] = (int16_t)ADV(ch, pen);
        pen += adv[i];
      }
      draw_run(hdc, &c, x, y, disp + lines[l].start, n, adv, ul + lines[l].start, clip);
    }
    y += lh;
  }
  return (int16_t)total_h;
#undef ADV
}
