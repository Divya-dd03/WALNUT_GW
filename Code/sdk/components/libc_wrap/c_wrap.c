#include <sys/reent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include "fs_api.h"
#include "_ansi.h"
#include "osi_api.h"
#include <stddef.h>
#include "ipc_cmd.h"
#include "c_wrap.h"
#include "common_api.h"
#include "dirent.h"
#include <sys/time.h>
#include <errno.h>
#include "c_wrap.h"

#define FILE_PATH_LEN_MAX 128

osiMutex_t *tzset_mutex = NULL;

typedef __FILE FILE;

extern uint8_t get_time_value[8];

static int map_fdi_errno_to_lib(int fdi_errno)
{
    switch (fdi_errno) {
    case ERR_NONE:
        return 0;

    case ERR_READ:
    case ERR_WRITE:
    case ERR_ERASE:
    case ERR_ACCESS:
        return EIO;

    case ERR_PARAM:
        return EINVAL;

    case ERR_OPEN:
    case ERR_MAX_OPEN:
    case ERR_FILE_ALREADY_OPEN:
    case ERR_FILE_NUM_LIMIT:
        return ENFILE;

    case ERR_EXISTS:
        return EEXIST;

    case ERR_NOTEXISTS:
    case ERR_NOTOPEN:
    case ERR_FILE_NOT_EXIST:
        return ENOENT;

    case ERR_SPACE:
    case ERR_FLASH_FULL:
    case ERR_FILE_NO_SPACE:
        return ENOSPC;

    case ERR_MEDIA_TYPE:
        return EFTYPE;

    case ERR_NOT_DONE:
        return EINTR;

    case ERR_SYSTEM:
    case ERR_INIT:
        return ENOSYS;

    case ERR_TIMEOUT:
        return ETIMEDOUT;

    case ERR_LOCK:
        return EACCES;

    case ERR_CRASH_N_BURN:
        return EPERM;

    case ERR_MALLOC:
        return ENOMEM;

    case ERR_MAXITEMSIZE:
        return EFBIG;

    case ERR_SIZENOTAVAIL:
        return ENODATA;

    case ERR_FILE_NAME_LIMIT:
        return ENAMETOOLONG;

    default:
        return ENOTSUP;
    }
}

static void add_prefix_if_needed(const char *path, char *result, size_t result_size)
{
    const char *prefix = "D:/";
    size_t prefix_len = strlen(prefix);
    size_t path_len = strlen(path);

    memset(result, 0, result_size);

    // If path starts with "/", remove this character
    if (path[0] == '/') {
        path++;
        path_len--;
    }

    // Check if it already starts with "D:/"
    if (strncmp(path, prefix, prefix_len) != 0) {
        // If result_size is not enough to store the prefix and path, report an error
        if (result_size < prefix_len + path_len + 1) {
            SDK_LOG_E("Result buffer is too small");
            return;
        }

        // Add prefix to the result string
        snprintf(result, result_size, "%s%s", prefix, path);
    } else {
        // Directly copy the path to the result string
        snprintf(result, result_size, "%s", path);
    }
    // SDK_LOG_D("path:%s, result:%s", path, result);
}

static void remove_postfix_if_needed(char *path)
{
    size_t path_len = strlen(path);

    // If path end with "/", remove this character
    if ('/' == path[path_len - 1]) {
        path[path_len - 1] = '\0';
    }
}

FILE *fopen(const char *__restrict _name, const char *__restrict _type)
{
    char newname[FILE_PATH_LEN_MAX];

    add_prefix_if_needed(_name, newname, FILE_PATH_LEN_MAX);

    FILE *fd = malloc(sizeof(FILE));
    if (NULL == fd) {
        errno = ENOMEM;
        return NULL;
    }
    memset(fd, 0, sizeof(FILE));

    int ret = fs_open(newname, _type);
    fd->_file = (short)ret;
    if (fd->_file == 0) {
        SDK_LOG_E("open(%s) fail", newname);
        free(fd);
        errno = map_fdi_errno_to_lib(-1);
        return NULL;
    }

    if (fs_eof((int)fd->_file)) {
        fd->_flags = __SEOF;
    }

    return fd;
}

