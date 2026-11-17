/**
 * @file    debugger.h
 * @brief   Debugger records and disassembly, symbol, breakpoint, and expression interfaces.
 * @author  cupidthecat
 * @date    2026-11-16
 *
 * Copyright (C) 2026 cupidthecat
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://gnu.org>.
 */

#ifndef CUPID_GB_DEBUGGER_H
#define CUPID_GB_DEBUGGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "cupid/gb/gb.h"

#define CUPID_GB_DEBUG_MAX_BREAKPOINTS 128u
#define CUPID_GB_DEBUG_MAX_WATCHPOINTS 128u
#define CUPID_GB_DEBUG_SYMBOL_NAME 128u
#define CUPID_GB_DEBUG_EXPRESSION_SIZE 256u

enum {
    CUPID_GB_DEBUG_ACCESS_READ = 1u,
    CUPID_GB_DEBUG_ACCESS_WRITE = 2u,
    CUPID_GB_DEBUG_ACCESS_RW = CUPID_GB_DEBUG_ACCESS_READ | CUPID_GB_DEBUG_ACCESS_WRITE
};

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
    CUPID_GB_DEBUG_STOP_ERROR,
    CUPID_GB_DEBUG_STOP_ILLEGAL_OPCODE,
    CUPID_GB_DEBUG_STOP_WATCHPOINT
} CupidGbDebugStop;

typedef struct CupidGbDebugBreakpoint {
    uint16_t address;
    int bank; /* -1 matches every currently mapped bank. */
    bool enabled;
    char condition[CUPID_GB_DEBUG_EXPRESSION_SIZE]; /* Empty means unconditional. */
} CupidGbDebugBreakpoint;

/* Inclusive virtual address range. A negative bank matches all mappings;
 * otherwise the currently mapped bank must match at the original bus address. */
typedef struct CupidGbDebugWatchpoint {
    uint32_t id;
    uint16_t start;
    uint16_t end;
    int bank;
    uint8_t access;
    bool enabled;
    char condition[CUPID_GB_DEBUG_EXPRESSION_SIZE];
} CupidGbDebugWatchpoint;

/* First matched guest access in a hardware step. For reads, old/new are zero.
 * For writes, new is the attempted byte. Old is a passive pre-write view for
 * unconditional matches, or the sampled guest bus value when a write condition
 * evaluates the old token. */
typedef struct CupidGbDebugWatchHit {
    uint32_t id;
    uint16_t address;
    int bank;
    uint8_t access;
    uint8_t old_value;
    uint8_t new_value;
} CupidGbDebugWatchHit;

typedef struct CupidGbDebugValue {
    uint16_t value;
    int bank; /* -1 is a plain value; otherwise a bank-qualified address. */
} CupidGbDebugValue;

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
    CupidGbDebugWatchpoint watchpoints[CUPID_GB_DEBUG_MAX_WATCHPOINTS];
    size_t wp_count;
    CupidGbDebugWatchHit last_watch_hit;
    bool watch_pending;
    bool watch_error;
    bool watch_evaluating;
    bool watch_active;
    CupidGb *watch_gb;
    CupidGbMemoryAccessCallback previous_memory_access_callback;
    void *previous_memory_access_user_data;
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
/* Symbol and breakpoint bank identity. MMM01 uses controller bank numbers in
 * menu-last order, including unlocked 0xfffe/0xffff, independent of file layout. */
int cupid_gb_debugger_bank(const CupidGb *gb, uint16_t address);
void cupid_gb_debug_disassemble(const CupidGb *gb, uint16_t address,
                                CupidGbDebugInstruction *instruction);

