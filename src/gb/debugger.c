#include "cupid/gb/debugger.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cupid/gb/cartridge.h"
#include "cupid/gbc/cgb.h"

static bool ordinary_cartridge_ram(const CupidGb *gb) {
    if (!gb->cartridge_ram_size || gb->cartridge_ram_size > CUPID_GB_MAX_RAM_SIZE)
        return false;
    if (gb->header.mbc_type == CUPID_GB_MBC7 || gb->header.mbc_type == CUPID_GB_HUC3)
        return false;
    if (gb->header.mbc_type == CUPID_GB_HUC1 && gb->huc1_ir_mode)
        return false;
    return gb->header.mbc_type != CUPID_GB_MBC3 || gb->rtc_register < 8u;
}

static size_t cartridge_offset(const CupidGb *gb, uint16_t address) {
    const size_t offset = cupid_gb_effective_ram_bank(gb) * 0x2000u + (address - 0xa000u);
    return offset % gb->cartridge_ram_size;
}

uint8_t cupid_gb_debug_peek(const CupidGb *gb, uint16_t address) {
    if (!gb)
        return 0xffu;
    if (address >= 0x8000u && address < 0xa000u)
        return gb->video_ram[cupid_cgb_vram_offset(gb, address)];
    if (address >= 0xa000u && address < 0xc000u && ordinary_cartridge_ram(gb)) {
        if (gb->header.mbc_type == CUPID_GB_MBC2)
            return (uint8_t)(gb->cartridge_ram[address & 0x01ffu] | 0xf0u);
        return gb->cartridge_ram[cartridge_offset(gb, address)];
    }
    if (address >= 0xfe00u && address < 0xfea0u)
        return gb->object_attribute_memory[address - 0xfe00u];
    if (address >= 0xff30u && address <= 0xff3fu)
        return gb->io_registers[address - 0xff00u];
    if (gb->cgb_mode && address == 0xff69u)
        return gb->cgb_bg_palette_ram[gb->io_registers[0x68u] & 0x3fu];
    if (gb->cgb_mode && address == 0xff6bu)
        return gb->cgb_obj_palette_ram[gb->io_registers[0x6au] & 0x3fu];
    return cupid_gb_read_u8(gb, address);
}

bool cupid_gb_debug_poke(CupidGb *gb, uint16_t address, uint8_t value) {
    if (!gb || !gb->loaded || address < 0x8000u || (address >= 0xfea0u && address < 0xff00u))
        return false;
    if (address < 0xa000u) {
        gb->video_ram[cupid_cgb_vram_offset(gb, address)] = value;
    } else if (address < 0xc000u) {
        if (!ordinary_cartridge_ram(gb))
            return false;
        size_t offset;
        if (gb->header.mbc_type == CUPID_GB_MBC2) {
            offset = address & 0x01ffu;
            value &= 0x0fu;
        } else {
            offset = cartridge_offset(gb, address);
        }
        if (gb->cartridge_ram[offset] != value && gb->header.has_battery)
            ++gb->battery_write_generation;
        gb->cartridge_ram[offset] = value;
    } else if (address < 0xe000u) {
        gb->work_ram[cupid_cgb_wram_offset(gb, address)] = value;
    } else if (address < 0xfe00u) {
        gb->work_ram[cupid_cgb_wram_offset(gb, (uint16_t)(address - 0x2000u))] = value;
    } else if (address < 0xfea0u) {
        gb->object_attribute_memory[address - 0xfe00u] = value;
    } else if (address >= 0xff30u && address <= 0xff3fu) {
        gb->io_registers[address - 0xff00u] = value;
    } else {
        /* Register edits intentionally apply device side effects. A semantic
         * DIV write, for example, can clock a timer edge without a CPU cycle. */
        cupid_gb_write_u8(gb, address, value);
    }
    return true;
}

int cupid_gb_debugger_bank(const CupidGb *gb, uint16_t address) {
    if (!gb)
        return 0;
    if (address < 0x4000u)
        return (int)cupid_gb_effective_rom_bank(gb, true);
    if (address < 0x8000u)
        return (int)cupid_gb_effective_rom_bank(gb, false);
    if (address < 0xa000u)
        return (int)(cupid_cgb_vram_offset(gb, address) / 0x2000u);
    if (address < 0xc000u)
        return (int)cupid_gb_effective_ram_bank(gb);
    if (address < 0xe000u)
        return (int)(cupid_cgb_wram_offset(gb, address) / 0x1000u);
    if (address < 0xfe00u)
        return (int)(cupid_cgb_wram_offset(gb, (uint16_t)(address - 0x2000u)) / 0x1000u);
    return 0;
}

