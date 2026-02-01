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
#include "MicroBitRadioFlashConfig.h"
#include "MicroBit.h"
#include <stdlib.h>
#include <set>


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
    for(uint32_t currentPage = 1; currentPage <=totalPages; currentPage++)
    {
        // clear NAKs at each new page
        receivedNAKs.clear();

        // if last page, send remainder of packets instead of # packets per page
        if(currentPage==totalPages)
        {
            uint32_t remBytes = user_size - ((currentPage - 1) * R_FLASH_PAGE_SIZE);
            uint32_t remPackets = (remBytes + R_PAYLOAD_SIZE - 1) / R_PAYLOAD_SIZE;
            sendPage(remPackets, currentPage, uBit);
        }
        else
            sendPage(packetsPerPage, currentPage, uBit);

        sendEndOfPagePacket(uBit);
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
                    handleNAK(p, currentPage, uBit);
                }
            }
            else if(receivedNAKs.empty() && timer>200+packetsPerPage)
            {
                break;
            }
            else if(timer>200+packetsPerPage)
            {
                timer = 0;
                for(auto seq : receivedNAKs)
                    sendSinglePacket(seq, currentPage, uBit);
                receivedNAKs.clear();
                sendEndOfPagePacket(uBit);
            }
            timer++;
            uBit.sleep(10);
        }
    }
}

void MicroBitRadioFlashSender::sendEndOfPagePacket(MicroBit &uBit)
{
    uint8_t packet[R_HEADER_SIZE] = {0};
    packet[0] = 120;

    uint16_t hsum = 0;
    for(uint32_t i = 0; i<7; i++)
    {
        hsum+= packet[i];
    }
    packet[7] = (uint8_t)((hsum >> 8) & 0xFF);
    packet[8] = (uint8_t)(hsum & 0xFF);

    PacketBuffer b(packet,R_HEADER_SIZE);
    uBit.radio.datagram.send(b);
}

void MicroBitRadioFlashSender::sendSinglePacket(uint16_t seq, uint32_t currentPage, MicroBit &uBit)
{
    // Packet Structure:
    // 0            1   2   3    4   5       6       7        8        9       10      11  .... 15
    // +-----------------------------------------------------------------------------------------+
    // | Sndr/Recvr | Seq # | Page # | Total packets | Header Checksum | Data Checksum | Padding |
    // +-----------------------------------------------------------------------------------------+
    // |                                              Data                                       |
    // +-----------------------------------------------------------------------------------------+
    // 16                                                                                        31

    // packet address, (pages and sequence numbers start from 1, not 0 :) )
    uint32_t absolutePacket = ((currentPage - 1) * packetsPerPage) + (seq - 1);
    uint8_t *packetAddress = &__user_start__ + (absolutePacket * R_PAYLOAD_SIZE);

    uint8_t packet[R_HEADER_SIZE + R_PAYLOAD_SIZE] = {0};
    
    // first byte is id (sender or receiver) 255 for sender
    packet[0] = 255;

    // seq #
    packet[1] = (uint8_t)((seq >> 8) & 0xFF);
    packet[2] = (uint8_t)(seq & 0xFF);

    // page #
    packet[3] = (uint8_t)((currentPage >> 8) & 0xFF);
    packet[4] = (uint8_t)(currentPage & 0xFF);
    
    // total packets
    packet[5] = (uint8_t)((totalPackets >> 8) & 0xFF);
    packet[6] = (uint8_t)(totalPackets & 0xFF);
    
    // header checksum
    uint16_t hsum = 0;
    for(uint32_t i = 0; i<7; i++)
    {
        hsum+= packet[i];
    }
    packet[7] = (uint8_t)((hsum >> 8) & 0xFF);
    packet[8] = (uint8_t)(hsum & 0xFF);

    // check size of data to be read, if less than size of packet payload read only that size, else read the payload number of bytes
    if((user_end-(uint32_t)packetAddress)<R_PAYLOAD_SIZE)
        memcpy(&packet[R_HEADER_SIZE],packetAddress,(user_end-(uint32_t)packetAddress));
    else
        memcpy(&packet[R_HEADER_SIZE],packetAddress,R_PAYLOAD_SIZE);
    
    // data checksum
    uint16_t sum = 0;
    for(uint32_t j = R_HEADER_SIZE; j<R_PAYLOAD_SIZE+R_HEADER_SIZE; j++)
    {
        sum+= packet[j];
    }
    packet[9] = (uint8_t)((sum >> 8) & 0xFF);
    packet[10] = (uint8_t)((sum & 0xFF));

    // 25% chance of packet data being corrupt for testing :)
    // if(uBit.random(4)==0)
    // {
    //     uint8_t junk[R_PAYLOAD_SIZE] = {0};
    //     memcpy(&packet[16],&junk,R_PAYLOAD_SIZE);
    // }

    PacketBuffer b(packet,R_PAYLOAD_SIZE+R_HEADER_SIZE);

    // ManagedString out = ManagedString("id: ") + ManagedString((int)packet[0]) + ManagedString("\n")
    // + ManagedString("seq: ") + ManagedString((int)((uint16_t)packet[1]<<8) | ((uint16_t)packet[2])) + ManagedString("\n")
    // + ManagedString("page#: ") + ManagedString((int)((uint16_t)packet[3]<<8) | ((uint16_t)packet[4])) + ManagedString("\n")
    // + ManagedString("tpackets: ") + ManagedString((int)((uint16_t)packet[5]<<8) | ((uint16_t)packet[6])) + ManagedString("\n")
    // + ManagedString("header checksum: ") + ManagedString((int)((uint16_t)packet[7]<<8) | ((uint16_t)packet[8])) + ManagedString("\n")
    // + ManagedString("data checksum: ") + ManagedString((int)((uint16_t)packet[9]<<8) | ((uint16_t)packet[10])) + ManagedString("\n") + ManagedString("\n");
    // uBit.serial.send(out);
    uBit.radio.datagram.send(b);
    
    uBit.sleep(10);  
}

void MicroBitRadioFlashSender::sendPage(uint16_t npackets, uint32_t currentPage, MicroBit &uBit)
{
    for(uint16_t i = 1; i<=npackets; i++)
    {
        sendSinglePacket(i, currentPage, uBit);
    }
}

void MicroBitRadioFlashSender::handleNAK(PacketBuffer p, uint32_t currentPage, MicroBit &uBit)
{
    // NAK Packet Structure
    // 0    1              2               3    4   5  .....  7    8     9 .....  15  
    // +---------------------------------------------------------------------------+
    // | ID | Seq of packet for retransmit | Page # | Padding | Checksum | Padding |
    // +---------------------------------------------------------------------------+
    uint8_t id = p[0];
    uint16_t seq = ((uint16_t)p[1]<<8) | ((uint16_t)p[2]);
    uint16_t pageN = ((uint16_t)p[3]<<8) | ((uint16_t)p[4]);

    // only accept NAKs for the current page
    if(pageN!=currentPage)
        return;

    // uBit.serial.send(ManagedString("FOO\n"));

    receivedNAKs.insert(seq);
}