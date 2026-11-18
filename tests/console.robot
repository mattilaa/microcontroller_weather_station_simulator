*** Settings ***
Documentation       Single-key commands on the UART console.

Resource            resources/firmware.resource

Test Setup          Boot Firmware
Test Teardown       Stop Firmware


*** Test Cases ***
Help Command Lists Commands
    Press Key And Expect    h    Commands:
    Wait For Output    q\\s+quit QEMU

Status Command Shows State And All Tasks
    Wait For Output    temp =
    Press Key And Expect    s    --- status @ \\d+ ms ---
    Wait For Output    LED: (on|off), temp: \\d+\\.\\d\\d C, avg: \\d+\\.\\d\\d C
    Wait For Output    task heartbeat: period 500 ms, runs \\d+
    Wait For Output    task sensor: period 1000 ms, runs \\d+
    Wait For Output    task console: period 10 ms, runs \\d+

History Command Dumps Collected Samples
    Wait For Output    temp =
    Wait For Output    temp =    timeout=3s
    Press Key And Expect    d    last [2-8] of 8 samples \\(oldest first\\):
    Wait For Output    \\n {2}\\d+\\.\\d\\d C\\n {2}\\d+\\.\\d\\d C

History Never Exceeds Window Size
    [Documentation]    After more than 8 sensor periods the history is capped at 8 samples.
    Wait For Output    LED    # booted
    Collect Output    9.5s
    Press Key And Expect    d    last 8 of 8 samples

Verbose Command Silences Periodic Output
    Press Key And Expect    v    verbose off
    ${quiet}=    Collect Output    1.6s
    Should Not Contain    ${quiet}    LED
    Should Not Contain    ${quiet}    temp =
    Press Key And Expect    v    verbose on
    Wait For Output    LED (ON|OFF)    timeout=2s

Unknown Command Is Reported
    Press Key And Expect    x    unknown command 'x' \\(h for help\\)

Quit Command Exits QEMU With Code 0
    Press Key And Expect    q    bye!
    ${code}=    Wait For Exit    timeout=5s
    Should Be Equal As Integers    ${code}    0
