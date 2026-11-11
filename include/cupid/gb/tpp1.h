#ifndef CUPID_GB_TPP1_H
#define CUPID_GB_TPP1_H

#include "cupid/gb/gb.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    CUPID_GB_TPP1_MOTOR = 0x03,
    CUPID_GB_TPP1_CLOCK_RUNNING = 0x04,
    CUPID_GB_TPP1_CLOCK_OVERFLOW = 0x08
};

bool cupid_gb_tpp1_valid_mode(uint8_t mode);
void cupid_gb_tpp1_advance(CupidGbTpp1 *device, int64_t host_seconds);
uint8_t cupid_gb_tpp1_read(const CupidGb *gb, uint16_t address);
void cupid_gb_tpp1_write_window(CupidGb *gb, uint16_t address, uint8_t value);
void cupid_gb_tpp1_write_register(CupidGb *gb, uint16_t address, uint8_t value);

#ifdef __cplusplus
}
#endif

#endif
