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
#include <set>

namespace codal
{
    /**
     * Provides Sender logic for partial flashing over radio, built using the MicroBitRadio library
     * Implements a reliable multicast protocol, where Microbits have fixed roles with one Sender and many Receivers
     */
    class MicroBitRadioFlashSender
    {
        private:
        MicroBit uBit; //reference to the microbit device

        uint32_t user_start = (uint32_t)&__user_start__; //address for the start of the code placed in FLASH_USER region, from the linker symbol __user_start__
        uint32_t user_end = (uint32_t)&__user_end__; //address for the end of the code placed in FLASH_USER region, from the linker symbol __user_end__ (not necessarily the same as the end of the defined region)
        uint32_t user_size = user_end - user_start; //size of the user code
        
        uint32_t totalPackets = (user_size + R_PAYLOAD_SIZE - 1)/R_PAYLOAD_SIZE; //total number of packets for the entire payload
        uint32_t packetsPerPage = R_FLASH_PAGE_SIZE / R_PAYLOAD_SIZE; //number of packets sent per page
        uint32_t totalPages = (totalPackets + packetsPerPage - 1)/ packetsPerPage; //total number of pages
        
        std::set<uint16_t> receivedNAKs;

        /**
         * Calculates checksum for the packet's header and compares with the received checksum inside the packet
         * 
         * @param p The packet being checked
         * @return true if both checksums are the same, false otherwise
         */
        bool isHeaderCheckSumOK(PacketBuffer p);

        /**
         * Sends one packet from a page of FLASH_USER, defined in the linker script
         * 
         * Packet Structure:
         * 0            1   2   3    4   5       6       7        8        9       10      11  .... 15
         * +-----------------------------------------------------------------------------------------+
         * | Sndr/Recvr | Seq # | Page # | Total packets | Header Checksum | Data Checksum | Padding |
         * +-----------------------------------------------------------------------------------------+
         * |                                              Data                                       |
         * +-----------------------------------------------------------------------------------------+
         * 16                                                                                        31
         * 
         * @param seq the sequence number of the packet being sent, this is also used to calculate the correct address to read from
         * @param currentpage the number of the current page being sent
         * @param uBit reference to the microbit device
         */
        void sendSinglePacket(uint16_t seq, uint32_t currentpage, MicroBit &uBit);

        /**
         * Sends a page of flash by calling sendSinglePacket(), for 0 to npackets
         * 
         * @param npackets the number of packets sent this page
         * @param currentpage the number of the current page being sent
         * @param uBit reference to the microbit device
         */
        void sendPage(uint16_t npackets, uint32_t currentpage, MicroBit &uBit);

        /**
         * Parses a NAK from a receiver Microbit and adds its sequence number to a list of NAKs used for retransmission
         * 
         * NAK Packet Structure (same length as sender packet header, but ignoring certain fields)
         * 0    1              2               3    4   5  .....  7    8     9 .....  15  
         * +---------------------------------------------------------------------------+
         * | ID | Seq of packet for retransmit | Page # | Padding | Checksum | Padding |
         * +---------------------------------------------------------------------------+
         * 
         * @param p the NAK packet received
         * @param currentpage the number of the page that was last sent, used to check if the NAK is for the current page
         * @param uBit reference to the microbit device
         */
        void handleNAK(PacketBuffer p, uint32_t currentpage, MicroBit &uBit);

        public:
        /**
         * Constructor.
         * 
         * Creates an instance of a MicroBitRadioFlashSender which can send its user code
         * to other MicroBit receivers which can then reflash with the received code
         * 
         * @param uBit A reference to the micro:bit object
         */
        MicroBitRadioFlashSender(MicroBit &uBit);

        /**
         * Main Sender logic loop.
         * 
         * Called to begin cycle of transmitting the sender's user code, listening for NAKs 
         * from receivers and retransmitting
         * 
         * @param uBit A reference to the micro:bit object
         */
        void Smain(MicroBit &uBit);

    };

} //namespace