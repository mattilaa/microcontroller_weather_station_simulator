*** Settings ***
Documentation       The scheduler runs each periodic task at its configured rate.
...                 Timing tolerances are generous because CI runners can stall QEMU.

Resource            resources/firmware.resource

Test Setup          Boot Firmware
Test Teardown       Stop Firmware


*** Variables ***
${LED_LINE}         \\[\\s*(\\d+) ms\\] LED (?:ON|OFF)
${TEMP_LINE}        \\[\\s*(\\d+) ms\\] temp = (\\d+\\.\\d\\d) C \\(avg (\\d+\\.\\d\\d) C\\)


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

Sensor Reports Temperature Every Second
    ${output}=    Collect Output    3.2s
    ${timestamps}=    Find All    \\[\\s*(\\d+) ms\\] temp =    ${output}
    ${count}=    Get Length    ${timestamps}
    Should Be True    ${count} >= 3    msg=Only ${count} temperature readings in 3.2 s
    Differences Should Be Within    ${timestamps}    900    1200

Temperature Stays In Simulated Range
    ${output}=    Collect Output    4.2s
    ${readings}=    Find All    ${TEMP_LINE}    ${output}
    Should Not Be Empty    ${readings}
    FOR    ${reading}    IN    @{readings}
        ${raw}=    Set Variable    ${reading}[1]
        ${avg}=    Set Variable    ${reading}[2]
        Should Be True    15.0 <= ${raw} <= 35.0    msg=Raw temperature ${raw} C out of range
        Should Be True    15.0 <= ${avg} <= 35.0    msg=Average ${avg} C out of range
    END

First Average Equals First Reading
    [Documentation]    With a single sample in the window, the moving average is that sample.
    ${line}=    Wait For Output    temp = \\d+\\.\\d\\d C \\(avg \\d+\\.\\d\\d C\\)
    ${values}=    Find All    (\\d+\\.\\d\\d) C    ${line}
    Should Be Equal    ${values}[0]    ${values}[1]
