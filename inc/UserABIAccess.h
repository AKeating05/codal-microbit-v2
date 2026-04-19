#pragma once
#include "UserABI.h"

//pointer to function table location in memory, must be the same address as in linker script
#define USER_ABI_ADDR 0x70F00
#define ABI ((const UserABI *)USER_ABI_ADDR)