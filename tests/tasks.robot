*** Settings ***
Documentation       The scheduler runs each periodic task at its configured rate.
...                 Timing tolerances are generous because CI runners can stall QEMU.

Resource            resources/firmware.resource

Test Setup          Boot Firmware
Test Teardown       Shut Down Firmware


*** Variables ***
${LED_LINE}         \\[\\s*(\\d+) ms\\] LED (?:ON|OFF)


*** Test Cases ***
Heartbeat Toggles LED Every 500 ms
    ${output}=    Collect Output    3.2s
    ${states}=    Find All    LED (ON|OFF)    ${output}
    ${count}=    Get Length    ${states}
    Should Be True    ${count} >= 5    msg=Only ${count} LED updates in 3.2 s
    FOR    ${i}    IN RANGE    1    ${count}
        Should Not Be Equal    ${states}[${i}]    ${states}[${i - 1}]
        ...    msg=LED did not alternate: ${states}
    END
    ${timestamps}=    Find All    ${LED_LINE}    ${output}
    Differences Should Be Within    ${timestamps}    450    650

Weather Sensors Are Read Every Second
    ${output}=    Collect Output    3.2s
    ${timestamps}=    Find All    \\[\\s*(\\d+) ms\\] \\d\\d:\\d\\d temp    ${output}
    ${count}=    Get Length    ${timestamps}
    Should Be True    ${count} >= 3    msg=Only ${count} readings in 3.2 s
    Differences Should Be Within    ${timestamps}    900    1200

Simulated Clock Advances Ten Minutes Per Reading
    ${output}=    Collect Output    4.2s
    ${readings}=    Find All    ${READING}    ${output}
    ${minutes}=    Evaluate    [int(r[0]) * 60 + int(r[1]) for r in $readings]
    Differences Should Be Within    ${minutes}    9    13
