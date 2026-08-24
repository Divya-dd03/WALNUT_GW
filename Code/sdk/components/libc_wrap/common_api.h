/**
 * @file common_api.h
 */

#ifndef __COMMON_API_H__
#define __COMMON_API_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "osi_api.h"
#include "string.h"
#include "stdlib.h"
#include "stdio.h"

#define __FILENAME__ (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)

// Define log levels
#define LOG_LEVEL_NONE      0
#define LOG_LEVEL_ERROR     1
#define LOG_LEVEL_WARN      2
#define LOG_LEVEL_INFO      3
#define LOG_LEVEL_DEBUG     4
#define LOG_LEVEL_VERBOSE   5


// Set the current log level
#define LOG_LEVEL LOG_LEVEL_DEBUG

#define USE_USB_LOG

#ifdef USE_USB_LOG
#define SDK_LOG(fmt, ...) RTI_LOG(fmt "\r\n", ##__VA_ARGS__)
#else
#define SDK_LOG RTI_LOG
#endif

// Output logs based on the log level.
#if LOG_LEVEL >= LOG_LEVEL_ERROR
    #define SDK_LOG_E(fmt, ...) SDK_LOG("[%lu][E][%p][%s:%d %s] " fmt, osiGetTicks(), osiThreadCurrent(), __FILENAME__, __LINE__, __FUNCTION__, ##__VA_ARGS__)
#else
    #define SDK_LOG_E(fmt, ...) /* [E][%s:%d][%s] */
#endif

#if LOG_LEVEL >= LOG_LEVEL_WARN
    #define SDK_LOG_W(fmt, ...) SDK_LOG("[%lu][W][%p][%s:%d %s] " fmt, osiGetTicks(), osiThreadCurrent(), __FILENAME__, __LINE__, __FUNCTION__, ##__VA_ARGS__)
#else
    #define SDK_LOG_W(fmt, ...) /* [W][%s:%d][%s] */
#endif

#if LOG_LEVEL >= LOG_LEVEL_INFO
    #define SDK_LOG_I(fmt, ...) SDK_LOG("[%lu][I][%p][%s:%d %s] " fmt, osiGetTicks(), osiThreadCurrent(), __FILENAME__, __LINE__, __FUNCTION__, ##__VA_ARGS__)
#else
    #define SDK_LOG_I(fmt, ...) /* [I][%s:%d][%s] */
#endif

#if LOG_LEVEL >= LOG_LEVEL_DEBUG
    #define SDK_LOG_D(fmt, ...) SDK_LOG("[%lu][D][%p][%s:%d %s] " fmt, osiGetTicks(), osiThreadCurrent(), __FILENAME__, __LINE__, __FUNCTION__, ##__VA_ARGS__)
#else
    #define SDK_LOG_D(fmt, ...) /* [D][%s:%d][%s] */
#endif

#if LOG_LEVEL >= LOG_LEVEL_VERBOSE
    #define SDK_LOG_V(fmt, ...) SDK_LOG("[%lu][V][%p][%s:%d %s] " fmt, osiGetTicks(), osiThreadCurrent(), __FILENAME__, __LINE__, __FUNCTION__, ##__VA_ARGS__)
#else
    #define SDK_LOG_V(fmt, ...) /* [V][%s:%d][%s] */
#endif

#ifndef UNIT_TEST_NO_RETURN
#define UNIT_TEST_NO_RETURN 1
#endif

#define UNIT_TEST_LOG(fmt, ...) SDK_LOG_W("*** " fmt " ***", ##__VA_ARGS__)
#define UNIT_TEST_EXPECT_WITH_RETURN(cond, ret) \
{ \
    if (!(cond)) \
    { \
        UNIT_TEST_LOG("unit test failed"); \
        return ret; \
    } \
}

#define UNIT_TEST_EXPECT_NO_RETURN(cond) \
{ \
    if (!(cond)) \
    { \
        UNIT_TEST_LOG("unit test failed"); \
    } \
}

#if UNIT_TEST_NO_RETURN
    #define UNIT_TEST_EXPECT(cond, ret) UNIT_TEST_EXPECT_NO_RETURN(cond)
#else
    #define UNIT_TEST_EXPECT(cond, ret) UNIT_TEST_EXPECT_WITH_RETURN(cond, ret)
