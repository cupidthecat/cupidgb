#ifndef CUPID_GB_STATE_H
#define CUPID_GB_STATE_H

#include "cupid/gb/gb.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum CupidGbStateResult {
    CUPID_GB_STATE_OK = 0,
    CUPID_GB_STATE_INVALID,
    CUPID_GB_STATE_UNSUPPORTED_VERSION,
    CUPID_GB_STATE_WRONG_ROM,
    CUPID_GB_STATE_WRONG_MODEL,
    CUPID_GB_STATE_WRONG_BOOT_ROM,
    CUPID_GB_STATE_BUFFER_TOO_SMALL,
    CUPID_GB_STATE_OUT_OF_MEMORY,
    CUPID_GB_STATE_NO_ROM
} CupidGbStateResult;

/* Trusted in-memory snapshots may cache the immutable cartridge identity. Initialize
 * once for the current instance and discard the context on cartridge replacement,
 * model changes, or any mutation of ROM bytes, including future cheat/patch features.
 * Binding checks cannot detect an in-place patch or a replacement that reuses an address.
 * Do not use this context to validate untrusted disk files; the ordinary APIs compute
 * the full cartridge digest on every call. Context objects are never serialized.
 */
typedef struct CupidGbStateContext {
    const CupidGb *owner;
    const uint8_t *rom;
    size_t rom_size;
    CupidGbModel model;
    uint8_t rom_digest[32];
} CupidGbStateContext;

CupidGbStateResult cupid_gb_state_context_init(CupidGbStateContext *context, const CupidGb *gb);
CupidGbStateResult cupid_gb_save_state_with_context(const CupidGb *gb, uint8_t *data,
                                                    size_t capacity, size_t *written,
                                                    const CupidGbStateContext *context);
CupidGbStateResult cupid_gb_load_state_with_context(CupidGb *gb, const uint8_t *data, size_t length,
                                                    const CupidGbStateContext *context);

/* Calls require an initialized, loaded instance at an instruction boundary with no concurrent
 * core access. The native format is versioned and independent of host structure layout.
 * ROMs, boot ROMs, and save paths are session resources and are never included in state data.
 * Restoring an active boot overlay requires the same boot ROM to be available in the target.
 * The target's current boot-overlay enabled flag need not match the saved flag.
 */
size_t cupid_gb_state_size(const CupidGb *gb);
CupidGbStateResult cupid_gb_save_state(const CupidGb *gb, uint8_t *data, size_t capacity,
                                       size_t *written);
/* Failure leaves the entire target instance unchanged, including pending audio and ownership.
 * On success, pending core audio is restored; callers must discard queued host audio and reset
 * their host pacing baseline before resuming. These buffers use the CupidGB native format.
 */
CupidGbStateResult cupid_gb_load_state(CupidGb *gb, const uint8_t *data, size_t length);
const char *cupid_gb_state_result_name(CupidGbStateResult result);

#ifdef __cplusplus
}
#endif
#endif
