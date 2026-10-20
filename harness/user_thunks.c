/* HCall decoders for the USER functions of runtime/user.h (parts 1 and 2: tasks stars-0z4.6
 * and stars-0z4.7, and the dialog button helpers of stars-0z4.10). Native
 * builds only. Arguments are decoded from the PASCAL stack words: hcall_word(c, 0) is the LAST
 * pushed word, and a DWORD or far pointer is pushed high word first, so its low word is word n
 * and its high word word n+1. Functions that return void in Win16 leave c->ret at 0. */
#include "harness/user_thunks.h"

#include "harness/harness.h"
#include "runtime/user.h"

static uint32_t w32(const HCall *c, unsigned n) {
  return (uint32_t)hcall_word(c, n) | ((uint32_t)hcall_word(c, n + 1) << 16);
}
#define W(n) hcall_word(c, (n))
#define S(n) ((int16_t)hcall_word(c, (n)))

static void t_RegisterClass(HCall *c) { c->ret = RegisterClass(w32(c, 0)); }
static void t_CreateWindow(HCall *c) {
  c->ret = CreateWindow(w32(c, 13), w32(c, 11), w32(c, 9), S(8), S(7), S(6), S(5), W(4), W(3), W(2), w32(c, 0));
}
static void t_DestroyWindow(HCall *c) { c->ret = DestroyWindow(W(0)); }
static void t_ShowWindow(HCall *c) { c->ret = ShowWindow(W(1), S(0)); }
static void t_MoveWindow(HCall *c) { c->ret = MoveWindow(W(5), S(4), S(3), S(2), S(1), W(0)); }
static void t_SetWindowPos(HCall *c) { c->ret = SetWindowPos(W(6), W(5), S(4), S(3), S(2), S(1), W(0)); }
static void t_GetWindowRect(HCall *c) { GetWindowRect(W(2), w32(c, 0)); }
static void t_GetClientRect(HCall *c) { GetClientRect(W(2), w32(c, 0)); }
static void t_ClientToScreen(HCall *c) { ClientToScreen(W(2), w32(c, 0)); }
static void t_ScreenToClient(HCall *c) { ScreenToClient(W(2), w32(c, 0)); }
static void t_MapWindowPoints(HCall *c) { MapWindowPoints(W(4), W(3), w32(c, 1), W(0)); }
static void t_WindowFromPoint(HCall *c) { c->ret = WindowFromPoint(w32(c, 0)); }
static void t_GetParent(HCall *c) { c->ret = GetParent(W(0)); }
static void t_GetWindow(HCall *c) { c->ret = GetWindow(W(1), W(0)); }
static void t_GetDesktopWindow(HCall *c) { c->ret = GetDesktopWindow(); }
static void t_IsWindow(HCall *c) { c->ret = IsWindow(W(0)); }
static void t_IsWindowVisible(HCall *c) { c->ret = IsWindowVisible(W(0)); }
static void t_IsIconic(HCall *c) { c->ret = IsIconic(W(0)); }
static void t_IsZoomed(HCall *c) { c->ret = IsZoomed(W(0)); }
static void t_IsWindowEnabled(HCall *c) { c->ret = IsWindowEnabled(W(0)); }
static void t_EnableWindow(HCall *c) { c->ret = EnableWindow(W(1), W(0)); }
static void t_GetWindowText(HCall *c) { c->ret = (uint16_t)GetWindowText(W(3), w32(c, 1), S(0)); }
static void t_SetWindowText(HCall *c) { SetWindowText(W(2), w32(c, 0)); }
static void t_GetWindowTextLength(HCall *c) { c->ret = (uint16_t)GetWindowTextLength(W(0)); }
static void t_GetWindowLong(HCall *c) { c->ret = GetWindowLong(W(1), S(0)); }
static void t_SetWindowLong(HCall *c) { c->ret = SetWindowLong(W(3), S(2), w32(c, 0)); }
static void t_GetWindowWord(HCall *c) { c->ret = GetWindowWord(W(1), S(0)); }
static void t_SetWindowWord(HCall *c) { c->ret = SetWindowWord(W(2), S(1), W(0)); }
static void t_CallWindowProc(HCall *c) { c->ret = CallWindowProc(w32(c, 5), W(4), W(3), W(2), w32(c, 0)); }
static void t_DefWindowProc(HCall *c) { c->ret = DefWindowProc(W(4), W(3), W(2), w32(c, 0)); }
static void t_GetWindowPlacement(HCall *c) { c->ret = GetWindowPlacement(W(2), w32(c, 0)); }

