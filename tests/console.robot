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
    Wait For Output    speed: 600x
    Wait For Output    log: \\d+ records, next sequence \\d+
    Wait For Output    network: up
    Wait For Output    task weather: period 1000 ms, runs \\d+
    Wait For Output    task network: period 5 ms, runs \\d+
    Wait For Output    task console: period 10 ms, runs \\d+

Weather Report Shows Latest Sample And Interval Means
    Wait For Output    temp
    Press Key And Expect    w    --- weather, day 1 \\d\\d:\\d\\d:\\d\\d, speed 600x ---
    Wait For Output    temperature: +-?\\d+\\.\\d\\d C \\(10-min mean -?\\d+\\.\\d\\d C\\)
    Wait For Output    humidity: +\\d+\\.\\d\\d %RH \\(10-min mean \\d+\\.\\d\\d %RH\\)
    Wait For Output    wind speed: +\\d+\\.\\d\\d m/s \\(10-min mean \\d+\\.\\d\\d m/s\\)
    Wait For Output    wind direction: +\\d+ deg [NESW]+ \\(10-min mean \\d+ deg [NESW]+\\)
    Wait For Output    rain: +\\d+\\.\\d\\d mm in this interval \\(today \\d+\\.\\d\\d mm\\)
    Wait For Output    cloud cover: +\\d+ % \\(10-min mean \\d+ %\\)

Interval Mean Starts At The First Sample
    [Documentation]    Right after boot the interval holds one sample, so its mean equals it.
    Wait For Output    ms\\] 06:00:00 temp    # the first sample, at boot
    Press Key And Expect    w    --- weather
    # Match the whole line: a pattern ending in .* can match a half-received line.
    ${temperature}=    Wait For Output    temperature: +-?\\d+\\.\\d\\d C \\(10-min mean -?\\d+\\.\\d\\d C\\)
    ${values}=    Find All    (-?\\d+\\.\\d\\d) C    ${temperature}
    Should Be Equal    ${values}[0]    ${values}[1]

History Command Dumps Collected Records
    # Records end at 06:10 and 06:20: wait for the third sample line.
    Wait For Output    temp
    Wait For Output    temp    timeout=3s
    Wait For Output    temp    timeout=3s
    Press Key And Expect    d    last [2-8] 10-minute records \\(oldest first\\):
    Wait For Output    \\n {2}${READING}\\n {2}${READING}

History Shows At Most 8 Readings
    [Documentation]    After more than 8 sensor periods the dump is capped at 8 readings.
    Collect Output    9.5s
    Press Key And Expect    d    last 8 10-minute records

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

Stack Stays Within Its Budget
    [Documentation]    Run the deepest code paths (console reports, UDP replies
    ...                with log pages, a large ping), then check the measured
    ...                peak stack use leaves at least half of the 4 KiB unused.
    Wait For Output    temp    timeout=3s
    Wait For Output    temp    timeout=3s
    FOR    ${key}    IN    w    d    n
        Send Key    ${key}
    END
    Send UDP Command    weather
    Send UDP Command    log 0
    Send UDP Command    help
    Ping Firmware    1400
    Press Key And Expect    s    --- status
    ${line}=    Wait For Output    stack: peak \\d+ of 4096 bytes
    ${peak}=    Find All    peak (\\d+)    ${line}
    Should Be True    ${peak}[0] <= 2048    msg=Stack peak ${peak}[0] of 4096 bytes

Unknown Command Is Reported
    Press Key And Expect    x    unknown command 'x' \\(h for help\\)

Quit Command Exits QEMU With Code 0
    Press Key And Expect    q    bye!
    ${code}=    Wait For Exit    timeout=5s
    Should Be Equal As Integers    ${code}    0
