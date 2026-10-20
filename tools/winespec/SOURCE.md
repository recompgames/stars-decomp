# Vendored Wine 16-bit spec files

These six files are verbatim copies from the Wine source tree. They are input to
tools/gen_imports.py, which writes runtime/win16_api.inc. Do not edit them.

- Upstream: https://github.com/wine-mirror/wine
- Commit: 5b6b85421c2a2161728b2a55e9f09271770a3eb0
- Licence: LGPL-2.1 (Wine), as recorded in docs/PLAN.md section 2, row 10.
- Downloaded: 2026-10-15, with curl -fL from these URLs:

  - https://raw.githubusercontent.com/wine-mirror/wine/5b6b85421c2a2161728b2a55e9f09271770a3eb0/dlls/krnl386.exe16/krnl386.exe16.spec
  - https://raw.githubusercontent.com/wine-mirror/wine/5b6b85421c2a2161728b2a55e9f09271770a3eb0/dlls/user.exe16/user.exe16.spec
  - https://raw.githubusercontent.com/wine-mirror/wine/5b6b85421c2a2161728b2a55e9f09271770a3eb0/dlls/gdi.exe16/gdi.exe16.spec
  - https://raw.githubusercontent.com/wine-mirror/wine/5b6b85421c2a2161728b2a55e9f09271770a3eb0/dlls/commdlg.dll16/commdlg.dll16.spec
  - https://raw.githubusercontent.com/wine-mirror/wine/5b6b85421c2a2161728b2a55e9f09271770a3eb0/dlls/toolhelp.dll16/toolhelp.dll16.spec
  - https://raw.githubusercontent.com/wine-mirror/wine/5b6b85421c2a2161728b2a55e9f09271770a3eb0/dlls/win87em.dll16/win87em.dll16.spec

SHA-256 (shasum -a 256):

```
a10df709dc917f83f8ccea6d4ba7719bf0fb442613db149dd89de581872a2395  commdlg.dll16.spec
3bd84b26a9d4b7aa91a9114d0a46dc13e4d6fcd71290120afdd9b7cfaffe76ab  gdi.exe16.spec
8d799b701d1cf16d4e882b5e354dc6e357056d4adcd83e629571dc4b33b02075  krnl386.exe16.spec
155de04942d5e53200c33698a366ddab5a3d51303e4cd9802e4ed8d4c23f195e  toolhelp.dll16.spec
d873fa88079c4f922cbba2fb3a75f40e2ff23b4d56434b0f4326169397539e32  user.exe16.spec
568e9c159d718eb082f5fc441ea9f5d65950c7ac289902efdeac810afc0b6a91  win87em.dll16.spec
```
