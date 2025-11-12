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

bool rec = false;

MicroBitRadioFlashReceiver::MicroBitRadioFlashReceiver(MicroBit &uBit)
    : uBit(uBit)
{
    uBit.radio.enable();
    uBit.radio.setGroup(0);
    uBit.radio.setTransmitPower(6);
    uBit.messageBus.listen(MICROBIT_ID_RADIO, MICROBIT_RADIO_EVT_DATAGRAM, this, &MicroBitRadioFlashReceiver::onData, MESSAGE_BUS_LISTENER_IMMEDIATE);
    while(1)
    {
        if(rec)
        {
            
            PacketBuffer p = uBit.radio.datagram.recv();
            handlePacket(p);
            rec = false; 
        }
        uBit.sleep(500);
    }
}

void MicroBitRadioFlashReceiver::onData(MicroBitEvent e)
{
    // PacketBuffer p = uBit.radio.datagram.recv();
    // handlePacket(p);
    rec = true;
}

void MicroBitRadioFlashReceiver::handlePacket(PacketBuffer packet)
{
    // Packet Structure:
    // 0            1    2    3   4    5   6    7     8    --   15
    // +--------------------------------------------------------+
    // | Sndr/Recvr | Seq Num | Flash Addr | Checksum | Padding |
    // +--------------------------------------------------------+
    // |                        Data                            |
    // +--------------------------------------------------------+
    // 16                                                       31

    uint8_t id = packet[0];
    uint32_t seq = ((uint32_t)packet[1]) | ((uint32_t)packet[2] << 8);
    uBit.serial.printf("id=%d seq=%lu [1]=%d [2]=%d\n", id, seq, packet[1], packet[2]);
    // uBit.display.print((int)seq);
    uBit.display.image.setPixelValue(0,0,255);
    uBit.sleep(10);
    uBit.display.image.setPixelValue(0,0,0);
}