#endif


#define CIS_EXPECT(cond, ret, fmt, ...) \
{ \
    if (!(cond)) \
    { \
        SDK_LOG_E(fmt ", ret = %d", ##__VA_ARGS__, ret); \
        return ret; \
    } \
}

#define cis_assert(cond, ret) \
{ \
    if (!(cond)) \
    { \
        SDK_LOG_E("!CIS Assert Failed, ret = -0x%X, %s:%d", -ret, __FILE__, __LINE__); \
        return ret; \
    } \
}


// Variable container structure
typedef struct {
    uint16_t size;     // Current size
    uint16_t capacity; // Maximum capacity
    uint16_t elementSize;   // Size of each element
    void* buffer;      // Pointer to different types of data
    void (*freeElement)(void* element); // Function pointer for freeing elements
    osiMutex_t* mutex;
} VariableContainer;

/**
 * @brief initContainer
 *
 * @note Initializes a dynamic container
 *
 * @param capacity  The initial size of the container
 * @param elementSize  The byte size occupied by each element in the container, eg.sizeof(int)
 * @param freeElement  Free function of element variable, if the element is the base variable(int char...), it can be NULL
 *
 * @return
 *      - VariableContainer*  point of container
 */
VariableContainer* initContainer(uint16_t capacity, size_t elementSize, void (*freeElement)(void*));

/**
 * @brief freeContainer
 *
 * @note Free a dynamic container
 *
 * @param container  point of container
 *
 * @return
 *      - void
 */
void freeContainer(VariableContainer* container);

/**
 * @brief lockContainer
 *
 * @note Lock the container
 *
 * @param container  point of container
 *
 * @return
 *      - bool  true is success; false is failure;
 */
bool lockContainer(VariableContainer* container);

/**
 * @brief unlockContainer
 *
 * @note Unlock the container
 *
 * @param container  point of container
 *
 * @return
 *      - void
 */
void unlockContainer(VariableContainer* container);

/**
 * @brief getContainerEmptyElement
 *
 * @note Gets an available blank element pointer from the container
 *
 * @param container  point of container
 *
 * @return
 *      - void*  element pointer
 */
void* getContainerEmptyElement(VariableContainer* container);

/**
 * @brief getContainerElement
 *
 * @note Gets a pointer to the element in the container that specifies index
 *
 * @param container  point of container
 * @param index  the element index that you want to query
 *
 * @return
 *      - void*  Return NULL if index is greater than the container capacity, otherwise return a pointer to the index element
 */
void* getContainerElement(VariableContainer* container, uint16_t index);

/**
 * @brief removeContainerElement
 *
 * @note Removes the element that specifies index from the container
 *
 * @param container  point of container
 * @param index  the element index that you want to delete
 *
 * @return
 *      - void*  element pointer
 */
int removeContainerElement(VariableContainer* container, uint16_t index);

/**
 * @brief removeContainerAllElement
 *
 * @note Removes all element from the container
 *
 * @param container  point of container
 *
 * @return
 *      - int  Returns 0 if successful, -1 otherwise
 */
int removeContainerAllElement(VariableContainer* container);

/**
 * @brief print_oneLine
 *
 * @note Concatenate the title and byte array into a byte stream, print the byte stream out in one line
 *
 * @param title  description of byte stream
 * @param buf  byte array
 * @param len  length of byte array
 *
 * @return
 *      - int  Returns 0 if successful, -1 otherwise
 */
void print_oneLine(const char *title, const unsigned char *buf, size_t len);

/**
 * @brief split_into_random
 *
 * @note Used to split a large integer into a fixed-length array of random numbers,
 *       the sum of the array elements equal to the large integer
 *
 * @param number  a large integer
 * @param random  fixed-length integers
 * @param count  length of random
 *
 * @return
 *      - void
 */
void split_into_random(uint16_t number, uint16_t *random, int count);

/**
 * @brief Generate the required QR code
 *
 * @param txt_buf  QR data
 * @param txt_len  txt_buf len
 * @param qr_version  the size of QR
 *
 * @return
 *      - -1 Error
 */
int gen_qr_encode_used(char* txt_buf, uint32_t txt_len, uint8_t qr_version);

#ifdef __cplusplus
}
#endif

#endif /* __COMMON_API_H__ */
