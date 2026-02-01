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
#include "MicroBitRadioFlashConfig.h"
#include "nrf_nvmc.h"
#include "nrf.h"
#include "nrf_sdm.h"
#include <stdio.h>
#include <map>


void MicroBitRadioFlashReceiver::flashUserPage(uint32_t flash_addr, uint8_t *pageBuffer)
{
    uint32_t page = flash_addr / R_FLASH_PAGE_SIZE;
    uint32_t *flash_ptr = (uint32_t *)flash_addr;
    uint32_t *data = (uint32_t *)pageBuffer;

    while (sd_flash_write(flash_ptr, data, 1024) == NRF_ERROR_BUSY)
        __WFE();

}

void MicroBitRadioFlashReceiver::eraseAllUserPages()
{
    for(uint32_t addr = user_start; addr<user_end; addr+=R_FLASH_PAGE_SIZE)
    {
        uint32_t page = addr/R_FLASH_PAGE_SIZE;
        while(sd_flash_page_erase(page) == NRF_ERROR_BUSY)
            __WFE();
    }
}

bool MicroBitRadioFlashReceiver::checkAllWritten()
{
    for(auto &kv : packetMap)
    {
        if(!kv.second)
            return false;
    }
    return true;
}

void MicroBitRadioFlashReceiver::printInfo(MicroBit &uBit)
{
    ManagedString out = ManagedString("Missing packets: ");

    for(auto &kv : packetMap)
    {
        if(!kv.second)
            out = out + ManagedString((int)kv.first) + ManagedString(", ");
    }

    out = out + ManagedString("\n") + ManagedString("\n");

    out =  out + ManagedString("Received NAKs: ");

    for(auto &kv : receivedNAKs)
    {
        if(kv.second)
            out = out + ManagedString((int)kv.first) + ManagedString(", ");
    }

    out = out + ManagedString("\n") + ManagedString("\n");
    uBit.serial.send(out);
}

bool MicroBitRadioFlashReceiver::isCheckSumOK(PacketBuffer p)
{
    uint16_t recSum = ((uint16_t)p[9]<<8) | ((uint16_t)p[10]);
    uint16_t sum = 0;
    for(uint32_t j = 16; j<16+R_PAYLOAD_SIZE; j++)
    {
        sum+= p[j];
    }
    bool res = sum==recSum ? true : false;
    return res;
}

bool MicroBitRadioFlashReceiver::isHeaderCheckSumOK(PacketBuffer p)
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


MicroBitRadioFlashReceiver::MicroBitRadioFlashReceiver(MicroBit &uBit)
    : uBit(uBit),
    transferComplete(false),
    totalPackets(0),
    currentPage(1),
    lastSeqN(0),
    start_time(0)
{   
    memset(pageBuffer, 0, sizeof(pageBuffer));
    uBit.radio.enable();
    uBit.radio.setGroup(0);
    uBit.radio.setTransmitPower(6);

    id = uBit.random(255);
}

void MicroBitRadioFlashReceiver::Rmain(MicroBit &uBit)
{
    uint32_t timer = 0;
    while(!transferComplete)
    {
        PacketBuffer p = uBit.radio.datagram.recv();
        if(p.length() >=16)
        {
            if((p[0] == 255) && isHeaderCheckSumOK(p))
                handleSenderPacket(p, uBit);
            else if((p[0] == 0) && isHeaderCheckSumOK(p))
                handleReceiverPacket(p, uBit);
            else if((p[0] == 120) && isHeaderCheckSumOK(p))
            {
                if(!checkAllWritten())
                {
                    uBit.sleep(uBit.random(20));
                    sendNAKs(uBit);
                }
                receivedNAKs.clear();
            }
        }
        // else if(timer>100+totalPackets)
        // {
        //     timer = 0;
        //     uBit.sleep(uBit.random(20));
        //     sendNAKs(uBit);
        //     receivedNAKs.clear();
        // }
        // else if(lastSeqN!=0)
        // {
        //     timer++;
        // }

        uBit.sleep(10);
    }
}