void cupid_gb_debug_disassemble(const CupidGb *gb, uint16_t address,
                                CupidGbDebugInstruction *instruction) {
    static const char *const registers[] = {"B", "C", "D", "E", "H", "L", "(HL)", "A"};
    static const char *const pairs[] = {"BC", "DE", "HL", "SP"};
    static const char *const stack_pairs[] = {"BC", "DE", "HL", "AF"};
    static const char *const indirect[] = {"BC", "DE", "HL+", "HL-"};
    static const char *const conditions[] = {"NZ", "Z", "NC", "C"};
    static const char *const arithmetic[] = {"ADD A,", "ADC A,", "SUB", "SBC A,",
                                             "AND",    "XOR",    "OR",  "CP"};
    static const char *const accumulator[] = {"RLCA", "RRCA", "RLA", "RRA",
                                              "DAA",  "CPL",  "SCF", "CCF"};
    static const char *const rotations[] = {"RLC", "RRC", "RL", "RR", "SLA", "SRA", "SWAP", "SRL"};
    if (!instruction)
        return;
    memset(instruction, 0, sizeof(*instruction));
    for (unsigned index = 0; index < 3; ++index)
        instruction->bytes[index] = cupid_gb_debug_peek(gb, (uint16_t)(address + index));
    const unsigned opcode = instruction->bytes[0];
    const unsigned first = instruction->bytes[1];
    const unsigned immediate = first | ((unsigned)instruction->bytes[2] << 8u);
    const int displacement = first < 128u ? (int)first : (int)first - 256;
    const unsigned relative = (uint16_t)((int)address + 2 + displacement);
    const unsigned group = opcode >> 6u;
    const unsigned destination = (opcode >> 3u) & 7u;
    const unsigned source = opcode & 7u;
    const unsigned pair = destination >> 1u;
    const bool odd = (destination & 1u) != 0u;
    instruction->length = 1u;
    instruction->valid = true;
    char *text = instruction->text;
    const size_t capacity = sizeof(instruction->text);
    if (opcode == 0xcbu) {
        const unsigned operation = first >> 6u;
        const unsigned bit = (first >> 3u) & 7u;
        const char *reg = registers[first & 7u];
        instruction->length = 2u;
        if (operation == 0u)
            snprintf(text, capacity, "%s %s", rotations[bit], reg);
        else
            snprintf(text, capacity, "%s %u, %s",
                     operation == 1u   ? "BIT"
                     : operation == 2u ? "RES"
                                       : "SET",
                     bit, reg);
        return;
    }
    if (group == 0u) {
        switch (source) {
            case 0:
                if (destination == 0u)
                    snprintf(text, capacity, "NOP");
                else if (destination == 1u) {
                    instruction->length = 3u;
                    snprintf(text, capacity, "LD ($%04X), SP", immediate);
                } else if (destination == 2u) {
                    instruction->length = 2u;
                    snprintf(text, capacity, "STOP");
                } else {
                    instruction->length = 2u;
                    if (destination == 3u)
                        snprintf(text, capacity, "JR $%04X", relative);
                    else
                        snprintf(text, capacity, "JR %s, $%04X", conditions[destination - 4u],
                                 relative);
                }
                break;
            case 1:
                if (odd)
                    snprintf(text, capacity, "ADD HL, %s", pairs[pair]);
                else {
                    instruction->length = 3u;
                    snprintf(text, capacity, "LD %s, $%04X", pairs[pair], immediate);
                }
                break;
            case 2:
                snprintf(text, capacity, odd ? "LD A, (%s)" : "LD (%s), A", indirect[pair]);
                break;
            case 3:
                snprintf(text, capacity, "%s %s", odd ? "DEC" : "INC", pairs[pair]);
                break;
            case 4:
            case 5:
                snprintf(text, capacity, "%s %s", source == 4u ? "INC" : "DEC",
                         registers[destination]);
                break;
            case 6:
                instruction->length = 2u;
                snprintf(text, capacity, "LD %s, $%02X", registers[destination], first);
                break;
            default:
                snprintf(text, capacity, "%s", accumulator[destination]);
                break;
        }
    } else if (group == 1u) {
        if (opcode == 0x76u)
            snprintf(text, capacity, "HALT");
        else
            snprintf(text, capacity, "LD %s, %s", registers[destination], registers[source]);
    } else if (group == 2u) {
        snprintf(text, capacity, "%s %s", arithmetic[destination], registers[source]);
    } else {
        switch (source) {
            case 0:
                if (destination < 4u)
                    snprintf(text, capacity, "RET %s", conditions[destination]);
                else {
                    instruction->length = 2u;
                    if (destination == 4u)
                        snprintf(text, capacity, "LDH ($FF%02X), A", first);
                    else if (destination == 5u)
                        snprintf(text, capacity, "ADD SP, %+d", displacement);
                    else if (destination == 6u)
                        snprintf(text, capacity, "LDH A, ($FF%02X)", first);
                    else
                        snprintf(text, capacity, "LD HL, SP%+d", displacement);
                }
                break;
            case 1:
                if (!odd)
                    snprintf(text, capacity, "POP %s", stack_pairs[pair]);
                else {
                    const char *const operations[] = {"RET", "RETI", "JP HL", "LD SP, HL"};
                    snprintf(text, capacity, "%s", operations[pair]);
                }
                break;
            case 2:
                if (destination < 4u) {
                    instruction->length = 3u;
                    snprintf(text, capacity, "JP %s, $%04X", conditions[destination], immediate);
                } else if (destination == 4u)
                    snprintf(text, capacity, "LD ($FF00+C), A");
                else if (destination == 6u)
                    snprintf(text, capacity, "LD A, ($FF00+C)");
                else {
                    instruction->length = 3u;
                    snprintf(text, capacity, destination == 5u ? "LD ($%04X), A" : "LD A, ($%04X)",
                             immediate);
                }
                break;
            case 3:
                if (destination == 0u) {
                    instruction->length = 3u;
                    snprintf(text, capacity, "JP $%04X", immediate);
                } else if (destination >= 6u)
                    snprintf(text, capacity, "%s", destination == 6u ? "DI" : "EI");
                else
                    instruction->valid = false;
                break;
            case 4:
                if (destination < 4u) {
                    instruction->length = 3u;
                    snprintf(text, capacity, "CALL %s, $%04X", conditions[destination], immediate);
                } else
                    instruction->valid = false;
                break;
            case 5:
                if (!odd)
                    snprintf(text, capacity, "PUSH %s", stack_pairs[pair]);
                else if (pair == 0u) {
                    instruction->length = 3u;
                    snprintf(text, capacity, "CALL $%04X", immediate);
                } else
                    instruction->valid = false;
                break;
            case 6:
                instruction->length = 2u;
                snprintf(text, capacity, "%s $%02X", arithmetic[destination], first);
                break;
            default:
                snprintf(text, capacity, "RST $%02X", destination * 8u);
                break;
        }
    }
    if (!instruction->valid)
        snprintf(text, capacity, "DB $%02X", opcode);
}

