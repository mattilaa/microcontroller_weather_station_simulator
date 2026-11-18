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
    Should Be True    ${rows} >= 145    msg=Only ${rows} readings imported
    PNG File Should Be Valid    ${png}

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
