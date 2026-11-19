/**
 * @file    mbc6.c
 * @brief   MBC6 split banking and byte-wide page-programmable cartridge flash.
 * @author  cupidthecat
 * @date    2026-11-18
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://gnu.org>.
 */

#include "mbc6_internal.h"
#include "state_io.h"

enum {
    WINDOW_SIZE = 0x2000,
    SECTOR_SIZE = 0x20000,
    UNLOCK_ADDRESS_A = 0x5555,
    UNLOCK_ADDRESS_B = 0x2aaa,
    STATUS_READY = 0x80,
    STATUS_TIMEOUT = 0x10,
    STATUS_PROTECTED = 0x02
};

enum {
    READ_ARRAY,
    READ_ID,
    READ_HIDDEN,
    COLLECT_PAGE,
    COLLECT_HIDDEN_PAGE,
    READ_STATUS,
    COLLECT_BUFFER,
    COLLECT_HIDDEN_BUFFER
};

static bool collecting(uint8_t mode) {
    return mode == COLLECT_PAGE || mode == COLLECT_HIDDEN_PAGE || mode == COLLECT_BUFFER ||
           mode == COLLECT_HIDDEN_BUFFER;
}

static uint8_t ready_status(const CupidGbMbc6 *mbc) {
    return STATUS_READY | (mbc->sector_protected ? STATUS_PROTECTED : 0u);
}

enum { UNLOCK_IDLE, UNLOCK_SECOND, UNLOCK_COMMAND };

static unsigned program_window(uint16_t address) {
    return (address >> 13u) & 1u;
}

unsigned cupid_gb_mbc6_rom_bank(const CupidGb *gb, uint16_t address, bool physical) {
    const unsigned window = program_window(address);
    const unsigned bank = gb->mbc6.rom_bank[window];
    if (gb->mbc6.flash_selected[window])
        return bank & 0x7fu;
    return physical && gb->rom_bank_count ? bank & (unsigned)(gb->rom_bank_count * 2u - 1u) : bank;
}

static size_t flash_address(const CupidGbMbc6 *mbc, uint16_t address) {
    return (size_t)(mbc->rom_bank[program_window(address)] & 0x7fu) * WINDOW_SIZE +
           (address & (WINDOW_SIZE - 1u));
}

size_t cupid_gb_mbc6_ram_offset(const CupidGb *gb, uint16_t address) {
    const unsigned window = (address >> 12u) & 1u;
    return (size_t)gb->mbc6.ram_bank[window] * 0x1000u + (address & 0xfffu);
}

bool cupid_gb_mbc6_read_program(const CupidGb *gb, uint16_t address, uint8_t *value) {
    const CupidGbMbc6 *mbc = &gb->mbc6;
    const unsigned window = program_window(address);
    if (!mbc->flash_selected[window]) {
        const size_t bank = cupid_gb_mbc6_rom_bank(gb, address, true);
        const size_t offset = bank * WINDOW_SIZE + (address & (WINDOW_SIZE - 1u));
        *value = offset < gb->rom_size ? gb->rom[offset] : 0xff;
        return true;
    }
    if (!mbc->flash_enabled || !gb->mbc6_flash)
        return false;
    const size_t offset = flash_address(mbc, address);
    switch (mbc->flash_mode) {
        case READ_ID:
            *value = (offset & 0xffu) == 0 ? 0xc2u : (offset & 0xffu) == 1 ? 0x81u : 0xffu;
            break;
        case READ_HIDDEN:
            *value = gb->mbc6_flash[CUPID_GB_MBC6_FLASH_SIZE + (offset & 0xffu)];
            break;
        case COLLECT_PAGE:
        case COLLECT_HIDDEN_PAGE:
        case COLLECT_BUFFER:
        case COLLECT_HIDDEN_BUFFER:
        case READ_STATUS:
            *value = ready_status(mbc);
            break;
        default:
            *value = gb->mbc6_flash[offset];
            break;
    }
    return true;
}

