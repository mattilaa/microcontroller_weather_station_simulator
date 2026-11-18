*** Settings ***
Documentation       Static checks on the ELF image: memory layout and the
...                 embedded C++ rules (no RTTI, exceptions, heap or
...                 constructors run before main).

Resource            resources/firmware.resource


*** Variables ***
${LOAD_ADDRESS}     0x40000000
# Code + read-only data + initialized data. Raise deliberately if needed.
${SIZE_BUDGET}      32768


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

No Exception Handling Or Unwinding
    Symbols Should Not Match    __gxx_personality    _Unwind_    __cxa_throw    __cxa_begin_catch
    Section Should Not Exist    .eh_frame
    Section Should Not Exist    .gcc_except_table

No Heap Allocation
    Symbols Should Not Match    ^_Znwm    ^_Znam    ^_ZdlPv    ^_ZdaPv    ^malloc$    ^free$

No Thread-Safe Static Guards Or Atexit Destructors
    Symbols Should Not Match    __cxa_guard_    __cxa_atexit    ^atexit$

No Constructors Run Before Main
    [Documentation]    All globals are constinit, so .init_array must be empty.
    ${size}=    Get Section Size    .init_array
    Should Be Equal As Integers    ${size}    0

Image Fits Size Budget
    ${size}=    Get Loaded Size
    Log    Loaded image size: ${size} bytes
    Should Be True    ${size} <= ${SIZE_BUDGET}
    ...    msg=Image is ${size} bytes, budget is ${SIZE_BUDGET}
