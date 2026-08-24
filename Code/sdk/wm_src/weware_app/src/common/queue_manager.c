/**
 * @file queue_manager.c
 * @brief Generic named queue manager implementation for weware
 */

/*---------------------------------------------------------------
 * Includes
 *--------------------------------------------------------------*/
#include "common/queue_manager.h"
#include "common/utils.h"
#include "system/storage/flash_paths.h"
#include "system/reset/pre_boot_handler.h"
#include "module/module_manager.h"
#include "module/gps/gps_packet.h"

/* SDK Platform Abstraction Layer */
#include "sdk_platform.h"
#include "functionality/sdk_functionality_os.h"
#include "functionality/sdk_functionality_file.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>  /* UINT32_MAX */

 /*---------------------------------------------------------------
  * Log Configuration
  *--------------------------------------------------------------*/
 #define LOG_TAG "QUEUE"
 #define LOG_MODULE_LEVEL LOG_LEVEL_ERROR
#include "module/log/log.h"

 /*---------------------------------------------------------------
  * Types
  *--------------------------------------------------------------*/
 
struct Queue {
    UINT32     head;
    UINT32     tail;
    UINT32     count;

    void*      mutex;  /* SDK-agnostic mutex handle */
    UINT8     *buffer;
 
     /* File-backed overflow */
     UINT32     file_size;
     UINT32     file_read_offset;
 };
 
 /*---------------------------------------------------------------
  * Static State
  *--------------------------------------------------------------*/
 
 static Queue *g_queues[QUEUE_MANAGER_MAX_QUEUES] = {0};

#define TCP_SEND_QUEUE_NAME "TCP_SEND_Q"
#define TCP_CMD_RESP_PACKET_TYPE 38

static void queue_make_filename(const QueueConfig *cfg,
                                char *out,
                                size_t len,
                                const char *suffix);

static inline BOOL queue_is_tcp_send_q(const QueueConfig *cfg)
{
    return (cfg && cfg->name && strcmp(cfg->name, TCP_SEND_QUEUE_NAME) == 0);
}

static inline ModuleId queue_tcp_type_to_source_module(UINT8 type)
{
    if (type == (UINT8)GPS_PACKET_TYPE) return MODULE_ID_GPS;
    if (type == (UINT8)BLE_PACKET_TYPE) return MODULE_ID_BLE;
    if (type == (UINT8)TCP_CMD_RESP_PACKET_TYPE) return MODULE_ID_CMD;
    return MODULE_ID_COUNT;
}

static inline BOOL queue_tcp_source_allowed(ModuleId src)
{
    return (src == MODULE_ID_GPS || src == MODULE_ID_BLE || src == MODULE_ID_CMD);
}

static UINT16 queue_tcp_payload_len_from_message(const ModuleMessage *m, const char **out_payload)
{
    if (!m || !out_payload) return 0U;

    const char *payload = module_message_payload_ptr(m);
    if (!payload) return 0U;

    /* TCP send queue (compact file): payload length is always @c data_len; require WE header. */
    if ((unsigned char)payload[0] != 0x57 || (unsigned char)payload[1] != 0x45) {
        return 0U;
    }
    if (m->data_len == 0u) {
        return 0U;
    }
    if (!m->use_dynamic_buffer && m->data_len > MODULE_MESSAGE_INLINE_SIZE) {
        return 0U;
    }

    *out_payload = payload;
    return (UINT16)m->data_len;
}

static Result queue_tcp_file_write_compact_record(void *fp, const ModuleMessage *m, UINT32 *out_record_size)
{
    if (!fp || !m || !out_record_size) return RESULT_INVALID_PARAM;
    *out_record_size = 0U;

    const char *payload = NULL;
    UINT16 payload_len = queue_tcp_payload_len_from_message(m, &payload);
    if (!payload || payload_len == 0U) {
        return RESULT_INVALID_PARAM;
    }
    UINT8 type = (UINT8)payload[2];
    if (!(type == (UINT8)GPS_PACKET_TYPE || type == (UINT8)BLE_PACKET_TYPE || type == (UINT8)TCP_CMD_RESP_PACKET_TYPE)) {
        return RESULT_INVALID_PARAM;
    }
    UINT8 hdr[3];
    hdr[0] = (UINT8)(payload_len & 0xFF);
    hdr[1] = (UINT8)((payload_len >> 8) & 0xFF);
    hdr[2] = type;

    UINT32 written = 0;
    if (sdk_file_write(fp, hdr, sizeof(hdr), &written) != SDK_RESULT_SUCCESS || written != sizeof(hdr)) {
        return RESULT_ERROR;
    }
    if (sdk_file_write(fp, payload, payload_len, &written) != SDK_RESULT_SUCCESS || written != payload_len) {
        return RESULT_ERROR;
    }
    *out_record_size = (UINT32)sizeof(hdr) + (UINT32)payload_len;
    return RESULT_SUCCESS;
}

