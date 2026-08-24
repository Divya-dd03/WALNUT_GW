
#ifndef _DIRENT_H_
#define _DIRENT_H_

#include <sys/types.h>

struct dirent {
    long d_ino;
    long d_off;
    unsigned short d_reclen;
    unsigned char d_type;
    char d_name[32];        /* We must not include limits.h! */
};

typedef struct {
    int dd_fd;
    struct dirent dd_buf;   /* got one buf only */
} DIR;

#define DT_UNKNOWN  0
#define DT_FIFO     1
#define DT_CHR      2
#define DT_DIR      4
#define DT_BLK      6
#define DT_REG      8
#define DT_LNK      10
#define DT_SOCK     12
#define DT_WHT      14

int mkdir(const char *pathname, mode_t mode);

int rmdir(const char *pathname);

DIR *opendir(const char *pathname);

struct dirent *readdir(DIR *dirp);

int closedir(DIR *dirp);

#endif /* _DIRENT_H_ */


