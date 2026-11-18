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
    Dictionary Should Contain Key    ${r}    rain
    Dictionary Should Contain Key    ${r}    rain_today
    Should Be Equal As Integers    ${r}[day]    1
    Should Match Regexp    ${r}[time]    ^\\d\\d:\\d\\d:\\d\\d$
    Should Be Equal As Integers    ${r}[speed]    600

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
        Should Be True    0 <= ${r}[7] <= 2    msg=Rain ${r}[7] mm in 10 minutes
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
    Should Match Regexp    ${r}[time]    ^12:(3[4-9]|4\\d):\\d\\d$

Invalid Time Is Rejected
    ${reply}=    Send UDP Command    time 24:00
    Should Be Equal    ${reply}    error: expected time HH:MM
    ${reply}=    Send UDP Command    time 1:00
    Should Be Equal    ${reply}    error: expected time HH:MM

Fast-Forward Logs A Record Every Ten Minutes
    ${before}=    Fetch Weather Log
    ${reply}=    Send UDP Command    advance 1440
    Should Match Regexp    ${reply}    ^ok advanced to day 2 \\d\\d:\\d\\d$
    ${after}=    Fetch Weather Log
    ${added}=    Evaluate    len($after) - len($before)
    Should Be True    144 <= ${added} <= 146    msg=${added} records for one simulated day

Fast-Forward Rejects Invalid Durations
    ${reply}=    Send UDP Command    advance 5
    Should Start With    ${reply}    error: expected advance <minutes>
    ${reply}=    Send UDP Command    advance 99999
    Should Start With    ${reply}    error: expected advance <minutes>

Log Is Served In Pages
    Send UDP Command    advance 600
    ${page}=    Send UDP Command    log 0
    # The first record covers 06:00-06:10 and is stamped with its end.
    Should Match Regexp    ${page}    ^log next=20 count=20\\n0,1,06:10,
    ${page}=    Send UDP Command    log 99999
    Should Match Regexp    ${page}    ^log next=\\d+ count=0$
    ${reply}=    Send UDP Command    log abc
    Should Be Equal    ${reply}    error: expected log <sequence>

Rain Falls In Showers Measured By A Tipping Bucket
    [Documentation]    Over three simulated days it rains at least once, in
    ...                multiples of the 0.2 mm bucket, and not all the time.
    Send UDP Command    advance 4320
    ${log}=    Fetch Weather Log
    ${rain}=    Evaluate    [r["rain"] for r in $log]
    ${total}=    Evaluate    sum($rain)
    ${rainy}=    Evaluate    sum(1 for x in $rain if x > 0)
    Should Be True    ${total} > 0    msg=No rain in three days
    Should Be True    ${rainy} < len($rain) / 3    msg=Rain in ${rainy} of ${{len($rain)}} readings
    ${not_tips}=    Evaluate    [x for x in $rain if abs(x / 0.2 - round(x / 0.2)) > 1e-6]
    Should Be Empty    ${not_tips}    msg=Not multiples of 0.2 mm: ${not_tips}

It Is More Humid When It Rains
    Send UDP Command    advance 4320
    ${log}=    Fetch Weather Log
    ${wet}=    Evaluate    [r["humidity"] for r in $log if r["rain"] > 0]
    ${dry}=    Evaluate    [r["humidity"] for r in $log if r["rain"] == 0]
    Should Not Be Empty    ${wet}
    ${difference}=    Evaluate    sum($wet) / len($wet) - sum($dry) / len($dry)
    Should Be True    ${difference} > 10    msg=Only ${difference} %RH more humid in rain

Daily Rain Total Counts From Midnight
    Send UDP Command    advance 4320
    ${now}=    Query Weather
    ${log}=    Fetch Weather Log
    # Today's records up to the queried sample. The 00:00 record still
    # belongs to yesterday, and the weather task keeps logging meanwhile.
    # (Robot variables can't be used inside a generator, so pass them in.)
    ${logged}=    Evaluate
    ...    (lambda log, day, until: round(sum(r["rain"] for r in log if r["day"] == day and "00:00" < r["time"] <= until), 2))($log, $now["day"], $now["time"][:5])
    # The sample's total also counts the interval in progress (< 10 minutes).
    Should Be True    ${logged} <= ${now}[rain_today] <= ${logged} + 2
    ...    msg=rain_today ${now}[rain_today] mm vs ${logged} mm in today's records
