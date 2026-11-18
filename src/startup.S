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

    /* Zero .bss. */
    ldr     x0, =__bss_start
    ldr     x1, =__bss_end
1:  cmp     x0, x1
    b.hs    2f
    str     xzr, [x0], #8
    b       1b
2:
    /* Run C++ global constructors, then enter main(). */
    bl      __run_global_constructors
    bl      main

    /* main() should never return; if it does, park. */
park:
    wfe
    b       park