int fclose(FILE *fd)
{
    int ret = 0;

    if (fd == NULL) {
        errno = EINVAL;
        return -1;
    }

    ret = fs_close(fd->_file);
    if (ret != 0) {
        free(fd);
        errno = map_fdi_errno_to_lib(-1);
        return -1;
    }

    free(fd);
    return 0;
}

size_t fread(void *__restrict ptr, size_t _size, size_t _n, FILE *__restrict fd)
{
    size_t ret = 0;

    ret = fs_read(ptr, _size, _n, (int)fd->_file);
    if (ret <= 0) {
        errno = map_fdi_errno_to_lib(-1);
        return 0;
    }

    if (fs_eof((int)fd->_file)) {
        fd->_flags = __SEOF;
    }

    return ret;
}

size_t fwrite(const void *__restrict ptr, size_t _size, size_t _n, FILE *fd)
{
    size_t ret = 0;

    ret = fs_write(ptr, _size, _n, (int)fd->_file);
    if (ret <= 0) {
        errno = map_fdi_errno_to_lib(-1);
        return 0;
    }

    if (fs_eof((int)fd->_file)) {
        fd->_flags = __SEOF;
    }

    return ret;
}

void f_itoa(int num, char *str, int base)
{
    int i = 0;
    int isNegative = 0;

    if (num == 0) {
        str[i++] = '0';
        str[i] = '\0';
        return;
    }

    if (num < 0 && base == 10) {
        isNegative = 1;
        num = -num;
    }

    while (num != 0) {
        int rem = num % base;
        str[i++] = (rem > 9) ? (rem - 10) + 'a' : rem + '0';
        num = num / base;
    }

    if (isNegative) {
        str[i++] = '-';
    }

    str[i] = '\0';

    for (int j = 0, k = i - 1; j < k; j++, k--) {
        char temp = str[j];
        str[j] = str[k];
        str[k] = temp;
    }
}

void f_ftoa(float num, char *str, int precision)
{
    int intPart = (int)num;
    float fracPart = num - intPart;
    int i = 0;

    if (num < 0) {
        str[i++] = '-';
        intPart = -intPart;
        fracPart = -fracPart;
    }

    f_itoa(intPart, str + i, 10);
    while (str[i] != '\0') {
        i++;
    }

    str[i++] = '.';

    for (int j = 0; j < precision; j++) {
        fracPart *= 10;
        int digit = (int)fracPart;
        str[i++] = digit + '0';
        fracPart -= digit;
    }

    str[i] = '\0';
}

void f_strcpy(char *dest, const char *src)
{
    while (*src) {
        *dest++ = *src++;
    }
    *dest = '\0';
}

int f_vsnprintf(char *__restrict buffer, size_t n, const char *__restrict format, va_list args)
{
    size_t i = 0, j = 0;

    while (format[i] != '\0' && j < n - 1) {
        if (format[i] == '%') {
            i++;
            switch (format[i]) {
            case 'd': {
                int num = va_arg(args, int);
                char temp[32];
                f_itoa(num, temp, 10);
                f_strcpy(buffer + j, temp);
                while (buffer[j] != '\0') {
                    j++;
                }
                break;
            }
            case 's': {
                const char *s = va_arg(args, const char *);
                f_strcpy(buffer + j, s);
                while (buffer[j] != '\0') {
                    j++;
                }
                break;
            }
            case 'f': {
                float num = (float)va_arg(args, double);
                char temp[64];
                f_ftoa(num, temp, 6);
                f_strcpy(buffer + j, temp);
                while (buffer[j] != '\0') {
                    j++;
                }
                break;
            }
            case 'x': {
                int num = va_arg(args, int);
                char temp[32];
                f_itoa(num, temp, 16);
                f_strcpy(buffer + j, temp);
                while (buffer[j] != '\0') {
                    j++;
                }
                break;
            }
            case '%': {
                buffer[j++] = '%';
                break;
            }
            default:
                buffer[j++] = format[i];
                break;
            }
        } else {
            buffer[j++] = format[i];
        }
        i++;
    }
    buffer[j] = '\0';

    return j;
}

