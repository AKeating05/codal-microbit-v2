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

/**
 * Write one buffered page to the given address in flash
 * @param flash_addr the address in flash to write to
 * @param pageBuffer the page buffer to write
 */
void MicroBitRadioFlashReceiver::flashUserPage(uint32_t flash_addr, uint8_t *pageBuffer)
{
    uint32_t *flash_ptr = (uint32_t *)flash_addr;
    uint32_t *data = (uint32_t *)pageBuffer;

    while (sd_flash_write(flash_ptr, data, 1024) == NRF_ERROR_BUSY)
        __WFE();

}

/**
 * Erases all pages in the FLASH_USER memory region
 */
void MicroBitRadioFlashReceiver::eraseAllUserPages()
{
    for(uint32_t addr = USER_BASE_ADDRESS; addr<USER_END_ADDRESS; addr+=R_FLASH_PAGE_SIZE)
    {
        uint32_t page = addr/R_FLASH_PAGE_SIZE;
        while(sd_flash_page_erase(page) == NRF_ERROR_BUSY)
            __WFE();
    }
}

/**
 * Check if all sequence numbers in packetMap are true (all packets written to page buffer)
 * @return true if all packets written, false otherwise
 */
bool MicroBitRadioFlashReceiver::isBufferWritten()
{
    for(auto &kv : packetMap)
    {
        if(!kv.second)
            return false;
    }
    return true;
}

/**
 * Diagnostic for debugging which prints packetMap and receivedNAKs over serial,
 * caution, serial is relatively slow, so depending on the value of R_SLEEP_TIME
 * may not print accurately
 * 
 * @param uBit reference to the microbit device
 */
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

/**
 * Calculates checksum for the packet and compares with the received checksum inside the packet
 * 
 * @param p The packet being checked
 * @return true if both checksums are the same, false otherwise
 */
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

/**
 * Calculates checksum for the packet's HEADER and compares with the received checksum inside the packet
 * 
 * @param p The packet being checked
 * @return true if both checksums are the same, false otherwise
 */
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

/**
 * Updates loading animation on display
 * @param uBit reference to the microbit device
 */
