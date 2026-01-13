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
#include <map>

extern "C"
{
    extern uint8_t __user_start__;
    extern uint8_t __user_end__;
}

volatile bool packetsComplete = false;
volatile bool flashComplete = false;
volatile bool packetEvent = false;
uint32_t totalPackets;
uint32_t lastSeqN = 0;
uint32_t payloadSize;
uint32_t sleepCount;
std::map<uint32_t, bool> packetMap;
std::map<uint32_t, bool> receivedNAKs;
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

bool checkAllWritten()
{
    for(uint32_t i=1; i<=packetMap.size(); i++)
    {
        if(!packetMap[i])
            return false;
    }
    return true;
}

// void MicroBitRadioFlashReceiver::printInfo()
// {
//     ManagedString out = ManagedString("Missing packets: ");

//     for(uint32_t i=0; i<missingPacketSeqs.size(); i++)
//     {
//         out = out + ManagedString((int)missingPacketSeqs[i]) + ManagedString(", ");
//     }

//     out = out + ManagedString("\n") + ManagedString("\n");

//     out =  out + ManagedString("Received NAKs: ");

//     for(uint32_t i=0; i<receivedNAKs.size(); i++)
//     {
//         out = out + ManagedString((int)receivedNAKs[i]) + ManagedString(", ");
//     }

//     out = out + ManagedString("\n") + ManagedString("\n");

//     uBit.serial.send(out);
// }

bool isCheckSumOK(PacketBuffer p)
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

bool isHeaderCheckSumOK(PacketBuffer p)
{
    uint16_t recSum = ((uint16_t)p[11]<<8) | ((uint16_t)p[12]);
    uint16_t hsum = 0;
    for(uint32_t j = 0; j<9; j++)
    {
        hsum+= p[j];
    }
    bool res = hsum==recSum ? true : false;
    return res;
}


MicroBitRadioFlashReceiver::MicroBitRadioFlashReceiver(MicroBit &uBit)
    : uBit(uBit)
{
    memset(pageBuffer, 0, sizeof(pageBuffer));
    uBit.radio.enable();
    uBit.radio.setGroup(0);
    uBit.radio.setTransmitPower(6);
}

void MicroBitRadioFlashReceiver::Rmain()
{
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




void MicroBitRadioFlashReceiver::handleSenderPacket(PacketBuffer packet)
{
    // Packet Structure:
    // 0            1    2    3             4               5         6         7       8      9    10   11        12       13        15
    // +------------------------------------------------------------------------------------------------------------------------------+
    // | Sndr/Recvr | Seq Num | Page number | Total packets | Remaining # bytes | Payload size | Checksum | Header Checksum | Padding |
    // +------------------------------------------------------------------------------------------------------------------------------+
    // |                                                                 Data                                                         |
    // +------------------------------------------------------------------------------------------------------------------------------+
    // 16                                                                                                                             31

    if(isCheckSumOK(packet))
    {
        uint8_t id = packet[0];
        uint16_t seq = ((uint16_t)packet[1]<<8) | ((uint16_t)packet[2]);
        uint8_t pageNum = packet[3];
        uint16_t remaining = ((uint16_t)packet[5]<<8) | ((uint16_t)packet[6]);
        // set total packets and payload size fields if this is the first packet received
        if(lastSeqN==0)
        {
            totalPackets = packet[4];
            payloadSize = ((uint16_t)packet[7]<<8) | ((uint16_t)packet[8]);

            // populate packet map
            for (uint32_t i=0; i<totalPackets; i++)
            {
                packetMap[i+1] = false;
            }
        }

        // check if packet has already been written in case of retransmit
        if(packetMap[(uint32_t)seq]==true)
            return;
        
        // record sequence number to check for missing packets later
        lastSeqN = seq;
        // copy packet into buffer
        memcpy(&pageBuffer[(seq-1)*payloadSize], &packet[16],payloadSize);
        packetMap[(uint32_t)seq] = true;

        // if buffer fully written correctly, proceed to flash
        if(checkAllWritten())
        {
            packetsComplete = true;
            uBit.radio.disable();


            flashUserProgram(0x76000,pageBuffer);
            __DSB();
            __ISB();
            NVIC_SystemReset();
        }
    
        ManagedString out = ManagedString("RECEIVEDid: ") + ManagedString((int) id) + ManagedString("\n")
        + ManagedString("seq: ") + ManagedString((int)seq) + ManagedString("\n")
        // + ManagedString("page#: ") + ManagedString((int)pageNum) + ManagedString("\n")
        + ManagedString("tpackets: ") + ManagedString((int)totalPackets) + ManagedString("\n")
        // + ManagedString("remaining: ") + ManagedString((int)remaining) + ManagedString("\n")
        // + ManagedString("payloadSize: ") + ManagedString((int)payloadSize) + ManagedString("\n")
        // + ManagedString("Rchecksum: ") + ManagedString((int)recChecksum) + ManagedString("\n")
        // + ManagedString("checksum: ") + ManagedString((int)sum) 
        + ManagedString("\n") + ManagedString("\n");
        uBit.serial.send(out);
    }
}

void MicroBitRadioFlashReceiver::handleReceiverPacket(PacketBuffer packet)
{
    uint8_t id = packet[0];
    uint16_t seq = ((uint16_t)packet[1]<<8) | ((uint16_t)packet[2]);

    ManagedString out = ManagedString("RECEIVEDid: ") + ManagedString((int) id) + ManagedString("\n")
    + ManagedString("seq: ") + ManagedString((int)seq) + ManagedString("\n")
    + ManagedString("Header checksum: ") + ManagedString((int)((uint16_t)packet[11]<<8) | ((uint16_t)packet[12])) + ManagedString("\n") + ManagedString("\n");
    uBit.serial.send(out);


    receivedNAKs[(uint32_t)seq] = true;
}

void MicroBitRadioFlashReceiver::sendNAKs()
{
    // NAK Packet Structure
    // 0    1              2               3  .....  11   12    13 ....  15  
    // +------------------------------------------------------------------+
    // | ID | Seq of packet for retransmit | Padding | Checksum | Padding |
    // +------------------------------------------------------------------+

    for(uint32_t i=1; i<=packetMap.size(); i++)
    {
        if(!packetMap[i] && !receivedNAKs[i])
        {
            uint8_t packet[16] = {0};
            // sequence number (ID for receiver is 0)
            packet[1] = (i >> 8) & 0xFF;
            packet[2] = i & 0xFF;
            
            // header checksum
            uint16_t hsum = 0;
            for(uint32_t j = 0; j<9; j++)
            {
                hsum+= packet[j];
            }
            packet[11] = (uint8_t)(hsum >> 8) & 0xFF;
            packet[12] = (uint8_t)(hsum & 0xFF);
        
            ManagedString out = ManagedString("SENTid: ") + ManagedString((int)packet[0]) + ManagedString("\n")
            + ManagedString("seq: ") + ManagedString((int)((uint16_t)packet[1]<<8) | ((uint16_t)packet[2])) + ManagedString("\n")
            + ManagedString("Header checksum: ") + ManagedString((int)((uint16_t)packet[11]<<8) | ((uint16_t)packet[12])) + ManagedString("\n") + ManagedString("\n");
            uBit.serial.send(out);

            PacketBuffer b(packet,16);
            uBit.radio.datagram.send(b);
        }
        else if(!packetMap[i] && receivedNAKs[i])
        {
            uBit.sleep(50);
        }
    }
}