int fprintf(FILE *__restrict fd, const char *__restrict format, ...)
{
    int ret = 0;
    va_list args;
    char buffer[1024];

    va_start(args, format);
    f_vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    ret = fwrite(buffer, sizeof(char), strlen(buffer), fd);

    return ret;
}

int vfprintf(FILE *__restrict fd, const char *__restrict format, va_list arg)
{
    int ret = 0;
    char buffer[1024];

    int written = f_vsnprintf(buffer, sizeof(buffer), format, arg);
    if (written > 0) {
        ret = fwrite(buffer, sizeof(char), strlen(buffer), fd);
    }

    return ret;
}

char *fgets(char *__restrict ptr, int n, FILE *__restrict fd)
{
    if (n <= 0 || fd->_file == 0) {
        errno = EINVAL;
        return NULL;
    }

    int result = fread(ptr, sizeof(char), n, fd);
    if (result <= 0) {
        return NULL;
    }

    return ptr;
}

int fputs(const char *__restrict buffer, FILE *__restrict fd)
{
    int ret = 0;
    size_t len = strlen(buffer);

    ret = fwrite(buffer, sizeof(char), len, fd);
    if (ret <= 0 ) {
        return -1;
    }

    return 0;
}



int truncate(const char *path, off_t length)
{
    int ret = 0;

    if (path == NULL) {
        errno = EINVAL;
        return -1;
    }

    FILE *fd = fopen(path, "rb+");
    if (fd == NULL) {
        return -2;
    }

    ret = fs_truncate(fd->_file, length);

    fclose(fd);
    if (ret < 0) {
        errno = map_fdi_errno_to_lib(-1);
        return -1;
    }

    return ret;
}

ssize_t read(int fd, void *buf, size_t count)
{
    size_t ret = 0;

    if (fd <= 0) {
        errno = EBADF;
        return -1;
    }

    ret = fs_read(buf, 1, count, fd);
    if (ret <= 0) {
        errno = map_fdi_errno_to_lib(-1);
        return 0;
    }

    return ret;
}

int ftruncate(int fd, off_t length)
{
    int ret = fs_truncate(fd, length);
    if (ret < 0) {
        errno = map_fdi_errno_to_lib(-1);
    }

    return ret;
}

ssize_t write(int fd, const void *buf, size_t count)
{
    size_t ret = 0;

    if (fd <= 0) {
        errno = EBADF;
        return -1;
    }

    ret = fs_write(buf, 1, count, fd);
    if (ret <= 0) {
        errno = map_fdi_errno_to_lib(-1);
        return 0;
    }

    return ret;
}

off_t lseek(int fd, off_t offset, int whence)
{
    if (fd <= 0) {
        errno = EBADF;
        return 0;
    }

    int ret = fs_seek(fd, offset, whence);
    if (ret < 0) {
        errno = map_fdi_errno_to_lib(-1);
    }

    return ret;
}

int stat(const char *__restrict __path, struct stat *__restrict __sbuf )
{
    FileStat_t stat = {0};
    char newname[FILE_PATH_LEN_MAX];

    add_prefix_if_needed(__path, newname, FILE_PATH_LEN_MAX);

    if (fs_stat(newname, &stat) != 0) {
        errno = map_fdi_errno_to_lib(-1);
        return -1;
    }

    __sbuf->st_size = stat.file_size;
    __sbuf->st_blksize = 4 * 1024;
    __sbuf->st_blocks = stat.file_size / __sbuf->st_blksize + 1;
    if (stat.file_type == 1) {
        // dir
        __sbuf->st_mode |= _IFDIR;
    } else {
        __sbuf->st_mode |= _IFREG;
    }

    return 0;
}

