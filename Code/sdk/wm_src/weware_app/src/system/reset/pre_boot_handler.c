/**
 * @file pre_boot_handler.c
 * @brief Pre-boot filesystem record I/O and persistence before SoC reset.
 */

#include "system/reset/pre_boot_handler.h"
#include "system/storage/file_system.h"
#include "common/utils.h"

#include <string.h>

#define LOG_TAG          "PREBOOT"
#define LOG_MODULE_LEVEL LOG_LEVEL_DEBUG
#include "module/log/log.h"

#define PRE_BOOT_MAGIC    0x46494250u  /* 'PBIF' */
#define PRE_BOOT_VERSION  4u

#pragma pack(push, 1)
typedef struct {
    UINT32 magic;
    UINT32 version;
    UINT32 reset_type;
    UINT32 reset_time;
    UINT32 total_resets;
    char   reset_module[sizeof(((ResetHandlerLastSwResetInfo *)0)->module_name)];
    UINT32 tcp_send_queue_file_read_offset;
    UINT8  tcp_send_queue_offset_valid;
} PreBootOnDisk;
#pragma pack(pop)

enum { PB_CACHE_HAVE = 1u << 0 };

static const char *const k_reset_type_name[] = { "NONE", "CFUN", "SOFT", "HARD" };

static PreBootOnDisk s_disk;
static UINT8         s_cache_flags;

/* --- Small helpers --- */

const char *reset_type_name(ResetType type)
{
    unsigned u = (unsigned)type;
    return (u < sizeof(k_reset_type_name) / sizeof(k_reset_type_name[0]))
               ? k_reset_type_name[u]
               : "?";
}

static BOOL reset_type_valid(UINT32 v)
{
    return v <= (UINT32)RESET_TYPE_HARD;
}

static BOOL disk_header_ok(const PreBootOnDisk *r)
{
    return r != NULL
        && r->magic == PRE_BOOT_MAGIC
        && r->version == PRE_BOOT_VERSION
        && reset_type_valid(r->reset_type);
}

static void disk_init_empty(PreBootOnDisk *r)
{
    memset(r, 0, sizeof(*r));
    r->magic   = PRE_BOOT_MAGIC;
    r->version = PRE_BOOT_VERSION;
}

static void disk_clear_reset_slot(PreBootOnDisk *r)
{
    r->reset_type      = (UINT32)RESET_TYPE_NONE;
    r->reset_time      = 0;
    r->reset_module[0] = '\0';
}

static void disk_clear_tcp_offset(PreBootOnDisk *r)
{
    r->tcp_send_queue_file_read_offset = 0;
    r->tcp_send_queue_offset_valid     = 0;
}

static BOOL cache_valid(void)
{
    return (s_cache_flags & PB_CACHE_HAVE) != 0;
}

static void cache_set(const PreBootOnDisk *r)
{
    if (r)
        memcpy(&s_disk, r, sizeof(s_disk));
    s_cache_flags = (UINT8)(s_cache_flags | PB_CACHE_HAVE);
}

static void staged_from_cache(PreBootOnDisk *staged)
{
    if (cache_valid())
        memcpy(staged, &s_disk, sizeof(*staged));
    else
        disk_init_empty(staged);
}

/* --- File I/O --- */

static Result disk_read(PreBootOnDisk *out)
{
    UINT32 n = 0;

    memset(out, 0, sizeof(*out));
    if (file_system_read_file(PRE_BOOT_INFO_FILE_PATH, (char *)out, (UINT32)sizeof(*out), &n)
            != RESULT_SUCCESS) {
        return RESULT_NOT_FOUND;
    }
    if (n != sizeof(*out)) {
        LOG_WARN("read: size %u expect %u", (unsigned)n, (unsigned)sizeof(*out));
        return RESULT_ERROR;
    }
    return RESULT_SUCCESS;
}

static Result disk_write(const PreBootOnDisk *r, const char *ctx)
{
    Result w = file_system_write_file(PRE_BOOT_INFO_FILE_PATH,
                                      (const char *)r, (UINT32)sizeof(*r));
    if (w != RESULT_SUCCESS)
        LOG_ERROR("%s: write %s ret=%d", ctx, PRE_BOOT_INFO_FILE_PATH, (int)w);
    return w;
}

