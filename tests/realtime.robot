*** Settings ***
Documentation       Simulation speed: real time by default, the QEMU speed
...                 switch (-fw_cfg opt/weather/speed), changing the speed
...                 while running, and how samples become 10-minute records.

Resource            resources/firmware.resource

Test Teardown       Shut Down Firmware


*** Test Cases ***
Runs In Real Time Without The Switch
    Start Firmware With Network    speed=${NONE}
    Wait For Output    simulation speed: 1x \\(real time\\), default    timeout=${BOOT_TIMEOUT}
    ${output}=    Collect Output    3.2s
    ${seconds}=    Seconds Of Samples    ${output}
    Differences Should Be Within    ${seconds}    1    2

Reads The Speed Switch From QEMU
    Start Firmware With Network    speed=3600
    Wait For Output    simulation speed: 3600x, from QEMU -fw_cfg opt/weather/speed
    ...    timeout=${BOOT_TIMEOUT}
    ${output}=    Collect Output    3.2s
    ${seconds}=    Seconds Of Samples    ${output}
    Differences Should Be Within    ${seconds}    3500    3800

No Record Before Ten Simulated Minutes
    Boot Firmware    speed=${NONE}
    ${page}=    Send UDP Command    log 0
    Should Be Equal    ${page}    log next=0 count=0

Speed Can Be Changed Over UDP
    Boot Firmware    speed=${NONE}
    ${reply}=    Send UDP Command    speed 60
    Should Be Equal    ${reply}    ok speed 60
    ${reply}=    Send UDP Command    speed
    Should Be Equal    ${reply}    speed 60
    Collect Output    1.2s    # skip the update that straddles the change
    ${output}=    Collect Output    3.2s
    ${seconds}=    Seconds Of Samples    ${output}
    Differences Should Be Within    ${seconds}    55    70
    ${reply}=    Send UDP Command    speed 0
    Should Start With    ${reply}    error: expected speed
    ${reply}=    Send UDP Command    speed 3601
    Should Start With    ${reply}    error: expected speed

Console Keys Change The Speed
    Boot Firmware    speed=${NONE}
    Press Key And Expect    +    speed 10x
    Press Key And Expect    +    speed 60x
    Press Key And Expect    -    speed 10x
    Press Key And Expect    -    speed 1x
    Press Key And Expect    -    speed 1x    # already the slowest

A Record Is The Mean Of Its Samples
    [Documentation]    At 60x every sample is one simulated minute; the first
    ...                record (06:00-06:10) must lie within its samples' range.
    Boot Firmware    speed=60
    ${output}=    Collect Output    11.5s
    ${samples}=    Find All    ${READING}    ${output}
    ${temperatures}=    Evaluate    [float(s[2]) for s in $samples if s[0] == "06" and int(s[1]) <= 10]
    ${log}=    Fetch Weather Log
    Should Not Be Empty    ${log}    msg=No record after 10 simulated minutes
    Should Be Equal    ${log}[0][time]    06:10
    ${record}=    Set Variable    ${log}[0][temperature]
    Should Be True    min($temperatures) <= ${record} <= max($temperatures)
    ...    msg=Record ${record} C outside its samples ${temperatures}


*** Keywords ***
Seconds Of Samples
    [Documentation]    Simulated time of day, in seconds, of every sample line in ${output}.
    [Arguments]    ${output}
    ${times}=    Find All    \\] (\\d\\d:\\d\\d:\\d\\d) temp    ${output}
    ${seconds}=    Evaluate    [int(t[0:2]) * 3600 + int(t[3:5]) * 60 + int(t[6:8]) for t in $times]
    RETURN    ${seconds}
