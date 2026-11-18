*** Settings ***
Documentation       Weather scenarios: each runs for a simulated day and must
...                 show its character. Switched over UDP ("scenario <name>"),
...                 at boot (-fw_cfg opt/weather/scenario) and from the console.

Resource            resources/firmware.resource

Test Setup          Boot Firmware
Test Teardown       Shut Down Firmware


*** Test Cases ***
Lists The Scenarios
    ${reply}=    Send UDP Command    scenarios
    Should Be Equal    ${reply}    scenarios: default summer_heat winter_high storm thunderstorm
    ${reply}=    Send UDP Command    scenario
    Should Start With    ${reply}    scenario default:

Rejects An Unknown Scenario
    ${reply}=    Send UDP Command    scenario monsoon
    Should Be Equal    ${reply}    error: unknown scenario (try: scenarios)

Very Hot Summer Day
    ${day}=    Run Scenario For A Day    summer_heat
    Should Be True    ${day}[temperature_max] >= 33    msg=Hottest only ${day}[temperature_max] C
    Should Be True    ${day}[humidity_min] <= 35    msg=Driest only ${day}[humidity_min] %RH
    Should Be True    ${day}[rain] <= 2    msg=${day}[rain] mm of rain on a hot dry day
    Should Be True    ${day}[pressure_mean] >= 1012    msg=Mean pressure ${day}[pressure_mean] hPa
    Should Be True    ${day}[cumulus_max] >= 15    msg=No afternoon cumulus

Cold Winter Day Under High Pressure
    ${day}=    Run Scenario For A Day    winter_high
    Should Be True    ${day}[temperature_max] <= -3    msg=Warmest ${day}[temperature_max] C
    Should Be True    ${day}[pressure_min] >= 1028    msg=Pressure fell to ${day}[pressure_min] hPa
    Should Be True    ${day}[wind_max] <= 6    msg=Wind up to ${day}[wind_max] m/s in a calm high
    Should Be True    ${day}[cumulus_max] <= 2    msg=Cumulus without convection
    Should Be True    ${day}[rain] == 0    msg=${day}[rain] mm of precipitation

Severe Storm
    ${day}=    Run Scenario For A Day    storm
    Should Be True    ${day}[pressure_min] <= 980    msg=Low only ${day}[pressure_min] hPa
    Should Be True    ${day}[wind_mean] >= 12    msg=Mean wind only ${day}[wind_mean] m/s
    Should Be True    ${day}[wind_max] >= 20    msg=Strongest wind only ${day}[wind_max] m/s
    Should Be True    ${day}[gust_max] >= 30    msg=Strongest gust only ${day}[gust_max] m/s
    Should Be True    ${day}[rain] >= 20    msg=Only ${day}[rain] mm of rain
    Should Be True    ${day}[nimbostratus_max] >= 50    msg=No deep rain cloud
    Should Be True    ${day}[veering] >= 60    msg=Wind veered only ${day}[veering] degrees

Thunderstorm Day
    ${day}=    Run Scenario For A Day    thunderstorm
    Should Be True    ${day}[cumulonimbus_max] >= 30    msg=Cumulonimbus only ${day}[cumulonimbus_max] %
    Should Be True    ${day}[rain_max_hour] >= 1.5    msg=Heaviest hour only ${day}[rain_max_hour] mm
    Should Be True    ${day}[temperature_max] >= 25    msg=Hottest only ${day}[temperature_max] C
    Should Be True    ${day}[wind_max] >= 8    msg=No gusts: ${day}[wind_max] m/s

Scenario Switch At Boot
    [Setup]    NONE
    ${network_args}=    Open Network Peer
    Start Firmware    @{network_args}    -fw_cfg    name=opt/weather/scenario,string=storm
    Wait For Output    scenario: storm \\(severe storm    timeout=${BOOT_TIMEOUT}
    Wait For Output    q\\s+quit QEMU    # boot finished: the network is up
    ${reply}=    Send UDP Command    scenario
    Should Start With    ${reply}    scenario storm:
    ${r}=    Query Weather
    Should Be Equal    ${r}[scenario]    storm
    Should Be True    ${r}[pressure] < 1000    msg=Storm at ${r}[pressure] hPa

Console Key Cycles The Scenarios
    Press Key And Expect    c    scenario summer_heat: very hot summer day
    Press Key And Expect    c    scenario winter_high:
    Press Key And Expect    c    scenario storm:
    Press Key And Expect    c    scenario thunderstorm:
    Press Key And Expect    c    scenario default:


*** Keywords ***
Run Scenario For A Day
    [Documentation]    Switch to ${name}, fast-forward 24 hours and summarise the
    ...                records logged since the switch.
    [Arguments]    ${name}
    ${reply}=    Send UDP Command    scenario ${name}
    Should Be Equal    ${reply}    ok scenario ${name}
    ${start}=    Send UDP Command    log 999999
    ${start}=    Evaluate    int($start.split("next=")[1].split()[0])
    Send UDP Command    advance 1440
    ${log}=    Fetch Weather Log    ${start}
    ${summary}=    Evaluate    __import__("scenario_summary").summarise($log)
    Log    ${summary}
    RETURN    ${summary}