void MicroBitRadioFlashReceiver::handleSenderPacket(PacketBuffer packet, MicroBit &uBit)
{
    // Packet Structure:
    // 0            1   2   3    4   5       6       7        8        9       10      11  .... 15
    // +-----------------------------------------------------------------------------------------+
    // | Sndr/Recvr | Seq # | Page # | Total packets | Header Checksum | Data Checksum | Padding |
    // +-----------------------------------------------------------------------------------------+
    // |                                              Data                                       |
    // +-----------------------------------------------------------------------------------------+
    // 16                                                                                        31

    if(isCheckSumOK(packet))
    {
        uint8_t id = packet[0];
        uint16_t seq = ((uint16_t)packet[1]<<8) | ((uint16_t)packet[2]);
        uint16_t page = ((uint16_t)packet[3]<<8) | ((uint16_t)packet[4]);
        if(page!=currentPage)
            return;

        if(lastSeqN==0)
        {
            if(currentPage == 1)
            {
                start_time = uBit.systemTime();
                totalPackets = ((uint16_t)packet[5]<<8) | ((uint16_t)packet[6]);
                totalPages = (totalPackets + packetsPerPage - 1) / packetsPerPage;
            }

            uint16_t packetsThisPage = packetsPerPage;
            if(currentPage==totalPages)
                packetsThisPage = (totalPackets - packetsPerPage * (currentPage - 1));
            
            packetMap.clear();
            // populate packet map & NAK map
            for (uint16_t i=1; i<=packetsThisPage; i++)
            {
                packetMap.emplace(i,false);
                receivedNAKs.emplace(i,false);
            }
        }

        // check if packet has already been written in case of retransmit
        if(packetMap[seq]==true)
            return;
        
        // record last sequence number
        lastSeqN = seq;
        // copy packet into buffer
        memcpy(&pageBuffer[(seq-1)*R_PAYLOAD_SIZE], &packet[16],R_PAYLOAD_SIZE);
        packetMap[seq] = true;

        // ManagedString out = ManagedString("RECEIVEDid: ") + ManagedString((int) id) + ManagedString("\n")
        // + ManagedString("seq: ") + ManagedString((int)seq) + ManagedString("\n")
        // + ManagedString("tpackets: ") + ManagedString((int)totalPackets) + ManagedString("\n")
        // + ManagedString("\n");
        // uBit.serial.send(out);

        // if buffer fully written correctly, proceed to flash
        if(checkAllWritten())
        {
            if(currentPage==1) eraseAllUserPages();
            flashUserPage((user_start + ((currentPage-1) * R_FLASH_PAGE_SIZE)),pageBuffer);
            

            // reset buffer and flags
            packetMap.clear();
            receivedNAKs.clear();
            lastSeqN = 0;
            memset(pageBuffer, 0, sizeof(pageBuffer));
            currentPage++;
            if(currentPage>totalPages)
            {
                uint32_t end_time = uBit.systemTime();
                uint32_t total_time = end_time - start_time;
                uint32_t throughput = (totalPackets*R_PAYLOAD_SIZE) / total_time;
                uint32_t throughputR = (totalPackets*R_PAYLOAD_SIZE) % total_time;
                ManagedString out = ManagedString("time: ") + ManagedString((int) total_time) + ManagedString("\n")
                + ManagedString("throughput: ") + ManagedString((int) throughput) + ManagedString("\n")
                + ManagedString("remainder: ") + ManagedString((int) throughputR) + ManagedString("\n");
                uBit.serial.send(out);
                uBit.sleep(500);
                transferComplete = true;
                uBit.radio.disable();
                __DSB();
                __ISB();
                NVIC_SystemReset();
            }
        }
    }
}

void MicroBitRadioFlashReceiver::handleReceiverPacket(PacketBuffer packet, MicroBit &uBit)
{
    uint8_t id = packet[0];
    uint16_t seq = ((uint16_t)packet[1]<<8) | ((uint16_t)packet[2]);

    // ManagedString out = ManagedString("RECEIVEDid: ") + ManagedString((int) id) + ManagedString("\n")
    // + ManagedString("seq: ") + ManagedString((int)seq) + ManagedString("\n") + ManagedString("\n");
    // uBit.serial.send(out);


    receivedNAKs[seq] = true;
}

void MicroBitRadioFlashReceiver::sendNAKs(MicroBit &uBit)
{
    // NAK Packet Structure
    // 0    1              2               3    4   5  .....  7    8     9 ....  15  
    // +---------------------------------------------------------------------------+
    // | ID | Seq of packet for retransmit | Page # | Padding | Checksum | Padding |
    // +---------------------------------------------------------------------------+

    printInfo(uBit);
    for(uint16_t i=1; i<=packetMap.size(); i++)
    {
        if(!packetMap.at(i) && !receivedNAKs.at(i))
        {
            uint8_t packet[R_HEADER_SIZE] = {0};
            // sequence number (ID for receiver is 0)
            packet[1] = (uint8_t)((i >> 8) & 0xFF);
            packet[2] = (uint8_t)(i & 0xFF);

            // current page
            packet[3] = (uint8_t)((currentPage >> 8) & 0xFF);
            packet[4] = (uint8_t)(currentPage & 0xFF);
            
            // header checksum
            uint16_t hsum = 0;
            for(uint32_t j = 0; j<7; j++)
            {
                hsum+= packet[j];
            }
            packet[7] = (uint8_t)((hsum >> 8) & 0xFF);
            packet[8] = (uint8_t)((hsum & 0xFF));
        
            // ManagedString out = ManagedString("SENTid: ") + ManagedString((int)packet[0]) + ManagedString("\n")
            // + ManagedString("seq: ") + ManagedString((int)((uint16_t)packet[1]<<8) | ((uint16_t)packet[2])) + ManagedString("\n")
            // + ManagedString("Header checksum: ") + ManagedString((int)((uint16_t)packet[7]<<8) | ((uint16_t)packet[8])) + ManagedString("\n") + ManagedString("\n");
            // uBit.serial.send(out);

            PacketBuffer b(packet,16);
            uBit.radio.datagram.send(b);
        }
    }
}