static Result queue_tcp_file_read_compact_record(void *fp, ModuleMessage *out, UINT32 *io_offset, UINT32 file_size)
{
    if (!fp || !out || !io_offset) return RESULT_INVALID_PARAM;
    if (*io_offset + 3U > file_size) return RESULT_NOT_FOUND;

    UINT8 hdr[3];
    UINT32 io = 0;
    sdk_file_seek(fp, *io_offset, 0);
    if (sdk_file_read(fp, hdr, sizeof(hdr), &io) != SDK_RESULT_SUCCESS || io != sizeof(hdr)) {
        return RESULT_ERROR;
    }

    UINT16 payload_len = (UINT16)hdr[0] | ((UINT16)hdr[1] << 8);
    UINT8 type = hdr[2];
    if (payload_len == 0U || payload_len > MODULE_MESSAGE_INLINE_SIZE) {
        return RESULT_ERROR;
    }
    if (*io_offset + 3U + (UINT32)payload_len > file_size) {
        return RESULT_ERROR;
    }
    if (!(type == (UINT8)GPS_PACKET_TYPE || type == (UINT8)BLE_PACKET_TYPE || type == (UINT8)TCP_CMD_RESP_PACKET_TYPE)) {
        return RESULT_ERROR;
    }

    ModuleId src = queue_tcp_type_to_source_module(type);
    if (!queue_tcp_source_allowed(src)) {
        return RESULT_ERROR;
    }

    memset(out, 0, sizeof(ModuleMessage));
    out->source_module = src;
    out->destination_module = MODULE_ID_TCP;
    out->use_dynamic_buffer = FALSE;
    out->dynamic_buffer = NULL;
    out->data_len = 0;

    if (sdk_file_read(fp, out->message, payload_len, &io) != SDK_RESULT_SUCCESS || io != payload_len) {
        return RESULT_ERROR;
    }
    if (payload_len < MODULE_MESSAGE_INLINE_SIZE) {
        out->message[payload_len] = '\0';
    }
    out->data_len = (UINT32)payload_len;

    *io_offset += 3U + (UINT32)payload_len;
    return RESULT_SUCCESS;
}

static UINT32 queue_tcp_file_count_records(Queue *q, const QueueConfig *cfg)
{
    if (!q || !cfg || !queue_is_tcp_send_q(cfg)) return 0U;
    if (q->file_read_offset >= q->file_size) return 0U;

    char file[64];
    queue_make_filename(cfg, file, sizeof(file), "_overflow.dat");
    void *fp = sdk_file_open(file, "rb");
    if (!fp) return 0U;

    UINT32 offset = q->file_read_offset;
    UINT32 count = 0U;
    while (offset + 3U <= q->file_size) {
        UINT8 hdr[3];
        UINT32 io = 0;
        sdk_file_seek(fp, offset, 0);
        if (sdk_file_read(fp, hdr, sizeof(hdr), &io) != SDK_RESULT_SUCCESS || io != sizeof(hdr)) break;
        UINT16 payload_len = (UINT16)hdr[0] | ((UINT16)hdr[1] << 8);
        if (payload_len == 0U || payload_len > MODULE_MESSAGE_INLINE_SIZE) break;
        if (offset + 3U + (UINT32)payload_len > q->file_size) break;
        offset += 3U + (UINT32)payload_len;
        count++;
    }

    sdk_file_close(fp);
    return count;
}

static Result queue_tcp_restore_saved_file_offset(Queue *q, const QueueConfig *cfg)
{
    UINT32 saved_offset = 0U;

    if (!q || !cfg || !queue_is_tcp_send_q(cfg) || q->file_size == 0U) {
        return RESULT_SUCCESS;
    }

    if (!pre_boot_handler_take_tcp_send_queue_offset(&saved_offset)) {
        return RESULT_SUCCESS;
    }

    if (saved_offset > q->file_size) {
        LOG_WARN("Queue '%s' saved TCP file offset %u exceeds file size %u; resuming from 0",
                 cfg->name, (unsigned)saved_offset, (unsigned)q->file_size);
        q->file_read_offset = 0U;
        return RESULT_ERROR;
    }

    if (saved_offset == q->file_size) {
        char file[64];
        queue_make_filename(cfg, file, sizeof(file), "_overflow.dat");
        sdk_file_delete(file);
        q->file_size = 0U;
        q->file_read_offset = 0U;
        LOG_INFO("Queue '%s' restored TCP file offset at EOF; consumed overflow file removed",
                 cfg->name);
        return RESULT_SUCCESS;
    }

    q->file_read_offset = saved_offset;
    LOG_INFO("Queue '%s' restored TCP file offset %u/%u",
             cfg->name, (unsigned)q->file_read_offset, (unsigned)q->file_size);
    return RESULT_SUCCESS;
}

static Result queue_tcp_persist_file_offset(Queue *q, const QueueConfig *cfg)
{
    if (!q || !cfg || !queue_is_tcp_send_q(cfg)) {
        return RESULT_SUCCESS;
    }

    if (q->file_size == 0U || q->file_read_offset >= q->file_size) {
        return pre_boot_handler_clear_tcp_send_queue_offset();
    }

    return pre_boot_handler_save_tcp_send_queue_offset(q->file_read_offset);
}
 
 /*---------------------------------------------------------------
  * Internal Helpers
  *--------------------------------------------------------------*/
 
 static inline BOOL queue_has_file(const QueueConfig *cfg)
 {
     return (cfg && cfg->max_file_size > 0U);
 }
 
static inline void queue_lock(Queue *q, const QueueConfig *cfg)
{
    if (q && cfg && cfg->thread_safe && q->mutex) {
        sdk_mutex_lock(q->mutex, (UINT32)-1);
    }
}