static void t_GetMessage(HCall *c) { c->ret = GetMessage(w32(c, 3), W(2), W(1), W(0)); }
static void t_PeekMessage(HCall *c) { c->ret = PeekMessage(w32(c, 4), W(3), W(2), W(1), W(0)); }
static void t_PostMessage(HCall *c) { c->ret = PostMessage(W(4), W(3), W(2), w32(c, 0)); }
static void t_SendMessage(HCall *c) { c->ret = SendMessage(W(4), W(3), W(2), w32(c, 0)); }
static void t_TranslateMessage(HCall *c) { c->ret = TranslateMessage(w32(c, 0)); }
static void t_DispatchMessage(HCall *c) { c->ret = DispatchMessage(w32(c, 0)); }
static void t_PostQuitMessage(HCall *c) { PostQuitMessage(S(0)); }

static void t_BeginPaint(HCall *c) { c->ret = BeginPaint(W(2), w32(c, 0)); }
static void t_EndPaint(HCall *c) { EndPaint(W(2), w32(c, 0)); }
static void t_InvalidateRect(HCall *c) { InvalidateRect(W(3), w32(c, 1), W(0)); }
static void t_ValidateRect(HCall *c) { ValidateRect(W(2), w32(c, 0)); }
static void t_UpdateWindow(HCall *c) { UpdateWindow(W(0)); }
static void t_GetDC(HCall *c) { c->ret = GetDC(W(0)); }
static void t_ReleaseDC(HCall *c) { c->ret = ReleaseDC(W(1), W(0)); }
static void t_ScrollWindow(HCall *c) { ScrollWindow(W(6), S(5), S(4), w32(c, 2), w32(c, 0)); }

/* ---- USER part 2 (stars-0z4.7) ---- */

static void t_InitApp(HCall *c) { c->ret = InitApp(W(0)); }
static void t_ExitWindows(HCall *c) { c->ret = ExitWindows(w32(c, 1), W(0)); }
static void t_SetTimer(HCall *c) { c->ret = SetTimer(W(4), W(3), W(2), w32(c, 0)); }
static void t_KillTimer(HCall *c) { c->ret = KillTimer(W(1), W(0)); }
static void t_GetTickCount(HCall *c) { c->ret = GetTickCount(); }
static void t_GetCursorPos(HCall *c) { GetCursorPos(w32(c, 0)); }
static void t_SetCapture(HCall *c) { c->ret = SetCapture(W(0)); }
static void t_ReleaseCapture(HCall *c) {
  (void)c;
  ReleaseCapture();
}
static void t_GetCapture(HCall *c) { c->ret = GetCapture(); }
static void t_SetFocus(HCall *c) { c->ret = SetFocus(W(0)); }
static void t_GetFocus(HCall *c) { c->ret = GetFocus(); }
static void t_SetActiveWindow(HCall *c) { c->ret = SetActiveWindow(W(0)); }
static void t_GetActiveWindow(HCall *c) { c->ret = GetActiveWindow(); }
static void t_SetCursor(HCall *c) { c->ret = SetCursor(W(0)); }
static void t_LoadCursor(HCall *c) { c->ret = LoadCursor(W(2), w32(c, 0)); }
static void t_MessageBeep(HCall *c) { MessageBeep(W(0)); }
static void t_FlashWindow(HCall *c) { c->ret = FlashWindow(W(1), W(0)); }
static void t_GetKeyState(HCall *c) { c->ret = (uint16_t)GetKeyState(S(0)); }
static void t_GetAsyncKeyState(HCall *c) { c->ret = (uint16_t)GetAsyncKeyState(S(0)); }
static void t_GetSystemMetrics(HCall *c) { c->ret = (uint16_t)GetSystemMetrics(S(0)); }
static void t_GetSysColor(HCall *c) { c->ret = GetSysColor(W(0)); }
static void t_SetRect(HCall *c) { SetRect(w32(c, 4), S(3), S(2), S(1), S(0)); }
static void t_SetRectEmpty(HCall *c) { SetRectEmpty(w32(c, 0)); }
static void t_CopyRect(HCall *c) { c->ret = CopyRect(w32(c, 2), w32(c, 0)); }
static void t_IsRectEmpty(HCall *c) { c->ret = IsRectEmpty(w32(c, 0)); }
static void t_PtInRect(HCall *c) { c->ret = PtInRect(w32(c, 2), w32(c, 0)); }
static void t_OffsetRect(HCall *c) { OffsetRect(w32(c, 2), S(1), S(0)); }
static void t_InflateRect(HCall *c) { InflateRect(w32(c, 2), S(1), S(0)); }
static void t_IntersectRect(HCall *c) { c->ret = IntersectRect(w32(c, 4), w32(c, 2), w32(c, 0)); }
static void t_UnionRect(HCall *c) { c->ret = UnionRect(w32(c, 4), w32(c, 2), w32(c, 0)); }
static void t_EqualRect(HCall *c) { c->ret = EqualRect(w32(c, 2), w32(c, 0)); }
static void t_FillRect(HCall *c) { c->ret = (uint16_t)FillRect(W(3), w32(c, 1), W(0)); }
static void t_FrameRect(HCall *c) { c->ret = (uint16_t)FrameRect(W(3), w32(c, 1), W(0)); }
/* _wsprintf is VARARGS (cdecl): word 0 is the low word of the output pointer, then the format
 * pointer, then the variable arguments from byte 8 on. */
