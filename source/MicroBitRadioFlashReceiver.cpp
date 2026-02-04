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
    uint32_t *flash_ptr = (uint32_t *)flash_addr;
    uint32_t *data = (uint32_t *)pageBuffer;

    while (sd_flash_write(flash_ptr, data, 1024) == NRF_ERROR_BUSY)
        __WFE();

}

void MicroBitRadioFlashReceiver::eraseAllUserPages()
{
    for(uint32_t addr = USER_BASE_ADDRESS; addr<USER_END_ADDRESS; addr+=R_FLASH_PAGE_SIZE)
    {
        uint32_t page = addr/R_FLASH_PAGE_SIZE;
        while(sd_flash_page_erase(page) == NRF_ERROR_BUSY)
            __WFE();
    }
}

bool MicroBitRadioFlashReceiver::isBufferWritten()
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
    for(uint32_t j = R_HEADER_SIZE; j<R_HEADER_SIZE+R_PAYLOAD_SIZE; j++)
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

void MicroBitRadioFlashReceiver::updateLoadingScreen(MicroBit &uBit)
{
    if(!((packetsWritten) % fraction))
    {
        if(xPixel==5)
        {
            xPixel = 0;
            yPixel++;
        }
        uBit.display.image.setPixelValue(xPixel,yPixel,255);
        xPixel++; 
    }
}


MicroBitRadioFlashReceiver::MicroBitRadioFlashReceiver(MicroBit &uBit)
    : uBit(uBit)
{   
    memset(pageBuffer, 0, sizeof(pageBuffer));

    this->totalPackets = 0;
    this->totalPages = 0;
    this->packetsPerPage = R_FLASH_PAGE_SIZE / R_PAYLOAD_SIZE;

    // this->user_start = (uint32_t)&__user_start__;
    // this->user_end = (uint32_t)&__user_end__;
    // this->user_size = user_end - user_start;

    this->pageState = RECEIVING;
    this->transferComplete = false;
    // this->pageInitialised = false;
    this->currentPage = 1;
    this->lastSeqN = 0;
    this->lastRxTime = 0;
    this->lastNAKTime = 0;
    this->start_time = 0;
    

    this->xPixel = 0;
    this->yPixel = 0;
    this->fraction = 1;
    this->packetsWritten = 0;

    
    uBit.radio.setGroup(42);
    uBit.radio.setTransmitPower(6);
    uBit.radio.enable();
}