static inline void queue_unlock(Queue *q, const QueueConfig *cfg)
{
    if (q && cfg && cfg->thread_safe && q->mutex) {
        sdk_mutex_unlock(q->mutex);
    }
}
 
 static void queue_make_filename(const QueueConfig *cfg,
                                 char *out,
                                 size_t len,
                                 const char *suffix)
 {
     if (!cfg || !out || len < 48u) {
         return;
     }

     snprintf(out, len, FLASH_DIR_QUEUE "queue_%s%s",
              cfg->name ? cfg->name : "unknown",
              suffix);
 }

 /*---------------------------------------------------------------
  * Persistence (RAM ↔ File)
  *--------------------------------------------------------------*/
 
static Result queue_persist_save(Queue *q, const QueueConfig *cfg)
{
    if (!q || !cfg || !cfg->persist_on_reboot) {
        return RESULT_INVALID_PARAM;
    }

    char file[64];
    queue_make_filename(cfg, file, sizeof(file), ".dat");

    queue_lock(q, cfg);

    void* fp = sdk_file_open(file, "wb");
    if (!fp) {
        queue_unlock(q, cfg);
        LOG_WARN("Queue '%s' persist save failed: cannot open file %s", cfg->name, file);
        return RESULT_ERROR;
    }

    UINT32 written = 0;
    if (sdk_file_write(fp, &q->count, sizeof(q->count), &written) != SDK_RESULT_SUCCESS || written != sizeof(q->count) ||
        sdk_file_write(fp, &q->head, sizeof(q->head), &written) != SDK_RESULT_SUCCESS || written != sizeof(q->head) ||
        sdk_file_write(fp, &q->tail, sizeof(q->tail), &written) != SDK_RESULT_SUCCESS || written != sizeof(q->tail)) {
        goto error;
    }

    UINT32 idx = q->head;
    for (UINT32 i = 0; i < q->count; i++) {
        UINT8 *elem = q->buffer + (idx * cfg->element_size);
        if (sdk_file_write(fp, elem, cfg->element_size, &written) != SDK_RESULT_SUCCESS || written != cfg->element_size) {
            goto error;
        }
        idx = (idx + 1U) % cfg->capacity;
    }

    sdk_file_close(fp);
    queue_unlock(q, cfg);
 
    LOG_INFO("Queue '%s' persisted (%u elements)",
             cfg->name, q->count);
    return RESULT_SUCCESS;

error:
    sdk_file_close(fp);
    queue_unlock(q, cfg);
    LOG_WARN("Queue '%s' persist save failed: write error", cfg->name);
    return RESULT_ERROR;
}

static Result queue_persist_load(Queue *q, const QueueConfig *cfg)
{
    if (!q || !cfg || !cfg->persist_on_reboot) {
        return RESULT_INVALID_PARAM;
    }

    char file[64];
    queue_make_filename(cfg, file, sizeof(file), ".dat");

    void* fp = sdk_file_open(file, "rb");
    if (!fp) {
        /* First boot - no persisted data exists, this is normal */
        LOG_DEBUG("Queue '%s' persist load: no persisted data (first boot)", cfg->name);
        return RESULT_SUCCESS;
    }

    queue_lock(q, cfg);

    UINT32 count, head, tail;
    UINT32 read_len = 0;
    if (sdk_file_read(fp, &count, sizeof(count), &read_len) != SDK_RESULT_SUCCESS || read_len != sizeof(count) ||
        sdk_file_read(fp, &head, sizeof(head), &read_len) != SDK_RESULT_SUCCESS || read_len != sizeof(head) ||
        sdk_file_read(fp, &tail, sizeof(tail), &read_len) != SDK_RESULT_SUCCESS || read_len != sizeof(tail) ||
        count > cfg->capacity ||
        head  >= cfg->capacity ||
        tail  >= cfg->capacity) {
        goto error;
    }

    q->count = q->head = q->tail = 0;

    UINT32 idx = head;
    for (UINT32 i = 0; i < count; i++) {
        UINT8 *elem = q->buffer + (idx * cfg->element_size);
        if (sdk_file_read(fp, elem, cfg->element_size, &read_len) != SDK_RESULT_SUCCESS || read_len != cfg->element_size) {
            goto error;
        }
        idx = (idx + 1U) % cfg->capacity;
    }

    q->count = count;
    q->head  = head;
    q->tail  = tail;

    sdk_file_close(fp);
    queue_unlock(q, cfg);

    LOG_INFO("Queue '%s' restored (%u elements)",
             cfg->name, count);
    return RESULT_SUCCESS;

error:
    sdk_file_close(fp);
    queue_unlock(q, cfg);
    sdk_file_delete(file);
    LOG_WARN("Queue '%s' persist load failed: corrupted or invalid data, file deleted", cfg->name);
    return RESULT_ERROR;
}

static BOOL queue_overflow_drained_locked(const Queue *q, const QueueConfig *cfg)
{
    if (!queue_has_file(cfg) || q->file_size == 0U) {
        return TRUE;
    }
    return (q->file_read_offset >= q->file_size);
}

