*** Settings ***
Documentation       Ethernet / IPv4 stack over the virtio-net device. The test
...                 library is a second host on the same network segment and
...                 checks every reply field, including checksums.

Resource            resources/firmware.resource

Test Setup          Boot Firmware
Test Teardown       Shut Down Firmware


*** Test Cases ***
Announces Itself With Gratuitous ARP
    Wait For Gratuitous ARP

Answers ARP Request For Its Address
    Send ARP Request    10.0.2.15
    ${mac}=    Wait For ARP Reply
    Should Be Equal    ${mac}    52:54:00:12:34:56

Ignores ARP Request For Another Address
    Send ARP Request    10.0.2.99
    No Reply Should Arrive    1s

Answers Ping
    Ping Firmware    hello firmware

Answers Large Ping
    Ping Firmware    1400

Answers Many Pings In A Row
    [Documentation]    More requests than descriptors in each virtqueue (16),
    ...                so receive and transmit buffers must be recycled.
    FOR    ${i}    IN RANGE    1    41
        Ping Firmware    sequence=${i}
    END

Answers UDP Ping
    ${reply}=    Send UDP Command    ping
    Should Be Equal    ${reply}    pong

Lists UDP Commands
    ${reply}=    Send UDP Command    help
    Should Be Equal    ${reply}    commands: weather, time HH:MM, log [sequence], advance <minutes>, ping, help

Rejects Unknown UDP Command
    ${reply}=    Send UDP Command    make coffee
    Should Be Equal    ${reply}    error: unknown command

Accepts UDP Without Checksum
    ${reply}=    Send UDP Without Checksum    ping
    Should Be Equal    ${reply}    pong

Drops Packet With Bad IP Header Checksum
    Send IPv4 With Bad Header Checksum
    No Reply Should Arrive    1s

Drops UDP Datagram With Bad Checksum
    Send UDP With Bad Checksum
    No Reply Should Arrive    1s

Ignores Frames For Another MAC Address
    Send Ping To Other MAC
    No Reply Should Arrive    1s

Ignores Packets For Another IP Address
    Send Ping To Other IP
    No Reply Should Arrive    1s

Ignores UDP To Other Ports
    Send UDP To Port    5001
    No Reply Should Arrive    1s

Keeps Working After Bad Traffic
    Send IPv4 With Bad Header Checksum
    Send UDP With Bad Checksum
    Send Ping To Other MAC
    ${reply}=    Send UDP Command    ping
    Should Be Equal    ${reply}    pong