static Result disk_store(const PreBootOnDisk *r, const char *ctx)
{
    Result w = disk_write(r, ctx);
    if (w == RESULT_SUCCESS)
        cache_set(r);
    return w;
}

static Result disk_load_or_create(PreBootOnDisk *out)
{
    Result rr = disk_read(out);
    if (rr == RESULT_NOT_FOUND) {
        LOG_DEBUG("load: no pre-boot file, creating default");
        disk_init_empty(out);
        (void)disk_write(out, "fresh");
    } else if (rr == RESULT_ERROR || !disk_header_ok(out)) {
        LOG_WARN("load: invalid record, recreating");
        (void)file_system_delete(PRE_BOOT_INFO_FILE_PATH);
        disk_init_empty(out);
        (void)disk_write(out, "fresh");
    }
    cache_set(out);
    return RESULT_SUCCESS;
}

static void prior_from_disk(const PreBootOnDisk *r, ResetHandlerLastSwResetInfo *prior)
{
    memset(prior, 0, sizeof(*prior));
    if (!disk_header_ok(r) || r->reset_type == (UINT32)RESET_TYPE_NONE)
        return;
    prior->reset_type   = (ResetType)r->reset_type;
    prior->reset_time   = r->reset_time;
    prior->total_resets = r->total_resets;
    utils_strncpy_safe(prior->module_name, r->reset_module, sizeof(prior->module_name));
    prior->valid = TRUE;
}

static Result disk_consume_reset_slot(void)
{
    PreBootOnDisk consumed;

    if (!cache_valid())
        return RESULT_ERROR;
    memcpy(&consumed, &s_disk, sizeof(consumed));
    disk_clear_reset_slot(&consumed);
    return disk_store(&consumed, "consume_reset");
}

/* --- Public API --- */

Result pre_boot_handler_load_prior_reset(ResetHandlerLastSwResetInfo *prior)
{
    PreBootOnDisk disk;

    if (!prior)
        return RESULT_INVALID_PARAM;

    Result lr = disk_load_or_create(&disk);
    if (lr != RESULT_SUCCESS)
        return lr;

    prior_from_disk(&disk, prior);
    if (prior->valid && disk_consume_reset_slot() != RESULT_SUCCESS)
        LOG_WARN("load_prior: failed to clear reset slot on disk");

    return RESULT_SUCCESS;
}

Result pre_boot_handler_save_before_soc_reset(ResetType reset_type, UINT32 reset_time,
                                              const char *module_name)
{
    PreBootOnDisk staged;

    if (reset_type != RESET_TYPE_SOFT && reset_type != RESET_TYPE_HARD)
        return RESULT_INVALID_PARAM;

    staged_from_cache(&staged);
    staged.reset_type   = (UINT32)reset_type;
    staged.reset_time   = reset_time;
    staged.total_resets = cache_valid() ? (s_disk.total_resets + 1u) : 1u;
    if (module_name && module_name[0] != '\0')
        utils_strncpy_safe(staged.reset_module, module_name, sizeof(staged.reset_module));
    else
        staged.reset_module[0] = '\0';

    return disk_store(&staged, "pre_reset");
}

Result pre_boot_handler_save_tcp_send_queue_offset(UINT32 offset)
{
    PreBootOnDisk staged;

    staged_from_cache(&staged);
    staged.tcp_send_queue_file_read_offset = offset;
    staged.tcp_send_queue_offset_valid     = 1u;
    return disk_store(&staged, "tcpq_save");
}

Result pre_boot_handler_clear_tcp_send_queue_offset(void)
{
    PreBootOnDisk staged;

    staged_from_cache(&staged);
    disk_clear_tcp_offset(&staged);
    return disk_store(&staged, "tcpq_clr");
}

BOOL pre_boot_handler_take_tcp_send_queue_offset(UINT32 *out_offset)
{
    if (!out_offset || !cache_valid() || s_disk.tcp_send_queue_offset_valid == 0u)
        return FALSE;

    *out_offset = s_disk.tcp_send_queue_file_read_offset;
    if (pre_boot_handler_clear_tcp_send_queue_offset() != RESULT_SUCCESS) {
        disk_clear_tcp_offset(&s_disk);
        LOG_WARN("tcpq_take: clear on disk failed");
    }
    return TRUE;
}
