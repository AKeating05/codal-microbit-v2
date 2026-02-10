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

/**
 * Packet payload size and header size, payload size is configurable
 */
#define R_PAYLOAD_SIZE 32
#define R_HEADER_SIZE 16
#define R_FLASH_PAGE_SIZE 4096

/**
 * Note: These addresses define the start and end of the FLASH_USER region, not the start and end of the code placed there
 * They must be the same as the addresses of this region in the linker script nrf52833-softdevice.ld
 */
#define USER_BASE_ADDRESS 0x71000
#define USER_END_ADDRESS 0x77000

#define R_SLEEP_TIME 100 
#define R_NAK_WINDOW 3*R_SLEEP_TIME

/**
 * Linker symbols for the start and end of code placed in the flash_user section
 */
extern uint8_t __user_start__, __user_end__;