# Look-alike fonts (task stars-0z4.5)

The runtime draws text with raster fonts compiled into `runtime/fontdata.c`. They are look-alikes
for the Windows 3.1 System font, MS Sans Serif 8 pt and Arial (owner amendment 2026-10-17,
`docs/PLAN.md` decision 8). **No Microsoft font file is used, copied or derived anywhere in this
repository.** Glyph shapes and widths differ from the Windows fonts; every pixel expectation is
UNVERIFIED against Windows 3.1.

## Source font

| Field | Value |
|-------|-------|
| Name | Liberation Sans (Regular and Bold) |
| Version | 2.1.5 (release tarball `liberation-fonts-ttf-2.1.5.tar.gz`) |
| Licence | SIL Open Font License 1.1, full text in `LICENSE-LiberationSans-OFL.txt` (copied unmodified from the tarball) |
| Copyright | Digitized data (c) 2010 Google Corporation (Arimo, Tinos, Cousine); (c) 2012 Red Hat, Inc. (Reserved Font Name "Liberation") |
| Source URL | https://github.com/liberationfonts/liberation-fonts/files/7261482/liberation-fonts-ttf-2.1.5.tar.gz (release page: https://github.com/liberationfonts/liberation-fonts/releases/tag/2.1.5) |
| SHA-256 of the tarball | `7191c669bf38899f73a2094ed00f7b800553364f90e2637010a69c0e268f25d0` |
| SHA-256 of `LiberationSans-Regular.ttf` | `76d04c18ea243f426b7de1f3ad208e927008f961dc5945e5aad352d0dfde8ee8` |
| SHA-256 of `LiberationSans-Bold.ttf` | `788abee4c806d660e8aee46689dd8540cd4bb98da03dcc9d171ce3efd99a9173` |
| SHA-256 of `LICENSE` | `93fed46019c38bbe566b479d22148e2e8a1e85ada614accb0211c37b2c61c19b` |

The TTF files themselves are not stored in the repository (only their rasterised bitmaps, as
generated tables). Under the OFL the derived bitmaps are a modified version: they are published
under the name "System" / "MS Sans Serif" / "Arial" as FNT face names only inside the runtime and
**do not use the Reserved Font Name "Liberation"** as a font name. The OFL text above covers the
generated `runtime/fontdata.c`.

The OFL reserved font names of this family are "Liberation", "Arimo", "Tinos" and "Cousine". None
of them is used in the derived fonts (face names in the generated data are only "System",
"MS Sans Serif" and "Arial"). The OFL text (`LICENSE-LiberationSans-OFL.txt`) must ship with the
browser build, alongside the wasm and its font data (task stars-6p5.1).

## How the bitmaps are made

`tools/genfont.c` (build-time only, not part of `make`) loads the two TTFs with FreeType
(2.14.3 at generation time; v35 TrueType bytecode interpreter, `FT_LOAD_TARGET_MONO`, hinted
1-bit rendering, no anti-aliasing) and writes Windows FNT 3.0 resources as C byte arrays:

    F=$(brew --prefix freetype)
    clang -O1 tools/genfont.c -I$F/include/freetype2 -L$F/lib -lfreetype -o /tmp/genfont
    /tmp/genfont LiberationSans-Regular.ttf LiberationSans-Bold.ttf > runtime/fontdata.c

FreeType is used only to generate the file (FreeType Licence / GPLv2 dual licence); it is not
linked into the runtime. Fonts produced:

| Index | FNT face | Source | em px | Cell height | Ascent |
|-------|----------|--------|-------|-------------|--------|
| 0 | System | Sans Bold | 13 | 16 | 13 |
| 1 | MS Sans Serif | Sans Regular | 11 | 13 | 11 |
| 2-10 | Arial (regular) | Sans Regular | 8, 9, 10, 11, 13, 16, 19, 24, 32 | 9, 10, 11, 12, 15, 17, 21, 27, 36 | 7, 8, 9, 10, 12, 14, 17, 22, 29 |
| 11-19 | Arial (bold) | Sans Bold | same | same | same |

Characters 32-255 (Windows ANSI; 0x80-0x9f mapped from cp1252, missing ones are empty boxes).

## When the owner's Windows 3.11 fonts arrive

Replace the look-alikes with the real FONT resources loaded from the owner's `.FON` files
(`runtime/fnt.h` already decodes the same FNT 2.0/3.0 format); `text_register_font` takes any FNT
resource. Those files must stay out of git (`original/win311/`).