void cupid_gb_debugger_init(CupidGbDebugger *debugger);
void cupid_gb_debugger_cleanup(CupidGbDebugger *debugger);
/* Execution reset preserves breakpoints and symbols; it does not change gb. */
void cupid_gb_debugger_reset_execution(CupidGbDebugger *debugger);
bool cupid_gb_debugger_breakpoint_add(CupidGbDebugger *debugger, int bank, uint16_t address);
/* Conditions are pure expressions evaluated only at matching instruction addresses.
 * Invalid conditions leave the collection intact. A missing symbol at runtime
 * stops execution with ERROR instead of silently bypassing the breakpoint. */
bool cupid_gb_debugger_breakpoint_add_condition(CupidGbDebugger *debugger, const CupidGb *gb,
                                                int bank, uint16_t address, const char *condition);
/* Pure 16-bit expressions support registers, symbols, decimal/$/0x literals,
 * parentheses, byte [address] and little-endian word {address} reads, bank:address,
 * arithmetic, shifts, bitwise operators, comparisons, && and ||. Precedence from
 * low to high: comparisons; + - | ||; * / % & ^ &&; shifts; bank qualification.
 * Arithmetic wraps to 16 bits; division/remainder by zero and large shifts yield
 * zero. Both logical operands are evaluated. Assignment and unary operators are
 * rejected. Reads use debugger peek semantics without changing guest hardware.
 * Bank qualifiers wrap to nine bits; a zero qualifier retains the mapped view.
 * Maximum text is 255 bytes and nesting is bounded to 32 levels. */
bool cupid_gb_debugger_evaluate(const CupidGbDebugger *debugger, const CupidGb *gb,
                                const char *expression, CupidGbDebugValue *result,
                                size_t *error_offset);
bool cupid_gb_debugger_breakpoint_remove(CupidGbDebugger *debugger, size_t index);
void cupid_gb_debugger_breakpoints_clear(CupidGbDebugger *debugger);
bool cupid_gb_debugger_match_breakpoint(const CupidGbDebugger *debugger, const CupidGb *gb);
/* Watch definitions are host metadata, never native hardware-state fields.
 * Writes match attempted guest accesses before DMA redirects and bus locks.
 * Conditions use the same bounded expressions as execution breakpoints and
 * additionally accept old/new during watch evaluation. Ranges are inclusive.
 * Invalid definitions do not modify existing watches. IDs remain stable after
 * removal. Each successful add creates a watchpoint even for an identical
 * definition. New IDs follow the last live ID plus one, or start at one when
 * empty; removing the tail or clearing all entries permits ID reuse. */
bool cupid_gb_debugger_watchpoint_add(CupidGbDebugger *debugger, const CupidGb *gb, int bank,
                                      uint16_t start, uint16_t end, uint8_t access,
                                      const char *condition);
bool cupid_gb_debugger_watchpoint_remove(CupidGbDebugger *debugger, uint32_t id);
void cupid_gb_debugger_watchpoints_clear(CupidGbDebugger *debugger);
/* Surround exactly one guest hardware step with these hooks when the host uses
 * the normal execution API. End always unregisters the scoped observer and
 * reports a matched access only after the hardware step has completed. Pure
 * debugger peeks, expression reads, and editor pokes remain unobserved. */
void cupid_gb_debugger_watch_begin(CupidGbDebugger *debugger, CupidGb *gb);
CupidGbDebugStop cupid_gb_debugger_watch_end(CupidGbDebugger *debugger, CupidGb *gb);
void cupid_gb_debugger_begin(CupidGbDebugger *debugger, const CupidGb *gb, CupidGbDebugMode mode);
/* Check is the before-instruction hook for a host's ordinary run loop. It
 * checks breakpoints regardless of the command mode, including after reset. */
CupidGbDebugStop cupid_gb_debugger_check(CupidGbDebugger *debugger, const CupidGb *gb);
/* Step executes at most one instruction or HALT/STOP idle cycle. A stopped
 * result pauses the session, except BUDGET, which leaves its command active.
 * ILLEGAL_OPCODE follows a completed fetch and retains the resulting halt;
 * continuing advances peripheral clocks until an explicit IE edit releases it.
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
