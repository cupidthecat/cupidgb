#ifndef CUPID_GB_INFRARED_H
#define CUPID_GB_INFRARED_H

#include "cupid/gb/gb.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { CUPID_GB_INFRARED_WARMUP_LIMIT = 34048, CUPID_GB_INFRARED_DECAY_LIMIT = 48048 };

/* Host input and callbacks are live connections, outside native snapshots.
 * Each emitting latch reports its own changed level. Cartridge callbacks see
 * the committed latch; color-port callbacks see the preceding register value.
 * The output query combines both currently latched LEDs. */
void cupid_gb_set_infrared_callback(CupidGb *gb, CupidGbInfraredCallback callback, void *user_data);
void cupid_gb_set_infrared_input(CupidGb *gb, bool active);
bool cupid_gb_infrared_output(const CupidGb *gb);
bool cupid_gb_infrared_detected(const CupidGb *gb);

/* Device entry points preserve LED latches when a guest closes the receive
 * window. The CGB output latch is writable in monochrome compatibility mode. */
void cupid_gb_infrared_write_cartridge(CupidGb *gb, uint8_t value);
void cupid_gb_infrared_write_port(CupidGb *gb, uint8_t value);
void cupid_gb_infrared_tick(CupidGb *gb, unsigned dots);

#ifdef __cplusplus
}
#endif

#endif
