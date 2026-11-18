*** Settings ***
Documentation       Shared settings and keywords for the firmware test suites.
...                 Override the defaults on the command line, e.g.
...                 --variable FIRMWARE:build-release/firmware.elf

Library             Collections
Library             qemu_firmware.QemuFirmware    ${QEMU}    ${FIRMWARE}
Library             elf_image.ElfImage    ${FIRMWARE}


*** Variables ***
${FIRMWARE}         build/firmware.elf
${QEMU}             qemu-system-aarch64
${BOOT_TIMEOUT}     10s


*** Keywords ***
Boot Firmware
    [Documentation]    Start QEMU and wait until the firmware has printed its help text.
    Start Firmware
    Wait For Output    q\\s+quit QEMU    timeout=${BOOT_TIMEOUT}

Press Key And Expect
    [Documentation]    Send a key to the UART console and wait for the response.
    [Arguments]    ${key}    ${pattern}    ${timeout}=5s
    Send Key    ${key}
    ${match}=    Wait For Output    ${pattern}    timeout=${timeout}
    RETURN    ${match}

Differences Should Be Within
    [Documentation]    Fail unless each gap between consecutive timestamps is
    ...                between ${min} and ${max} ms.
    [Arguments]    ${timestamps}    ${min}    ${max}
    ${count}=    Get Length    ${timestamps}
    FOR    ${i}    IN RANGE    1    ${count}
        ${gap}=    Evaluate    int(${timestamps}[${i}]) - int(${timestamps}[${i - 1}])
        Should Be True    ${min} <= ${gap} <= ${max}
        ...    msg=Gap of ${gap} ms between ${timestamps}[${i - 1}] and ${timestamps}[${i}] ms is outside ${min}..${max} ms
    END