static void t_wsprintf(HCall *c) {
  uint32_t va = MK_FP(FP_SEG(c->args), (uint16_t)(FP_OFF(c->args) + 8));
  c->ret = (uint16_t)_wsprintf(w32(c, 0), w32(c, 2), va);
}

/* Windows 3.1 look (task stars-0z4.9) */
static void t_MessageBox(HCall *c) { c->ret = (uint16_t)MessageBox(W(5), w32(c, 3), w32(c, 1), W(0)); }
static void t_GetWindowDC(HCall *c) { c->ret = GetWindowDC(W(0)); }
static void t_SetScrollPos(HCall *c) { c->ret = (uint16_t)SetScrollPos(W(3), S(2), S(1), W(0)); }
static void t_GetScrollPos(HCall *c) { c->ret = (uint16_t)GetScrollPos(W(1), S(0)); }
static void t_SetScrollRange(HCall *c) { SetScrollRange(W(4), S(3), S(2), S(1), W(0)); }
static void t_GetScrollRange(HCall *c) { GetScrollRange(W(5), S(4), w32(c, 2), w32(c, 0)); }
static void t_ShowScrollBar(HCall *c) { ShowScrollBar(W(2), S(1), W(0)); }
static void t_ShowCursor(HCall *c) { c->ret = (uint16_t)ShowCursor(W(0)); }
static void t_DrawIcon(HCall *c) { c->ret = DrawIcon(W(3), S(2), S(1), W(0)); }
static void t_AdjustWindowRect(HCall *c) { AdjustWindowRect(w32(c, 3), w32(c, 1), W(0)); }
static void t_LoadIcon(HCall *c) { c->ret = LoadIcon(W(2), w32(c, 0)); }
static void t_LoadBitmap(HCall *c) { c->ret = LoadBitmap(W(2), w32(c, 0)); }
/* WinHelp(word str word long): hwnd, lpHelpFile, uCommand, dwData */
static void t_WinHelp(HCall *c) { c->ret = WinHelp(W(7), w32(c, 5), W(4), w32(c, 0)); }
static void t_DestroyIcon(HCall *c) { c->ret = DestroyIcon(W(0)); }
static void t_DestroyCursor(HCall *c) { c->ret = DestroyCursor(W(0)); }

/* Standard controls part 1 (stars-0z4.10): USER.96 CheckRadioButton(word word word word), .97 CheckDlgButton(word word word),
 * .98 IsDlgButtonChecked(word word) (tools/winespec/user.exe16.spec). */
static void t_CheckRadioButton(HCall *c) { c->ret = CheckRadioButton(W(3), S(2), S(1), S(0)); }
static void t_CheckDlgButton(HCall *c) { c->ret = CheckDlgButton(W(2), S(1), W(0)); }
static void t_IsDlgButtonChecked(HCall *c) { c->ret = IsDlgButtonChecked(W(1), S(0)); }

/* Dialog manager (stars-0z4.12). Signatures from tools/winespec/user.exe16.spec:
 * 87 DialogBox(word str word segptr), 88 EndDialog(word s_word), 89 CreateDialog(word str word segptr),
 * 91 GetDlgItem(word word), 92 SetDlgItemText(word word segstr), 93 GetDlgItemText(word word segptr word),
 * 101 SendDlgItemMessage(word word word word long), 308 DefDlgProc(word word word long). */