static Result queue_persist_delete_snapshot(const QueueConfig *cfg)
{
    char file[64];

    if (!cfg || !cfg->persist_on_reboot) {
        return RESULT_SUCCESS;
    }

    queue_make_filename(cfg, file, sizeof(file), ".dat");
    if (sdk_file_exists(file) != SDK_RESULT_SUCCESS) {
        return RESULT_SUCCESS;
    }

    if (sdk_file_delete(file) != SDK_RESULT_SUCCESS) {
        LOG_WARN("Queue '%s' persist snapshot delete failed (%s)", cfg->name, file);
        return RESULT_ERROR;
    }

    LOG_INFO("Queue '%s' persist snapshot deleted (%s)", cfg->name, file);
    return RESULT_SUCCESS;
}

Result queue_persist_clear_snapshot_after_consume(Queue *q, const QueueConfig *cfg)
{
    BOOL ram_empty;
    BOOL overflow_drained;

    if (!q || !cfg) {
        return RESULT_INVALID_PARAM;
    }
    if (!cfg->persist_on_reboot) {
        return RESULT_SUCCESS;
    }

    queue_lock(q, cfg);
    ram_empty = (q->count == 0U);
    overflow_drained = queue_overflow_drained_locked(q, cfg);
    queue_unlock(q, cfg);

    if (!ram_empty) {
        return RESULT_SUCCESS;
    }

    if (overflow_drained && queue_is_tcp_send_q(cfg)) {
        (void)pre_boot_handler_clear_tcp_send_queue_offset();
    }

    return queue_persist_delete_snapshot(cfg);
}
 
/*---------------------------------------------------------------
 * File Overflow Handling
 *--------------------------------------------------------------*/

static Result queue_file_restore(Queue *q, const QueueConfig *cfg)
{
    if (!q || !cfg || !queue_has_file(cfg)) {
        return RESULT_INVALID_PARAM;
    }

    char file[64];
    queue_make_filename(cfg, file, sizeof(file), "_overflow.dat");

    /* Check if overflow file exists */
    if (sdk_file_exists(file) != SDK_RESULT_SUCCESS) {
        /* File doesn't exist, nothing to restore - this is normal */
        LOG_DEBUG("Queue '%s' overflow restore: no overflow file exists", cfg->name);
        return RESULT_SUCCESS;
    }

    /* Open file to get its size */
    void* fp = sdk_file_open(file, "rb");
    if (!fp) {
        /* File exists but can't be opened, treat as if it doesn't exist */
        LOG_WARN("Queue '%s' overflow restore: file exists but cannot be opened", cfg->name);
        return RESULT_SUCCESS;
    }

    UINT32 file_size = 0;
    sdk_file_get_size(fp, &file_size);
    sdk_file_close(fp);

    if (file_size <= 0) {
        /* Empty or invalid file, remove it */
        sdk_file_delete(file);
        LOG_DEBUG("Queue '%s' overflow restore: empty file deleted", cfg->name);
        return RESULT_SUCCESS;
    }

    /* Restore overflow file state */
    q->file_size = (UINT32)file_size;
    q->file_read_offset = 0; /* Start reading from beginning */

    if (queue_is_tcp_send_q(cfg)) {
        (void)queue_tcp_restore_saved_file_offset(q, cfg);
    }

    LOG_INFO("Queue '%s' overflow file restored (%u bytes)",
             cfg->name, q->file_size);
    return RESULT_SUCCESS;
}

static Result queue_file_append(Queue *q,
                                const QueueConfig *cfg,
                                const void *elem)
 {
     if (!q || !cfg || !elem || !queue_has_file(cfg)) {
         return RESULT_INVALID_PARAM;
     }

    BOOL compact_tcp = queue_is_tcp_send_q(cfg);
    UINT32 record_size = cfg->element_size;
    if (compact_tcp) {
        const ModuleMessage *m = (const ModuleMessage *)elem;
        const char *payload = NULL;
        UINT16 payload_len = queue_tcp_payload_len_from_message(m, &payload);
        if (!payload || payload_len == 0U) {
            return RESULT_ERROR;
        }
        record_size = 3U + (UINT32)payload_len;
    }
 
     /* Check for integer overflow before addition */
    if (record_size > UINT32_MAX - q->file_size) {
        LOG_WARN("Queue '%s' file size overflow prevented (file_size=%u, record_size=%u)", 
                cfg->name, q->file_size, record_size);
         /* Reset file to prevent corruption */
         char file[64];
         queue_make_filename(cfg, file, sizeof(file), "_overflow.dat");
         sdk_file_delete(file);
         q->file_size = 0;
         q->file_read_offset = 0;
     }

     /* If file is full, clear oldest entries to make room (circular buffer behavior) */
     /* Simple approach: when full, clear file and start fresh to avoid complex rotation and crashes */
    if (q->file_size + record_size > cfg->max_file_size) {
         char file[64];
         queue_make_filename(cfg, file, sizeof(file), "_overflow.dat");
         sdk_file_delete(file);  /* Delete old file */
         
         LOG_INFO("Queue '%s' overflow file cleared to make room (was %u bytes)", 
                 cfg->name, q->file_size);
         q->file_size = 0;
         q->file_read_offset = 0;
     }

    /* Prepare file path for append */
    char file[64];
    queue_make_filename(cfg, file, sizeof(file), "_overflow.dat");

    void* fp = sdk_file_open(file, "ab+");
    if (!fp) {
        LOG_WARN("Queue '%s' overflow append failed: cannot open file %s", cfg->name, file);
        return RESULT_ERROR;
    }

    Result wr = RESULT_SUCCESS;
    UINT32 compact_written = 0U;
    if (compact_tcp) {
        wr = queue_tcp_file_write_compact_record(fp, (const ModuleMessage *)elem, &compact_written);
    } else {
        UINT32 written = 0;
        SdkResult ret = sdk_file_write(fp, elem, cfg->element_size, &written);
        if (ret != SDK_RESULT_SUCCESS || written != cfg->element_size) {
            wr = RESULT_ERROR;
        } else {
            compact_written = cfg->element_size;
        }
    }
    sdk_file_close(fp);

    if (wr != RESULT_SUCCESS) {
        LOG_WARN("Queue '%s' overflow append failed", cfg->name);
        return RESULT_ERROR;
    }

    q->file_size += compact_written;
    LOG_DEBUG("Queue '%s' overflow appended element (%u/%u bytes)", 
              cfg->name, q->file_size, cfg->max_file_size);
    return RESULT_SUCCESS;
 }
 
