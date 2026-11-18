*** Settings ***
Documentation       Single-key commands on the UART console.

Resource            resources/firmware.resource

Test Setup          Boot Firmware
Test Teardown       Shut Down Firmware


*** Test Cases ***
Help Command Lists Commands
    Press Key And Expect    h    Commands:
    Wait For Output    q\\s+quit QEMU

Status Command Shows State And All Tasks
    Wait For Output    temp
    Press Key And Expect    s    --- status @ \\d+ ms ---
    Wait For Output    weather: ${READING}
    Wait For Output    log: \\d+ readings, next sequence \\d+
    Wait For Output    network: up
    Wait For Output    task weather: period 1000 ms, runs \\d+
    Wait For Output    task network: period 5 ms, runs \\d+
    Wait For Output    task console: period 10 ms, runs \\d+

Weather Report Shows Latest Readings And Averages
    Wait For Output    temp
    Press Key And Expect    w    --- weather, day 1 \\d\\d:\\d\\d ---
    Wait For Output    temperature: +-?\\d+\\.\\d\\d C \\(avg -?\\d+\\.\\d\\d C\\)
    Wait For Output    humidity: +\\d+\\.\\d\\d %RH \\(avg \\d+\\.\\d\\d %RH\\)
    Wait For Output    wind speed: +\\d+\\.\\d\\d m/s \\(avg \\d+\\.\\d\\d m/s\\)
    Wait For Output    wind direction: +\\d+ deg [NESW]+ \\(avg \\d+ deg [NESW]+\\)
    Wait For Output    rain: +\\d+\\.\\d\\d mm \\(today \\d+\\.\\d\\d mm\\)

First Average Equals First Reading
    [Documentation]    With one sample in each window, every average equals its reading.
    Wait For Output    ms\\] 06:00 temp    # the first reading, at boot
    Press Key And Expect    w    --- weather
    # Match the whole line: a pattern ending in .* can match a half-received line.
    ${temperature}=    Wait For Output    temperature: +-?\\d+\\.\\d\\d C \\(avg -?\\d+\\.\\d\\d C\\)
    ${values}=    Find All    (-?\\d+\\.\\d\\d) C    ${temperature}
    Should Be Equal    ${values}[0]    ${values}[1]

History Command Dumps Collected Readings
    Wait For Output    temp
    Wait For Output    temp    timeout=3s
    Press Key And Expect    d    last [2-8] readings \\(oldest first\\):
    Wait For Output    \\n {2}${READING}\\n {2}${READING}

History Shows At Most 8 Readings
    [Documentation]    After more than 8 sensor periods the dump is capped at 8 readings.
    Collect Output    9.5s
    Press Key And Expect    d    last 8 readings

Network Command Shows Address And Counters
    Ping Firmware
    Press Key And Expect    n    network: up, MAC 52:54:00:12:34:56, IP 10.0.2.15, UDP port 5000
    Wait For Output    frames rx \\d+, tx \\d+, dropped \\d+
    Wait For Output    arp replies 0, echo replies 1, udp requests 0

Verbose Command Silences Periodic Output
    Press Key And Expect    v    verbose off
    ${quiet}=    Collect Output    1.6s
    Should Not Contain    ${quiet}    temp
    Press Key And Expect    v    verbose on
    Wait For Output    ${READING}    timeout=2s

Unknown Command Is Reported
    Press Key And Expect    x    unknown command 'x' \\(h for help\\)

Quit Command Exits QEMU With Code 0
    Press Key And Expect    q    bye!
    ${code}=    Wait For Exit    timeout=5s
    Should Be Equal As Integers    ${code}    0
