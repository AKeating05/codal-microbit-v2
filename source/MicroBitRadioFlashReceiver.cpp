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

#include "MicroBitConfig.h"
#include "MicroBitRadio.h"
#include "MicroBitRadioFlashReceiver.h"
#include "MicroBit.h"
#include "NRF52FlashManager.h"

bool rec = false;
bool flashComplete = false;
uint8_t pageBuffer[4096] = {0};
uint32_t pageIndex = 0;
uint16_t packetsReceived = 0;
uint8_t isMissingPackets = 0;

extern "C"
{
    extern uint8_t __user_start__;
    extern uint8_t __user_end__;
}

MicroBitRadioFlashReceiver::MicroBitRadioFlashReceiver(MicroBit &uBit)
    : uBit(uBit)
{
    uBit.radio.enable();
    uBit.radio.setGroup(0);
    uBit.radio.setTransmitPower(6);
    uBit.messageBus.listen(MICROBIT_ID_RADIO, MICROBIT_RADIO_EVT_DATAGRAM, this, &MicroBitRadioFlashReceiver::onData, MESSAGE_BUS_LISTENER_IMMEDIATE);
    while(!flashComplete)
    {
        if(rec)
        {
            
            PacketBuffer p = uBit.radio.datagram.recv();
            handlePacket(p);
            rec = false; 
        }
        uBit.sleep(200);
    }
}

void MicroBitRadioFlashReceiver::onData(MicroBitEvent e)
{
    rec = true;
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
        isMissingPackets = 1;
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
        NRF52FlashManager flasher(0x00076C00, 1, 4096);
        flasher.erase(0x00076C00);
        flasher.write(0x00076C00, (uint32_t *)pageBuffer, 1024);
        flashComplete = true;
    }
}