void MicroBitRadioFlashReceiver::updateLoadingScreen(MicroBit &uBit)
{
    if(!fraction)
        fraction = 1;
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

/**
 * Constructor
 * 
 * Creates an instance of MicroBitRadioFlashReceiver which receives a new user program from a
 * MicroBitRadioFlashSender and flashes it into memory
 * 
 * @param uBit A reference to the micro:bit object
 */
MicroBitRadioFlashReceiver::MicroBitRadioFlashReceiver(MicroBit &uBit)
    : uBit(uBit)
{   
    //on class instantiation, clear page buffer and erase user flash region
    memset(pageBuffer, 0, sizeof(pageBuffer));
    eraseAllUserPages();

    //protocol
    this->totalPackets = 0;
    this->totalPages = 0;
    this->packetsPerPage = R_FLASH_PAGE_SIZE / R_PAYLOAD_SIZE;
    this->packetsThisPage = packetsPerPage;

    this->pageState = RECEIVING;
    this->transferComplete = false;
    this->currentPage = 1;
    this->lastSeqN = 0;
    this->lastRxTime = 0;
    this->readyToNAK = false;

    //stats collection variables
    this->recID = 0;
    this->time = 0;
    this->nakRounds = 0;
    this->start_time = 0;

    //loading screen animation
    this->xPixel = 0;
    this->yPixel = 0;
    this->fraction = 1;
    this->packetsWritten = 0;



    uBit.radio.setGroup(42);
    uBit.radio.setTransmitPower(4);
    uBit.radio.enable();
}

/**
 * Main receiver loop, called to begin cycle of listening for packets and requesting retransmissions
 * 
 * @param uBit A reference to the micro:bit object
 */
void MicroBitRadioFlashReceiver::Rmain(MicroBit &uBit)
{   
    srand(uBit.systemTime()); //seed random generator with system time

    while(!transferComplete) //main listening loop
    {
        PacketBuffer p = uBit.radio.datagram.recv();
        if(p.length() >=R_HEADER_SIZE) //on packet receipt
        {
            if((p[0] == 120) && isHeaderCheckSumOK(p)) //handle sender packet
            {
                    handleSenderPacket(p, uBit);
                    updateLoadingScreen(uBit); // update loading animation
            }
            else if((p[0] == 121) && isHeaderCheckSumOK(p)) // handle receiver packet
                handleReceiverPacket(p, uBit);
            else if((p[0] == 122) && isHeaderCheckSumOK(p)) //handle end of page packet
            {
                // uBit.serial.send("--Recover EOP--\n\n");

                //Enter RECOVERY state, backoff for a random period and set NAK flag true
                pageState = RECOVERY; 
                uBit.sleep(rand() % (2*R_NAK_WINDOW));
                readyToNAK = true;
            }   
        }
        else if(pageState==RECOVERY && !isBufferWritten() && readyToNAK) //if in RECOVERY AND packets are missing AND NAK flag is true
        {
            //send NAKs and set flag false
            sendNAKs(uBit);
            readyToNAK = false;

            // clear received NAKs and reinitialise
            receivedNAKs.clear();
            for (uint16_t i=1; i<=packetsThisPage; i++)
                receivedNAKs[i] = false;
        }
        else if(uBit.systemTime() - lastRxTime > 200*R_NAK_WINDOW && lastSeqN!=0) //if nothing from sender for a long time and page has started reset
            return;
        else if(uBit.systemTime() - lastRxTime > 4*R_NAK_WINDOW && lastSeqN!=0) //if nothing from sender for 4 NAK windows and page has started enter RECOVERY state
        {
            // uBit.serial.send("--Recover timeout--\n\n");
            //Enter RECOVERY state, backoff for a random period of time and set NAK flag true
            pageState = RECOVERY;
            uBit.sleep(rand() % (2*R_NAK_WINDOW));
            readyToNAK = true;
        }
        

        uBit.sleep(R_SLEEP_TIME); //check packet receipt every R_SLEEP_TIME
        time++;
    }
}



/**
 * Write one data packet into the page buffer, writes page buffer into flash if page is complete
 * 
 * @param packet The received data packet
 * @param uBit reference to the microbit device
 */
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
        //extract packet info
        // uint8_t id = packet[0];
        uint16_t seq = ((uint16_t)packet[1]<<8) | ((uint16_t)packet[2]);
        uint16_t page = ((uint16_t)packet[3]<<8) | ((uint16_t)packet[4]);

        if(page==currentPage) //check packet is for the correct page
        {
            if(currentPage == 1) //if first page
            {
                recID = time; //set ID to time idle before transmission (used later for stats)
                start_time = uBit.systemTime();

                //calculate total pages from packet field
                totalPackets = ((uint16_t)packet[5]<<8) | ((uint16_t)packet[6]);
                totalPages = (totalPackets + packetsPerPage - 1) / packetsPerPage;
                fraction = totalPackets / 25; //fraction for screen loading animation
            }
            else if(currentPage==totalPages) //on last page adjust number of packets to be written
                packetsThisPage = (totalPackets - packetsPerPage * (currentPage - 1));

            if(lastSeqN==0) //if first packet received
            {
                pageState = RECEIVING; //set state
                // populate packet map & NAK map
                for (uint16_t i=1; i<=packetsThisPage; i++)
                {
                    packetMap[i] = false;
                    receivedNAKs[i] = false;
                }
            }
        }
        else
            return;

        // check if packet has already been written in case of retransmit
        if(packetMap[seq])
            return;
        
        // record last sequence number
        lastSeqN = seq;
        lastRxTime = uBit.systemTime();

        // copy packet into buffer
        memcpy(&pageBuffer[(seq-1)*R_PAYLOAD_SIZE], &packet[R_HEADER_SIZE],R_PAYLOAD_SIZE);
        packetMap[seq] = true;
        packetsWritten++;

        

        // ManagedString out = ManagedString("RECEIVEDid: ") + ManagedString((int) id) + ManagedString("\n")
        // + ManagedString("seq: ") + ManagedString((int)seq) + ManagedString("\n")
        // + ManagedString("tpackets: ") + ManagedString((int)totalPackets) + ManagedString("\n")
        // + ManagedString("\n");
        // uBit.serial.send(out);

        // if buffer fully written correctly, proceed to flash
        if(isBufferWritten())
        {
            //flash buffered page to 0x71000 + (page #) * 4096
            flashUserPage((USER_BASE_ADDRESS + ((currentPage-1) * R_FLASH_PAGE_SIZE)),pageBuffer);
            
            // reset buffer and flags
            packetMap.clear();
            receivedNAKs.clear();
            lastSeqN = 0;
            lastRxTime = 0;
            pageState = RECEIVING;
            memset(pageBuffer, 0, sizeof(pageBuffer));
            currentPage++;

            if(currentPage>totalPages) //if all pages complete
            {
                transferComplete = true;
                //compute statistics for evaluation of the system
                uint32_t end_time = uBit.systemTime();
                uint32_t total_time = end_time - start_time;
                uint32_t throughput = ((totalPackets*R_PAYLOAD_SIZE*8000) / total_time);
                
                uint8_t packet[16] = {0};

                //id
                packet[0] = (uint8_t)((recID >> 24) & 0xFF);
                packet[1] = (uint8_t)((recID >> 16) & 0xFF);
                packet[2] = (uint8_t)((recID >> 8) & 0xFF);
                packet[3] = (uint8_t)(recID & 0xFF);

                //nak rounds
                packet[4] = (uint8_t)((nakRounds >> 24) & 0xFF);
                packet[5] = (uint8_t)((nakRounds >> 16) & 0xFF);
                packet[6] = (uint8_t)((nakRounds >> 8) & 0xFF);
                packet[7] = (uint8_t)(nakRounds & 0xFF);

                //throughput
                packet[8] = (uint8_t)((throughput >> 24) & 0xFF);
                packet[9] = (uint8_t)((throughput >> 16) & 0xFF);
                packet[10] = (uint8_t)((throughput >> 8) & 0xFF);
                packet[11] = (uint8_t)(throughput & 0xFF);

                //time
                packet[12] = (uint8_t)((total_time >> 24) & 0xFF);
                packet[13] = (uint8_t)((total_time >> 16) & 0xFF);
                packet[14] = (uint8_t)((total_time >> 8) & 0xFF);
                packet[15] = (uint8_t)(total_time & 0xFF);


                //sleep for the amount of time between reset and start of transmission,
                //this ensures that no two receivers have the same ID as long as they are not reset at the same time
                uBit.sleep((recID));
                PacketBuffer b(packet,16);

                for(uint8_t i=0;i<3;i++)
                {
                    uBit.radio.datagram.send(b);
                    uBit.sleep(rand() % 5);
                }

                //disable radio and perform system reset
                uBit.sleep(2000);
                uBit.radio.disable();
                __DSB();
                __ISB();
                NVIC_SystemReset();
            }
        }
    }
}