int lstat(const char *__restrict __path, struct stat *__restrict __sbuf )
{
    return stat(__path, __sbuf);
}

double difftime(time_t _time2, time_t _time1)
{
    double ret;

    ret = (double)(_time2 - _time1);
    return ret;
}

//int _gettimeofday(struct timeval *tv, void *__tz)
//{
//    return gettimeofday(tv, __tz);
//}

#if 0
extern unsigned int millisecond_get(void);

time_t mktime(struct tm *_timeptr)
{
    time_t rtc_time_info = 0;
    osiTime_t *tm;
    tm = malloc(sizeof(osiTime_t));
    memcpy(tm, _timeptr, 7 * sizeof(int));
    rtc_time_info = osiMkTime(tm);
    free(tm);
    return rtc_time_info;
}

time_t time(time_t *_timer)
{
    osiTime_t get_time = {0};
    time_t rtc_time_info = 0;

    osiGetTime(&get_time);
    rtc_time_info = osiMkTime(&get_time);
    *_timer = rtc_time_info;
    return rtc_time_info;
}

struct tm *gmtime(const time_t *_timer)
{
    static struct tm tm_timer = {0};
    time_t timeValue = *_timer;
    long timezoneOffset = 28800;

    // total secondsSince1970
    time_t secondsSince1970 = timeValue;

    secondsSince1970 -= timezoneOffset;

    // get year
    int year = 1970;
    while (secondsSince1970 > 31536000) { //one year seconds
        secondsSince1970 -= 31536000;
        year++;
        // leap year
        if ((((year % 4) == 0) && ((year % 100) != 0)) || ((year % 400) == 0)) {
            if (secondsSince1970 > 31622400) { // leap year seconds
                secondsSince1970 -= 31622400;
                year++;
            } else {
                break;
            }
        }
    }

    // get month
    int month = 1;
    int daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    while (secondsSince1970 > 86400 * daysInMonth[month - 1]) {
        secondsSince1970 -= 86400 * daysInMonth[month - 1];
        month++;
        if (((month == 2) && (((year % 4) == 0) && ((year % 100) != 0))) || ((year % 400) == 0)) {
            if (secondsSince1970 > 86400 * 29) {
                secondsSince1970 -= 86400 * 29;
            } else {
                break;
            }
        }
    }

    // get day
    int day = 1;
    while (secondsSince1970 > 86400) {
        secondsSince1970 -= 86400;
        day++;
    }

    // get hour
    int hour = 0;
    while (secondsSince1970 > 3600) {
        secondsSince1970 -= 3600;
        hour++;
    }

    // get minute
    int minute = 0;
    while (secondsSince1970 > 60) {
        secondsSince1970 -= 60;
        minute++;
    }

    // get second
    int second = secondsSince1970;

    // get tm_wday
    int daysSince1970 = (timeValue - timezoneOffset) / 86400;
    int weekDay = (daysSince1970 + 4) % 7; // 1970.1.1 is Thursday

    // get tm_yday
    int yday = 0;
    for (int i = 0; i < month - 1; i++) {
        yday += daysInMonth[i];
        if (((i == 1) && (((year % 4) == 0) && ((year % 100) != 0))) || ((year % 400) == 0)) {
            yday += 1;
        }
    }
    yday += day - 1;

    tm_timer.tm_year = year - 1900;
    tm_timer.tm_mon = month - 1;
    tm_timer.tm_mday = day;
    tm_timer.tm_hour = hour;
    tm_timer.tm_min = minute;
    tm_timer.tm_sec = second;
    tm_timer.tm_wday = weekDay;
    tm_timer.tm_yday = yday;
    tm_timer.tm_isdst = -1;
    tm_timer.tm_gmtoff = timezoneOffset;
    tm_timer.tm_zone = "GMT";

