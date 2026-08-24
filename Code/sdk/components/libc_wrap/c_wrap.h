/*
 * @file    cisfs_api.h
 * @author  .
 * @date
 * DO NOT MODIFY IT!
 */


#ifndef _EXPORT_C_WRAP_H_
#define _EXPORT_C_WRAP_H_
#include <sys/reent.h>
#include <sys/types.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include "fs_api.h"
#include "_ansi.h"
#include <stddef.h>
#include "reent.h"
#include "dirent-cis.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef __FILE FILE;

typedef enum {
    ERR_NONE = 0,               /* 0x00 - command was successful */
    ERR_READ,                   /* 0x01 - error reading the flash */
    ERR_WRITE,                  /* 0x02 - error writing to the flash */
    ERR_PARAM,                  /* 0x03 - incorrect parameter to a function */
    ERR_OPEN,                   /* 0x04 - a data has been opened already */
    ERR_EXISTS,                 /* 0x05 - data already exists */
    ERR_NOTEXISTS,              /* 0x06 - data does not exist */
    ERR_SPACE,                  /* 0x07 - no free space left. needs reclaim */
    ERR_NOTOPEN,                /* 0x08 - accessed an unopened data stream */
    ERR_ERASE,                  /* 0x09 - error erasing the flash block */
    ERR_MAX_EXISTS,             /* 0x0A - defined maximum parameter limit */
    ERR_FORMAT,                 /* 0x0B - unformatted media */
    ERR_MEDIA_TYPE,             /* 0x0C - unsupported media type */
    ERR_NOT_DONE,               /* 0x0D - aborted before completion */
    ERR_WRITE_PROTECT,          /* 0x0E - attempt write on protected media */
    ERR_FLASH_FULL,             /* 0x0F - flash media is full */
    ERR_SYSTEM,                 /* 0x10 - system error */
    ERR_MAX_OPEN,               /* 0x11 - tried to open more than maximum
                                *        number of data that can be opened at
                                *        any given time. */
    ERR_INIT,                   /* 0x12 - error during initialization */
    ERR_Q_FULL,                 /* 0x13 - FDI queue is full */
//#ifdef LWIP_IPNETBUF_SUPPORT
//    ERR_TIME_OUT,               /* 0x14 - system timeout */
//#else
    ERR_TIMEOUT,                /* 0x14 - system timeout */
//#endif
    ERR_SUSPEND,                /* 0x15 - flash is in program suspended mode. */
    ERR_NOTRDY,                 /* 0x16 - flash device not ready */
    ERR_LOCK,                   /* 0x17 - error while locking */
    ERR_Q_NOT_EMPTY,            /* 0x18 - error while queue is not empty */
    ERR_PCKTID_MUTEX,           /* 0x19 - Try to Operate to packet object when
                                *        it is not finished */
    ERR_WIP,                    /* 0x1A - Indicates that there is already a
                                *        WIP object */
    ERR_CRASH_N_BURN,           /* 0x1B - not sure what this error code means */
    ERR_NO_MORE_ENTRIES,        /* 0x1C - err code used by DAV internally */
    ERR_PLR_TEST_FAILURE,       /* 0x1D - err code used by DAV PLR */
    ERR_DIFF_SIZE,              /* 0x1E - err code used by DAV Compare Object */
    ERR_NOT_COMPARE,            /* 0x1F - err code used by DAV Compare Object */
    ERR_ACCESS,                 /* 0x20 - file system access error */
    ERR_OPENMODE,               /* 0x21 - file was opened in the wrong mode */
    ERR_EOF,                    /* 0x22 - error return from test gsm if end of
                                *        file is true */
    ERR_MALLOC,                 /* 0x23 - error code for malloc error */
    ERR_PERF,                   /* 0x24 - error code for perfromance test */
    ERR_MAXITEMSIZE,            /* 0x25 - maximum data size limit reached */
    ERR_TRUNCATE,               /* 0x26 -  */
    ERR_STATE,                  /* 0x27 - error in DAV state, indicates a sw bug */
    ERR_NOTHING_TO_DO,          /* 0x28 - nothing to defrag */
    ERR_NOTNEEDED,              /* 0x29 - defrag not needed */
    ERR_NODEFRAGWHILEWIP,       /* 0x2A - can't defrag while WIP object exists */
    ERR_PINNED,                 /* 0x2B - can't deallocate or reallocate a pinned object */
    ERR_SIZENOTAVAIL,           /* 0x2C - unable to provide the requested size */
    ERR_FILE_NUM_LIMIT,
    ERR_FILE_NAME_LIMIT,
    ERR_FILE_ALREADY_OPEN,
    ERR_FILE_NOT_EXIST,
    ERR_FILE_NO_SPACE,

    ERR_MAX_CODE                /* This entry should always be the
                                * LAST ENTRY in this enum */
} FDI_ERR_CODE;

