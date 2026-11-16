#ifndef CUPID_GB_STARTUP_H
#define CUPID_GB_STARTUP_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct CupidGb;

/* Host cold-start policy, excluded from native execution states. */
typedef struct CupidGbPowerOn {
    uint64_t seed;
    uint64_t generation_seed;
    bool fixed_seed;
} CupidGbPowerOn;

/* Select reproducible power-on noise for subsequent init/ROM loads. Zero is a
 * valid seed, not a zero-memory mode. Neither setter changes live guest memory
 * or the realized generation. Policy survives init/state loads; cleanup clears it. */
void cupid_gb_set_power_on_seed(struct CupidGb *gb, uint64_t seed);
void cupid_gb_clear_power_on_seed(struct CupidGb *gb);
/* Seed of the last realized cold-start generation, including automatic choices.
 * Native-state loading restores memory without replacing this host metadata. */
uint64_t cupid_gb_get_power_on_seed(const struct CupidGb *gb);

/* Prepare initial firmware execution after loading a cartridge and its boot ROM,
 * before running guest code. Reconstruct WRAM, HRAM, OAM and VRAM from the same
 * realized cold-start generation, then initialize firmware wave/palette RAM and
 * the ordinary boot-entry registers. This deliberately replaces volatile memory.
 * SRAM, RTC data, seed policy and host registrations survive; no new seed is drawn.
 * This is not a native hard/quick reset. NULL or missing firmware changes nothing.
 * Use cupid_gb_enter_boot_rom for re-entry that preserves live volatile memory. */
void cupid_gb_start_boot_rom(struct CupidGb *gb);

#ifdef __cplusplus
}
#endif

#endif
