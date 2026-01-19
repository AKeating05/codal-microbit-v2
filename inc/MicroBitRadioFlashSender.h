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
        MicroBit uBit;

        uint32_t user_start = (uint32_t)&__user_start__;
        uint32_t user_end = (uint32_t)&__user_end__;
        uint32_t user_size = user_end - user_start;
        uint32_t npackets = (user_size + R_PAYLOAD_SIZE - 1)/R_PAYLOAD_SIZE;

        std::set<uint16_t> receivedNAKs;

        /**
         * 
         */
        bool isCheckSumOK(PacketBuffer p);

        /**
         * 
         */
        bool isHeaderCheckSumOK(PacketBuffer p);
        void sendUserProgram(MicroBit &uBit);
        void sendSinglePacket(uint16_t seq, MicroBit &uBit);
        void handleNAK(PacketBuffer p, MicroBit &uBit);

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