// typedef struct _reent {
//     char *env;
//     char *tzname;
//     int daylight;
//     long tm_gmtoff;
//     char *current_locale;
//     char *current_category;
//     char *locale_names[2];
// } _reent;

// struct dirent {
//     long d_ino;
//     long d_off;
//     unsigned short d_reclen;
//     unsigned char d_type;
//     char d_name[32];        /* We must not include limits.h! */
// };

// typedef struct {
//     int dd_fd;
//     struct dirent dd_buf;   /* got one buf only */
// } DIR;


//O_RDONLY and O_WRONLY and O_RDWR mutually exclusive
//#define     O_RDONLY    1<<0    //0x0000     just read
//#define     O_WRONLY    1<<1    //0x0001     just write
//#define     O_RDWR      1<<2    //0x0002     read and write
//#define     O_CREAT     1<<3    //0x0200     create and open
//#define     O_TRUNC     1<<4    //0x0400    just for O_WRONLY   if file exist,Delete content and then write
//#define     O_APPEND    1<<5    //0x0008    just for O_WRONLY and O_RDWR    Append at the end

//struct tm {
//    int  tm_sec;     /* seconds after the minute [0-60] */
//    int  tm_min;     /* minutes after the hour [0-59] */
//    int  tm_hour;    /* hours since midnight [0-23] */
//    int  tm_mday;    /* day of the month [1-31] */
//    int  tm_mon;     /* months since January [0-11] */
//    int  tm_year;    /* years since 1900 */
//    int  tm_wday;    /* days since Sunday [0-6] */
//    int  tm_yday;    /* days since January 1 [0-365] */
//    int  tm_isdst;   /* Daylight Savings Time flag */
//    long tm_gmtoff;  /* offset from CUT in seconds */
//    char *tm_zone;   /* timezone abbreviation */
//};

// FILE *fopen(const char *__restrict _name, const char *__restrict _type);
// int fclose(FILE *fd);
// size_t fread(void *__restrict ptr, size_t _size, size_t _n, FILE *__restrict fd);
// size_t fwrite(const void *__restrict ptr , size_t _size, size_t _n, FILE * fd);
// int fprintf(FILE *__restrict fd, const char *__restrict format, ...);
// int vfprintf(FILE *__restrict fd, const char *__restrict format, va_list arg);
// char *fgets(char *__restrict ptr, int n, FILE *__restrict fd);
// int	fputs (const char *__restrict buffer, FILE *__restrict fd);

//int open(const char *pathname, int flags);
//ssize_t read(int fd, void * buf, size_t count);
//ssize_t write(int fd, const void * buf, size_t count);
//int close(int fd);

 int mkdir(const char *pathname, mode_t mode);

 int rmdir(const char *pathname);

 DIR *opendir(const char *pathname);

 struct dirent *readdir(DIR *dirp);

 int closedir(DIR *dirp);

 void rewinddir(DIR *dir);

 void seekdir(DIR * dir, off_t offset);

 off_t telldir(DIR *dir);

//double difftime(time_t _time2, time_t _time1);
//time_t mktime(struct tm *_timeptr);
//time_t time(time_t *_timer);
//struct tm *gmtime(const time_t *_timer);
//struct tm *Localtime(const time_t *_timer);


// void _tzset_unlocked_r(struct _reent *);
// void _tzset_unlocked(void);
// void _tz_lock(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _EXPORT_H_ */
