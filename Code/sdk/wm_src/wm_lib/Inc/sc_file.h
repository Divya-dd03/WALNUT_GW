/**
  ******************************************************************************
  * @file    sc_file.h
  * @brief   File system header file.
  * @author  Walnut Medical
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2024 Walnut Medical
  * All rights reserved.
  *
  ******************************************************************************
  */
#ifndef __SC_FILE_H__
#define __SC_FILE_H__

#include "sc_enum.h"
#include "sc_def.h"
#include "zx_api.h"

#define sAPI_access(filename) wm_file_exists(filename)
#define sAPI_remove(filename) wm_file_remove(filename)
#define MAX_FILE_LINE_LEN 1025

/* File Functions */
SC_STATUS wm_write_file_wb(const char* fileData, size_t dataLength, const char* filePath, WriteFileOption file_option);
SC_STATUS wm_append_file_ab(const char* fileData, size_t dataLength, const char* filePath);
SC_STATUS wm_read_file_rb(const char* filepath, char* buff);
UINT32 wm_get_file_size(const char* file_path);
BOOL wm_file_exists(const char* filename);
int wm_file_remove(const char* t_path);
int wm_fread_line(int* file_hdl, char* buff);
BOOL wm_checkFilesInCSV(const char* csvFilePath);

/* Directory Functions */
SC_STATUS wm_create_folder(char* t_folder);
void wm_list_curr_dir(char* dir_path);
int wm_rmDir_recu(const char* dirPath, int depth);

/* Memory Functions */
int wm_format_D(void);
void wm_intrmem_chk(int* t_total, int* t_free, int* t_used);
void wm_extrmem_chk(int* t_total, int* t_free, int* t_used);
int wm_get_disk_info(const char* dirPath, int* num_files, int* total_size);
int _wm_get_disk_info(const char* dirPath, int depth, int* num_files, int* total_size);
void wm_get_partition_info(const char* ptable_name);

#endif