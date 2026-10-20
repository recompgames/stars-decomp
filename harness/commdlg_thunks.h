/* Native-only adapter: registers COMMDLG.GetOpenFileName (1), GetSaveFileName (2), PrintDlg (20)
 * and CommDlgExtendedError (26) of runtime/commdlg.h with the harness dispatcher. */
#ifndef STARS_HARNESS_COMMDLG_THUNKS_H
#define STARS_HARNESS_COMMDLG_THUNKS_H

void commdlg_register(void);

#endif /* STARS_HARNESS_COMMDLG_THUNKS_H */
