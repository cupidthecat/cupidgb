#ifndef CUPID_GB_CPU_H
#define CUPID_GB_CPU_H

#include <stdbool.h>
#include <stdint.h>

#include "cupid/gb/gb.h"

enum {
    CUPID_GB_FLAG_Z = 0x80,
    CUPID_GB_FLAG_N = 0x40,
    CUPID_GB_FLAG_H = 0x20,
    CUPID_GB_FLAG_C = 0x10,
    CUPID_GB_INTERRUPT_VBLANK   = 0x01,
    CUPID_GB_INTERRUPT_LCD_STAT = 0x02,
    CUPID_GB_INTERRUPT_TIMER    = 0x04,
    CUPID_GB_INTERRUPT_SERIAL   = 0x08,
    CUPID_GB_INTERRUPT_JOYPAD   = 0x10,
    CUPID_GB_INTERRUPT_CYCLES = 5,
    CUPID_GB_HALT_CYCLES       = 1
};

void    cupid_gb_request_interrupt(CupidGb *gb, uint8_t mask);
bool    cupid_gb_service_interrupt(CupidGb *gb);
void    cupid_gb_trigger_oam_bug_write_access(CupidGb *gb, uint16_t address);
uint8_t cupid_gb_execute_unprefixed(CupidGb *gb, uint8_t opcode);
uint8_t cupid_gb_fetch_u8(CupidGb *gb);

#endif
