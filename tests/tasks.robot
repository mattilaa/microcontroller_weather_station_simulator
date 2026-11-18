*** Settings ***
Documentation       The scheduler runs each periodic task at its configured rate.
...                 Timing tolerances are generous because CI runners can stall QEMU.

Resource            resources/firmware.resource

Test Setup          Boot Firmware
Test Teardown       Shut Down Firmware


*** Test Cases ***
Weather Sensors Are Read Every Second
    ${output}=    Collect Output    3.2s
    ${timestamps}=    Find All    \\[\\s*(\\d+) ms\\] \\d\\d:\\d\\d:\\d\\d temp    ${output}
    ${count}=    Get Length    ${timestamps}
    Should Be True    ${count} >= 3    msg=Only ${count} readings in 3.2 s
    Differences Should Be Within    ${timestamps}    900    1200

Simulated Clock Advances Ten Minutes Per Reading
    ${output}=    Collect Output    4.2s
    ${readings}=    Find All    ${READING}    ${output}
    ${minutes}=    Evaluate    [int(r[0]) * 60 + int(r[1]) for r in $readings]
    Differences Should Be Within    ${minutes}    9    13