static Result queue_file_read(Queue *q,
                               const QueueConfig *cfg,
                               void *out)
{
    if (!q || !cfg || !out || !queue_has_file(cfg)) {
        return RESULT_INVALID_PARAM;
    }
 
    if (q->file_read_offset >= q->file_size) {
        return RESULT_NOT_FOUND;
    }
 
    char file[64];
    queue_make_filename(cfg, file, sizeof(file), "_overflow.dat");

    void* fp = sdk_file_open(file, "rb");
    if (!fp) {
        LOG_WARN("Queue '%s' overflow read failed: cannot open file %s", cfg->name, file);
        return RESULT_ERROR;
    }

    Result rr = RESULT_SUCCESS;
    if (queue_is_tcp_send_q(cfg)) {
        rr = queue_tcp_file_read_compact_record(fp, (ModuleMessage *)out, &q->file_read_offset, q->file_size);
    } else {
        sdk_file_seek(fp, q->file_read_offset, 0);  /* SEEK_SET = 0 */
        UINT32 read = 0;
        SdkResult ret = sdk_file_read(fp, out, cfg->element_size, &read);
        if (ret != SDK_RESULT_SUCCESS || read != cfg->element_size) {
            rr = RESULT_ERROR;
        } else {
            q->file_read_offset += cfg->element_size;
        }
    }
    sdk_file_close(fp);
    if (rr != RESULT_SUCCESS) {
        LOG_WARN("Queue '%s' overflow read failed", cfg->name);
        return RESULT_ERROR;
    }

    if (q->file_read_offset >= q->file_size) {
        /* All data read, delete the overflow file */
        sdk_file_delete(file);
        q->file_size = 0;
        q->file_read_offset = 0;
        LOG_DEBUG("Queue '%s' overflow file emptied and deleted", cfg->name);
    }
 
    return RESULT_SUCCESS;
}
 
 /*---------------------------------------------------------------
  * Queue Creation / Destruction
  *--------------------------------------------------------------*/
 
 Result queue_manager_create(const QueueConfig *cfg, Queue **out)
 {
     if (!cfg || !cfg->name || !out) {
         LOG_ERROR("Create failed: invalid parameters");
         return RESULT_INVALID_PARAM;
     }
 
     UINT32 slot;
     for (slot = 0; slot < QUEUE_MANAGER_MAX_QUEUES; slot++) {
         if (!g_queues[slot]) {
             break;
         }
     }
     if (slot == QUEUE_MANAGER_MAX_QUEUES) {
         LOG_ERROR("Create failed: maximum queues reached (%u)", QUEUE_MANAGER_MAX_QUEUES);
         return RESULT_BUSY;
     }
 
    Queue *q = (Queue*)sdk_memory_alloc(sizeof(Queue));
    if (!q) {
        LOG_ERROR("Queue '%s' create failed: out of memory (Queue struct)", cfg->name);
        return RESULT_OUT_OF_MEMORY;
    }
    memset(q, 0, sizeof(Queue));

    q->buffer = (UINT8*)sdk_memory_alloc(cfg->element_size * cfg->capacity);
    if (!q->buffer) {
        sdk_memory_free(q);
        LOG_ERROR("Queue '%s' create failed: out of memory (buffer, %u bytes)", 
                  cfg->name, cfg->element_size * cfg->capacity);
        return RESULT_OUT_OF_MEMORY;
    }

    if (cfg->thread_safe) {
        SdkResult mutex_result = sdk_mutex_create(&q->mutex, 0);
        if (mutex_result != SDK_RESULT_SUCCESS) {
            sdk_memory_free(q->buffer);
            sdk_memory_free(q);
            LOG_ERROR("Queue '%s' create failed: mutex creation failed", cfg->name);
            return RESULT_ERROR;
        }
    } else {
        q->mutex = NULL;
    }
 
    if (cfg->persist_on_reboot) {
        queue_persist_load(q, cfg);
    }

    /* Restore overflow file state if file-backed overflow is enabled */
    if (queue_has_file(cfg)) {
        queue_file_restore(q, cfg);
    }

    g_queues[slot] = q;
    *out = q;
 
     LOG_INFO("Queue created: %s (cap=%u, elem=%u)",
              cfg->name, cfg->capacity, cfg->element_size);
     return RESULT_SUCCESS;
 }
 
