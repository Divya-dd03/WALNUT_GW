/**
 * @file queue_manager.h
 * @brief Generic named queue manager for weware
 * 
 * Provides creation and management of in-memory queues with:
 *  - A symbolic name
 *  - Fixed element size
 *  - Fixed maximum length (number of elements)
 * 
 * Queues are implemented as ring buffers and can be used from any module.
 */

#ifndef WEWARE_QUEUE_MANAGER_H
#define WEWARE_QUEUE_MANAGER_H

/*---------------------------------------------------------------
 * Standard Includes
 *--------------------------------------------------------------*/
#include "common/types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*---------------------------------------------------------------
 * Type Definitions
 *--------------------------------------------------------------*/

/**
 * @brief Opaque queue handle type
 */
typedef struct Queue Queue;

/**
 * @brief Maximum number of queues that can be registered
 */
#define QUEUE_MANAGER_MAX_QUEUES 16

/**
 * @brief Queue creation options
 */
typedef struct
{
    const char *name;            /**< Unique name for this queue (null-terminated string) */
    UINT32 element_size;         /**< Size of each element in bytes */
    UINT32 capacity;             /**< Maximum number of elements in the queue */
    BOOL   thread_safe;         /**< If TRUE, protects operations with a mutex */

    /* Optional file-backed overflow configuration */
    UINT32 max_file_size;       /**< Maximum file size in bytes when using overflow (0 to disable) */
    
    /* Queue persistence across reboot */
    BOOL persist_on_reboot;      /**< If TRUE, queue data can be explicitly saved before reboot and loaded after reboot */
} QueueConfig;

/*---------------------------------------------------------------
 * Public API
 *--------------------------------------------------------------*/

/**
 * @brief Create a circular queue
 * @param cfg Queue configuration (must not be NULL)
 * @param out Output parameter for the created queue handle (must not be NULL)
 * @return RESULT_SUCCESS on success,
 *         RESULT_INVALID_PARAM on bad arguments,
 *         RESULT_BUSY if maximum number of queues reached,
 *         RESULT_OUT_OF_MEMORY if memory allocation fails
 */
Result queue_manager_create(const QueueConfig *cfg, Queue **out);

/**
 * @brief Persist queue contents for the next boot
 * @param queue Queue handle
 * @param cfg Queue configuration (must not be NULL)
 * @return RESULT_SUCCESS on success, RESULT_INVALID_PARAM on bad arguments
 * @note This is an explicit pre-reboot step; queue_manager_destroy() no longer persists automatically.
 */
Result queue_manager_persist(Queue *queue, const QueueConfig *cfg);

/**
 * @brief Delete persist snapshot (@c .dat) after a successful consumer commit (e.g. TCP ACK).
 * @details Removes @c C:/queue/queue_<name>.dat when the in-memory ring is empty so a
 *          later boot does not reload payloads already ACK-sent. For TCP send queue, also clears
 *          the saved overflow read offset when the overflow file is fully drained.
 */
Result queue_persist_clear_snapshot_after_consume(Queue *queue, const QueueConfig *cfg);

/**
 * @brief Destroy a queue and free its resources
 * @param queue Queue handle (must not be NULL)
 * @param cfg Queue configuration (must not be NULL)
 * @return RESULT_SUCCESS on success, RESULT_INVALID_PARAM on NULL queue
 * @note Does not persist queue contents; call queue_manager_persist() separately when needed.
 */
Result queue_manager_destroy(Queue *queue, const QueueConfig *cfg);

/**
 * @brief Enqueue (push) an element to the tail of the queue
 * @param queue Queue handle
 * @param cfg Queue configuration (must not be NULL)
 * @param data Pointer to element data (must be at least element_size bytes)
 * @return RESULT_SUCCESS on success,
 *         RESULT_INVALID_PARAM on bad arguments,
 *         RESULT_BUSY if queue is full
 */
Result queue_push(Queue *queue, const QueueConfig *cfg, const void *data);

/**
 * @brief Dequeue (pop) an element from the head of the queue
 * @param queue Queue handle
 * @param cfg Queue configuration (must not be NULL)
 * @param out_data Output buffer (must be at least element_size bytes)
 * @param num_elements Maximum number of elements to pop
 * @param out_popped Output: actual number of elements popped
 * @return RESULT_SUCCESS on success,
 *         RESULT_INVALID_PARAM on bad arguments,
 *         RESULT_NOT_FOUND if queue is empty
 */
Result queue_pop(Queue *queue, const QueueConfig *cfg, void *out_data, UINT32 num_elements, UINT32 *out_popped);

/**
 * @brief Peek at the element at the head of the queue without removing it
 * @param queue Queue handle
 * @param cfg Queue configuration (must not be NULL)
 * @param out_data Output buffer (must be at least element_size bytes)
 * @return RESULT_SUCCESS on success,
 *         RESULT_INVALID_PARAM on bad arguments,
 *         RESULT_NOT_FOUND if queue is empty
 */
Result queue_peek(Queue *queue, const QueueConfig *cfg, void *out_data);

/**
 * @brief Copy up to max_elements from the logical head of the queue without removing them.
 * @details Order matches queue_pop (overflow file segment first, then in-memory ring). Use
 *          queue_pop with the same count after the consumer commits (e.g. TCP ACK) to remove.
 * @param out Output buffer for max_elements * element_size bytes (contiguous array of elements)
 * @param max_elements Maximum elements to copy
 * @param out_count Output: elements copied (0 if empty)
 * @return RESULT_SUCCESS if out_count > 0, RESULT_NOT_FOUND if queue empty, else error
 */
Result queue_peek_batch(Queue *queue, const QueueConfig *cfg, void *out, UINT32 max_elements, UINT32 *out_count);

/**
 * @brief Get current number of elements in the queue
 * @param queue Queue handle
 * @param out_count Output: current element count
 * @return RESULT_SUCCESS on success, RESULT_INVALID_PARAM on bad arguments
 */
Result queue_get_count(Queue *queue, UINT32 *out_count);

/**
 * @brief Check if queue is empty
 * @param queue Queue handle
 * @return TRUE if empty or invalid, FALSE otherwise
 */
BOOL queue_is_empty(Queue *queue);

/**
 * @brief Check if queue is full
 * @param queue Queue handle
 * @param cfg Queue configuration (must not be NULL)
 * @return TRUE if full or invalid, FALSE otherwise
 */
BOOL queue_is_full(Queue *queue, const QueueConfig *cfg);

/**
 * @brief Remove the newest in-memory element (slot before @c tail).
 * @note Does not read or modify overflow file; empty in-memory ring returns @c RESULT_NOT_FOUND.
 */
Result queue_pop_tail(Queue *queue, const QueueConfig *cfg, void *out_data);

#ifdef __cplusplus
}
#endif

#endif /* WEWARE_QUEUE_MANAGER_H */