void MicroBitRadioFlashReceiver::Rmain(MicroBit &uBit)
{   
    while(!transferComplete)
    {
        PacketBuffer p = uBit.radio.datagram.recv();
        if(p.length() >=R_HEADER_SIZE)
        {
            if((p[0] == 120) && isHeaderCheckSumOK(p))
            {
                handleSenderPacket(p, uBit);
                // updateLoadingScreen(uBit); // update loading animation
            }
            else if((p[0] == 121) && isHeaderCheckSumOK(p))
                handleReceiverPacket(p, uBit);
            else if((p[0] == 122) && isHeaderCheckSumOK(p))
                pageState = RECOVERY;
        }

        if(pageState==RECOVERY && //receiver is in recovery mode
            !isBufferWritten() && //packets are missing
            uBit.systemTime()-lastNAKTime > R_NAK_WINDOW) //time since NAKs were last sent is greater than x
        {
            lastNAKTime = uBit.systemTime();
            uBit.sleep(uBit.random(2*R_SLEEP_TIME));
            sendNAKs(uBit);
        }
        if((uBit.systemTime() - lastRxTime > 10000) && lastSeqN!=0 && !transferComplete) //should scale with number of packets
            return;

        uBit.sleep(R_SLEEP_TIME/2);
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

        uint16_t packetsThisPage = packetsPerPage;


        if(page==currentPage)
        {
            if(currentPage == 1)
            {
                start_time = uBit.systemTime();
                totalPackets = ((uint16_t)packet[5]<<8) | ((uint16_t)packet[6]);
                totalPages = (totalPackets + packetsPerPage - 1) / packetsPerPage;
                fraction = max(1, totalPackets / 25);
            }
            else if(currentPage==totalPages)
                packetsThisPage = (totalPackets - packetsPerPage * (currentPage - 1));

            if(lastSeqN==0)
            {
                pageState = RECEIVING;
                // populate packet map & NAK map
                for (uint16_t i=1; i<=packetsThisPage; i++)
                {
                    packetMap[i] = false;
                    receivedNAKs[i] = false;
                }
                // pageInitialised = true;
            }
        }
        else
            return;

        // check if packet has already been written in case of retransmit
        if(packetMap.at(seq))
            return;
        
        // record last sequence number
        lastSeqN = seq;
        lastRxTime = uBit.systemTime();
        // copy packet into buffer
        memcpy(&pageBuffer[(seq-1)*R_PAYLOAD_SIZE], &packet[R_HEADER_SIZE],R_PAYLOAD_SIZE);
        packetMap[seq] = true;
        packetsWritten++;

        

        ManagedString out = ManagedString("RECEIVEDid: ") + ManagedString((int) id) + ManagedString("\n")
        + ManagedString("seq: ") + ManagedString((int)seq) + ManagedString("\n")
        + ManagedString("tpackets: ") + ManagedString((int)totalPackets) + ManagedString("\n")
        + ManagedString("\n");
        uBit.serial.send(out);

        // if buffer fully written correctly, proceed to flash
        if(isBufferWritten())
        {
            if(currentPage==1) eraseAllUserPages();
            flashUserPage((USER_BASE_ADDRESS + ((currentPage-1) * R_FLASH_PAGE_SIZE)),pageBuffer);
            
            // reset buffer and flags
            packetMap.clear();
            receivedNAKs.clear();
            lastSeqN = 0;
            lastRxTime = uBit.systemTime();
            lastNAKTime = 0;
            pageState = RECEIVING;
            // pageInitialised = false;
            memset(pageBuffer, 0, sizeof(pageBuffer));
            currentPage++;
            if(currentPage>totalPages)
            {
                transferComplete = true;
                uint32_t end_time = uBit.systemTime();
                uint32_t total_time = end_time - start_time;
                uint32_t throughput = (totalPackets*R_PAYLOAD_SIZE) / total_time;
                ManagedString out = ManagedString("time: ") + ManagedString((int) total_time) + ManagedString("\n")
                + ManagedString("throughput: ") + ManagedString((int) throughput) + ManagedString("\n");
                uBit.serial.send(out);
                uBit.sleep(500);
                
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
    uint16_t page = ((uint16_t)packet[3]<<8) | ((uint16_t)packet[4]);


    // infer that transmission of the current page has ended from NAK
    if(page == currentPage && pageState == RECEIVING)
        pageState = RECOVERY;
        
    // ManagedString out = ManagedString("RECEIVEDid: ") + ManagedString((int) id) + ManagedString("\n")
    // + ManagedString("seq: ") + ManagedString((int)seq) + ManagedString("\n") + ManagedString("\n");
    // uBit.serial.send(out);

    receivedNAKs[seq] = true; // add NAK to list of NAKs
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
            // sequence number (ID for receiver is 121)
            packet[0] = 121;
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
        
            ManagedString out = ManagedString("NAKid: ") + ManagedString((int)packet[0]) + ManagedString("\n")
            + ManagedString("seq: ") + ManagedString((int)((uint16_t)packet[1]<<8) | ((uint16_t)packet[2])) + ManagedString("\n")
            + ManagedString("Header checksum: ") + ManagedString((int)((uint16_t)packet[7]<<8) | ((uint16_t)packet[8])) + ManagedString("\n") + ManagedString("\n");
            uBit.serial.send(out);

            PacketBuffer b(packet,R_HEADER_SIZE);
            uBit.radio.datagram.send(b);
            uBit.sleep(R_SLEEP_TIME);
        }
    }

    // potentially clear NAKs from other receivers after sending to avoid never sending a NAK for a specific packet 
    // (eg. packet 4 NAKed, sender retransmits, receiver one gets it but receiver 2 doesn't, 2 has seen NAK so will never send one)
}