/*
 * Linker script for QEMU "virt" machine (AArch64).
 * RAM starts at 0x40000000; QEMU loads the ELF segments there directly
 * when the image is passed with -kernel.
 */

ENTRY(_start)

MEMORY
{
    RAM (rwx) : ORIGIN = 0x40000000, LENGTH = 128M
}

STACK_SIZE = 64K;

SECTIONS
{
    .text : ALIGN(16)
    {
        KEEP(*(.text.boot))
        *(.text .text.*)
    } > RAM

    .rodata : ALIGN(16)
    {
        *(.rodata .rodata.*)
    } > RAM

    /* Global constructors (only needed if something isn't constinit). */
    .init_array : ALIGN(8)
    {
        __init_array_start = .;
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
        KEEP(*(.init_array))
        __init_array_end = .;
    } > RAM

    .data : ALIGN(16)
    {
        *(.data .data.*)
    } > RAM

    .bss (NOLOAD) : ALIGN(16)
    {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(16);
        __bss_end = .;
    } > RAM

    .stack (NOLOAD) : ALIGN(16)
    {
        . += STACK_SIZE;
        __stack_top = .;
    } > RAM

    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) *(.fini_array*) }
}