    return &tm_timer;

}

struct tm *Localtime(const time_t *_timer)
{
    static struct tm tm_timer = {0};
    time_t timeValue = *_timer;
    long timezoneOffset = 28800;

    // total secondsSince1970
    time_t secondsSince1970 = timeValue;

    // get year
    int year = 1970;
    while (secondsSince1970 > 31536000) { //one year seconds
        secondsSince1970 -= 31536000;
        year++;
        // leap year
        if ((((year % 4) == 0) && ((year % 100) != 0)) || ((year % 400) == 0)) {
            if (secondsSince1970 > 31622400) { // leap year seconds
                secondsSince1970 -= 31622400;
                year++;
            } else {
                break;
            }
        }
    }

    // get month
    int month = 1;
    int daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    while (secondsSince1970 > 86400 * daysInMonth[month - 1]) {
        secondsSince1970 -= 86400 * daysInMonth[month - 1];
        month++;
        if (((month == 2) && (((year % 4) == 0) && ((year % 100) != 0))) || ((year % 400) == 0)) {
            if (secondsSince1970 > 86400 * 29) {
                secondsSince1970 -= 86400 * 29;
            } else {
                break;
            }
        }
    }

    // get day
    int day = 1;
    while (secondsSince1970 > 86400) {
        secondsSince1970 -= 86400;
        day++;
    }

    // get hour
    int hour = 0;
    while (secondsSince1970 > 3600) {
        secondsSince1970 -= 3600;
        hour++;
    }

    // get minute
    int minute = 0;
    while (secondsSince1970 > 60) {
        secondsSince1970 -= 60;
        minute++;
    }

    // get second
    int second = secondsSince1970;

    // get tm_wday
    int daysSince1970 = timeValue / 86400;
    int weekDay = (daysSince1970 + 4) % 7; // 1970.1.1 is Thursday

    // get tm_yday
    int yday = 0;
    for (int i = 0; i < month - 1; i++) {
        yday += daysInMonth[i];
        if (((i == 1) && (((year % 4) == 0) && ((year % 100) != 0))) || ((year % 400) == 0)) {
            yday += 1;
        }
    }
    yday += day - 1;

    tm_timer.tm_year = year - 1900;
    tm_timer.tm_mon = month - 1;
    tm_timer.tm_mday = day;
    tm_timer.tm_hour = hour;
    tm_timer.tm_min = minute;
    tm_timer.tm_sec = second;
    tm_timer.tm_wday = weekDay;
    tm_timer.tm_yday = yday;
    tm_timer.tm_isdst = -1;
    tm_timer.tm_gmtoff = timezoneOffset;
    tm_timer.tm_zone = "CST";

    return &tm_timer;
}
#endif

int puts(char const *s)
{
    RTI_LOG(s);
    return 0;
}