void cupid_gb_debugger_init(CupidGbDebugger *debugger) {
    if (debugger)
        memset(debugger, 0, sizeof(*debugger));
}

void cupid_gb_debugger_reset_execution(CupidGbDebugger *debugger) {
    if (!debugger)
        return;
    debugger->mode = CUPID_GB_DEBUG_PAUSED;
    debugger->last_stop = CUPID_GB_DEBUG_STOP_NONE;
    debugger->steps = 0;
    debugger->call_depth = 0;
    debugger->call_count = 0;
    debugger->skip_breakpoint = false;
}

void cupid_gb_debugger_symbols_clear(CupidGbDebugger *debugger) {
    if (debugger) {
        free(debugger->symbols);
        debugger->symbols = NULL;
        debugger->symbol_count = 0;
    }
}

void cupid_gb_debugger_cleanup(CupidGbDebugger *debugger) {
    if (debugger) {
        cupid_gb_debugger_symbols_clear(debugger);
        cupid_gb_debugger_init(debugger);
    }
}

bool cupid_gb_debugger_breakpoint_add(CupidGbDebugger *debugger, int bank, uint16_t address) {
    if (!debugger || bank < -1 || bank > UINT16_MAX)
        return false;
    for (size_t index = 0; index < debugger->bp_count; ++index) {
        CupidGbDebugBreakpoint *point = &debugger->breakpoints[index];
        if (point->bank == bank && point->address == address) {
            point->enabled = true;
            return true;
        }
    }
    if (debugger->bp_count == CUPID_GB_DEBUG_MAX_BREAKPOINTS)
        return false;
    debugger->breakpoints[debugger->bp_count++] = (CupidGbDebugBreakpoint){address, bank, true};
    return true;
}

