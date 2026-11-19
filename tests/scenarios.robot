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
    Should Be Equal    ${reply}    scenarios: default summer_heat winter_high storm thunderstorm mcs supercell
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
    Should Be True    ${day}[wind_mean] >= 8    msg=Mean wind only ${day}[wind_mean] m/s
    Should Be True    ${day}[wind_max] >= 20    msg=Strongest wind only ${day}[wind_max] m/s
    Should Be True    ${day}[gust_max] >= 30    msg=Strongest gust only ${day}[gust_max] m/s
    Should Be True    ${day}[rain] >= 20    msg=Only ${day}[rain] mm of rain
    Should Be True    ${day}[nimbostratus_max] >= 50    msg=No deep rain cloud
    Should Be True    ${day}[veering] >= 60    msg=Wind veered only ${day}[veering] degrees

Cirrus Spreads Ahead Of The Severe Storm
    [Documentation]    The low's cirrus shield comes hours before its rain and
    ...                wind: a calm morning under thick cirrus, then the storm.
    ${day}=    Run Scenario For A Day    storm
    Should Be True    ${day}[first_rain_hours] is not None and ${day}[first_rain_hours] >= 4
    ...    msg=Rain began after ${day}[first_rain_hours] h
    Should Be True    ${day}[cirrus_first_hour] >= 60    msg=Cirrus only ${day}[cirrus_first_hour] % at first
    Should Be True    ${day}[wind_before_rain] <= 8    msg=Wind ${day}[wind_before_rain] m/s before the storm
    Should Be True    ${day}[wind_max] >= 20    msg=Strongest wind only ${day}[wind_max] m/s

Thunderstorm Day
    ${day}=    Run Scenario For A Day    thunderstorm
    Should Be True    ${day}[cumulonimbus_max] >= 30    msg=Cumulonimbus only ${day}[cumulonimbus_max] %
    Should Be True    ${day}[rain_max_hour] >= 1.5    msg=Heaviest hour only ${day}[rain_max_hour] mm
    Should Be True    ${day}[temperature_max] >= 25    msg=Hottest only ${day}[temperature_max] C
    Should Be True    ${day}[wind_max] >= 8    msg=No gusts: ${day}[wind_max] m/s

Anvil Cirrus Spreads Before The Thunderstorm
    ${day}=    Run Scenario For A Day    thunderstorm
    Should Be True    ${day}[first_cumulonimbus_hours] is not None    msg=No cumulonimbus
    Should Be True    ${day}[cirrus_before_cumulonimbus] >= 60
    ...    msg=Cirrus only ${day}[cirrus_before_cumulonimbus] % in the hour before the cumulonimbus

Approaching MCS Hits Hardest At Its Gust Front
    [Documentation]    A mesoscale convective system: anvil cirrus long before
    ...                it, the most extreme wind at the gust front, a line of
    ...                cumulonimbus, then trailing rain as the wind dies down.
    ${day}=    Run Scenario For A Day    mcs
    Should Be True    ${day}[storm_start] is not None    msg=No gust front
    Should Be True    ${day}[cirrus_before_rain] >= 60    msg=Cirrus only ${day}[cirrus_before_rain] % before the rain
    Should Be True    ${day}[front_wind] >= 20    msg=Gust front only ${day}[front_wind] m/s
    Should Be True    ${day}[gust_max] >= 40    msg=Strongest gust only ${day}[gust_max] m/s
    Should Be True    ${day}[late_wind] <= ${day}[front_wind] / 2
    ...    msg=Wind ${day}[late_wind] m/s hours later, ${day}[front_wind] m/s at the front
    Should Be True    ${day}[cumulonimbus_max] >= 30    msg=No convective line
    Should Be True    ${day}[nimbostratus_max] >= 50    msg=No trailing rain cloud
    Should Be True    ${day}[rain] >= 15    msg=Only ${day}[rain] mm of rain
    Should Be Equal As Integers    ${day}[tornado_records]    0

Supercell Blows Hard Throughout And Ends In A Tornado
    ${day}=    Run Scenario For A Day    supercell
    Should Be True    ${day}[storm_start] is not None    msg=No storm
    Should Be True    ${day}[cirrus_before_rain] >= 50    msg=Cirrus only ${day}[cirrus_before_rain] % before the rain
    Should Be True    ${day}[storm_minutes] >= 60    msg=Storm winds for only ${day}[storm_minutes] min
    Should Be True    ${day}[storm_wind_min] >= 18    msg=Wind fell to ${day}[storm_wind_min] m/s mid-storm
    Should Be True    ${day}[cumulonimbus_max] >= 80    msg=Cumulonimbus only ${day}[cumulonimbus_max] %
    Should Be True    ${day}[tornado_records] >= 1    msg=No tornado
    Should Be True    ${day}[first_tornado] >= ${day}[storm_end] - 2 and ${day}[first_tornado] > ${day}[storm_start] + 3
    ...    msg=Tornado at record ${day}[first_tornado], storm ${day}[storm_start]..${day}[storm_end]
    Should Be True    ${day}[tornado_gust] >= 55    msg=Tornado gusts only ${day}[tornado_gust] m/s
    Should Be True    ${day}[pressure_min] <= ${day}[pressure_mean] - 5    msg=No pressure drop

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
    Press Key And Expect    c    scenario mcs:
    Press Key And Expect    c    scenario supercell:
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