Result queue_manager_persist(Queue *q, const QueueConfig *cfg)
{
    Result persist_result;

    if (!q || !cfg) {
        LOG_ERROR("Persist failed: invalid parameters");
        return RESULT_INVALID_PARAM;
    }

    if (!cfg->persist_on_reboot) {
        return RESULT_SUCCESS;
    }

    persist_result = queue_persist_save(q, cfg);
    if (result_is_error(persist_result)) {
        return persist_result;
    }

    persist_result = queue_tcp_persist_file_offset(q, cfg);
    if (result_is_error(persist_result)) {
        LOG_WARN("Queue '%s' TCP reboot offset persist failed", cfg->name);
    }

    return persist_result;
}

Result queue_manager_destroy(Queue *q, const QueueConfig *cfg)
{
    if (!q) {
        LOG_ERROR("Destroy failed: invalid queue handle");
        return RESULT_INVALID_PARAM;
    }

    for (UINT32 i = 0; i < QUEUE_MANAGER_MAX_QUEUES; i++) {
        if (g_queues[i] == q) {
            g_queues[i] = NULL;
            break;
        }
    }
 
    if (q->mutex) {
        sdk_mutex_delete(q->mutex);
    }
    if (q->buffer) {
        sdk_memory_free(q->buffer);
    }
    sdk_memory_free(q);
    
    LOG_INFO("Queue '%s' destroyed", (cfg && cfg->name) ? cfg->name : "unknown");
    return RESULT_SUCCESS;
}
 
 /*---------------------------------------------------------------
  * Queue Operations
  *--------------------------------------------------------------*/
 
 Result queue_push(Queue *q, const QueueConfig *cfg, const void *data)
 {
     if (!q || !cfg || !data) {
         return RESULT_INVALID_PARAM;
     }
 
     /* Validate queue state */
     if (!q->buffer) {
         LOG_ERROR("Queue '%s' push failed: buffer is NULL", cfg->name);
         return RESULT_ERROR;
     }
     
     if (q->head >= cfg->capacity || q->tail >= cfg->capacity) {
         LOG_ERROR("Queue '%s' push failed: invalid indices (head=%u, tail=%u, cap=%u)", 
                  cfg->name, q->head, q->tail, cfg->capacity);
         return RESULT_ERROR;
     }
 
     queue_lock(q, cfg);
 
     if (q->count >= cfg->capacity) {
         if (!queue_has_file(cfg)) {
             queue_unlock(q, cfg);
             return RESULT_BUSY;
         }
 
         UINT8 *oldest = q->buffer + (q->head * cfg->element_size);
         /* Bounds check before accessing buffer */
         if (q->head >= cfg->capacity) {
             LOG_ERROR("Queue '%s' push failed: head index out of bounds (%u >= %u)", 
                      cfg->name, q->head, cfg->capacity);
             queue_unlock(q, cfg);
             return RESULT_ERROR;
         }
         
         Result r = queue_file_append(q, cfg, oldest);
         if (result_is_error(r)) {
             queue_unlock(q, cfg);
             return r;
         }
 
         q->head = (q->head + 1U) % cfg->capacity;
         q->count--;
     }
 
     /* Bounds check before memcpy */
     if (q->tail >= cfg->capacity) {
         LOG_ERROR("Queue '%s' push failed: tail index out of bounds (%u >= %u)", 
                  cfg->name, q->tail, cfg->capacity);
         queue_unlock(q, cfg);
         return RESULT_ERROR;
     }
     
     memcpy(q->buffer + (q->tail * cfg->element_size),
            data, cfg->element_size);
 
     q->tail = (q->tail + 1U) % cfg->capacity;
     q->count++;
 
     queue_unlock(q, cfg);
     return RESULT_SUCCESS;
 }
 
Result queue_pop(Queue *q,
                 const QueueConfig *cfg,
                 void *out,
                 UINT32 max,
                 UINT32 *popped)
{
    if (!q || !cfg || !out || max == 0) {
        LOG_ERROR("Pop failed: invalid parameters");
        return RESULT_INVALID_PARAM;
    }
 
    UINT32 cnt = 0;
    UINT8 *dst = (UINT8 *)out;
 
    queue_lock(q, cfg);
    
    /* Validate queue state */
    if (!q->buffer) {
        LOG_ERROR("Queue '%s' pop failed: buffer is NULL", cfg->name);
        queue_unlock(q, cfg);
        if (popped) *popped = 0;
        return RESULT_ERROR;
    }
    
    if (q->head >= cfg->capacity || q->tail >= cfg->capacity) {
        LOG_ERROR("Queue '%s' pop failed: invalid indices (head=%u, tail=%u, cap=%u)", 
                 cfg->name, q->head, q->tail, cfg->capacity);
        queue_unlock(q, cfg);
        if (popped) *popped = 0;
        return RESULT_ERROR;
    }
 
    while (cnt < max) {
        Result r;
 
        /* First read from overflow file if available */
        if (queue_has_file(cfg) && q->file_read_offset < q->file_size) {
            r = queue_file_read(q, cfg, dst + (cnt * cfg->element_size));
        } else if (q->count > 0) {
            /* Bounds check before accessing buffer */
            if (q->head >= cfg->capacity) {
                LOG_ERROR("Queue '%s' pop failed: head index out of bounds (%u >= %u)", 
                         cfg->name, q->head, cfg->capacity);
                break;
            }
            
            /* Then read from in-memory queue */
            memcpy(dst + (cnt * cfg->element_size),
                   q->buffer + (q->head * cfg->element_size),
                   cfg->element_size);
            q->head = (q->head + 1U) % cfg->capacity;
            q->count--;
            r = RESULT_SUCCESS;
        } else {
            r = RESULT_NOT_FOUND;
        }
 
        if (r != RESULT_SUCCESS) {
            break;
        }
        cnt++;
    }
 
    queue_unlock(q, cfg);
 
    if (popped) {
        *popped = cnt;
    }
 
    if (cnt == 0) {
        LOG_DEBUG("Queue '%s' pop: queue empty", cfg->name);
        return RESULT_NOT_FOUND;
    }
    
    LOG_DEBUG("Queue '%s' pop: popped %u element(s)", cfg->name, cnt);
    return RESULT_SUCCESS;
}
 
