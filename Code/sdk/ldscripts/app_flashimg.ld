/* All rights reserved.
 *
 */
 #include "core_config.h"

OUTPUT_FORMAT("elf32-littlearm", "elf32-littlearm", "elf32-littlearm")
OUTPUT_ARCH(arm)


ENTRY(appimg_enter)
EXTERN(appimg_enter)
EXTERN(appimg_exit)
EXTERN(appimg_get_param)
EXTERN(appimg_set_param)
PROVIDE(appimg_get_param = 0);
PROVIDE(appimg_set_param = 0);

#define ALIGNMENT           ALIGN(0x20)
#define HEADER_APP_IMG      LONG(0x41505032) \
                            LONG(0) \
                            LONG(0) \
                            LONG(0x41515132) \
                            LONG(ORIGIN(flash)) \
                            LONG(LENGTH(flash)) \
                            LONG(__flash_end - __flash_start) \
                            LONG(ORIGIN(ram)) \
                            LONG(LENGTH(ram)) \
                            LONG(__ram_end - __ram_start) \
                            LONG(__apiList_start) \
                            LONG(__apiEntry_start) \
                            LONG(appimg_enter) 

#define HEADER_LOAD(t, n)   LONG(t) \
                            LONG(__##n##_load_start - __flash_start) \
                            LONG(__##n##_end - __##n##_start) \
                            LONG(__##n##_start)

#define HEADER_CLEAR(n)     LONG(3) \
                            LONG(0) \
                            LONG(__##n##_end - __##n##_start) \
                            LONG(__##n##_start)
#define HEADER_DUMMY        LONG(0) LONG(0) LONG(0) LONG(0)
 

#define DEF_RO .text .text.* .gnu.linkonce.t.* .rdata .rodata .rodata.* .gnu.linkonce.r.* .glue_7 .glue_7t .vfp11_veneer .v4_bx
#define DEF_RW .data .data.* .gnu.linkonce.d.*
#define DEF_ZI .bss .bss.* COMMON .scommon .sbss .sbss.* .sbss2 .sbss2.* .gnu.linkonce.b.*

MEMORY {
    flash (rx): ORIGIN = CONFIG_APP_FLASH_ADDRESS, LENGTH = CONFIG_APP_FLASH_SIZE
    ram (rwx): ORIGIN = CONFIG_RAM_ADDRESS, LENGTH = CONFIG_RAM_SIZE
}

SECTIONS {
    . = ORIGIN(flash);
    __flash_start = .;

    .imageheader : {
        HEADER_APP_IMG
        HEADER_LOAD(4, text)
        HEADER_LOAD(4, init_array)
        HEADER_LOAD(1, data)
        HEADER_LOAD(1, app_code)
        HEADER_CLEAR(bss)
        HEADER_DUMMY
     } > flash

    . = ORIGIN(ram);
    __ram_start = .;

    .app_code ALIGNMENT : {
        __app_code_start = .;
        *(DEF_RO)
        __app_code_end = .;
        . = ALIGNMENT;
        __app_code_load_start = LOADADDR(.app_code);
    } AT > flash

    .data ALIGNMENT : {
        __data_start = .;
        *(DEF_RW)
        . = ALIGNMENT;
        __apiEntry_start = .;
        KEEP(*(.apiEntry));
        __data_end = .;
        . = ALIGNMENT;
        __data_load_start = LOADADDR(.data);
    } AT>flash

    .bss ALIGNMENT : {
        __bss_start = .;
        *(DEF_ZI)
        __bss_end = .;
        . = ALIGNMENT;
    }

    . = LOADADDR(.data) +  SIZEOF(.data);

    .text ALIGNMENT : {
        __text_start = .;
        __apiList_start = .;
        KEEP(*(.apiList));
        __text_end = .;
        . = ALIGNMENT;
        __text_load_start = LOADADDR(.text);
    } > flash

    .init_array ALIGNMENT : {
        __init_array_start = .;
        KEEP(*(.init_array*))
        __init_array_end = .;
        . = ALIGNMENT;
        __init_array_load_start = LOADADDR(.init_array);
    } > flash

    __flash_end = ADDR(.init_array) + SIZEOF(.init_array);
    __ram_end = ADDR(.app_code) + SIZEOF(.bss) + SIZEOF(.data) + SIZEOF(.app_code);

    ASSERT(__flash_end <= ORIGIN(flash) + LENGTH(flash), "FLASH overflow")
    ASSERT(__ram_end <= ORIGIN(ram) + LENGTH(ram), "RAM overflow")

    .comment 0 : { *(.comment) }
    /DISCARD/ : { *(.dynstr*) }
    /DISCARD/ : { *(.dynamic*) }
    /DISCARD/ : { *(.plt*) }
    /DISCARD/ : { *(.interp*) }
    /DISCARD/ : { *(.gnu*) }
}
