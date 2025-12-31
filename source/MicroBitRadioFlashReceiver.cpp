/*
The MIT License (MIT)

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.
*/

#include "MicroBitRadioFlashReceiver.h"
#include "nrf_nvmc.h"
#include "nrf.h"
#include "nrf_sdm.h"
#include <stdio.h>

extern "C"
{
    extern uint8_t __user_start__;
    extern uint8_t __user_end__;
}

volatile bool packetsComplete = false;
volatile bool flashComplete = false;
volatile bool packetEvent = false;
uint8_t pageBuffer[4096];


void flashUserProgram(uint32_t flash_addr, uint8_t *pageBuffer)
{
    uint32_t page = flash_addr / 4096;
    uint32_t *flash_ptr = (uint32_t *)flash_addr;
    uint32_t *data = (uint32_t *)pageBuffer;

    while (sd_flash_page_erase(page) == NRF_ERROR_BUSY)
        __WFE();

    for (uint32_t offset = 0; offset < 1024; offset += 256)
    {
        flashComplete = false;

        while (sd_flash_write(flash_ptr + offset, data + offset, 256) == NRF_ERROR_BUSY)
            __WFE();
    }
}


MicroBitRadioFlashReceiver::MicroBitRadioFlashReceiver(MicroBit &uBit)
    : uBit(uBit),
    pageIndex(0),
    // totalPackets(0),
    packetsReceived(0),
    isMissingPackets(false)

{
    memset(pageBuffer, 0, sizeof(pageBuffer));
    uBit.radio.enable();
    uBit.radio.setGroup(0);
    uBit.radio.setTransmitPower(6);
    uBit.messageBus.listen(MICROBIT_ID_RADIO, MICROBIT_RADIO_EVT_DATAGRAM, this, &MicroBitRadioFlashReceiver::onData, MESSAGE_BUS_LISTENER_IMMEDIATE);
    while(!packetsComplete)
    {
        if(packetEvent)
        {
            
            PacketBuffer p = uBit.radio.datagram.recv();
            handlePacket(p);
            packetEvent = false; 
        }
        uBit.sleep(200);
    }
}


void MicroBitRadioFlashReceiver::onData(MicroBitEvent)
{
    packetEvent = true;
}

void MicroBitRadioFlashReceiver::handlePacket(PacketBuffer packet)
{
    // Packet Structure:
    // 0            1    2    3             4               5         6         7       8      9    10   11        15
    // +------------------------------------------------------------------------------------------------------------+
    // | Sndr/Recvr | Seq Num | Page number | Total packets | Remaining # bytes | Payload size | Checksum | Padding |
    // +------------------------------------------------------------------------------------------------------------+
    // |                                                       Data                                                 |
    // +------------------------------------------------------------------------------------------------------------+
    // 16                                                                                                          31

    uint8_t id = packet[0];
    uint16_t seq = ((uint16_t)packet[1]<<8) | ((uint16_t)packet[2]);
    uint8_t pageNum = packet[3];
    uint8_t totalPackets = packet[4];
    uint16_t remaining = ((uint16_t)packet[5]<<8) | ((uint16_t)packet[6]);
    uint16_t payloadSize = ((uint16_t)packet[7]<<8) | ((uint16_t)packet[8]);

    // checksum
    uint16_t recChecksum = ((uint16_t)packet[9]<<8) | ((uint16_t)packet[10]);
    uint16_t sum = 0;
    for(uint32_t j = 16; j<payloadSize + 16; j++)
    {
        sum+= packet[j];
    }

    if(sum!=recChecksum)
    {
        isMissingPackets = true;
    }
    else
    {
        memcpy(&pageBuffer[pageIndex], &packet[16],payloadSize);
        pageIndex += payloadSize;
    }
    ManagedString out = ManagedString("id: ") + ManagedString((int) id) + ManagedString("\n")
        + ManagedString("seq: ") + ManagedString((int)seq) + ManagedString("\n")
        + ManagedString("page#: ") + ManagedString((int)pageNum) + ManagedString("\n")
        + ManagedString("tpackets: ") + ManagedString((int)totalPackets) + ManagedString("\n")
        + ManagedString("remaining: ") + ManagedString((int)remaining) + ManagedString("\n")
        + ManagedString("payloadSize: ") + ManagedString((int)payloadSize) + ManagedString("\n")
        + ManagedString("Rchecksum: ") + ManagedString((int)recChecksum) + ManagedString("\n")
        + ManagedString("checksum: ") + ManagedString((int)sum) + ManagedString("\n") + ManagedString("\n");
    uBit.serial.send(out);
    

    packetsReceived++;
    if(packetsReceived==totalPackets && !isMissingPackets)
    {
        packetsComplete = true;
        uBit.radio.disable();


        flashUserProgram(0x76000,pageBuffer);
        __DSB();
        __ISB();
        NVIC_SystemReset();
    }
}