#ifndef CUPID_GB_HUC3_H
#define CUPID_GB_HUC3_H

#include "cupid/gb/infrared.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    CUPID_GB_HUC3_SRAM_READ_ONLY = 0x0,
    CUPID_GB_HUC3_SRAM = 0xa,
    CUPID_GB_HUC3_COMMAND = 0xb,
    CUPID_GB_HUC3_RESPONSE = 0xc,
    CUPID_GB_HUC3_STATUS = 0xd,
    CUPID_GB_HUC3_INFRARED = 0xe,
    CUPID_GB_HUC3_IR_WARMUP_LIMIT = CUPID_GB_INFRARED_WARMUP_LIMIT,
    CUPID_GB_HUC3_IR_DECAY_LIMIT = CUPID_GB_INFRARED_DECAY_LIMIT
};

/* Minute-boundary advancement preserves the baseline's second remainder. */
void cupid_gb_huc3_advance(CupidGbHuc3 *device, int64_t host_seconds);
uint8_t cupid_gb_huc3_read_window(const CupidGb *gb);
void cupid_gb_huc3_write_window(CupidGb *gb, uint8_t value);
void cupid_gb_huc3_tick_infrared(CupidGb *gb, unsigned dots);
/* Uses current guest counters and the supplied host second remainder; no catch-up. */
uint32_t cupid_gb_huc3_seconds_to_alarm(const CupidGb *gb, int64_t host_seconds);

/* Input is the remote light level. Output exposes the cartridge LED to a host
 * transport, including after the guest closes the infrared register window. */
void cupid_gb_huc3_set_infrared_input(CupidGb *gb, bool active);
bool cupid_gb_huc3_infrared_output(const CupidGb *gb);

#ifdef __cplusplus
}
#endif

#endif