bool cupid_gb_debugger_breakpoint_remove(CupidGbDebugger *debugger, size_t index) {
    if (!debugger || index >= debugger->bp_count)
        return false;
    --debugger->bp_count;
    memmove(debugger->breakpoints + index, debugger->breakpoints + index + 1u,
            (debugger->bp_count - index) * sizeof(debugger->breakpoints[0]));
    return true;
}

void cupid_gb_debugger_breakpoints_clear(CupidGbDebugger *debugger) {
    if (debugger)
        debugger->bp_count = 0;
}

bool cupid_gb_debugger_match_breakpoint(const CupidGbDebugger *debugger, const CupidGb *gb) {
    if (!debugger || !gb || !gb->loaded)
        return false;
    const int bank = cupid_gb_debugger_bank(gb, gb->cpu.pc);
    for (size_t index = 0; index < debugger->bp_count; ++index) {
        const CupidGbDebugBreakpoint *point = &debugger->breakpoints[index];
        if (point->enabled && point->address == gb->cpu.pc &&
            (point->bank < 0 || point->bank == bank))
            return true;
    }
    return false;
}

static CupidGbDebugStop stopped(CupidGbDebugger *debugger, CupidGbDebugStop reason) {
    debugger->last_stop = reason;
    debugger->mode = CUPID_GB_DEBUG_PAUSED;
    debugger->skip_breakpoint = false;
    return reason;
}

void cupid_gb_debugger_begin(CupidGbDebugger *debugger, const CupidGb *gb, CupidGbDebugMode mode) {
    if (!debugger)
        return;
    /* Only a breakpoint stop resumes past that one occurrence. Starting Run
     * elsewhere must still test a breakpoint before its first instruction. */
    const bool skip = debugger->last_stop == CUPID_GB_DEBUG_STOP_BREAKPOINT && gb &&
                      debugger->skip_address == gb->cpu.pc &&
                      debugger->skip_bank == cupid_gb_debugger_bank(gb, gb->cpu.pc);
    cupid_gb_debugger_reset_execution(debugger);
    if (!gb || !gb->loaded) {
        stopped(debugger, CUPID_GB_DEBUG_STOP_NO_ROM);
        return;
    }
    if (mode < CUPID_GB_DEBUG_PAUSED || mode > CUPID_GB_DEBUG_FINISH) {
        stopped(debugger, CUPID_GB_DEBUG_STOP_ERROR);
        return;
    }
    debugger->mode = mode;
    debugger->skip_breakpoint = skip || mode == CUPID_GB_DEBUG_STEP ||
                                mode == CUPID_GB_DEBUG_NEXT || mode == CUPID_GB_DEBUG_FINISH;
    debugger->skip_address = gb->cpu.pc;
    debugger->skip_bank = cupid_gb_debugger_bank(gb, gb->cpu.pc);
}

CupidGbDebugStop cupid_gb_debugger_check(CupidGbDebugger *debugger, const CupidGb *gb) {
    if (!debugger)
        return CUPID_GB_DEBUG_STOP_ERROR;
    if (!gb || !gb->loaded)
        return stopped(debugger, CUPID_GB_DEBUG_STOP_NO_ROM);
    const bool skip = debugger->skip_breakpoint && debugger->skip_address == gb->cpu.pc &&
                      debugger->skip_bank == cupid_gb_debugger_bank(gb, gb->cpu.pc);
    debugger->skip_breakpoint = false;
    if (!skip && cupid_gb_debugger_match_breakpoint(debugger, gb)) {
        debugger->skip_address = gb->cpu.pc;
        debugger->skip_bank = cupid_gb_debugger_bank(gb, gb->cpu.pc);
        return stopped(debugger, CUPID_GB_DEBUG_STOP_BREAKPOINT);
    }
    return CUPID_GB_DEBUG_STOP_NONE;
}

static bool call_opcode(uint8_t opcode) {
    return opcode == 0xcdu || (opcode & 0xe7u) == 0xc4u || (opcode & 0xc7u) == 0xc7u;
}

static bool return_opcode(uint8_t opcode) {
    return opcode == 0xc9u || opcode == 0xd9u || (opcode & 0xe7u) == 0xc0u;
}