static bool sector_zero_writable(const CupidGbMbc6 *mbc) {
    return mbc->write_enabled && !mbc->sector_protected;
}

static void clear_page(CupidGbMbc6 *mbc) {
    mbc->page_count = 0;
    mbc->page_base = 0;
    memset(mbc->page, 0, sizeof(mbc->page));
}

static void begin_buffer(CupidGbMbc6 *mbc, bool hidden) {
    mbc->flash_mode = hidden ? COLLECT_HIDDEN_BUFFER : COLLECT_BUFFER;
    mbc->status = 0;
    mbc->page_count = 0;
    mbc->page_base = 0;
    memset(mbc->page, 0xff, sizeof(mbc->page));
}

static void reset_command(CupidGbMbc6 *mbc) {
    mbc->flash_mode = READ_ARRAY;
    mbc->sequence = UNLOCK_IDLE;
    mbc->setup = 0;
    mbc->status = 0;
    clear_page(mbc);
}

static void complete(CupidGbMbc6 *mbc) {
    clear_page(mbc);
    mbc->flash_mode = READ_STATUS;
    /* Status reports completion immediately. Chip operation latency and
     * intermediate polling behavior require hardware measurements. */
    /* Failure also finishes the operation; bit 1 reports sector protection. */
    mbc->status = ready_status(mbc);
}

static void erase(CupidGb *gb, size_t start, size_t length) {
    uint8_t *storage = gb->mbc6_flash + start;
    for (size_t i = 0; i < length; ++i) {
        if (storage[i] != 0xffu) {
            memset(storage, 0xff, length);
            ++gb->battery_write_generation;
            return;
        }
    }
}

static void page_write(CupidGb *gb, size_t offset, uint8_t value) {
    CupidGbMbc6 *mbc = &gb->mbc6;
    if (mbc->flash_mode == COLLECT_PAGE || mbc->flash_mode == COLLECT_HIDDEN_PAGE) {
        /* Older states contain a sequential prefix and a zero-filled tail.
         * Retain that representation until the next write so loading and saving
         * an untouched pending state preserves its bytes. */
        memset(mbc->page + mbc->page_count, 0xff, sizeof(mbc->page) - mbc->page_count);
        mbc->page_base = 0;
        mbc->flash_mode = mbc->flash_mode == COLLECT_PAGE ? COLLECT_BUFFER : COLLECT_HIDDEN_BUFFER;
    }
    const unsigned index = (unsigned)offset & (CUPID_GB_MBC6_PAGE_SIZE - 1u);
    const bool hidden = mbc->flash_mode == COLLECT_HIDDEN_BUFFER;
    /* page_count stores the previous buffer index plus one, with zero meaning
     * that no data has arrived. Repeating that index triggers programming. */
    if (mbc->page_count != index + 1u) {
        mbc->page[index] = value;
        mbc->page_count = (uint8_t)(index + 1u);
        return;
    }
    if (value == 0xf0u) {
        reset_command(mbc);
        return;
    }
    const size_t base = offset & ~(size_t)(CUPID_GB_MBC6_PAGE_SIZE - 1u);
    const size_t target = hidden ? base & (CUPID_GB_MBC6_HIDDEN_SIZE - 1u) : base;
    const bool writable =
        hidden ? mbc->write_enabled : target >= SECTOR_SIZE || sector_zero_writable(mbc);
    if (writable) {
        uint8_t *destination = gb->mbc6_flash + target + (hidden ? CUPID_GB_MBC6_FLASH_SIZE : 0);
        bool changed = false;
        for (size_t i = 0; i < CUPID_GB_MBC6_PAGE_SIZE; ++i) {
            const uint8_t programmed = destination[i] & mbc->page[i];
            changed |= programmed != destination[i];
            destination[i] = programmed;
        }
        if (changed)
            ++gb->battery_write_generation;
    }
    complete(mbc);
}

