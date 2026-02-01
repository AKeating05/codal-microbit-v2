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
    MicroBitRadioFlashReceiver(MicroBit &uBit);
    void Rmain(MicroBit &uBit);

    private:
    MicroBit &uBit;

    uint32_t user_start = (uint32_t)&__user_start__;
    uint32_t user_end = (uint32_t)&__user_end__;
    uint32_t user_size = user_end - user_start;

    uint32_t start_time;

    uint32_t totalPackets;
    uint32_t totalPages;
    uint32_t packetsPerPage = R_FLASH_PAGE_SIZE / R_PAYLOAD_SIZE;
    

    uint8_t pageBuffer[4096];

    volatile bool pageBuffered;
    volatile bool transferComplete;
    uint32_t currentPage;
    uint32_t lastSeqN;

    uint8_t id;

    std::map<uint16_t, bool> packetMap;
    std::map<uint16_t, bool> receivedNAKs;
    

    void handleSenderPacket(PacketBuffer packet, MicroBit &uBit);
    void handleReceiverPacket(PacketBuffer packet, MicroBit &uBit);
    bool isCheckSumOK(PacketBuffer p);
    bool isHeaderCheckSumOK(PacketBuffer p);
    bool checkAllWritten();
    void flashUserPage(uint32_t flash_addr, uint8_t *pageBuffer);
    void eraseAllUserPages();
    void sendNAKs(MicroBit &uBit);
    void printInfo(MicroBit &uBit);
    
};

} //namespace