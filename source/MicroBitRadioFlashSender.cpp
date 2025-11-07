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
    extern uint32_t __etext;
}

MicroBitRadioFlashSender::MicroBitRadioFlashSender(MicroBit &uBit)
    : uBit(uBit), seq_num(0)
{
    uBit.radio.enable();
}

void MicroBitRadioFlashSender::sendUserProgram()
{
    uint8_t *currentAddr = (uint8_t*)&__etext;
    uint32_t user_start = (uint32_t)&__etext;
    uint32_t user_end = MICROBIT_TOP_OF_FLASH;
    uint32_t user_size = user_end - user_start;

    uint32_t npackets;
    
    if(!(user_size % 16))
        npackets = user_size/16;
    else
        npackets = (user_size/16) + 1;

    
    
    // Packet Structure:
    // 0            1    2    3   4    5   6    7     8    --   15
    // +--------------------------------------------------------+
    // | Sndr/Recvr | Seq Num | Flash Addr | Checksum | Padding |
    // +--------------------------------------------------------+
    // |                        Data                            |
    // +--------------------------------------------------------+
    // 16                                                       31
    
    for(uint32_t i = 0; i<npackets; i++)
    {
        uint8_t packet[32] = {0};
        
        // first byte is id (sender or receiver) 255 for sender
        packet[0] = 255;

        // sequence number
        packet[1] = (i >> 8) & 0xFF;
        packet[2] = i & 0xFF;
        
        // next 8 bytes: current address in memory read from
        uint32_t addr = (uint32_t)currentAddr;
        memcpy(&packet[3], &addr, sizeof(addr));

        // data
        memcpy(&packet[16],currentAddr, 16);

        // checksum
        uint16_t sum = 0;
        for(uint32_t j = 16; j<31; j++)
        {
            sum+= packet[j];
        }
        packet[7] = (uint8_t)(sum >> 8) & 0xFF;
        packet[8] = (uint8_t)(sum & 0xFF);


        PacketBuffer b(packet,32);
        uBit.radio.datagram.send(b);

        currentAddr += 16;
        uBit.display.print((int)i);
        uBit.sleep(500);
    }
}

uint32_t MicroBitRadioFlashSender::getSeqNum()
{
    return seq_num;
}