static void execute_command(CupidGb *gb, size_t offset, uint8_t value) {
    CupidGbMbc6 *mbc = &gb->mbc6;
    const uint8_t setup = mbc->setup;
    mbc->setup = 0;
    if (setup == 0x80u && value == 0x30u) {
        const size_t start = offset & ~(size_t)(SECTOR_SIZE - 1u);
        const bool writable = start || sector_zero_writable(mbc);
        if (writable)
            erase(gb, start, SECTOR_SIZE);
        complete(mbc);
        return;
    }
    if (setup == 0x60u && (value == 0x20u || value == 0x40u)) {
        if (offset >= SECTOR_SIZE || !mbc->write_enabled)
            return;
        if (mbc->sector_protected != (value == 0x20u)) {
            mbc->sector_protected = value == 0x20u;
            ++gb->battery_write_generation;
        }
        complete(mbc);
        return;
    }
    if ((offset & 0x7fffu) != UNLOCK_ADDRESS_A)
        return;
    if (setup == 0x80u && value == 0x10u) {
        const size_t start = sector_zero_writable(mbc) ? 0 : SECTOR_SIZE;
        erase(gb, start, CUPID_GB_MBC6_FLASH_SIZE - start);
        complete(mbc);
    } else if (setup == 0x77u && value == 0x77u) {
        mbc->flash_mode = READ_HIDDEN;
        mbc->status = 0;
    } else if (setup == 0x60u && (value == 0x04u || value == 0xe0u)) {
        if (!mbc->write_enabled) {
            return;
        } else if (value == 0xe0u) {
            begin_buffer(mbc, true);
        } else {
            erase(gb, CUPID_GB_MBC6_FLASH_SIZE, CUPID_GB_MBC6_HIDDEN_SIZE);
            complete(mbc);
        }
    } else if (!setup) {
        switch (value) {
            case 0x60:
            case 0x77:
            case 0x80:
                mbc->setup = value;
                break;
            case 0x90:
                mbc->flash_mode = READ_ID;
                mbc->status = 0;
                break;
            case 0xa0:
                begin_buffer(mbc, false);
                break;
        }
    }
}

static void write_flash(CupidGb *gb, uint16_t address, uint8_t value) {
    CupidGbMbc6 *mbc = &gb->mbc6;
    const size_t offset = flash_address(mbc, address);
    if (collecting(mbc->flash_mode)) {
        page_write(gb, offset, value);
        return;
    }
    if (value == 0xf0u) {
        reset_command(mbc);
        return;
    }
    const size_t command_address = offset & 0x7fffu;
    if (mbc->sequence == UNLOCK_COMMAND) {
        mbc->sequence = UNLOCK_IDLE;
        execute_command(gb, offset, value);
    } else if (mbc->sequence == UNLOCK_SECOND && command_address == UNLOCK_ADDRESS_B &&
               value == 0x55u) {
        mbc->sequence = UNLOCK_COMMAND;
    } else if (command_address == UNLOCK_ADDRESS_A && value == 0xaau) {
        mbc->sequence = UNLOCK_SECOND;
    } else {
        mbc->sequence = UNLOCK_IDLE;
        mbc->setup = 0;
    }
}

void cupid_gb_mbc6_write(CupidGb *gb, uint16_t address, uint8_t value) {
    CupidGbMbc6 *mbc = &gb->mbc6;
    if (address < 0x1000u) {
        switch (address >> 10u) {
            case 0:
                gb->ram_enabled = (value & 0x0fu) == 0x0au;
                break;
            case 1:
            case 2:
                mbc->ram_bank[(address >> 10u) - 1u] = value & 7u;
                break;
            case 3:
                mbc->flash_enabled = (value & 1u) != 0;
                break;
        }
    } else if (address == 0x1000u) {
        mbc->write_enabled = (value & 1u) != 0;
    } else if (address >= 0x2000u && address < 0x4000u) {
        const unsigned window = (address >> 12u) & 1u;
        if (address & 0x0800u)
            mbc->flash_selected[window] = (value & 8u) != 0;
        else
            mbc->rom_bank[window] = value;
    } else if (address >= 0x4000u && mbc->flash_enabled && gb->mbc6_flash &&
               mbc->flash_selected[program_window(address)]) {
        write_flash(gb, address, value);
    }
}

