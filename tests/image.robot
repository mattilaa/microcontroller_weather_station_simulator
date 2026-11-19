*** Settings ***
Documentation       Static checks on the ELF image: memory layout and the
...                 freestanding firmware rules (no RTTI, exceptions, heap or
...                 constructors run before main).

Resource            resources/firmware.resource


*** Variables ***
${LOAD_ADDRESS}     0x40000000
# The RAM region in linker/qemu_virt.ld; FLASH is everything below it.
${RAM_START}        0x40100000
${RAM_BUDGET}       32768
${STACK_SIZE}       4096
# Flash image: code + read-only data + .data's initial values. MCUs with
# 32 KiB of RAM typically have 128 KiB..1 MiB of flash; 64 KiB keeps an eye
# on code growth (Release is ~33 KiB, Debug ~29 KiB).
${FLASH_BUDGET}     65536


*** Test Cases ***
Entry Point Is Start Of RAM
    Entry Point Should Be    ${LOAD_ADDRESS}
    ${start}=    Get Symbol Address    _start
    Should Be Equal As Integers    ${start}    ${LOAD_ADDRESS}

Stack Is Placed After BSS
    ${bss_end}=    Get Symbol Address    __bss_end
    ${stack_top}=    Get Symbol Address    __stack_top
    Should Be True    ${stack_top} > ${bss_end}

No RTTI Data
    Symbols Should Not Match    ^_ZTI    ^_ZTS

No C++ Runtime Or Adapters
    Symbols Should Not Match    ^_Z    ^__cxa_    ^_GLOBAL__sub_I_

No Exception Handling Or Unwinding
    Symbols Should Not Match    __gxx_personality    _Unwind_    __cxa_throw    __cxa_begin_catch
    Symbols Should Not Match    __mlang_std_exceptions_    ^_setjmp$    ^fwrite$    ^stderr$    ^__stderrp$
    Section Should Not Exist    .eh_frame
    Section Should Not Exist    .gcc_except_table

No Heap Allocation
    Symbols Should Not Match    ^_Znwm    ^_Znam    ^_ZdlPv    ^_ZdaPv    ^malloc$    ^calloc$    ^realloc$    ^free$

Weather Simulation Uses MLang
    Get Symbol Address    handle_command__ptr_struct_Station_ptr_u8_u64_ptr_u8
    Get Symbol Address    weather_task_run__ptr_struct_Station_ptr_u8_bool_u64

No Thread-Safe Static Guards Or Atexit Destructors
    Symbols Should Not Match    __cxa_guard_    __cxa_atexit    ^atexit$

No Constructors Run Before Main
    [Documentation]    MLang globals use static initialization, so .init_array must be empty.
    ${size}=    Get Section Size    .init_array
    Should Be Equal As Integers    ${size}    0

Flash Image Fits Size Budget
    ${size}=    Get Loaded Size
    Log    Flash image size: ${size} bytes
    Should Be True    ${size} <= ${FLASH_BUDGET}
    ...    msg=Flash image is ${size} bytes, budget is ${FLASH_BUDGET}

RAM Fits In 32 KiB
    [Documentation]    .data + .bss + the stack. (The linker already refuses
    ...                to link more; this documents and double-checks it.)
    ${data}=    Get Section Size    .data
    ${bss}=    Get Section Size    .bss
    ${stack}=    Get Section Size    .stack
    ${ram}=    Evaluate    ${data} + ${bss} + ${stack}
    Log    RAM: .data ${data} + .bss ${bss} + stack ${stack} = ${ram} bytes
    Should Be True    ${ram} <= ${RAM_BUDGET}    msg=RAM use ${ram} bytes exceeds ${RAM_BUDGET}
    Should Be Equal As Integers    ${stack}    ${STACK_SIZE}

Data Runs From RAM And Loads From Flash
    ${load}=    Get Symbol Address    __data_load
    ${start}=    Get Symbol Address    __data_start
    ${bss_end}=    Get Symbol Address    __bss_end
    Should Be True    ${load} < ${RAM_START} <= ${start}    msg=.data must load from FLASH and run in RAM
    Should Be True    ${bss_end} <= ${RAM_START} + ${RAM_BUDGET}
