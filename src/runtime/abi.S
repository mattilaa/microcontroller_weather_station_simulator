/* The only ABI glue: LLVM's C runtime symbols and linker-defined addresses.
 * All behavior, including byte-copy loops and panic handling, lives in MLang.
 */
    .section .text.runtime_abi, "ax"
    .global abort
    .type abort, %function
abort:
    b firmware_panic__void

    .global memcpy
    .type memcpy, %function
memcpy:
    b memory_copy__ptr_u8_ptr_u8_u64

    .global memmove
    .type memmove, %function
memmove:
    b memory_move__ptr_u8_ptr_u8_u64

    .global memset
    .type memset, %function
memset:
    b memory_fill__ptr_u8_i32_u64

    .global memcmp
    .type memcmp, %function
memcmp:
    b memory_compare__ptr_u8_ptr_u8_u64

    .global firmware_stack_bottom
    .type firmware_stack_bottom, %function
firmware_stack_bottom:
    adrp x0, __stack_bottom
    add x0, x0, :lo12:__stack_bottom
    ret

    .global firmware_stack_top
    .type firmware_stack_top, %function
firmware_stack_top:
    adrp x0, __stack_top
    add x0, x0, :lo12:__stack_top
    ret
