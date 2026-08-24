/* All rights reserved.
 *
 */

#ifndef _FS_API_H_
#define _FS_API_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "osi_compiler.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MOUNT_ONLY (0)
#define MOUNT_FORMAT_ON_FAIL (1)
#define MOUNT_FORCE_FORMAT (2)
#define FS_SPI_CLK_52M   (2)
#define FS_SPI_CLK_26M   (1)
#define FS_SPI_CLK_13M   (0)
typedef struct
{
   char volume_label[4];
   uint8_t spi_port;
   uint8_t spi_clock;
   uint32_t offset;
   uint32_t size;
   uint32_t eb_size;
   uint32_t pb_size;
   uint32_t flags;
} ExtFsConfig_t;
/**
 * @brief file info
 */
typedef struct
{
   char file_name[256];	   ///< file name
   uint32_t size;          ///< size of file data in bytes
#ifndef LTEONLY_THIN
   uint32_t owner_id;
   uint32_t group_id;
#endif
   uint32_t permissions;
} DirFileInfo_t;

/**
 * @brief file state
 */
typedef struct
{
	char file_name[256];	   ///< file name
	uint32_t file_size;		///< size of file data in bytes
	int file_type;			   ///< file type: 0 is file��1 is directory
}FileStat_t;

/**
 * @brief open file
 *
 * @param filename   file name
 * @param mode       type specification of string
 *
 * @return
 *       -  stream index of file
 *          if an error is detected, FDI_fopen returns 0
 */
int fs_open(const char *filename, const char *mode);

/**
 * @brief close file
 *
 * @param stream  stream index of file
 *
 * @return
 *      -  0   success
 *      - -1   fail
 */
int fs_close(int stream);

/**
 * @brief read file
 *
 * @param ptr     pointer to buffer to place data read
 * @param size    size of element referenced by buffer pointer
 * @param nmemb   number of elements to be read
 * @param stream  stream index of file
 *
 * @return number of elements succesfully read
 */
size_t fs_read(void *ptr, size_t size, size_t nmemb, int stream);

/**
 * @brief write file
 *
 * @param ptr     pointer to buffer to place data written
 * @param size    size of element referenced by buffer pointer
 * @param nmemb   number of elements to be writted
 * @param stream  stream index of file
 *
 * @return number of elements succesfully write
 */
size_t fs_write(const void *ptr, size_t size, size_t nmemb, int stream);

/**
 * @brief seek file
 *
 * @param stream  stream index of file
 * @param offset  the offset to obtain to the position indicator (in bytes)
 * @param whence  the position to add offset(SEEK_SET, SEEK_CUR, SEEK_END)
 *
 * @return
 *      -  0   filename is valid
 *      - -1   fail
 */
int fs_seek(int stream, long int offset, int whence);

/**
 * @brief remove file
 *
 * @param filename   file name
 *
 * @return
 *      -  0   filename is valid
 *      - -1   fail
 */
int fs_remove(const char *filename);

/**
 * @brief rename file
 *
 * @param old_filename  old name of file
 * @param new_filename  new name to rename file
 *
 * @return
 *      -  0   old filename is valid
 *      - -1   fail
 */
int fs_rename(const char *old_filename, const char *new_filename);

/**
 * @brief get size of file
 *
 * @param filename   file name
 *
 * @return
 *      - size   size of file, element in FileStat_t
 *      - 0  fail
 */
uint32_t fs_size(const char *filename);

/**
 * @brief get size of file
 *
 * @param stream  stream index of file
 *
 * @return
 *      - size   size of file
 *      - 0  fail
 */
uint32_t fs_fsize(int stream);

/**
 * @brief get state of file
 *
 * @param filename   file name
 * @param stat       pointer to file state
 *
 * @return
 *      - 0       get status success
 *      - others  fail
 */
uint32_t fs_stat(const char *filename, FileStat_t *stat);

/**
 * @brief synchronize file data to hard disk
 *
 * @note  this interface is unusable, as a reserved interface
 *
 * @param stream  stream index of file
 *
 * @return
 *      -  0
 */
uint32_t fs_sync(int stream);

/**
 * @brief get the position of the file pointer
 *
 * @param stream  stream index of file
 *
 * @return
 *      - position   the position of file
 *      - others     fail
 */
int fs_tell(int stream);

/**
 * @brief test for end of file
 *
 * @param fd   file name id
 *
 * @return
 *      - true   filename is valid
 *      - false  fail
 */
int fs_eof(int fd);

/**
 * @brief report the number of bytes free
 *
 * @return
 *      - bytes   bytes free
 *      - others  fail
 */
uint32_t fs_freeSize(void);

/**
 * @brief report the number of bytes total
 *
 * @return
 *      - bytes   bytes total
 *      - others  fail
 */
uint32_t fs_totalSize(void);

/**
 * @brief external flash, report the number of bytes total
 *
 * @return
 *      - bytes   bytes total
 *      - others  fail
 */
uint32_t fs_exttotalSize(void);

/**
 * @brief report the number of bytes used
 *
 * @return
 *      - bytes   bytes used
 *      - others  fail
 */
uint32_t fs_usedSize(void);

/**
 * @brief make a dictionary
 *
 * @param dirname   dictionary name
 *
 * @return
 *      -  0      success
 *      - -1      fail
 */
uint32_t fs_makedir(const char *dirname);

/**
 * @brief open a dictionary
 *
 * @param dirname   dictionary name
 *
 * @return  stream index of dictionary
 *          if an error is detected, FDI_fopen returns 0
 */
uint32_t fs_opendir(const char *dirname);

/**
 * @brief read a dictionary
 *
 * @param dirstream     dictionary id
 * @param pFileInfo     file info
 *
 * @return
 *      -  1      success
 *      - -1      fail
 */
uint32_t fs_readdir(int dirstream, DirFileInfo_t *pFileInfo);

/**
 * @brief close a dictionary
 *
 * @param dirstream     dictionary id
 *
 * @return
 *      -  0      success
 *      - -1      fail
 */
uint32_t fs_closedir(int dirstream);

/**
 * @brief mount ext file system
 *
 * @param cfg     ext file system config
 *
 * @return
 *      -  true      success
 *      - -false      fail
 */
bool fs_mountExt(ExtFsConfig_t *cfg);

/**
 * @brief external flash, report the number of bytes free
 *
 * @param path  the Disk character
 *
 * @return
 *      - bytes   bytes free
 *      - others  fail
 */
uint32_t fs_extFreeSize(const char *path);

/**
 * @brief format file system
 *
 * @param path  the Disk character
 *
 * @return
 *      -  true      success
 *      - -false      fail
 */
bool fs_format(const char *volume_label);


int fs_truncate(int fd, size_t size);
int fs_telldir(int dirstream);
int fs_seekdir(int dirstream, uint32_t off);
int fs_rewinddir(int dirstream);

#ifdef __cplusplus
}
#endif
#endif

//Note: mkdir has added the check of file paths. add_prefix_if_needed(name, newname, FILE_PATH_LEN_MAX);remove_postfix_if_needed(newname);