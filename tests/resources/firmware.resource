*** Settings ***
Documentation       Shared settings and keywords for the firmware test suites.
...                 Override the defaults on the command line, e.g.
...                 --variable FIRMWARE:build-release/firmware.elf

Library             Collections
Library             qemu_firmware.QemuFirmware    ${QEMU}    ${FIRMWARE}
Library             elf_image.ElfImage    ${FIRMWARE}
Library             network_peer.NetworkPeer


*** Variables ***
${FIRMWARE}         build/firmware.elf
${QEMU}             qemu-system-aarch64
${BOOT_TIMEOUT}     10s
# Default simulation speed for the suites: ten simulated minutes per second,
# so tests see hours of weather quickly. ${NONE} = no switch (real time).
${SPEED}            600
# One line of sensor readings (groups: hours, minutes, temperature, humidity,
# wind speed, direction, compass point, rain), e.g.
# "06:10:00 temp 12.34 C | hum 71.20 %RH | wind 3.42 m/s from 225 deg SW | rain 0.40 mm"
${READING}          (\\d\\d):(\\d\\d):\\d\\d temp (-?\\d+\\.\\d\\d) C \\| hum (\\d+\\.\\d\\d) %RH \\| wind (\\d+\\.\\d\\d) m/s from (\\d+) deg ([NESW]+) \\| rain (\\d+\\.\\d\\d) mm


*** Keywords ***
Start Firmware With Network
    [Documentation]    Start QEMU with a virtio-net device wired to the test's
    ...                network peer, and the simulation speed switch set to
    ...                ${speed} (${NONE}: no switch, so the firmware runs in real time).
    [Arguments]    ${speed}=${SPEED}
    ${network_args}=    Open Network Peer
    ${speed_args}=    Speed Switch    ${speed}
    Start Firmware    @{network_args}    @{speed_args}

Speed Switch
    [Documentation]    QEMU arguments for the simulation speed switch.
    [Arguments]    ${speed}
    IF    $speed is None    RETURN    @{EMPTY}
    RETURN    -fw_cfg    name=opt/weather/speed,string=${speed}

Boot Firmware
    [Documentation]    Start QEMU (with network) and wait until the firmware has printed its help text.
    [Arguments]    ${speed}=${SPEED}
    Start Firmware With Network    ${speed}
    Wait For Output    q\\s+quit QEMU    timeout=${BOOT_TIMEOUT}

Shut Down Firmware
    Stop Firmware
    Close Network Peer

Press Key And Expect
    [Documentation]    Send a key to the UART console and wait for the response.
    [Arguments]    ${key}    ${pattern}    ${timeout}=5s
    Send Key    ${key}
    ${match}=    Wait For Output    ${pattern}    timeout=${timeout}
    RETURN    ${match}

Differences Should Be Within
    [Documentation]    Fail unless each gap between consecutive values is
    ...                between ${min} and ${max}.
    [Arguments]    ${values}    ${min}    ${max}
    ${count}=    Get Length    ${values}
    FOR    ${i}    IN RANGE    1    ${count}
        ${gap}=    Evaluate    int(${values}[${i}]) - int(${values}[${i - 1}])
        Should Be True    ${min} <= ${gap} <= ${max}
        ...    msg=Gap of ${gap} between ${values}[${i - 1}] and ${values}[${i}] is outside ${min}..${max}
    END

Query Weather
    [Documentation]    Ask the UDP weather service for the latest reading; returns it as a dictionary.
    ${reply}=    Send UDP Command    weather
    ${reading}=    Evaluate    json.loads($reply)    modules=json
    RETURN    ${reading}

Weather At
    [Documentation]    Jump the simulated clock to ${time} (HH:MM) and return the reading taken there.
    [Arguments]    ${time}
    ${reply}=    Send UDP Command    time ${time}
    Should Be Equal    ${reply}    ok time ${time}
    ${reading}=    Query Weather
    RETURN    ${reading}
