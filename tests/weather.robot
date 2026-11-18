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
    Dictionary Should Contain Key    ${r}    cloud_cover
    FOR    ${key}    IN    lcl    cirrus    altostratus    cumulus    cumulus_top    cumulonimbus
    ...    cirrus_base    cirrus_top    altostratus_base    altostratus_top
    ...    stratus    stratocumulus    nimbostratus    altocumulus    stratus_top
    ...    stratocumulus_top    nimbostratus_top    altocumulus_base    altocumulus_top
        Dictionary Should Contain Key    ${r}    ${key}
    END
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
        Should Be True    0 <= ${r}[8] <= 100    msg=Cloud cover ${r}[8] %
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
    Should Match Regexp    ${page}    ^log next=10 count=10\\n0,1,06:10,
    ${page}=    Send UDP Command    log 99999
    Should Match Regexp    ${page}    ^log next=\\d+ count=0$
    ${reply}=    Send UDP Command    log abc
    Should Be Equal    ${reply}    error: expected log <sequence>

Rain Falls In Showers Measured By A Tipping Bucket
    [Documentation]    Over 40 simulated hours it rains at least once, in
    ...                multiples of the 0.2 mm bucket, and not all the time.
    Send UDP Command    advance 2400    # 40 h: within the 256-record log
    ${log}=    Fetch Weather Log
    ${rain}=    Evaluate    [r["rain"] for r in $log]
    ${total}=    Evaluate    sum($rain)
    ${rainy}=    Evaluate    sum(1 for x in $rain if x > 0)
    Should Be True    ${total} > 0    msg=No rain in 40 hours
    Should Be True    ${rainy} < len($rain) / 3    msg=Rain in ${rainy} of ${{len($rain)}} readings
    ${not_tips}=    Evaluate    [x for x in $rain if abs(x / 0.2 - round(x / 0.2)) > 1e-6]
    Should Be Empty    ${not_tips}    msg=Not multiples of 0.2 mm: ${not_tips}

It Is More Humid When It Rains
    Send UDP Command    advance 2400    # 40 h: within the 256-record log
    ${log}=    Fetch Weather Log
    ${wet}=    Evaluate    [r["humidity"] for r in $log if r["rain"] > 0]
    ${dry}=    Evaluate    [r["humidity"] for r in $log if r["rain"] == 0]
    Should Not Be Empty    ${wet}
    ${difference}=    Evaluate    sum($wet) / len($wet) - sum($dry) / len($dry)
    Should Be True    ${difference} > 10    msg=Only ${difference} %RH more humid in rain

It Is Cloudier When It Rains
    [Documentation]    Rain falls only from a well-clouded sky.
    Send UDP Command    advance 2400    # 40 h: within the 256-record log
    ${log}=    Fetch Weather Log
    ${wet}=    Evaluate    [r["cloud_cover"] for r in $log if r["rain"] > 0]
    ${dry}=    Evaluate    [r["cloud_cover"] for r in $log if r["rain"] == 0]
    Should Not Be Empty    ${wet}
    ${clouds}=    Evaluate    [r["cloud_cover"] for r in $log]
    Should Be True    0 <= min($clouds) and max($clouds) <= 100
    ${difference}=    Evaluate    sum($wet) / len($wet) - sum($dry) / len($dry)
    Should Be True    ${difference} > 20    msg=Only ${difference} % more cloud cover in rain

Cloud Layers Sit At Their Levels
    Send UDP Command    advance 2400    # 40 h: within the 256-record log
    ${log}=    Fetch Weather Log
    FOR    ${r}    IN    @{log}
        Should Be True    800 <= ${r}[lcl] <= 1013    msg=LCL ${r}[lcl] hPa at ${r}[time]
        Should Be True    450 <= ${r}[cumulus_top] <= ${r}[lcl]    msg=Cumulus top ${r}[cumulus_top] hPa, base ${r}[lcl] hPa
        # High and middle layers: base below top (higher pressure), within their
        # model limits plus the ceilometer's few hPa of measurement noise.
        Should Be True    184 <= ${r}[cirrus_top] < ${r}[cirrus_base] <= 426
        ...    msg=Cirrus ${r}[cirrus_base]..${r}[cirrus_top] hPa at ${r}[time]
        Should Be True    414 <= ${r}[altostratus_top] < ${r}[altostratus_base] <= 806
        ...    msg=Altostratus ${r}[altostratus_base]..${r}[altostratus_top] hPa at ${r}[time]
        Should Be True    ${r}[cirrus_base] <= ${r}[altostratus_top] + 6    msg=Cirrus below altostratus at ${r}[time]
        # Low stratiform layers rest on the LCL; their depth grows with amount.
        Should Be True    ${r}[lcl] - 65 <= ${r}[stratus_top] <= ${r}[lcl] - 25
        ...    msg=Stratus top ${r}[stratus_top] hPa over LCL ${r}[lcl] hPa
        Should Be True    ${r}[lcl] - 150 <= ${r}[stratocumulus_top] <= ${r}[lcl] - 60
        ...    msg=Stratocumulus top ${r}[stratocumulus_top] hPa over LCL ${r}[lcl] hPa
        # Nimbostratus reaches the middle deck; altocumulus sits in it.
        Should Be Equal As Integers    ${r}[nimbostratus_top]    ${r}[altostratus_top]
        Should Be True    414 <= ${r}[altocumulus_top] < ${r}[altocumulus_base] <= 806
        ...    msg=Altocumulus ${r}[altocumulus_base]..${r}[altocumulus_top] hPa at ${r}[time]
        FOR    ${layer}    IN    cirrus    altostratus    cumulus    cumulonimbus
        ...    stratus    stratocumulus    nimbostratus    altocumulus
            Should Be True    0 <= ${r}[${layer}] <= 100    msg=${layer} ${r}[${layer}] %
        END
    END

