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
#include "MicroBitRadioFlashSender.h"
#include "MicroBit.h"

extern "C"
{
    extern uint8_t __testpage_start__;
    extern uint8_t __testpage_end__;
}



MicroBitRadioFlashSender::MicroBitRadioFlashSender(MicroBit &uBit)
    : uBit(uBit), seq_num(0)
{
    uBit.radio.enable();
    uBit.radio.setGroup(0);
    uBit.radio.setTransmitPower(6);
    sendUserProgram(uBit);
    // uBit.display.scroll("RF");
}

void MicroBitRadioFlashSender::sendUserProgram(MicroBit &uBit)
{

    uint8_t *currentAddr = &__testpage_start__;
    uint32_t user_start = (uint32_t)&__testpage_start__;
    uint32_t user_end = (uint32_t)&__testpage_end__;
    uint32_t user_size = user_end - user_start;
    
    uint32_t payloadSize = 256 - 16;
    uint16_t npackets = (user_size + payloadSize-1)/payloadSize;

    
    
    // Packet Structure:
    // 0            1    2    3  4  5  6   7    8     9   --   15
    // +--------------------------------------------------------+
    // | Sndr/Recvr | Seq Num | Flash Addr | Checksum | Padding |
    // +--------------------------------------------------------+
    // |                        Data                            |
    // +--------------------------------------------------------+
    // 16                                                       31

    for(uint16_t i = 0; i<npackets; i++)
    {
        uint8_t packet[payloadSize+16] = {0};
        
        // first byte is id (sender or receiver) 255 for sender
        packet[0] = 255;

        // sequence number
        packet[1] = (i >> 8) & 0xFF;
        packet[2] = i & 0xFF;

        uint32_t offset = (uint32_t)currentAddr - user_start;
        memcpy(&packet[3], &offset, 4);

        uint32_t rem = user_size - (i*payloadSize);
        uint32_t chunk =  rem > payloadSize ? payloadSize : rem;
        
        memcpy(&packet[16],currentAddr, chunk);


        // checksum
        uint16_t sum = 0;
        for(uint32_t j = 16; j<16+chunk; j++)
        {
            sum+= packet[j];
        }
        packet[7] = (uint8_t)(sum >> 8) & 0xFF;
        packet[8] = (uint8_t)(sum & 0xFF);


        PacketBuffer b(packet,payloadSize+16);
        // ManagedString out = ManagedString(packet[2]);
        // uBit.display.print(out);
        // uBit.serial.send(out);
        uBit.radio.datagram.send(b);
        
        currentAddr += chunk;
        
        uBit.sleep(2000);
    }
}

uint32_t MicroBitRadioFlashSender::getSeqNum()
{
    return seq_num;
}