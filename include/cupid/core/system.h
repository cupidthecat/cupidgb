#ifndef CUPID_CORE_SYSTEM_H
#define CUPID_CORE_SYSTEM_H

typedef enum CupidSystem {
    CUPID_SYSTEM_GB = 0,
    CUPID_SYSTEM_GBC,
    CUPID_SYSTEM_GBA
} CupidSystem;

const char *cupid_system_name(CupidSystem system);

#endif
