/*
 * Bare-metal AArch64 entry point for QEMU virt.
 * QEMU starts all CPUs at _start in EL1 (no firmware, no OS).
 */

    .section .text.boot, "ax"
    .global _start
_start:
    /* Only core 0 runs; park any secondary cores. */
    mrs     x0, mpidr_el1
    and     x0, x0, #0xff
    cbnz    x0, park

    /* Set up the stack. */
    ldr     x0, =__stack_top
    mov     sp, x0

    /* Allow FP/SIMD at EL1/EL0 (CPACR_EL1.FPEN = 0b11). */
    mov     x0, #(3 << 20)
    msr     cpacr_el1, x0
    isb

    /* Copy .data's initial values from FLASH to RAM. */
    ldr     x0, =__data_start
    ldr     x1, =__data_end
    ldr     x2, =__data_load
3:  cmp     x0, x1
    b.hs    4f
    ldr     x3, [x2], #8
    str     x3, [x0], #8
    b       3b
4:
    /* Zero .bss. */
    ldr     x0, =__bss_start
    ldr     x1, =__bss_end
1:  cmp     x0, x1
    b.hs    2f
    str     xzr, [x0], #8
    b       1b
2:
    /* Fill the unused stack with a pattern ("STACKPAT"), up to just below
       the current stack pointer, so runtime::stack_peak() can find the
       deepest point the stack ever reached. */
    ldr     x0, =__stack_bottom
    mov     x1, sp
    sub     x1, x1, #64
    ldr     x2, =0x5441504b43415453
5:  cmp     x0, x1
    b.hs    6f
    str     x2, [x0], #8
    b       5b
6:
    /* Run C++ global constructors, then enter main(). */
    bl      __run_global_constructors
    bl      main

    /* main() should never return; if it does, park. */
park:
    wfe
    b       park