bool cupid_gb_mbc6_state_valid(const CupidGb *gb) {
    const CupidGbMbc6 *mbc = &gb->mbc6;
    if (mbc->flash_mode > COLLECT_HIDDEN_BUFFER || mbc->sequence > UNLOCK_COMMAND ||
        mbc->ram_bank[0] > 7 || mbc->ram_bank[1] > 7 ||
        (mbc->setup && mbc->setup != 0x60 && mbc->setup != 0x77 && mbc->setup != 0x80) ||
        mbc->page_count > CUPID_GB_MBC6_PAGE_SIZE || (mbc->page_base & 0x7fu) ||
        (!mbc->page_count && mbc->page_base))
        return false;
    if (mbc->flash_mode == COLLECT_PAGE || mbc->flash_mode == COLLECT_HIDDEN_PAGE) {
        const size_t limit =
            mbc->flash_mode == COLLECT_PAGE ? CUPID_GB_MBC6_FLASH_SIZE : CUPID_GB_MBC6_HIDDEN_SIZE;
        if (mbc->page_base >= limit || mbc->sequence || mbc->setup)
            return false;
    } else if (mbc->flash_mode == COLLECT_BUFFER || mbc->flash_mode == COLLECT_HIDDEN_BUFFER) {
        if (mbc->page_base || mbc->sequence || mbc->setup)
            return false;
        if (!mbc->page_count) {
            for (unsigned i = 0; i < CUPID_GB_MBC6_PAGE_SIZE; ++i) {
                if (mbc->page[i] != 0xffu)
                    return false;
            }
        }
    } else if (mbc->page_count) {
        return false;
    }
    if (mbc->flash_mode == READ_STATUS) {
        const uint8_t result = mbc->status & (uint8_t)~STATUS_PROTECTED;
        /* Keep completed failure states written by the older implementation readable. */
        if (result != STATUS_READY && result != STATUS_TIMEOUT)
            return false;
    } else if (mbc->status) {
        return false;
    }
    if (mbc->flash_mode != COLLECT_BUFFER && mbc->flash_mode != COLLECT_HIDDEN_BUFFER) {
        for (unsigned i = mbc->page_count; i < CUPID_GB_MBC6_PAGE_SIZE; ++i) {
            if (mbc->page[i])
                return false;
        }
    }
    return true;
}

void cupid_gb_mbc6_state(StateIo *io, CupidGb *gb) {
    CupidGbMbc6 *mbc = &gb->mbc6;
    bytes(io, mbc->rom_bank, sizeof(mbc->rom_bank));
    bytes(io, mbc->ram_bank, sizeof(mbc->ram_bank));
    boolean(io, &mbc->flash_selected[0]);
    boolean(io, &mbc->flash_selected[1]);
    boolean(io, &mbc->flash_enabled);
    boolean(io, &mbc->write_enabled);
    boolean(io, &mbc->sector_protected);
    byte(io, &mbc->flash_mode);
    byte(io, &mbc->sequence);
    byte(io, &mbc->setup);
    byte(io, &mbc->status);
    byte(io, &mbc->page_count);
    double_word(io, &mbc->page_base);
    bytes(io, mbc->page, sizeof(mbc->page));
    bytes(io, gb->mbc6_flash, CUPID_GB_MBC6_STORAGE_SIZE);
    if (!cupid_gb_mbc6_state_valid(gb))
        io->invalid = true;
}
