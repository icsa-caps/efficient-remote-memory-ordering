#include <stdio.h>
#include <stdint.h>

#define ADDR_BAD_DEVICE 0xC0000000

int main(int argc, char* argv[])
{
    printf("Attempting to access bad device at address %x\n", ADDR_BAD_DEVICE);
    uint32_t* dev_ptr = (uint32_t*) ADDR_BAD_DEVICE;
    printf("Bad device data: %x\n", *dev_ptr);

    return 0;
}