CupidGbDebugStop cupid_gb_debugger_step(CupidGbDebugger *debugger, CupidGb *gb) {
    if (!debugger)
        return CUPID_GB_DEBUG_STOP_ERROR;
    if (!gb || !gb->loaded)
        return stopped(debugger, CUPID_GB_DEBUG_STOP_NO_ROM);
    if (debugger->mode == CUPID_GB_DEBUG_PAUSED)
        return debugger->last_stop;
    const CupidGbDebugStop before = cupid_gb_debugger_check(debugger, gb);
    if (before != CUPID_GB_DEBUG_STOP_NONE)
        return before;
    const uint16_t stack = gb->cpu.sp;
    CupidGbStepTrace trace;
    ++debugger->steps;
    if (!cupid_gb_step_observed(gb, &trace))
        return stopped(debugger, CUPID_GB_DEBUG_STOP_ERROR);
    const bool stack_push = gb->cpu.sp == (uint16_t)(stack - 2u);
    const bool stack_pop = gb->cpu.sp == (uint16_t)(stack + 2u);
    const bool interrupt_entry = trace.event == CUPID_GB_STEP_INTERRUPT;
    const bool executed_opcode = trace.event == CUPID_GB_STEP_INSTRUCTION;
    const bool track_calls =
        debugger->mode == CUPID_GB_DEBUG_NEXT || debugger->mode == CUPID_GB_DEBUG_FINISH;
    if (track_calls && stack_push &&
        (interrupt_entry || (executed_opcode && call_opcode(trace.opcode)))) {
        /* Stack history is bounded independently of the emulated stack. An
         * overflow stops inspection without changing any guest execution. */
        if (debugger->call_count ==
            sizeof(debugger->call_stacks) / sizeof(debugger->call_stacks[0]))
            return stopped(debugger, CUPID_GB_DEBUG_STOP_ERROR);
        debugger->call_stacks[debugger->call_count++] = gb->cpu.sp;
        ++debugger->call_depth;
    } else if (track_calls && executed_opcode && stack_pop && return_opcode(trace.opcode)) {
        size_t removed = 0;
        while (debugger->call_count && debugger->call_stacks[debugger->call_count - 1u] <= stack) {
            --debugger->call_count;
            ++removed;
        }
        debugger->call_depth -= removed ? (int)removed : 1;
    }
    if (debugger->mode == CUPID_GB_DEBUG_STEP)
        return stopped(debugger, CUPID_GB_DEBUG_STOP_STEP);
    if ((debugger->mode == CUPID_GB_DEBUG_NEXT && debugger->call_depth <= 0 && !gb->cpu.halted) ||
        (debugger->mode == CUPID_GB_DEBUG_FINISH && debugger->call_depth < 0))
        return stopped(debugger, CUPID_GB_DEBUG_STOP_TARGET);
    return CUPID_GB_DEBUG_STOP_NONE;
}

CupidGbDebugStop cupid_gb_debugger_run(CupidGbDebugger *debugger, CupidGb *gb, uint64_t budget) {
    if (!debugger)
        return CUPID_GB_DEBUG_STOP_ERROR;
    if (!gb || !gb->loaded)
        return stopped(debugger, CUPID_GB_DEBUG_STOP_NO_ROM);
    if (debugger->mode == CUPID_GB_DEBUG_PAUSED)
        return debugger->last_stop;
    for (uint64_t step = 0; step < budget; ++step) {
        const CupidGbDebugStop stop = cupid_gb_debugger_step(debugger, gb);
        if (stop != CUPID_GB_DEBUG_STOP_NONE)
            return stop;
    }
    debugger->last_stop = CUPID_GB_DEBUG_STOP_BUDGET;
    return CUPID_GB_DEBUG_STOP_BUDGET;
}

const char *cupid_gb_debugger_stop_name(CupidGbDebugStop stop) {
    switch (stop) {
        case CUPID_GB_DEBUG_STOP_NONE:
            return "Running";
        case CUPID_GB_DEBUG_STOP_STEP:
            return "Step complete";
        case CUPID_GB_DEBUG_STOP_BREAKPOINT:
            return "Breakpoint";
        case CUPID_GB_DEBUG_STOP_TARGET:
            return "Command complete";
        case CUPID_GB_DEBUG_STOP_BUDGET:
            return "Step budget exhausted";
        case CUPID_GB_DEBUG_STOP_NO_ROM:
            return "No cartridge loaded";
        case CUPID_GB_DEBUG_STOP_ERROR:
            return "Debugger execution failed";
        default:
            return "Unknown debugger stop";
    }
}