Result queue_peek(Queue *q, const QueueConfig *cfg, void *out_data)
{
    if (!q || !cfg || !out_data) {
        LOG_ERROR("Peek failed: invalid parameters");
        return RESULT_INVALID_PARAM;
    }
    
    /* Validate queue state */
    if (!q->buffer) {
        LOG_ERROR("Queue '%s' peek failed: buffer is NULL", cfg->name);
        return RESULT_ERROR;
    }
    
    if (q->head >= cfg->capacity || q->tail >= cfg->capacity) {
        LOG_ERROR("Queue '%s' peek failed: invalid indices (head=%u, tail=%u, cap=%u)", 
                 cfg->name, q->head, q->tail, cfg->capacity);
        return RESULT_ERROR;
    }
 
    queue_lock(q, cfg);
 
    if (q->count == 0) {
        queue_unlock(q, cfg);
        LOG_DEBUG("Queue '%s' peek: queue empty", cfg->name);
        return RESULT_NOT_FOUND;
    }
    
    /* Bounds check before memcpy */
    if (q->head >= cfg->capacity) {
        LOG_ERROR("Queue '%s' peek failed: head index out of bounds (%u >= %u)", 
                 cfg->name, q->head, cfg->capacity);
        queue_unlock(q, cfg);
        return RESULT_ERROR;
    }
 
    memcpy(out_data,
           q->buffer + (q->head * cfg->element_size),
           cfg->element_size);
 
    queue_unlock(q, cfg);
    return RESULT_SUCCESS;
}

/**
 * @brief Read one element from the overflow file at logical index @a which (0 = next pop)
 *        without advancing file_read_offset.
 */
static Result queue_file_peek_element(Queue *q, const QueueConfig *cfg, UINT32 which, void *out)
{
    if (!q || !cfg || !out || !queue_has_file(cfg))
        return RESULT_INVALID_PARAM;
    if (q->file_read_offset >= q->file_size)
        return RESULT_NOT_FOUND;

    char file[64];
    queue_make_filename(cfg, file, sizeof(file), "_overflow.dat");

    void *fp = sdk_file_open(file, "rb");
    if (!fp) {
        LOG_WARN("Queue '%s' peek file read failed: open %s", cfg->name, file);
        return RESULT_ERROR;
    }

    Result r = RESULT_SUCCESS;
    if (queue_is_tcp_send_q(cfg)) {
        UINT32 offset = q->file_read_offset;
        for (UINT32 i = 0; i <= which; i++) {
            if (offset >= q->file_size) {
                r = RESULT_NOT_FOUND;
                break;
            }
            if (i < which) {
                UINT8 hdr[3];
                UINT32 io = 0;
                sdk_file_seek(fp, offset, 0);
                if (sdk_file_read(fp, hdr, sizeof(hdr), &io) != SDK_RESULT_SUCCESS || io != sizeof(hdr)) {
                    r = RESULT_ERROR;
                    break;
                }
                UINT16 payload_len = (UINT16)hdr[0] | ((UINT16)hdr[1] << 8);
                if (payload_len == 0U || payload_len > MODULE_MESSAGE_INLINE_SIZE ||
                    offset + 3U + (UINT32)payload_len > q->file_size) {
                    r = RESULT_ERROR;
                    break;
                }
                offset += 3U + (UINT32)payload_len;
            } else {
                r = queue_tcp_file_read_compact_record(fp, (ModuleMessage *)out, &offset, q->file_size);
            }
        }
    } else {
        if (cfg->element_size == 0U) {
            sdk_file_close(fp);
            return RESULT_INVALID_PARAM;
        }
        UINT32 unread = q->file_size - q->file_read_offset;
        UINT32 file_slots = unread / cfg->element_size;
        if (which >= file_slots) {
            sdk_file_close(fp);
            return RESULT_NOT_FOUND;
        }
        UINT32 skip_bytes = which * cfg->element_size;
        if (skip_bytes > UINT32_MAX - q->file_read_offset) {
            sdk_file_close(fp);
            return RESULT_ERROR;
        }
        UINT32 abs_off = q->file_read_offset + skip_bytes;
        if (abs_off > q->file_size - cfg->element_size) {
            sdk_file_close(fp);
            return RESULT_ERROR;
        }
        sdk_file_seek(fp, abs_off, 0);
        UINT32 read = 0;
        SdkResult ret = sdk_file_read(fp, out, cfg->element_size, &read);
        if (ret != SDK_RESULT_SUCCESS || read != cfg->element_size) {
            r = RESULT_ERROR;
        }
    }
    sdk_file_close(fp);
    return r;
}

