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
#include <vector>

extern "C"
{
    extern uint8_t __user_start__;
    extern uint8_t __user_end__;
}

volatile bool packetsComplete = false;
volatile bool flashComplete = false;
volatile bool packetEvent = false;
volatile uint8_t totalPackets;
volatile uint16_t payloadSize;
volatile uint32_t sleepCount;
volatile vector<uint32_t> missingPacketSeqs;
volatile vector<uint32_t> receivedNAKs;
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

bool isInVector(uint32_t el, vector<uint32_t> v)
{
    for(uint32_t i=0; i<v.size(); i++)
    {
        if(v[i]==el)
            return true;
    }
    return false;
}

uint32_t searchVector(uint32_t el, vector<uint32_t> v)
{
    for(uint32_t i=0; i<v.size(); i++)
    {
        if(v[i]==el)
            return i;
    }
}



MicroBitRadioFlashReceiver::MicroBitRadioFlashReceiver(MicroBit &uBit)
    : uBit(uBit),
    totalPackets(0),
    lastSeqN(0),
    packetsWritten(0)

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

            // compute header checksum
            uint16_t recHChecksum = ((uint16_t)p[11]<<8) | ((uint16_t)p[12]);
            uint16_t hsum = 0;
            for(uint32_t j = 0; j<9; j++)
            {
                hsum+= p[j];
            }

            // branch if received packet comes from sender or receiver and header is correct
            if((p[0] == 255) && (hsum==recChecksum))
                handleSenderPacket(p);
            else if((p[0] == 0) && (hsum==recChecksum))
                handleReceiverPacket(p);

            packetEvent = false; 
        }
        else if(packetsWritten>0)
        {
            // if no packet event and at least one packet has been received
            sleepCount++;
        }

        uBit.sleep(200);

        // if loop has run more times than double the number of packets + 10 and there are missing packets, assume end of transmission
        // timer based approach ensures NAKs are still sent even if the last packet is dropped
        if(!missingPacketSeqs.empty() && sleepCount>(10 + totalPackets*2))
        {
            sleepCount = 0;
            // check to see if the last or last few packets were dropped
            if(lastSeqN!=totalPackets && !isInVector(totalPackets,missingPacketSeqs))
            {
                for(uint32_t i=lastSeqN+1; i<=totalPackets; i++)
                {
                    missingPacketSeqs.push_back(i);
                }
            }

            sendNAKs();
        }
    }
}


void MicroBitRadioFlashReceiver::onData(MicroBitEvent)
{
    packetEvent = true;
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

    uint8_t id = packet[0];
    uint16_t seq = ((uint16_t)packet[1]<<8) | ((uint16_t)packet[2]);
    uint8_t pageNum = packet[3];
    // set total packets and payload size fields if this is the first packet received
    if(lastSeqN==0)
    {
        totalPackets = packet[4];
        payloadSize = ((uint16_t)packet[7]<<8) | ((uint16_t)packet[8]);
    }
    uint16_t remaining = ((uint16_t)packet[5]<<8) | ((uint16_t)packet[6]);

    // check for missing packets and add sequence numbers to list if missing
    if((seq!=lastSeqN+1) && !isInVector(seq, missingPacketSeqs))
    {
        for(uint32_t i=lastSeqN+1; i<seq; i++)
        {
            missingPacketSeqs.push_back(i);
        }
    }
    

    // checksum current packet
    uint16_t recChecksum = ((uint16_t)packet[9]<<8) | ((uint16_t)packet[10]);
    uint16_t sum = 0;
    for(uint32_t j = 16; j<payloadSize + 16; j++)
    {
        sum+= packet[j];
    }

    // if current packet data is correct write packet
    if(sum==recChecksum)
    {
        // if missing and retransmission, remove from missing list
        if(isInVector(seq, missingPacketSeqs))
        {
            for(uint32_t i=0; i<missingPacketSeqs.size(); i++)
            {
                if(missingPacketSeqs[i]==seq)
                    missingPacketSeqs.erase(i);
            }
        }
        // record sequence number to check for missing packets later
        lastSeqN = seq;
        // copy packet into buffer
        memcpy(&pageBuffer[(seq-1)*payloadSize], &packet[16],payloadSize);
        packetsWritten++;
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
    

    // if buffer fully written correctly, proceed to flash
    if(packetsWritten==totalPackets)
    {
        packetsComplete = true;
        uBit.radio.disable();


        flashUserProgram(0x76000,pageBuffer);
        __DSB();
        __ISB();
        NVIC_SystemReset();
    }
}

void MicroBitRadioFlashReceiver::handleReceiverPacket(PacketBuffer packet)
{
    

}

void MicroBitRadioFlashReceiver::sendNAKs()
{
    // check for NAKs received, send NAK for first packet in list if NAK has not been received from other receiver
    // remove from both lists

    return;
}