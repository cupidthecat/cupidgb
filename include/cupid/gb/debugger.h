#ifndef CUPID_GB_DEBUGGER_H
#define CUPID_GB_DEBUGGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "cupid/gb/gb.h"

#define CUPID_GB_DEBUG_MAX_BREAKPOINTS 128u
#define CUPID_GB_DEBUG_SYMBOL_NAME 128u

typedef enum CupidGbDebugMode {
    CUPID_GB_DEBUG_PAUSED = 0,
    CUPID_GB_DEBUG_RUN,
    CUPID_GB_DEBUG_STEP,
    CUPID_GB_DEBUG_NEXT,
    CUPID_GB_DEBUG_FINISH
} CupidGbDebugMode;

typedef enum CupidGbDebugStop {
    CUPID_GB_DEBUG_STOP_NONE = 0,
    CUPID_GB_DEBUG_STOP_STEP,
    CUPID_GB_DEBUG_STOP_BREAKPOINT,
    CUPID_GB_DEBUG_STOP_TARGET,
    CUPID_GB_DEBUG_STOP_BUDGET,
    CUPID_GB_DEBUG_STOP_NO_ROM,
    CUPID_GB_DEBUG_STOP_ERROR
} CupidGbDebugStop;

typedef struct CupidGbDebugBreakpoint {
    uint16_t address;
    int bank; /* -1 matches every currently mapped bank. */
    bool enabled;
} CupidGbDebugBreakpoint;

typedef struct CupidGbDebugSymbol {
    uint16_t address;
    uint16_t bank;
    char name[CUPID_GB_DEBUG_SYMBOL_NAME];
} CupidGbDebugSymbol;

typedef struct CupidGbDebugInstruction {
    uint8_t bytes[3];
    uint8_t length;
    bool valid;
    char text[80];
} CupidGbDebugInstruction;

/* Host state only: never serialize this structure as emulated hardware state.
 * Initialize once, clean up owned symbols, and reset execution after replacing
 * a cartridge, loading a state, or performing a debugger memory edit. */
typedef struct CupidGbDebugger {
    CupidGbDebugBreakpoint breakpoints[CUPID_GB_DEBUG_MAX_BREAKPOINTS];
    size_t bp_count;
    CupidGbDebugSymbol *symbols;
    size_t symbol_count;
    CupidGbDebugMode mode;
    CupidGbDebugStop last_stop;
    uint64_t steps;
    int call_depth;
    uint16_t call_stacks[128];
    size_t call_count;
    bool skip_breakpoint;
    uint16_t skip_address;
    int skip_bank;
} CupidGbDebugger;

/* These calls require exclusive access at an instruction boundary. Peek never
 * changes hardware, advances clocks, or applies CPU VRAM/OAM/wave bus locks.
 * Poke rejects ROM and unusable addresses, directly edits mapped writable RAM,
 * and uses ordinary device semantics for I/O. It does not execute CPU cycles. */
uint8_t cupid_gb_debug_peek(const CupidGb *gb, uint16_t address);
bool cupid_gb_debug_poke(CupidGb *gb, uint16_t address, uint8_t value);
int cupid_gb_debugger_bank(const CupidGb *gb, uint16_t address);
void cupid_gb_debug_disassemble(const CupidGb *gb, uint16_t address,
                                CupidGbDebugInstruction *instruction);

void cupid_gb_debugger_init(CupidGbDebugger *debugger);
void cupid_gb_debugger_cleanup(CupidGbDebugger *debugger);
/* Execution reset preserves breakpoints and symbols; it does not change gb. */
void cupid_gb_debugger_reset_execution(CupidGbDebugger *debugger);
bool cupid_gb_debugger_breakpoint_add(CupidGbDebugger *debugger, int bank, uint16_t address);
bool cupid_gb_debugger_breakpoint_remove(CupidGbDebugger *debugger, size_t index);
void cupid_gb_debugger_breakpoints_clear(CupidGbDebugger *debugger);
bool cupid_gb_debugger_match_breakpoint(const CupidGbDebugger *debugger, const CupidGb *gb);
void cupid_gb_debugger_begin(CupidGbDebugger *debugger, const CupidGb *gb, CupidGbDebugMode mode);
/* Check is the before-instruction hook for a host's ordinary run loop. It
 * checks breakpoints regardless of the command mode, including after reset. */
CupidGbDebugStop cupid_gb_debugger_check(CupidGbDebugger *debugger, const CupidGb *gb);
/* Step executes at most one instruction or HALT/STOP idle cycle. A stopped
 * result pauses the session, except BUDGET, which leaves its command active.
 * Run budgets count calls to the hardware step, not guest clocks. */
CupidGbDebugStop cupid_gb_debugger_step(CupidGbDebugger *debugger, CupidGb *gb);
CupidGbDebugStop cupid_gb_debugger_run(CupidGbDebugger *debugger, CupidGb *gb, uint64_t budget);
const char *cupid_gb_debugger_stop_name(CupidGbDebugStop stop);

/* RGBDS-style hexadecimal bank:address name lines, with semicolon comments.
 * Loading replaces symbols transactionally; invalid input leaves them intact.
 * Duplicate names resolve to their last definition. Returned names remain
 * valid until clear, reload, or cleanup. Limits: 4 MiB input, 16,384 symbols. */
bool cupid_gb_debugger_load_symbols(CupidGbDebugger *debugger, const char *text, size_t length,
                                    size_t *error_line);
void cupid_gb_debugger_symbols_clear(CupidGbDebugger *debugger);
const char *cupid_gb_debugger_symbol_for_address(const CupidGbDebugger *debugger, int bank,
                                                 uint16_t address, bool exact);
bool cupid_gb_debugger_lookup_symbol(const CupidGbDebugger *debugger, const char *name,
                                     uint16_t *bank, uint16_t *address);

#ifdef __cplusplus
}
#endif
#endif