Result queue_peek_batch(Queue *q,
                        const QueueConfig *cfg,
                        void *out,
                        UINT32 max_elements,
                        UINT32 *out_count)
{
    if (!q || !cfg || !out || max_elements == 0 || !out_count) {
        LOG_ERROR("Peek_batch failed: invalid parameters");
        return RESULT_INVALID_PARAM;
    }

    *out_count = 0;

    UINT8 *dst = (UINT8 *)out;
    queue_lock(q, cfg);

    if (!q->buffer || q->head >= cfg->capacity || q->tail >= cfg->capacity) {
        LOG_ERROR("Queue '%s' peek_batch failed: invalid state (buf=%p head=%u tail=%u cap=%u)",
                  cfg->name, (void *)q->buffer, q->head, q->tail, cfg->capacity);
        queue_unlock(q, cfg);
        return RESULT_ERROR;
    }

    UINT32 file_slots = 0;
    if (queue_has_file(cfg) && q->file_read_offset < q->file_size) {
        if (queue_is_tcp_send_q(cfg)) {
            file_slots = queue_tcp_file_count_records(q, cfg);
        } else if (cfg->element_size > 0U) {
            UINT32 unread = q->file_size - q->file_read_offset;
            file_slots = unread / cfg->element_size;
        }
    }

    UINT32 total_logical = file_slots + q->count;
    if (total_logical == 0U) {
        queue_unlock(q, cfg);
        LOG_DEBUG("Queue '%s' peek_batch: queue empty", cfg->name);
        return RESULT_NOT_FOUND;
    }

    UINT32 cnt = 0;
    while (cnt < max_elements && cnt < total_logical) {
        Result r;
        if (cnt < file_slots) {
            r = queue_file_peek_element(q, cfg, cnt, dst + (cnt * cfg->element_size));
        } else {
            UINT32 mem_idx = cnt - file_slots;
            if (mem_idx >= q->count) {
                break;
            }
            UINT32 idx = (q->head + mem_idx) % cfg->capacity;
            if (idx >= cfg->capacity) {
                LOG_ERROR("Queue '%s' peek_batch: mem index out of bounds", cfg->name);
                break;
            }
            memcpy(dst + (cnt * cfg->element_size), q->buffer + (idx * cfg->element_size),
                   cfg->element_size);
            r = RESULT_SUCCESS;
        }

        if (r != RESULT_SUCCESS) {
            break;
        }
        cnt++;
    }

    queue_unlock(q, cfg);

    *out_count = cnt;
    if (cnt == 0U) {
        LOG_DEBUG("Queue '%s' peek_batch: nothing copied", cfg->name);
        return RESULT_NOT_FOUND;
    }

    LOG_DEBUG("Queue '%s' peek_batch: copied %u element(s)", cfg->name, cnt);
    return RESULT_SUCCESS;
}
 
Result queue_get_count(Queue *q, UINT32 *out_count)
{
    if (!q || !out_count) {
        LOG_ERROR("Get_count failed: invalid parameters");
        return RESULT_INVALID_PARAM;
    }
 
    *out_count = q->count;
    return RESULT_SUCCESS;
}
 
 BOOL queue_is_empty(Queue *q)
 {
     return (!q || q->count == 0);
 }
 
 BOOL queue_is_full(Queue *q, const QueueConfig *cfg)
 {
     if (!q || !cfg) {
         return TRUE;
     }
     return (q->count >= cfg->capacity);
 }

static Result queue_copy_tail_locked(Queue *q, const QueueConfig *cfg, void *out_data)
{
    if (!q->buffer || q->count == 0U) {
        return RESULT_NOT_FOUND;
    }
    if (q->tail >= cfg->capacity) {
        return RESULT_ERROR;
    }

    UINT32 last_idx = (q->tail + cfg->capacity - 1U) % cfg->capacity;
    if (last_idx >= cfg->capacity) {
        return RESULT_ERROR;
    }

    memcpy(out_data, q->buffer + (last_idx * cfg->element_size), cfg->element_size);
    return RESULT_SUCCESS;
}

Result queue_pop_tail(Queue *q, const QueueConfig *cfg, void *out_data)
{
    if (!q || !cfg || !out_data) {
        return RESULT_INVALID_PARAM;
    }

    queue_lock(q, cfg);

    if (!q->buffer) {
        queue_unlock(q, cfg);
        return RESULT_ERROR;
    }

    Result r = queue_copy_tail_locked(q, cfg, out_data);
    if (r != RESULT_SUCCESS) {
        queue_unlock(q, cfg);
        return r;
    }

    if (q->tail >= cfg->capacity) {
        queue_unlock(q, cfg);
        return RESULT_ERROR;
    }

    q->tail = (q->tail + cfg->capacity - 1U) % cfg->capacity;
    q->count--;

    queue_unlock(q, cfg);
    LOG_DEBUG("Queue '%s' pop_tail: count now %u", cfg->name, q->count);
    return RESULT_SUCCESS;
}
