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
#include <set>

extern "C"
{
    extern uint8_t __user_start__;
    extern uint8_t __user_end__;
}

std::set<uint16_t> receivedNAKs;

uint32_t user_start = (uint32_t)&__user_start__;
uint32_t user_end = (uint32_t)&__user_end__;
uint32_t user_size = user_end - user_start;
uint16_t payloadSize = 32 - 16;
uint16_t npackets = (user_size + payloadSize - 1)/payloadSize;


bool MicroBitRadioFlashSender::isCheckSumOK(PacketBuffer p)
{
    uint16_t recSum = ((uint16_t)p[9]<<8) | ((uint16_t)p[10]);
    uint16_t sum = 0;
    for(uint32_t j = 16; j<32; j++)
    {
        sum+= p[j];
    }
    bool res = sum==recSum ? true : false;
    return res;
}

bool MicroBitRadioFlashSender::isHeaderCheckSumOK(PacketBuffer p)
{
    uint16_t recSum = ((uint16_t)p[7]<<8) | ((uint16_t)p[8]);
    uint16_t hsum = 0;
    for(uint32_t j = 0; j<7; j++)
    {
        hsum+= p[j];
    }
    bool res = hsum==recSum ? true : false;
    return res;
}

MicroBitRadioFlashSender::MicroBitRadioFlashSender(MicroBit &uBit)
    : uBit(uBit)
{
    uBit.radio.enable();
    uBit.radio.setGroup(0);
    uBit.radio.setTransmitPower(6);
}

// main sender loop
void MicroBitRadioFlashSender::Smain(MicroBit &uBit)
{
    sendUserProgram(uBit);

    // start a timer incrementing every 50ms, 
    // if a correct NAK is received add it to the NAK set
    // else if the timer exceeds 100 (5 seconds) and there are no NAKs assume all received and terminate
    // else if the timer exceeds 100 (5 seconds) (but NAKs have been received) retransmit all NAKed packets, clear NAK set and restart timer
    uint32_t timer = 0;
    while(true)
    {
        PacketBuffer p = uBit.radio.datagram.recv();
        if(p.length() >=16)
        {
            // listen for NAKs
            if((p[0] == 0) && isHeaderCheckSumOK(p))
            {
                timer = 0;
                handleNAK(p, uBit);
            }
        }
        else if(receivedNAKs.empty() && timer>100)
        {
            break;
        }
        else if(timer>100)
        {
            timer = 0;
            for(auto seq : receivedNAKs)
                sendSinglePacket(seq, uBit);
            receivedNAKs.clear();
        }
        timer++;
        uBit.sleep(50);
    }
}

void MicroBitRadioFlashSender::sendSinglePacket(uint16_t seq, MicroBit &uBit)
{
    // Packet Structure:
    // 0            1    2    3     4         5       6      7        8        9       10     11  .....  15
    // +-------------------------------------------------------------------------------------------------+
    // | Sndr/Recvr | Seq Num | Total packets | Payload size | Header Checksum | Data Checksum | Padding |
    // +-------------------------------------------------------------------------------------------------+
    // |                                              Data                                               |
    // +-------------------------------------------------------------------------------------------------+
    // 16                                                                                                31

    // packet address
    uint8_t *packetAddress = &__user_start__ + (payloadSize*(seq-1));

    uint8_t packet[payloadSize+16] = {0};
    
    // first byte is id (sender or receiver) 255 for sender
    packet[0] = 255;

    packet[1] = (uint8_t)((seq >> 8) & 0xFF);
    packet[2] = (uint8_t)(seq & 0xFF);
    
    // total packets
    packet[3] = (uint8_t)((npackets >> 8) & 0xFF);
    packet[4] = (uint8_t)(npackets & 0xFF);
    
    // payload size
    packet[5] = (uint8_t)((payloadSize >> 8) & 0xFF);
    packet[6] = (uint8_t)(payloadSize & 0xFF);
    
    // header checksum
    uint16_t hsum = 0;
    for(uint32_t i = 0; i<7; i++)
    {
        hsum+= packet[i];
    }
    packet[7] = (uint8_t)((hsum >> 8) & 0xFF);
    packet[8] = (uint8_t)(hsum & 0xFF);

    // check size of data to be read, if less than size of packet payload read only that size, else read the payload number of bytes
    if((user_end-(uint32_t)packetAddress)<payloadSize)
        memcpy(&packet[16],packetAddress,(user_end-(uint32_t)packetAddress));
    else
        memcpy(&packet[16],packetAddress,payloadSize);
    
    // data checksum
    uint16_t sum = 0;
    for(uint32_t j = 16; j<payloadSize+16; j++)
    {
        sum+= packet[j];
    }
    packet[9] = (uint8_t)((sum >> 8) & 0xFF);
    packet[10] = (uint8_t)((sum & 0xFF));

    // 25% chance of packet data being corrupt for testing :)
    if(uBit.random(4)==0)
    {
        uint8_t junk[payloadSize] = {0};
        memcpy(&packet[16],&junk,payloadSize);
    }

    PacketBuffer b(packet,payloadSize+16);

    ManagedString out = ManagedString("id: ") + ManagedString((int)packet[0]) + ManagedString("\n")
    + ManagedString("seq: ") + ManagedString((int)((uint16_t)packet[1]<<8) | ((uint16_t)packet[2])) + ManagedString("\n")
    + ManagedString("tpackets: ") + ManagedString((int)((uint16_t)packet[3]<<8) | ((uint16_t)packet[4])) + ManagedString("\n")
    + ManagedString("payloadSize: ") + ManagedString((int)((uint16_t)packet[5]<<8) | ((uint16_t)packet[6])) + ManagedString("\n")
    + ManagedString("header checksum: ") + ManagedString((int)((uint16_t)packet[7]<<8) | ((uint16_t)packet[8])) + ManagedString("\n")
    + ManagedString("data checksum: ") + ManagedString((int)((uint16_t)packet[9]<<8) | ((uint16_t)packet[10])) + ManagedString("\n") + ManagedString("\n");
    uBit.serial.send(out);
    uBit.radio.datagram.send(b);
    
    uBit.sleep(200);  
}

void MicroBitRadioFlashSender::sendUserProgram(MicroBit &uBit)
{
    for(uint16_t i = 1; i<=npackets; i++)
    {
        sendSinglePacket(i, uBit);
    }
}

void MicroBitRadioFlashSender::handleNAK(PacketBuffer p, MicroBit &uBit)
{
    // NAK Packet Structure
    // 0    1              2               3  .....  7    8     9 ....  15  
    // +------------------------------------------------------------------+
    // | ID | Seq of packet for retransmit | Padding | Checksum | Padding |
    // +------------------------------------------------------------------+
    uint8_t id = p[0];
    uint16_t seq = ((uint16_t)p[1]<<8) | ((uint16_t)p[2]);

    uBit.serial.send(ManagedString("FOO\n"));

    receivedNAKs.insert(seq);
}