static void t_DialogBox(HCall *c) { c->ret = (uint16_t)DialogBox(W(5), w32(c, 3), W(2), w32(c, 0)); }
static void t_EndDialog(HCall *c) { c->ret = EndDialog(W(1), S(0)); }
static void t_CreateDialog(HCall *c) { c->ret = CreateDialog(W(5), w32(c, 3), W(2), w32(c, 0)); }
static void t_GetDlgItem(HCall *c) { c->ret = GetDlgItem(W(1), S(0)); }
static void t_SetDlgItemText(HCall *c) { SetDlgItemText(W(3), S(2), w32(c, 0)); }
static void t_GetDlgItemText(HCall *c) { c->ret = (uint16_t)GetDlgItemText(W(4), S(3), w32(c, 1), S(0)); }
static void t_SendDlgItemMessage(HCall *c) { c->ret = SendDlgItemMessage(W(5), S(4), W(3), W(2), w32(c, 0)); }
static void t_DefDlgProc(HCall *c) { c->ret = DefDlgProc(W(4), W(3), W(2), w32(c, 0)); }

/* Menus and accelerators (stars-0z4.13). Signatures from tools/winespec/user.exe16.spec: 150 LoadMenu(word str),
 * 151 CreateMenu(), 152 DestroyMenu(word), 154 CheckMenuItem(word word word), 155 EnableMenuItem(word word word),
 * 156 GetSystemMenu(word word), 157 GetMenu(word), 158 SetMenu(word word), 159 GetSubMenu(word word), 160 DrawMenuBar(word),
 * 177 LoadAccelerators(word str), 178 TranslateAccelerator(word word ptr), 250 GetMenuState(word word word),
 * 263 GetMenuItemCount(word), 410 InsertMenu(word word word word segptr), 411 AppendMenu(word word word segptr),
 * 412 RemoveMenu(word word word), 413 DeleteMenu(word word word), 415 CreatePopupMenu(),
 * 416 TrackPopupMenu(word word s_word s_word s_word word ptr). */
static void t_LoadMenu(HCall *c) { c->ret = LoadMenu(W(2), w32(c, 0)); }
static void t_CreateMenu(HCall *c) { c->ret = CreateMenu(); }
static void t_DestroyMenu(HCall *c) { c->ret = DestroyMenu(W(0)); }
static void t_CheckMenuItem(HCall *c) { c->ret = CheckMenuItem(W(2), W(1), W(0)); }
static void t_EnableMenuItem(HCall *c) { c->ret = EnableMenuItem(W(2), W(1), W(0)); }
static void t_GetSystemMenu(HCall *c) { c->ret = GetSystemMenu(W(1), W(0)); }
static void t_GetMenu(HCall *c) { c->ret = GetMenu(W(0)); }
static void t_SetMenu(HCall *c) { c->ret = SetMenu(W(1), W(0)); }
static void t_GetSubMenu(HCall *c) { c->ret = GetSubMenu(W(1), S(0)); }
static void t_DrawMenuBar(HCall *c) { DrawMenuBar(W(0)); }
static void t_LoadAccelerators(HCall *c) { c->ret = LoadAccelerators(W(2), w32(c, 0)); }
static void t_TranslateAccelerator(HCall *c) { c->ret = (uint16_t)TranslateAccelerator(W(3), W(2), w32(c, 0)); }
static void t_GetMenuState(HCall *c) { c->ret = GetMenuState(W(2), W(1), W(0)); }
static void t_GetMenuItemCount(HCall *c) { c->ret = (uint16_t)GetMenuItemCount(W(0)); }
static void t_InsertMenu(HCall *c) { c->ret = InsertMenu(W(5), W(4), W(3), W(2), w32(c, 0)); }
static void t_AppendMenu(HCall *c) { c->ret = AppendMenu(W(4), W(3), W(2), w32(c, 0)); }
static void t_RemoveMenu(HCall *c) { c->ret = RemoveMenu(W(2), W(1), W(0)); }
static void t_DeleteMenu(HCall *c) { c->ret = DeleteMenu(W(2), W(1), W(0)); }
static void t_CreatePopupMenu(HCall *c) { c->ret = CreatePopupMenu(); }
static void t_TrackPopupMenu(HCall *c) { c->ret = TrackPopupMenu(W(7), W(6), S(5), S(4), S(3), W(2), w32(c, 0)); }

