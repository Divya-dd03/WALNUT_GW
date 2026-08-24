/**
 ******************************************************************************
 * @file    wm_mex.h
 * @author  Walnut Medical
 * @brief   Memory Explorer (MEX) — command handlers for the Web Serial GUI.
 *          Commands 888-892 dispatched from wm_ui_app.c switch statement.
 ******************************************************************************
 * @attention
 * Copyright (c) 2024 Walnut Medical. All rights reserved.
 ******************************************************************************
 */

#ifndef __WM_MEX_H__
#define __WM_MEX_H__

#ifdef __cplusplus
extern "C" {
#endif

/*******************************************************************************
** MEX command IDs (mirror of WM_TASK_SELECTION entries in wm_enum.h)
******************************************************************************/
#define WM_MEX_CMD_STATS      888   /* Disk + heap stats  → JSON             */
#define WM_MEX_CMD_DIR_LIST   889   /* List directory     → JSON array        */
#define WM_MEX_CMD_FILE_READ  890   /* Read file chunk    → hex JSON          */
#define WM_MEX_CMD_FILE_DEL   891   /* Delete file        → result JSON       */
#define WM_MEX_CMD_FILE_WRITE 892   /* Write file chunk   ← hex JSON upload   */

/*******************************************************************************
** Response framing — every MEX response is wrapped with these delimiters so
** the browser can extract JSON even if debug text leaks on the same port.
******************************************************************************/
#define MEX_START  "<<<MEX:START>>>\r\n"
#define MEX_END    "\r\n<<<MEX:END>>>\r\n"

/*******************************************************************************
** Handler functions — one per MEX command, called from wm_ui_app switch.
******************************************************************************/
void wm_mex_stats(void);       /* cmd 888 */
void wm_mex_dir_list(void);    /* cmd 889 */
void wm_mex_file_read(void);   /* cmd 890 */
void wm_mex_file_del(void);    /* cmd 891 */
void wm_mex_file_write(void);            /* cmd 892 legacy (per-chunk)   */
void wm_mex_file_write_session(void);    /* cmd 892 session-based upload */

#ifdef __cplusplus
}
#endif

#endif /* __WM_MEX_H__ */