Cumulus Grows In The Afternoon And Vanishes At Night
    Send UDP Command    advance 2400    # 40 h: within the 256-record log
    ${log}=    Fetch Weather Log
    ${afternoon}=    Evaluate    [r["cumulus"] for r in $log if "12:00" <= r["time"] < "18:00"]
    ${night}=    Evaluate    [r["cumulus"] for r in $log if r["time"] < "06:00"]
    Should Not Be Empty    ${afternoon}
    Should Not Be Empty    ${night}
    ${afternoon_mean}=    Evaluate    sum($afternoon) / len($afternoon)
    ${night_mean}=    Evaluate    sum($night) / len($night)
    Should Be True    ${afternoon_mean} > 10    msg=Afternoon cumulus only ${afternoon_mean} %
    Should Be True    ${night_mean} < 2    msg=Night cumulus ${night_mean} %

Cumulonimbus Comes Only With Rain-Bearing Cloud
    [Documentation]    Thunderstorm cloud needs a heavily clouded, convective sky.
    Send UDP Command    advance 2400    # 40 h: within the 256-record log
    ${log}=    Fetch Weather Log
    ${storms}=    Evaluate    [r for r in $log if r["cumulonimbus"] >= 5]
    Should Not Be Empty    ${storms}    msg=No cumulonimbus in 40 hours
    FOR    ${r}    IN    @{storms}
        Should Be True    ${r}[cloud_cover] >= 60    msg=Cumulonimbus under ${r}[cloud_cover] % cloud at ${r}[time]
    END

High And Middle Layers Move And Thicken
    [Documentation]    The cirrus and altostratus levels drift, and a layer is
    ...                thicker when there is more of it.
    Send UDP Command    advance 2400    # 40 h: within the 256-record log
    ${log}=    Fetch Weather Log
    ${cirrus_bases}=    Evaluate    {r["cirrus_base"] for r in $log}
    ${as_tops}=    Evaluate    {r["altostratus_top"] for r in $log}
    Should Be True    len($cirrus_bases) > 20    msg=Cirrus base barely moves: ${cirrus_bases}
    Should Be True    len($as_tops) > 20    msg=Altostratus top barely moves: ${as_tops}
    # Depth of the quarter of records with the most altostratus vs the
    # quarter with the least.
    ${ranked}=    Evaluate    sorted($log, key=lambda r: r["altostratus"])
    ${quarter}=    Evaluate    len($ranked) // 4
    ${thin}=    Evaluate    [r["altostratus_base"] - r["altostratus_top"] for r in $ranked[:$quarter]]
    ${thick}=    Evaluate    [r["altostratus_base"] - r["altostratus_top"] for r in $ranked[-$quarter:]]
    Should Be True    sum($thick) / len($thick) > sum($thin) / len($thin) + 8
    ...    msg=Thick altostratus not deeper than thin

Stratus Forms In Humid Air
    Send UDP Command    advance 2400    # 40 h: within the 256-record log
    ${log}=    Fetch Weather Log
    ${with_stratus}=    Evaluate    [r["humidity"] for r in $log if r["stratus"] >= 5]
    ${all}=    Evaluate    [r["humidity"] for r in $log]
    Should Not Be Empty    ${with_stratus}    msg=No stratus in 40 hours
    ${difference}=    Evaluate    sum($with_stratus) / len($with_stratus) - sum($all) / len($all)
    Should Be True    ${difference} > 10    msg=Stratus air only ${difference} %RH more humid than average

Nimbostratus Comes Only With Rain-Bearing Cloud
    [Documentation]    Nimbostratus is the cloud of steady rain: a heavily
    ...                clouded sky, and it is the thickest low-based layer.
    Send UDP Command    advance 2400    # 40 h: within the 256-record log
    ${log}=    Fetch Weather Log
    ${rain_cloud}=    Evaluate    [r for r in $log if r["nimbostratus"] >= 5]
    FOR    ${r}    IN    @{rain_cloud}
        Should Be True    ${r}[cloud_cover] >= 60    msg=Nimbostratus under ${r}[cloud_cover] % cloud at ${r}[time]
        Should Be True    ${r}[nimbostratus_top] < ${r}[stratocumulus_top]
    END

Cloud Base Is Lower When It Is Humid
    [Documentation]    The LCL is nearer the ground (higher pressure) in humid air.
    Send UDP Command    advance 2400    # 40 h: within the 256-record log
    ${log}=    Fetch Weather Log
    ${humid}=    Evaluate    [r["lcl"] for r in $log if r["humidity"] >= 85]
    ${dry}=    Evaluate    [r["lcl"] for r in $log if r["humidity"] < 60]
    Should Not Be Empty    ${humid}
    Should Not Be Empty    ${dry}
    ${difference}=    Evaluate    sum($humid) / len($humid) - sum($dry) / len($dry)
    Should Be True    ${difference} > 40    msg=Humid-air LCL only ${difference} hPa lower

Daily Rain Total Counts From Midnight
    Send UDP Command    advance 2400    # 40 h: within the 256-record log
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
