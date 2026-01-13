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
#include <stdlib.h>

extern "C"
{
    extern uint8_t __user_start__;
    extern uint8_t __user_end__;
}



MicroBitRadioFlashSender::MicroBitRadioFlashSender(MicroBit &uBit)
    : uBit(uBit)
{
    uBit.radio.enable();
    uBit.radio.setGroup(0);
    uBit.radio.setTransmitPower(6);
}

void MicroBitRadioFlashSender::Smain()
{
    // send user program then listen for NAKs
    // then send packet for each unique NAK
    // repeat until no more NAKs
    uint32_t timer = 0;
    while(!packetsComplete)
    {
        // printInfo();
        PacketBuffer p = uBit.radio.datagram.recv();
        if(p.length() >=16)
        {
            // ManagedString out = ManagedString("FOO\n");
            // uBit.serial.send(out);
            // branch if received packet comes from sender or receiver and header is correct
            if((p[0] == 255) && isHeaderCheckSumOK(p))
                handleSenderPacket(p);
            else if((p[0] == 0) && isHeaderCheckSumOK(p))
                handleReceiverPacket(p);
        }
        else if(timer>100)
        {
            timer = 0;
            uBit.sleep(uBit.random(100));
            sendNAKs();
        }
        else if(lastSeqN!=0)
        {
            timer++;
        }

        uBit.sleep(50);
    }
}

void MicroBitRadioFlashSender::sendUserProgram()
{
    uint8_t *currentAddr = &__user_start__;
    uint32_t user_start = (uint32_t)&__user_start__;
    uint32_t user_end = (uint32_t)&__user_end__;
    uint32_t user_size = user_end - user_start;
    
    uint16_t payloadSize = 32 - 16;
    uint8_t npackets = (user_size + payloadSize - 1)/payloadSize;
    

    
    
    // Packet Structure:
    // 0            1    2    3             4               5         6         7       8      9    10   11        12       13        15
    // +------------------------------------------------------------------------------------------------------------------------------+
    // | Sndr/Recvr | Seq Num | Page number | Total packets | Remaining # bytes | Payload size | Checksum | Header Checksum | Padding |
    // +------------------------------------------------------------------------------------------------------------------------------+
    // |                                                                 Data                                                         |
    // +------------------------------------------------------------------------------------------------------------------------------+
    // 16                                                                                                                             31

    uint8_t pageNum = 1;
    for(uint8_t i = 1; i<=npackets; i++)
    {
        uint8_t packet[payloadSize+16] = {0};

        // first byte is id (sender or receiver) 255 for sender
        packet[0] = 255;
        // if(uBit.random(4)>0)
        // {
        
        

        // sequence number
        packet[1] = (i >> 8) & 0xFF;
        packet[2] = i & 0xFF;

        // page number and number of packets being sent
        packet[3] = pageNum;
        packet[4] = npackets;

        // remaining bytes to send
        packet[5] = ((user_end-(uint32_t)currentAddr) >> 8) & 0xFF;
        packet[6] = (user_end-(uint32_t)currentAddr) & 0xFF;

        // payload size
        packet[7] = (uint8_t)(payloadSize >> 8) & 0xFF;
        packet[8] = (uint8_t)(payloadSize & 0xFF);

        // check size of data to be read, if less than size of packet payload read only that size, else read the payload number of bytes
        if((user_end-(uint32_t)currentAddr)<payloadSize)
        {
            memcpy(&packet[16],currentAddr,(user_end-(uint32_t)currentAddr));
            currentAddr+=(user_end-(uint32_t)currentAddr);
        }
        else
        {
            memcpy(&packet[16],currentAddr,payloadSize);
            currentAddr+=payloadSize;
        }

        // data checksum
        uint16_t sum = 0;
        for(uint32_t j = 16; j<payloadSize+16; j++)
        {
            sum+= packet[j];
        }
        packet[9] = (uint8_t)(sum >> 8) & 0xFF;
        packet[10] = (uint8_t)(sum & 0xFF);

        // header checksum
        uint16_t hsum = 0;
        for(uint32_t i = 0; i<9; i++)
        {
            hsum+= packet[i];
        }
        packet[11] = (uint8_t)(hsum >> 8) & 0xFF;
        packet[12] = (uint8_t)(hsum & 0xFF);
        // }

        PacketBuffer b(packet,payloadSize+16);


        ManagedString out = ManagedString("id: ") + ManagedString((int)packet[0]) + ManagedString("\n")
        + ManagedString("seq: ") + ManagedString((int)((uint16_t)packet[1]<<8) | ((uint16_t)packet[2])) + ManagedString("\n")
        + ManagedString("page#: ") + ManagedString((int)packet[3]) + ManagedString("\n")
        + ManagedString("tpackets: ") + ManagedString((int)packet[4]) + ManagedString("\n")
        + ManagedString("remaining: ") + ManagedString((int)((uint16_t)packet[5]<<8) | ((uint16_t)packet[6])) + ManagedString("\n")
        + ManagedString("payloadSize: ") + ManagedString((int)((uint16_t)packet[7]<<8) | ((uint16_t)packet[8])) + ManagedString("\n")
        + ManagedString("checksum: ") + ManagedString((int)((uint16_t)packet[9]<<8) | ((uint16_t)packet[10])) + ManagedString("\n")
        + ManagedString("Header checksum: ") + ManagedString((int)((uint16_t)packet[11]<<8) | ((uint16_t)packet[12])) + ManagedString("\n") + ManagedString("\n");
        uBit.serial.send(out);
        
        uBit.radio.datagram.send(b);
        
        // increment page number every 4 packets
        pageNum += !((i + 1) % 4) ? 1:0; 
        
        uBit.sleep(200);
    }
}