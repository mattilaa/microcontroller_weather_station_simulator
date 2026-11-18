*** Settings ***
Documentation       The simulated weather behaves like weather: daily cycles,
...                 correlated quantities and physically sensible ranges.
...                 Uses the UDP service to read values and jump the clock.

Resource            resources/firmware.resource

Test Setup          Boot Firmware
Test Teardown       Shut Down Firmware


*** Test Cases ***
Weather Reading Has All Quantities
    ${r}=    Query Weather
    Dictionary Should Contain Key    ${r}    day
    Dictionary Should Contain Key    ${r}    time
    Dictionary Should Contain Key    ${r}    temperature
    Dictionary Should Contain Key    ${r}    humidity
    Dictionary Should Contain Key    ${r}    wind_speed
    Dictionary Should Contain Key    ${r}    wind_direction
    Dictionary Should Contain Key    ${r}    wind_compass
    Should Be Equal As Integers    ${r}[day]    1
    Should Match Regexp    ${r}[time]    ^\\d\\d:\\d\\d$

Readings Stay In Physical Ranges
    ${output}=    Collect Output    6.2s
    ${readings}=    Find All    ${READING}    ${output}
    ${count}=    Get Length    ${readings}
    Should Be True    ${count} >= 5
    FOR    ${r}    IN    @{readings}
        Should Be True    -10 <= ${r}[2] <= 40    msg=Temperature ${r}[2] C
        Should Be True    15 <= ${r}[3] <= 100    msg=Humidity ${r}[3] %RH
        Should Be True    0 <= ${r}[4] <= 40    msg=Wind speed ${r}[4] m/s
        Should Be True    0 <= ${r}[5] <= 359    msg=Wind direction ${r}[5] deg
    END

Temperature Follows The Daily Cycle
    [Documentation]    Coldest before dawn, warmest mid-afternoon.
    ${night}=    Weather At    03:00
    ${morning}=    Weather At    09:00
    ${afternoon}=    Weather At    15:00
    ${evening}=    Weather At    21:00
    Should Be True    ${night}[temperature] < ${morning}[temperature] < ${afternoon}[temperature]
    Should Be True    ${evening}[temperature] < ${afternoon}[temperature]
    Should Be True    ${afternoon}[temperature] - ${night}[temperature] > 10
    ...    msg=Day/night swing only ${night}[temperature] .. ${afternoon}[temperature] C

Humidity Is Higher When It Is Colder
    [Documentation]    Relative humidity falls as the air warms.
    ${night}=    Weather At    03:00
    ${afternoon}=    Weather At    15:00
    Should Be True    ${night}[humidity] - ${afternoon}[humidity] > 25
    ...    msg=Night ${night}[humidity] %RH vs afternoon ${afternoon}[humidity] %RH

Wind Varies Around The Prevailing South-West
    ${output}=    Collect Output    8.2s
    ${directions}=    Find All    from (\\d+) deg    ${output}
    ${distinct}=    Evaluate    len(set($directions))
    Should Be True    ${distinct} >= 3    msg=Wind direction barely changes: ${directions}
    FOR    ${d}    IN    @{directions}
        Should Be True    120 <= ${d} <= 330    msg=Direction ${d} far from SW
    END

Compass Point Matches The Direction
    ${r}=    Query Weather
    ${expected}=    Evaluate
    ...    ["N","NNE","NE","ENE","E","ESE","SE","SSE","S","SSW","SW","WSW","W","WNW","NW","NNW"][int((${r}[wind_direction] * 10 + 112) // 225) % 16]
    Should Be Equal    ${r}[wind_compass]    ${expected}

Clock Can Be Set Over UDP
    ${reply}=    Send UDP Command    time 12:34
    Should Be Equal    ${reply}    ok time 12:34
    ${r}=    Query Weather
    Should Match Regexp    ${r}[time]    ^12:(3[4-9]|4\\d)$

Invalid Time Is Rejected
    ${reply}=    Send UDP Command    time 24:00
    Should Be Equal    ${reply}    error: expected time HH:MM
    ${reply}=    Send UDP Command    time 1:00
    Should Be Equal    ${reply}    error: expected time HH:MM
