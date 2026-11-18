*** Settings ***
Documentation       The data import and plotting tool (tools/weather_tool.py)
...                 against the real firmware: its demo mode starts QEMU with
...                 user-mode networking, fast-forwards, imports the log over
...                 UDP and draws the chart.

Library             OperatingSystem
Library             Process
Resource            resources/firmware.resource

Suite Setup         Find Python


*** Variables ***
${TOOL}             ${CURDIR}/../tools/weather_tool.py


*** Test Cases ***
Demo Imports A Simulated Day And Plots It
    ${csv}=    Set Variable    ${OUTPUT DIR}/demo_weather.csv
    ${png}=    Set Variable    ${OUTPUT DIR}/demo_weather.png
    ${result}=    Run Process    ${PYTHON}    ${TOOL}    demo
    ...    --firmware    ${FIRMWARE}    --qemu    ${QEMU}
    ...    --minutes    1440    --csv    ${csv}    --out    ${png}
    ...    timeout=120s    stderr=STDOUT
    Log    ${result.stdout}
    Should Be Equal As Integers    ${result.rc}    0    msg=${result.stdout}
    Should Contain    ${result.stdout}    Imported
    ${rows}=    Count CSV Rows    ${csv}
    Should Be True    ${rows} >= 144    msg=Only ${rows} records for one simulated day
    PNG File Should Be Valid    ${png}

Record Collects Live Measurements At A Chosen Speed
    [Documentation]    Six simulated hours at 3600x take about six real seconds.
    ${csv}=    Set Variable    ${OUTPUT DIR}/record_weather.csv
    ${png}=    Set Variable    ${OUTPUT DIR}/record_weather.png
    ${result}=    Run Process    ${PYTHON}    ${TOOL}    record
    ...    --firmware    ${FIRMWARE}    --qemu    ${QEMU}    --speed    3600
    ...    --hours    6    --interval    1    --csv    ${csv}    --out    ${png}
    ...    timeout=120s    stderr=STDOUT
    Log    ${result.stdout}
    Should Be Equal As Integers    ${result.rc}    0    msg=${result.stdout}
    Should Contain    ${result.stdout}    Recording at 3600x
    ${rows}=    Count CSV Rows    ${csv}
    Should Be True    ${rows} >= 36    msg=Only ${rows} records in six simulated hours
    PNG File Should Be Valid    ${png}

Record Keeps Up At The Fastest Speed
    [Documentation]    72000x logs 120 records per second; with its default
    ...                import interval (chosen from the speed) record must not
    ...                lose any before the firmware's 256-record log wraps.
    ${csv}=    Set Variable    ${OUTPUT DIR}/fast_weather.csv
    ${result}=    Run Process    ${PYTHON}    ${TOOL}    record
    ...    --firmware    ${FIRMWARE}    --qemu    ${QEMU}    --speed    72000
    ...    --hours    48    --csv    ${csv}
    ...    --out    ${OUTPUT DIR}/fast_weather.png    timeout=120s    stderr=STDOUT
    Should Be Equal As Integers    ${result.rc}    0    msg=${result.stdout}
    Should Not Contain    ${result.stdout}    overwritten
    ${rows}=    Count CSV Rows    ${csv}
    Should Be True    ${rows} >= 288    msg=Only ${rows} records in 48 simulated hours
    ${text}=    Get File    ${csv}
    ${sequences}=    Evaluate    [int(line.split(",")[0]) for line in $text.splitlines()[1:] if line.strip()]
    ${consecutive}=    Evaluate    $sequences == list(range($sequences[0], $sequences[0] + len($sequences)))
    Should Be True    ${consecutive}    msg=Records missing from the import

Plot Works On An Imported CSV
    ${csv}=    Set Variable    ${OUTPUT DIR}/demo_weather.csv
    ${png}=    Set Variable    ${OUTPUT DIR}/replot.png
    File Should Exist    ${csv}
    ${result}=    Run Process    ${PYTHON}    ${TOOL}    plot    ${csv}
    ...    --out    ${png}    --hours    0    timeout=60s    stderr=STDOUT
    Should Be Equal As Integers    ${result.rc}    0    msg=${result.stdout}
    PNG File Should Be Valid    ${png}


*** Keywords ***
Find Python
    ${python}=    Evaluate    sys.executable    modules=sys
    Set Suite Variable    ${PYTHON}    ${python}

Count CSV Rows
    [Arguments]    ${path}
    ${text}=    Get File    ${path}
    ${lines}=    Evaluate    [l for l in $text.splitlines()[1:] if l.strip()]
    ${count}=    Get Length    ${lines}
    RETURN    ${count}

PNG File Should Be Valid
    [Arguments]    ${path}
    File Should Exist    ${path}
    ${size}=    Get File Size    ${path}
    Should Be True    ${size} > 20000    msg=Suspiciously small chart: ${size} bytes
    ${is_png}=    Evaluate    open($path, "rb").read(8) == bytes.fromhex("89504e470d0a1a0a")
    Should Be True    ${is_png}    msg=${path} is not a PNG file
