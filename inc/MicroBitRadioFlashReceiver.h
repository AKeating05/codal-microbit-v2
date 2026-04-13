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
#include "MicroBit.h"
#include "MicroBitRadioFlashConfig.h"
#include <map>


namespace codal
{

class MicroBitRadioFlashReceiver
{
    public:
    /**
     * Constructor
     * 
     * Creates an instance of MicroBitRadioFlashReceiver which receives a new user program from a
     * MicroBitRadioFlashSender and flashes it into memory
     * 
     * @param uBit A reference to the micro:bit object
     */
    MicroBitRadioFlashReceiver(MicroBit &uBit);

    /**
     * Main receiver loop, called to begin cycle of listening for packets and requesting retransmissions
     * 
     * @param uBit A reference to the micro:bit object
     */
    void Rmain(MicroBit &uBit);

    private:
    MicroBit &uBit; //reference to the microbit device

    // uint32_t user_start;
    // uint32_t user_end;
    // uint32_t user_size;
    uint8_t pageBuffer[MICROBIT_CODEPAGESIZE]; //buffer of one page of program data (protocol supports multipage programs so each page is buffered at a time)

    uint32_t recID; //unique ID used for performance data collection
    uint32_t time; //counter since boot, incremented every R_SLEEP_TIME/2, used as unique ID

    uint32_t totalPackets; //total number of packets in this transfer, determined by field in header of received packet
    uint32_t totalPages; //total number of pages in this transfer, determined by field in header of received packet
    uint32_t packetsPerPage; //number of packets in each page R_FLASH_PAGE_SIZE / R_PAYLOAD_SIZE
    uint32_t packetsThisPage; //packets in current page being written, usually = packetsPerPage but recalculated on last page to ensure correct size is written to memory

    std::map<uint16_t, bool> packetMap; //data structure used to keep track of which packets have been correctly received, initialised as {sequence number, false} for all sequence numbers, set to {seq, true} when received correctly
    std::map<uint16_t, bool> receivedNAKs; //data structure used to track which packets this device has received a NAK for (from another receiver), used to supress NAKs and avoid duplicates (NAK implosion)
    
    //state of this receiver
    typedef enum PageState
    {
        RECEIVING, //receiving packets
        RECOVERY //requesting retransmissions if necessary, or waiting if all received correctly
    }PageState;
    volatile PageState pageState;

    volatile bool transferComplete; //
    uint16_t currentPage; //current page number
    uint16_t lastSeqN; //sequence number of last data packet correctly received
    uint32_t lastRxTime; //
    uint32_t lastNAKTime;
    bool readyToNAK;
    uint32_t nakRounds;

    uint32_t start_time; //system time at start of transfer (first packet received)

    uint8_t xPixel; //x coordinate of screen loading animation
    uint8_t yPixel; //y coordinate of screen loading animation
    uint32_t packetsWritten; //number of packets written to flash
    uint32_t fraction; //total packets / number of screen pixels (25)
    

    /**
     * Write one data packet into the page buffer, writes page buffer into flash if page is complete
     * 
     * @param packet The received data packet
     * @param uBit reference to the microbit device
     */
    void handleSenderPacket(PacketBuffer packet, MicroBit &uBit);

    /**
     * Extract received NAK sequence number, set receivedNAKs to true for that seq number
     * If still in RECEIVING state, enter RECOVERY
     * 
     * @param packet The received NAK packet
     * @param uBit reference to the microbit device
     */
    void handleReceiverPacket(PacketBuffer packet, MicroBit &uBit);

    /**
     * Calculates checksum for the packet and compares with the received checksum inside the packet
     * 
     * @param p The packet being checked
     * @return true if both checksums are the same, false otherwise
     */
    bool isCheckSumOK(PacketBuffer p);
    /**
     * Calculates checksum for the packet's HEADER and compares with the received checksum inside the packet
     * 
     * @param p The packet being checked
     * @return true if both checksums are the same, false otherwise
     */
    bool isHeaderCheckSumOK(PacketBuffer p);

    void updateLoadingScreen(MicroBit &uBit);

    /**
     * Check if all sequence numbers in packetMap are true (all packets written to page buffer)
     * @return true if all packets written, false otherwise
     */
    bool isBufferWritten();

    /**
     * Write one buffered page to the given address in flash
     * @param flash_addr the address in flash to write to
     * @param pageBuffer the page buffer to write
     */
    void flashUserPage(uint32_t flash_addr, uint8_t *pageBuffer);

    /**
     * Erases all pages in the FLASH_USER memory region
     */
    void eraseAllUserPages();

    /**
     * 
     */
    void sendNAKs(MicroBit &uBit);
    void printInfo(MicroBit &uBit);
    
};

} //namespace