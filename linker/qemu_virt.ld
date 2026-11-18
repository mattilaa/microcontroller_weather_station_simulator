/*
 * Linker script for QEMU "virt" machine (AArch64), laid out like a
 * microcontroller: code, constants and the initial values of .data live in
 * FLASH; only .data, .bss and the stack use RAM, which is limited to 32 KiB.
 * A build that needs more RAM fails to link ("will not fit in region RAM").
 *
 * QEMU virt has no flash for a bare -kernel image, so FLASH is the start of
 * QEMU's RAM, used as read-only image memory. QEMU loads every segment at its
 * load address (LMA), so .data's initial values land in FLASH and startup.S
 * copies them into RAM, as on a real chip.
 */

ENTRY(_start)

MEMORY
{
    FLASH (rx) : ORIGIN = 0x40000000, LENGTH = 1M
    RAM (rwx)  : ORIGIN = 0x40100000, LENGTH = 32K
}

STACK_SIZE = 4K;

SECTIONS
{
    .text : ALIGN(16)
    {
        KEEP(*(.text.boot))
        *(.text .text.*)
    } > FLASH

    .rodata : ALIGN(16)
    {
        *(.rodata .rodata.*)
    } > FLASH

    /* Global constructors (only needed if something isn't constinit). */
    .init_array : ALIGN(8)
    {
        __init_array_start = .;
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
        KEEP(*(.init_array))
        __init_array_end = .;
    } > FLASH

    /* Runs from RAM, initial values stored in FLASH (copied at boot). */
    .data : ALIGN(16)
    {
        __data_start = .;
        *(.data .data.*)
        . = ALIGN(8);
        __data_end = .;
    } > RAM AT > FLASH
    __data_load = LOADADDR(.data);

    .bss (NOLOAD) : ALIGN(16)
    {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(16);
        __bss_end = .;
    } > RAM

    /* Filled with a pattern at boot so the peak use can be measured. */
    .stack (NOLOAD) : ALIGN(16)
    {
        __stack_bottom = .;
        . += STACK_SIZE;
        __stack_top = .;
    } > RAM

    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) *(.fini_array*) }
}
