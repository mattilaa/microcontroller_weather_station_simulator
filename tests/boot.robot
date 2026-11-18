*** Settings ***
Documentation       The firmware boots on QEMU virt without an operating system.

Resource            resources/firmware.resource

Test Setup          Start Firmware
Test Teardown       Stop Firmware


*** Test Cases ***
Prints Boot Banner
    Wait For Output    === bare-metal AArch64 C\\+\\+ on QEMU virt ===    timeout=${BOOT_TIMEOUT}

Runs At EL1 Without An Operating System
    Wait For Output    running at EL1, no operating system    timeout=${BOOT_TIMEOUT}

Prints Command Help On Boot
    Wait For Output    Commands:    timeout=${BOOT_TIMEOUT}
    Wait For Output    h\\s+help
    Wait For Output    s\\s+status
    Wait For Output    d\\s+dump temperature history
    Wait For Output    v\\s+toggle verbose output
    Wait For Output    q\\s+quit QEMU
