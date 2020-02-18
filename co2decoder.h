// Based on https://github.com/fe-c/MT8060-data-read code
// All rights for reading code owned https://geektimes.ru/users/fedorro/
// and https://github.com/revspace

#include <stdint.h>
#include <stdbool.h>

typedef enum
{
    HUMIDITY    = 0x41,
    TEMPERATURE = 0x42,
    CO2_PPM     = 0x50,
} co2_data_type;

typedef struct {
    co2_data_type type;
    uint16_t value;
    bool checksum_is_valid;
} co2message;

bool co2process(unsigned long ms, bool data);

void co2msg(co2message* msg);
