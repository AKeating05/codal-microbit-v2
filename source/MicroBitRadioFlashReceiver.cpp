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



MicroBitRadioFlashReceiver::MicroBitRadioFlashReceiver(MicroBit &uBit)
    : uBit(uBit)
{
    uBit.init();
    uBit.radio.enable();

    while(1)
    {
        PacketBuffer p uBit.radio.datagram.recv();
        handlePacket(p);
        uBit.sleep(1000);
    }
}

void handlePacket(PacketBuffer packet)
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
    uint32_t seq = packet[1] | packet[2] << 8;

    uBit.display.scroll(seq);
}