/**
 * Extract received NAK sequence number, set receivedNAKs to true for that seq number
 * If still in RECEIVING state, enter RECOVERY
 * 
 * @param packet The received NAK packet
 * @param uBit reference to the microbit device
 */
void MicroBitRadioFlashReceiver::handleReceiverPacket(PacketBuffer packet, MicroBit &uBit)
{
    // uint8_t id = packet[0];
    uint16_t seq = ((uint16_t)packet[1]<<8) | ((uint16_t)packet[2]);
    uint16_t page = ((uint16_t)packet[3]<<8) | ((uint16_t)packet[4]);


    // infer that transmission of the current page has ended if a NAK has been received, but still in RECEIVING state
    if(page == currentPage && pageState==RECEIVING)
    {
        // uBit.serial.send("--Recover heard NAK--\n\n");
        pageState = RECOVERY;
        uBit.sleep(rand() % (3*R_NAK_WINDOW));
        readyToNAK = true;
    }
        
    // ManagedString out = ManagedString("RECEIVEDid: ") + ManagedString((int) id) + ManagedString("\n")
    // + ManagedString("seq: ") + ManagedString((int)seq) + ManagedString("\n") + ManagedString("\n");
    // uBit.serial.send(out);

    receivedNAKs[seq] = true; // add NAK to map of NAKs
}

/**
 * Send NAKs for packets which haven't been received and for which no NAK
 * has been detected from another receiver
 * @param uBit reference to the microbit device 
 */
void MicroBitRadioFlashReceiver::sendNAKs(MicroBit &uBit)
{
    nakRounds++;
    // uBit.serial.send(ManagedString("Sending NAKs\n\n"));

    // NAK Packet Structure
    // 0    1              2               3    4   5  .....  7    8     9 ....  15  
    // +---------------------------------------------------------------------------+
    // | ID | Seq of packet for retransmit | Page # | Padding | Checksum | Padding |
    // +---------------------------------------------------------------------------+

    for(uint16_t i=1; i<=packetMap.size(); i++)
    {
        if(!packetMap.at(i) && !receivedNAKs.at(i)) //if the packet has not been received and a NAK has not been heard for it, send a NAK
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
        
            // ManagedString out = ManagedString("NAKid: ") + ManagedString((int)packet[0]) + ManagedString("\n")
            // + ManagedString("seq: ") + ManagedString((int)((uint16_t)packet[1]<<8) | ((uint16_t)packet[2])) + ManagedString("\n")
            // + ManagedString("Header checksum: ") + ManagedString((int)((uint16_t)packet[7]<<8) | ((uint16_t)packet[8])) + ManagedString("\n") + ManagedString("\n");
            // uBit.serial.send(out);

            //send the packet
            PacketBuffer b(packet,R_HEADER_SIZE);
            uBit.radio.datagram.send(b);
            uBit.sleep(R_SLEEP_TIME);
        }
    }
}