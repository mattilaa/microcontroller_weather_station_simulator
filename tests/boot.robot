*** Settings ***
Documentation       The firmware boots on QEMU virt without an operating system.

Resource            resources/firmware.resource

Test Teardown       Shut Down Firmware


*** Test Cases ***
Prints Boot Banner
    Start Firmware With Network
    Wait For Output    === bare-metal AArch64 MLang on QEMU virt ===    timeout=${BOOT_TIMEOUT}

Runs At EL1 Without An Operating System
    Start Firmware With Network
    Wait For Output    running at EL1, no operating system    timeout=${BOOT_TIMEOUT}

Brings Up The Network At Boot
    Start Firmware With Network
    Wait For Output    network: up, MAC 52:54:00:12:34:56, IP 10.0.2.15, UDP port 5000
    ...    timeout=${BOOT_TIMEOUT}

Prints Command Help On Boot
    Start Firmware With Network
    Wait For Output    Commands:    timeout=${BOOT_TIMEOUT}
    Wait For Output    h\\s+help
    Wait For Output    s\\s+status
    Wait For Output    w\\s+weather report
    Wait For Output    d\\s+dump the last 8 records
    Wait For Output    n\\s+network status
    Wait For Output    \\+\\s+faster simulation
    Wait For Output    c\\s+next weather scenario
    Wait For Output    v\\s+toggle verbose output
    Wait For Output    q\\s+quit QEMU

Runs Without A Network Device
    [Documentation]    Without a NIC the firmware reports it and keeps running.
    Start Firmware
    Wait For Output    network: no virtio-net device    timeout=${BOOT_TIMEOUT}
    Wait For Output    temp -?\\d+\\.\\d\\d C

Reports A Legacy Virtio Device
    [Documentation]    Without force-legacy=false QEMU exposes the legacy
    ...                virtio-mmio interface, which the driver rejects clearly.
    Start Firmware    -netdev    hubport,id=net0,hubid=0    -device    virtio-net-device,netdev=net0
    Wait For Output    network: legacy virtio-mmio device    timeout=${BOOT_TIMEOUT}
    Wait For Output    temp -?\\d+\\.\\d\\d C