int rename(const char *oldname, const char *newname)
{
    if (oldname == NULL || newname == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (0 == strcmp(oldname, newname)) {
        errno = EEXIST;
        return -1;
    }

    char fileoldname[FILE_PATH_LEN_MAX];
    add_prefix_if_needed(oldname, fileoldname, FILE_PATH_LEN_MAX);

    char filenewname[FILE_PATH_LEN_MAX];
    add_prefix_if_needed(newname, filenewname, FILE_PATH_LEN_MAX);

    int ret = fs_rename(fileoldname, filenewname);
    if (ret < 0) {
        errno = map_fdi_errno_to_lib(-1);
    }

    return ret;
}

int remove(const char *filename)
{
    if (filename == NULL) {
        errno = EINVAL;
        return -1;
    }

    char newname[FILE_PATH_LEN_MAX];
    add_prefix_if_needed(filename, newname, FILE_PATH_LEN_MAX);

    int ret = fs_remove(newname);
    if (ret < 0) {
        errno = map_fdi_errno_to_lib(-1);
    }

    return ret;
}

long int ftell(FILE *stream)
{
    if (stream == NULL) {
        errno = EINVAL;
        return -1;
    }

    long int ret = fs_tell(stream->_file);
    if (ret == -1) {
        errno = map_fdi_errno_to_lib(-1);
        return -1;
    }

    return ret;
}

off_t ftello(FILE *stream)
{
    return (off_t)ftell(stream);
}

int fseek(FILE *stream, long int offset, int whence)
{
    if (stream == NULL) {
        errno = EINVAL;
        return -1;
    }

    int ret = fs_seek(stream->_file, offset, whence);
    if (ret < 0) {
        errno = map_fdi_errno_to_lib(-1);
    }

    return ret;
}

int fseeko(FILE *stream, off_t offset, int whence)
{
    int ret = fseek(stream, (long int)offset, whence);
    if (ret < 0) {
        return -1;
    }
    return 0;
}

DIR *opendir(const char *name)
{
    if (name == NULL) {
        errno = EBADF;
        return NULL;
    }

    char newname[FILE_PATH_LEN_MAX];
    add_prefix_if_needed(name, newname, FILE_PATH_LEN_MAX);
    remove_postfix_if_needed(newname);

    DIR *open_dir = malloc(sizeof(DIR));
    if (open_dir == NULL) {
        errno = ENOMEM;
        return NULL;
    }

    uint32_t ret = fs_opendir(newname);
    if (ret == 0) {
        free(open_dir);
        errno = map_fdi_errno_to_lib(-1);
        return NULL;
    }

    memset(open_dir, 0, sizeof(DIR));
    open_dir->dd_fd = ret;
    memcpy(open_dir->dd_buf.d_name, name, 30 * sizeof(char));

    return open_dir;
}

struct dirent *readdir(DIR *dirp)
{
    if (dirp == NULL) {
        errno = EBADF;
        return NULL;
    }
    static struct dirent get_dir = {0};
    // struct stat st = {0};
    DirFileInfo_t read_info = {0};
    // char newname[FILE_PATH_LEN_MAX];

    uint32_t ret = fs_readdir(dirp->dd_fd, &read_info);

    if (ret == -1 || strlen(read_info.file_name) == 0) {
        errno = map_fdi_errno_to_lib(-1);
        return NULL;
    }
    memcpy(get_dir.d_name, read_info.file_name, 32);
    // RTI_LOG("fs_readdir ret: %d - %s per: 0x%x", ret, read_info.file_name, read_info.permissions);
    // add_prefix_if_needed(get_dir.d_name, newname, FILE_PATH_LEN_MAX);
    // ret = stat(newname, &st);
    // RTI_LOG("stat ret: %d - %s", ret, newname);
    // if(ret != 0){
    // return NULL;
    // }
    if (read_info.permissions == 0x200) {
        get_dir.d_type = DT_DIR;
    } else {
        get_dir.d_type = DT_REG;
    }

    return &get_dir;
}

off_t telldir(DIR *dir)
{
    if (dir == NULL) {
        errno = EBADF;
        return -1;
    }

    off_t ret = fs_telldir(dir->dd_fd);
    if (ret < 0) {
        errno = map_fdi_errno_to_lib(-1);
    }

    return ret;
}

void seekdir(DIR *dir, off_t offset)
{
    if (dir == NULL) {
        errno = EBADF;
        return;
    }

    int ret = fs_seekdir(dir->dd_fd, offset);
    if (ret < 0) {
        errno = map_fdi_errno_to_lib(-1);
    }

    return ;
}

void rewinddir(DIR *dir)
{
    if (dir == NULL) {
        errno = EBADF;
        return;
    }

    int ret = fs_rewinddir(dir->dd_fd);
    if (ret < 0) {
        errno = map_fdi_errno_to_lib(-1);
    }

    return ;
}

int unlink(const char *file_name)
{
    int ret = -1;

    if (file_name == NULL) {
        errno = EINVAL;
        return -1;
    }

    char newname[FILE_PATH_LEN_MAX];
    add_prefix_if_needed(file_name, newname, FILE_PATH_LEN_MAX);

    ret = fs_remove(newname);
    if (ret < 0) {
        errno = map_fdi_errno_to_lib(-1);
    }

    return ret;
}

int access(const char *pathname, int mode)
{
    if (pathname == NULL) {
        errno = EINVAL;
        return -1;
    }

    struct stat st = {0};
    char newname[FILE_PATH_LEN_MAX];

    add_prefix_if_needed(pathname, newname, FILE_PATH_LEN_MAX);
    remove_postfix_if_needed(newname);

    int ret = stat(newname, &st);
    if (ret != 0) {
        return -1;
    }

    if (S_ISDIR(st.st_mode)) {
        return 0;
    }

    if (st.st_size > 0) {
        return 0;
    }

    errno = EACCES;
    return -2;
}

int closedir(DIR *dir)
{
    if (dir == NULL) {
        errno = EBADF;
        return -1;
    }

    uint32_t ret = fs_closedir(dir->dd_fd);
    if (ret == -1) {
        free(dir);
        errno = map_fdi_errno_to_lib(-1);
        return -1;
    }

    free(dir);
    return 0;
}

int mkdir(const char *name, mode_t mode)
{
    if (name == NULL) {
        errno = EBADF;
        return -1;
    }

    char newname[FILE_PATH_LEN_MAX];
    add_prefix_if_needed(name, newname, FILE_PATH_LEN_MAX);
    remove_postfix_if_needed(newname);

    int ret = fs_makedir(newname);
    if (ret < 0) {
        errno = map_fdi_errno_to_lib(-1);
    }

    return ret;
}

int rmdir(const char *name)
{
    if (name == NULL) {
        errno = EBADF;
        return -1;
    }

    char newname[FILE_PATH_LEN_MAX];
    add_prefix_if_needed(name, newname, FILE_PATH_LEN_MAX);
    remove_postfix_if_needed(newname);

    int ret = fs_remove(newname);
    if (ret < 0) {
        errno = map_fdi_errno_to_lib(-1);
    }

    return ret;
}

char *strdup(const char *str)
{
    size_t len = strlen (str) + 1;

    char *copy = malloc (len);
    if (copy) {
        memcpy (copy, str, len);
    }

    return copy;
}

// void _tzset_unlocked_r(struct _reent *r)
// {
// time_t time_sec = 0;
// static struct tm *tm_timer_loc = NULL;
// time(&time_sec);
// tm_timer_loc = Localtime(&time_sec);
// r->daylight = tm_timer_loc->tm_isdst;
// r->tm_gmtoff = tm_timer_loc->tm_gmtoff;
// r->tzname = tm_timer_loc->tm_zone;
// return ;
// }
// void _tzset_unlocked(void)
// {
// if(tzset_mutex != NULL) {
// osiMutexUnlock(tzset_mutex);
// }
// return;
// }
// void _tz_lock(void)
// {
// if(tzset_mutex == NULL) {
// tzset_mutex = osiMutexCreate();
// }
// osiMutexLock(tzset_mutex);
// return;
// }

//void _exit(int status)
//{
//    _exit(status);
//}

//void _kill(void)
//{
//}
#if 0
void _getpid(void)
{
}

void _write(void)
{
}

void _close(void)
{
}

void _fstat(void)
{
}

void _isatty(void)
{
}

void _lseek(void)
{
}

void _read(void)
{
}

void _link(void)
{
}

void _unlink(void)
{
}
#endif