static bool hex_value(char byte, unsigned *value) {
    if (byte >= '0' && byte <= '9')
        *value = (unsigned)(byte - '0');
    else if (byte >= 'a' && byte <= 'f')
        *value = (unsigned)(byte - 'a') + 10u;
    else if (byte >= 'A' && byte <= 'F')
        *value = (unsigned)(byte - 'A') + 10u;
    else
        return false;
    return true;
}

static bool read_hex(const char **cursor, const char *end, uint16_t *value) {
    unsigned accumulator = 0, count = 0, digit;
    while (*cursor < end && hex_value(**cursor, &digit)) {
        if (++count > 4u)
            return false;
        accumulator = accumulator * 16u + digit;
        ++*cursor;
    }
    *value = (uint16_t)accumulator;
    return count != 0u;
}

bool cupid_gb_debugger_load_symbols(CupidGbDebugger *debugger, const char *text, size_t length,
                                    size_t *error_line) {
    if (error_line)
        *error_line = 0;
    if (!debugger || (!text && length) || length > 4u * 1024u * 1024u)
        return false;
    CupidGbDebugSymbol *symbols = NULL;
    size_t count = 0, capacity = 0, offset = 0, line = 0;
    while (offset < length) {
        ++line;
        const size_t start = offset;
        while (offset < length && text[offset] != '\n')
            ++offset;
        const char *cursor = text + start;
        const char *end = text + offset;
        if (offset < length)
            ++offset;
        for (const char *byte = cursor; byte < end; ++byte) {
            if (*byte == '\0')
                goto invalid;
            if (*byte == ';') {
                end = byte;
                break;
            }
        }
        while (cursor < end && isspace((unsigned char)*cursor))
            ++cursor;
        while (end > cursor && isspace((unsigned char)end[-1]))
            --end;
        if (cursor == end)
            continue;
        CupidGbDebugSymbol symbol = {0};
        if (!read_hex(&cursor, end, &symbol.bank) || cursor == end || *cursor++ != ':' ||
            !read_hex(&cursor, end, &symbol.address) || cursor == end ||
            !isspace((unsigned char)*cursor))
            goto invalid;
        while (cursor < end && isspace((unsigned char)*cursor))
            ++cursor;
        const size_t name_length = (size_t)(end - cursor);
        if (!name_length || name_length >= sizeof(symbol.name))
            goto invalid;
        for (size_t index = 0; index < name_length; ++index)
            if (!strchr("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.",
                        cursor[index]))
                goto invalid;
        memcpy(symbol.name, cursor, name_length);
        if (count == 16384u)
            goto invalid;
        if (count == capacity) {
            const size_t next_capacity = capacity ? capacity * 2u : 64u;
            CupidGbDebugSymbol *expanded = realloc(symbols, next_capacity * sizeof(*symbols));
            if (!expanded)
                goto invalid;
            symbols = expanded;
            capacity = next_capacity;
        }
        symbols[count++] = symbol;
    }
    cupid_gb_debugger_symbols_clear(debugger);
    debugger->symbols = symbols;
    debugger->symbol_count = count;
    return true;
invalid:
    free(symbols);
    if (error_line)
        *error_line = line;
    return false;
}

const char *cupid_gb_debugger_symbol_for_address(const CupidGbDebugger *debugger, int bank,
                                                 uint16_t address, bool exact) {
    if (!debugger)
        return NULL;
    const CupidGbDebugSymbol *best = NULL;
    for (unsigned pass = 0; pass < (bank == 0 ? 1u : 2u); ++pass) {
        const int selected = pass ? 0 : bank;
        for (size_t index = 0; index < debugger->symbol_count; ++index) {
            const CupidGbDebugSymbol *symbol = &debugger->symbols[index];
            if (symbol->bank != selected || symbol->address > address ||
                (exact && symbol->address != address) ||
                (unsigned)(address - symbol->address) > 0x1000u)
                continue;
            if (!best || symbol->address >= best->address)
                best = symbol;
        }
        if (best)
            break;
    }
    return best ? best->name : NULL;
}

bool cupid_gb_debugger_lookup_symbol(const CupidGbDebugger *debugger, const char *name,
                                     uint16_t *bank, uint16_t *address) {
    if (!debugger || !name)
        return false;
    for (size_t index = debugger->symbol_count; index > 0; --index) {
        const CupidGbDebugSymbol *symbol = &debugger->symbols[index - 1u];
        if (!strcmp(symbol->name, name)) {
            if (bank)
                *bank = symbol->bank;
            if (address)
                *address = symbol->address;
            return true;
        }
    }
    return false;
}
