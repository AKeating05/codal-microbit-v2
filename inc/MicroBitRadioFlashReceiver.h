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


namespace codal
{

class MicroBitRadioFlashReceiver
{
    public:
    MicroBitRadioFlashReceiver(MicroBit &uBit);
    MicroBit &uBit;
    // volatile bool packetEvent;
    // volatile bool flashComplete;
    // uint8_t pageBuffer[4096];
    // uint32_t totalPackets;
    // uint32_t lastSeqN;
    // uint16_t packetsWritten;

    void handleSenderPacket(PacketBuffer packet);
    void handleReceiverPacket(PacketBuffer packet);
    bool isCheckSumOK(PacketBuffer p);
    bool isHeaderCheckSumOK(PacketBuffer p);
    bool checkAllWritten();
    void flashUserProgram(uint32_t flash_addr, uint8_t *pageBuffer);
    void sendNAKs();
    void Rmain();
    void printInfo();
    
};

} //namespace