static const struct {
  uint16_t ordinal;
  HApiFn fn;
} g_apis[] = {
    {5, t_InitApp},              {7, t_ExitWindows},        {10, t_SetTimer},
    {12, t_KillTimer},           {13, t_GetTickCount},      {15, t_GetTickCount},
    {17, t_GetCursorPos},        {18, t_SetCapture},        {19, t_ReleaseCapture},
    {22, t_SetFocus},            {23, t_GetFocus},          {59, t_SetActiveWindow},
    {60, t_GetActiveWindow},     {69, t_SetCursor},         {72, t_SetRect},
    {73, t_SetRectEmpty},        {74, t_CopyRect},          {75, t_IsRectEmpty},
    {76, t_PtInRect},            {77, t_OffsetRect},        {78, t_InflateRect},
    {79, t_IntersectRect},       {80, t_UnionRect},         {81, t_FillRect},
    {83, t_FrameRect},           {104, t_MessageBeep},      {105, t_FlashWindow},
    {106, t_GetKeyState},        {173, t_LoadCursor},       {179, t_GetSystemMetrics},
    {180, t_GetSysColor},        {236, t_GetCapture},       {244, t_EqualRect},
    {249, t_GetAsyncKeyState},   {420, t_wsprintf},
    /* USER part 1 */
    {6, t_PostQuitMessage},      {28, t_ClientToScreen},    {29, t_ScreenToClient},
    {30, t_WindowFromPoint},     {31, t_IsIconic},          {32, t_GetWindowRect},
    {33, t_GetClientRect},       {34, t_EnableWindow},      {35, t_IsWindowEnabled},
    {36, t_GetWindowText},       {37, t_SetWindowText},     {38, t_GetWindowTextLength},
    {39, t_BeginPaint},          {40, t_EndPaint},          {41, t_CreateWindow},
    {42, t_ShowWindow},          {46, t_GetParent},         {47, t_IsWindow},
    {49, t_IsWindowVisible},     {53, t_DestroyWindow},     {56, t_MoveWindow},
    {57, t_RegisterClass},       {61, t_ScrollWindow},      {66, t_GetDC},
    {68, t_ReleaseDC},           {107, t_DefWindowProc},    {108, t_GetMessage},
    {109, t_PeekMessage},        {110, t_PostMessage},      {111, t_SendMessage},
    {113, t_TranslateMessage},   {114, t_DispatchMessage},  {122, t_CallWindowProc},
    {124, t_UpdateWindow},       {125, t_InvalidateRect},   {127, t_ValidateRect},
    {133, t_GetWindowWord},      {134, t_SetWindowWord},    {135, t_GetWindowLong},
    {136, t_SetWindowLong},      {232, t_SetWindowPos},     {258, t_MapWindowPoints},
    {262, t_GetWindow},          {272, t_IsZoomed},         {286, t_GetDesktopWindow},
    {370, t_GetWindowPlacement},
    /* Windows 3.1 look (stars-0z4.9) */
    {1, t_MessageBox},           {62, t_SetScrollPos},      {63, t_GetScrollPos},
    {64, t_SetScrollRange},      {65, t_GetScrollRange},    {67, t_GetWindowDC},
    {71, t_ShowCursor},          {84, t_DrawIcon},          {102, t_AdjustWindowRect},
    {174, t_LoadIcon},           {175, t_LoadBitmap},       {171, t_WinHelp},           {267, t_ShowScrollBar},    {457, t_DestroyIcon},
    {458, t_DestroyCursor},
    /* Standard controls part 1 (stars-0z4.10) */
    {96, t_CheckRadioButton},    {97, t_CheckDlgButton},    {98, t_IsDlgButtonChecked},
    /* Dialog manager (stars-0z4.12) */
    {87, t_DialogBox},           {88, t_EndDialog},         {89, t_CreateDialog},
    {91, t_GetDlgItem},          {92, t_SetDlgItemText},    {93, t_GetDlgItemText},
    {101, t_SendDlgItemMessage}, {308, t_DefDlgProc},
    /* Menus and accelerators (stars-0z4.13) */
    {150, t_LoadMenu},           {151, t_CreateMenu},       {152, t_DestroyMenu},
    {154, t_CheckMenuItem},      {155, t_EnableMenuItem},   {156, t_GetSystemMenu},
    {157, t_GetMenu},            {158, t_SetMenu},          {159, t_GetSubMenu},
    {160, t_DrawMenuBar},        {177, t_LoadAccelerators}, {178, t_TranslateAccelerator},
    {250, t_GetMenuState},       {263, t_GetMenuItemCount}, {410, t_InsertMenu},
    {411, t_AppendMenu},         {412, t_RemoveMenu},       {413, t_DeleteMenu},
    {415, t_CreatePopupMenu},    {416, t_TrackPopupMenu},
};

void user_register(void) {
  for (size_t i = 0; i < sizeof g_apis / sizeof g_apis[0]; i++)
    harness_register("USER", g_apis[i].ordinal, g_apis[i].fn);
}
