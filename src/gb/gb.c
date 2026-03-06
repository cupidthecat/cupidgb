/* =========================================================================
 * Game Boy system glue
 *   Initialization, step loop, and the full 0x0000-0xFFFF memory map.
 *   Subsystems (CPU, PPU, APU, timer, cartridge) live in their own files.
 * ========================================================================= */

#include "cupid/gb/gb.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "cupid/gb/cpu.h"
#include "cupid/gb/ppu.h"
#include "cupid/gb/apu.h"
#include "cupid/gb/timer.h"
#include "cupid/gb/cartridge.h"
#include "cupid/common/log.h"

/* --- serial transfer (memory-map helper) --- */

static void cupid_gb_handle_serial_transfer(CupidGb *gb, uint8_t value)
{
    if (gb == 0) {
        return;
    }

    if (value == 0x81u) {
        uint8_t serial_data = gb->io_registers[0x01u];

        fputc((int)serial_data, stdout);
        fflush(stdout);
        gb->io_registers[0x02u] = 0x00u;
        return;
    }

    gb->io_registers[0x02u] = value;
}

/* --- init --- */

void cupid_gb_init(CupidGb *gb)
{
    if (gb == 0) {
        return;
    }

    memset(gb, 0, sizeof(*gb));
    gb->cpu.a = 0x01u;
    gb->cpu.f = 0xb0u;
    gb->cpu.b = 0x00u;
    gb->cpu.c = 0x13u;
    gb->cpu.d = 0x00u;
    gb->cpu.e = 0xd8u;
    gb->cpu.h = 0x01u;
    gb->cpu.l = 0x4du;
    gb->cpu.sp = 0xfffeu;
    gb->cpu.pc = CUPID_GB_ENTRY_POINT;
    gb->io_registers[CUPID_GB_IO_LCDC] = 0x91u;
    gb->io_registers[CUPID_GB_IO_STAT] = 0x80u;
    gb->io_registers[CUPID_GB_IO_BGP] = 0xfcu;
    gb->io_registers[CUPID_GB_IO_OBP0] = 0xffu;
    gb->io_registers[CUPID_GB_IO_OBP1] = 0xffu;
    /* APU post-boot-ROM register state */
    gb->io_registers[0x10u] = 0x80u; /* NR10 */
    gb->io_registers[0x11u] = 0xbfu; /* NR11 */
    gb->io_registers[0x12u] = 0xf3u; /* NR12 */
    gb->io_registers[0x14u] = 0xbfu; /* NR14 */
    gb->io_registers[0x16u] = 0x3fu; /* NR21 */
    gb->io_registers[0x17u] = 0x00u; /* NR22 */
    gb->io_registers[0x19u] = 0xbfu; /* NR24 */
    gb->io_registers[0x1au] = 0x7fu; /* NR30 */
    gb->io_registers[0x1bu] = 0xffu; /* NR31 */
    gb->io_registers[0x1cu] = 0x9fu; /* NR32 */
    gb->io_registers[0x1eu] = 0xbfu; /* NR34 */
    gb->io_registers[0x20u] = 0xffu; /* NR41 */
    gb->io_registers[0x21u] = 0x00u; /* NR42 */
    gb->io_registers[0x23u] = 0xbfu; /* NR44 */
    gb->io_registers[0x24u] = 0x77u; /* NR50 */
    gb->io_registers[0x25u] = 0xf3u; /* NR51 */
    gb->io_registers[0x26u] = 0xf1u; /* NR52 – APU on, CH1 on */
    /* Sync APU struct with boot-ROM state */
    gb->apu.apu_on = true;
    gb->apu.fs_counter = 8191u;
    gb->apu.ch1_dac        = true;
    gb->apu.ch1_on         = true;
    gb->apu.ch1_duty       = 2u;   /* 50% (NR11 = 0xBF) */
    gb->apu.ch1_len        = 63u;  /* NR11 length bits = 0x3F → 64-63=1 → stored 1 */
    gb->apu.ch1_vol        = 15u;
    gb->apu.ch1_env_add    = false;
    gb->apu.ch1_env_period = 3u;
    gb->apu.ch1_env_timer  = 3u;
    gb->apu.ch1_freq       = 0u;
    gb->apu.ch1_timer      = 8192u;
    gb->apu.ch4_lfsr       = 0x7fffu;
    gb->io_registers[0x00u] = 0xcfu;  /* P1: no selection, all buttons released */
    gb->joypad = 0xffu;               /* all buttons released (0=pressed) */
    cupid_gb_reset_ppu(gb);
}

/* --- memory map: read --- */

uint8_t cupid_gb_read_u8(const CupidGb *gb, uint16_t address)
{
    if (gb == 0) {
        return 0xffu;
    }

    if (address <= 0x3fffu) {
        size_t bank = cupid_gb_effective_rom_bank(gb, true);
        size_t offset = bank * 0x4000u + (size_t)address;

        if (offset < gb->rom_size) {
            return gb->rom[offset];
        }
        return 0xffu;
    }

    if (address <= 0x7fffu) {
        size_t bank = cupid_gb_effective_rom_bank(gb, false);
        size_t offset = bank * 0x4000u + (size_t)(address - 0x4000u);

        if (offset < gb->rom_size) {
            return gb->rom[offset];
        }
        return 0xffu;
    }

    if (address >= 0x8000u && address <= 0x9fffu) {
        return gb->video_ram[address - 0x8000u];
    }

    if (address >= 0xa000u && address <= 0xbfffu) {
        if (!gb->ram_enabled) {
            return 0xffu;
        }
        /* MBC2: built-in 512x4-bit RAM, only bottom nibble valid */
        if (gb->header.mbc_type == CUPID_GB_MBC2) {
            size_t offset = (size_t)(address - 0xa000u) & 0x01ffu;
            return (uint8_t)(gb->cartridge_ram[offset] | 0xf0u);
        }
        /* MBC3: RTC register reads when selected */
        if (gb->header.mbc_type == CUPID_GB_MBC3 && gb->rtc_register >= 0x08u) {
            switch (gb->rtc_register) {
            case 0x08u: return gb->rtc.ls;
            case 0x09u: return gb->rtc.lm;
            case 0x0au: return gb->rtc.lh;
            case 0x0bu: return gb->rtc.ldl;
            case 0x0cu: return gb->rtc.ldh;
            default:    return 0xffu;
            }
        }
        /* MBC7: accelerometer / EEPROM registers */
        if (gb->header.mbc_type == CUPID_GB_MBC7) {
            if (address >= 0xa020u && address <= 0xa02fu) {
                return (uint8_t)(gb->mbc7_sensor_x & 0xffu);
            }
            if (address >= 0xa030u && address <= 0xa03fu) {
                return (uint8_t)((gb->mbc7_sensor_x >> 8u) & 0xffu);
            }
            if (address >= 0xa040u && address <= 0xa04fu) {
                return (uint8_t)(gb->mbc7_sensor_y & 0xffu);
            }
            if (address >= 0xa050u && address <= 0xa05fu) {
                return (uint8_t)((gb->mbc7_sensor_y >> 8u) & 0xffu);
            }
            return 0xffu;
        }
        /* HuC3: command interface */
        if (gb->header.mbc_type == CUPID_GB_HUC3 && gb->huc3_mode == 0x0cu) {
            return gb->huc3_value;
        }
        /* HuC1: IR mode returns 0xC0 (no IR signal) */
        if (gb->header.mbc_type == CUPID_GB_HUC1 && gb->huc1_ir_mode) {
            return 0xc0u;
        }
        {
            size_t offset = cupid_gb_effective_ram_bank(gb) * 0x2000u + (size_t)(address - 0xa000u);
            if (offset < gb->cartridge_ram_size) {
                return gb->cartridge_ram[offset];
            }
        }
        return 0xffu;
    }

    if (address >= 0xc000u && address <= 0xdfffu) {
        return gb->work_ram[address - 0xc000u];
    }

    if (address >= 0xe000u && address <= 0xfdffu) {
        return gb->work_ram[address - 0xe000u];
    }

    if (address >= 0xfe00u && address <= 0xfe9fu) {
        return gb->object_attribute_memory[address - 0xfe00u];
    }

    if (address >= 0xfea0u && address <= 0xfeffu) {
        return 0xffu;
    }

    if (address == 0xff26u) {
        /* NR52: rebuild on-the-fly with current channel status */
        return (uint8_t)(
            (gb->apu.apu_on ? 0x80u : 0u) | 0x70u |
            (gb->apu.ch4_on ? 0x08u : 0u) |
            (gb->apu.ch3_on ? 0x04u : 0u) |
            (gb->apu.ch2_on ? 0x02u : 0u) |
            (gb->apu.ch1_on ? 0x01u : 0u));
    }

    if (address == 0xff00u) {
        /* P1/JOYP: return joypad state based on selection bits */
        uint8_t sel = gb->io_registers[0x00u];
        uint8_t result = 0x0fu; /* default: no buttons pressed */
        if ((sel & 0x10u) == 0u) {
            result &= (gb->joypad & 0x0fu); /* direction: right/left/up/down */
        }
        if ((sel & 0x20u) == 0u) {
            result &= ((gb->joypad >> 4u) & 0x0fu); /* action: A/B/select/start */
        }
        return (uint8_t)(0xc0u | (sel & 0x30u) | result);
    }

    if (address == 0xff0fu) {
        return (uint8_t)(gb->interrupt_flags | 0xe0u);
    }

    if (address >= 0xff00u && address <= 0xff7fu) {
        return gb->io_registers[address - 0xff00u];
    }

    if (address >= 0xff80u && address <= 0xfffeu) {
        return gb->high_ram[address - 0xff80u];
    }

    if (address == 0xffffu) {
        return gb->interrupt_enable;
    }

    return 0xffu;
}

/* --- MBC3 real-time clock helpers --- */

static void cupid_gb_rtc_update(CupidGb *gb)
{
    int64_t now = (int64_t)time(NULL);
    int64_t elapsed;
    int64_t total;
    int days;

    if (gb->rtc.base_time == 0) {
        gb->rtc.base_time = now;
        return;
    }

    elapsed = now - gb->rtc.base_time;
    if (elapsed <= 0 || (gb->rtc.dh & 0x40u)) { /* halt flag = bit 6 */
        gb->rtc.base_time = now;
        return;
    }

    total = (int64_t)gb->rtc.s + elapsed;
    gb->rtc.s = (uint8_t)(total % 60);
    total /= 60;
    total += (int64_t)gb->rtc.m;
    gb->rtc.m = (uint8_t)(total % 60);
    total /= 60;
    total += (int64_t)gb->rtc.h;
    gb->rtc.h = (uint8_t)(total % 24);
    total /= 24;
    days = ((int)gb->rtc.dl | ((int)(gb->rtc.dh & 0x01u) << 8)) + (int)total;
    gb->rtc.dl = (uint8_t)((unsigned)days & 0xffu);
    gb->rtc.dh = (uint8_t)((gb->rtc.dh & 0xfeu) | (((unsigned)days >> 8) & 0x01u));
    if (days > 511) {
        gb->rtc.dh = (uint8_t)(gb->rtc.dh | 0x80u); /* carry flag = bit 7 */
    }
    gb->rtc.base_time = now;
}

static void cupid_gb_rtc_latch(CupidGb *gb)
{
    cupid_gb_rtc_update(gb);
    gb->rtc.ls = gb->rtc.s;
    gb->rtc.lm = gb->rtc.m;
    gb->rtc.lh = gb->rtc.h;
    gb->rtc.ldl = gb->rtc.dl;
    gb->rtc.ldh = gb->rtc.dh;
}

/* --- memory map: write --- */

void cupid_gb_write_u8(CupidGb *gb, uint16_t address, uint8_t value)
{
    if (gb == 0) {
        return;
    }

    if (address <= 0x1fffu) {
        switch (gb->header.mbc_type) {
        case CUPID_GB_MBC1:
        case CUPID_GB_MBC3:
        case CUPID_GB_MBC5:
        case CUPID_GB_MBC6:
        case CUPID_GB_MBC7:
        case CUPID_GB_MMM01:
            gb->ram_enabled = (value & 0x0fu) == 0x0au;
            break;
        case CUPID_GB_MBC2:
            /* MBC2: only responds when bit 8 of address is clear */
            if ((address & 0x0100u) == 0u) {
                gb->ram_enabled = (value & 0x0fu) == 0x0au;
            }
            break;
        case CUPID_GB_HUC1:
            if (value == 0x0eu) {
                gb->huc1_ir_mode = true;
                gb->ram_enabled = false;
            } else {
                gb->huc1_ir_mode = false;
                gb->ram_enabled = (value & 0x0fu) == 0x0au;
            }
            break;
        case CUPID_GB_HUC3:
            gb->huc3_mode = value;
            gb->ram_enabled = ((value & 0x0fu) == 0x0au);
            break;
        default:
            break;
        }
        return;
    }

    if (address <= 0x3fffu) {
        switch (gb->header.mbc_type) {
        case CUPID_GB_MBC1:
        case CUPID_GB_HUC1:
            gb->mbc1_bank_low5 = (uint8_t)(value & 0x1fu);
            if (gb->mbc1_bank_low5 == 0u) {
                gb->mbc1_bank_low5 = 1u;
            }
            gb->current_rom_bank = cupid_gb_effective_rom_bank(gb, false);
            break;
        case CUPID_GB_MBC2:
            /* MBC2: only responds when bit 8 of address is set */
            if ((address & 0x0100u) != 0u) {
                gb->current_rom_bank = (size_t)(value & 0x0fu);
                if (gb->current_rom_bank == 0u) {
                    gb->current_rom_bank = 1u;
                }
            }
            break;
        case CUPID_GB_MBC3:
        case CUPID_GB_HUC3:
            gb->current_rom_bank = (size_t)(value & 0x7fu);
            if (gb->current_rom_bank == 0u) {
                gb->current_rom_bank = 1u;
            }
            break;
        case CUPID_GB_MBC5:
            if (address <= 0x2fffu) {
                gb->mbc5_rom_bank = (uint16_t)(
                    (gb->mbc5_rom_bank & 0x0100u) | value);
            } else {
                gb->mbc5_rom_bank = (uint16_t)(
                    (gb->mbc5_rom_bank & 0x00ffu) | ((uint16_t)(value & 0x01u) << 8u));
            }
            gb->current_rom_bank = (size_t)gb->mbc5_rom_bank;
            break;
        case CUPID_GB_MBC6:
        case CUPID_GB_MBC7:
        case CUPID_GB_MMM01:
            gb->current_rom_bank = (size_t)(value & 0x7fu);
            if (gb->current_rom_bank == 0u) {
                gb->current_rom_bank = 1u;
            }
            break;
        default:
            break;
        }
        return;
    }

    if (address <= 0x5fffu) {
        switch (gb->header.mbc_type) {
        case CUPID_GB_MBC1:
        case CUPID_GB_HUC1:
            gb->mbc1_bank_high2 = (uint8_t)(value & 0x03u);
            gb->current_rom_bank = cupid_gb_effective_rom_bank(gb, false);
            gb->current_ram_bank = cupid_gb_effective_ram_bank(gb);
            break;
        case CUPID_GB_MBC3:
        case CUPID_GB_HUC3:
            if (value <= 0x03u) {
                gb->current_ram_bank = (size_t)value;
                gb->rtc_register = 0u;
            } else if (value >= 0x08u && value <= 0x0cu) {
                gb->rtc_register = value;
            }
            break;
        case CUPID_GB_MBC5:
            if (gb->header.has_rumble) {
                gb->current_ram_bank = (size_t)(value & 0x07u);
                /* bit 3 = rumble motor (ignored in emulation) */
            } else {
                gb->current_ram_bank = (size_t)(value & 0x0fu);
            }
            break;
        case CUPID_GB_MBC6:
        case CUPID_GB_MBC7:
        case CUPID_GB_MMM01:
            gb->current_ram_bank = (size_t)(value & 0x03u);
            break;
        default:
            break;
        }
        return;
    }

    if (address <= 0x7fffu) {
        switch (gb->header.mbc_type) {
        case CUPID_GB_MBC1:
        case CUPID_GB_HUC1:
            gb->mbc1_ram_banking_mode = (value & 0x01u) != 0u;
            gb->current_rom_bank = cupid_gb_effective_rom_bank(gb, false);
            gb->current_ram_bank = cupid_gb_effective_ram_bank(gb);
            break;
        case CUPID_GB_MBC3:
            /* Latch clock data: write 0x00 then 0x01 */
            if (value == 0x01u && gb->rtc.latch_prev == 0x00u) {
                cupid_gb_rtc_latch(gb);
            }
            gb->rtc.latch_prev = value;
            break;
        default:
            break;
        }
        return;
    }

    if (address >= 0x8000u && address <= 0x9fffu) {
        gb->video_ram[address - 0x8000u] = value;
        return;
    }

    if (address >= 0xa000u && address <= 0xbfffu) {
        if (!gb->ram_enabled) {
            return;
        }
        /* MBC2: built-in 512x4-bit RAM */
        if (gb->header.mbc_type == CUPID_GB_MBC2) {
            size_t offset = (size_t)(address - 0xa000u) & 0x01ffu;
            gb->cartridge_ram[offset] = (uint8_t)(value & 0x0fu);
            return;
        }
        /* MBC3: write to RTC register */
        if (gb->header.mbc_type == CUPID_GB_MBC3 && gb->rtc_register >= 0x08u) {
            switch (gb->rtc_register) {
            case 0x08u: gb->rtc.s = value; break;
            case 0x09u: gb->rtc.m = value; break;
            case 0x0au: gb->rtc.h = value; break;
            case 0x0bu: gb->rtc.dl = value; break;
            case 0x0cu: gb->rtc.dh = value; break;
            default: break;
            }
            gb->rtc.base_time = (int64_t)time(NULL);
            return;
        }
        /* MBC7: accelerometer latch control */
        if (gb->header.mbc_type == CUPID_GB_MBC7) {
            if (address >= 0xa000u && address <= 0xa00fu) {
                if (value == 0x55u) {
                    gb->mbc7_latch = true;
                }
            }
            return;
        }
        /* HuC3: command write */
        if (gb->header.mbc_type == CUPID_GB_HUC3 && gb->huc3_mode == 0x0bu) {
            gb->huc3_value = value;
            return;
        }
        {
            size_t offset = cupid_gb_effective_ram_bank(gb) * 0x2000u + (size_t)(address - 0xa000u);
            if (offset < gb->cartridge_ram_size) {
                gb->cartridge_ram[offset] = value;
            }
        }
        return;
    }

    if (address >= 0xc000u && address <= 0xdfffu) {
        gb->work_ram[address - 0xc000u] = value;
        return;
    }

    if (address >= 0xe000u && address <= 0xfdffu) {
        gb->work_ram[address - 0xe000u] = value;
        return;
    }

    if (address >= 0xfe00u && address <= 0xfe9fu) {
        gb->object_attribute_memory[address - 0xfe00u] = value;
        return;
    }

    if (address >= 0xfea0u && address <= 0xfeffu) {
        return;
    }

    if (address == 0xff0fu) {
        gb->interrupt_flags = (uint8_t)(value & 0x1fu);
        return;
    }

    if (address >= 0xff00u && address <= 0xff7fu) {
        if (address == 0xff02u) {
            cupid_gb_handle_serial_transfer(gb, value);
        } else if (address == 0xff04u) {
            gb->io_registers[0x04u] = 0u;
            gb->div_counter = 0u;
            gb->timer_counter = 0u;
        } else if (address == 0xff40u) {
            bool lcd_was_enabled = cupid_gb_lcd_enabled(gb);

            gb->io_registers[CUPID_GB_IO_LCDC] = value;
            if ((value & 0x80u) == 0u) {
                gb->ppu_counter = 0u;
                gb->frame_ready = false;
                gb->window_line_counter = 0u;
                gb->io_registers[CUPID_GB_IO_LY] = 0u;
                cupid_gb_set_ppu_mode(gb, CUPID_GB_PPU_MODE_HBLANK);
                cupid_gb_update_stat_irq(gb);
            } else if (!lcd_was_enabled) {
                cupid_gb_reset_ppu(gb);
            }
        } else if (address == 0xff41u) {
            gb->io_registers[CUPID_GB_IO_STAT] =
                (uint8_t)(0x80u | (value & 0x78u) | (gb->io_registers[CUPID_GB_IO_STAT] & 0x07u));
            cupid_gb_update_stat_irq(gb);
        } else if (address == 0xff44u) {
            gb->ppu_counter = 0u;
            gb->io_registers[CUPID_GB_IO_LY] = 0u;
            if (cupid_gb_lcd_enabled(gb)) {
                cupid_gb_set_ppu_mode(gb, CUPID_GB_PPU_MODE_OAM);
            } else {
                cupid_gb_set_ppu_mode(gb, CUPID_GB_PPU_MODE_HBLANK);
            }
            cupid_gb_update_stat_irq(gb);
        } else if (address == 0xff45u) {
            gb->io_registers[CUPID_GB_IO_LYC] = value;
            cupid_gb_update_stat_irq(gb);
        } else if (address == 0xff46u) {
            gb->io_registers[CUPID_GB_IO_DMA] = value;
            cupid_gb_run_dma_transfer(gb, value);
        } else if (address == 0xff07u) {
            gb->io_registers[0x07u] = (uint8_t)(value & 0x07u);
            gb->timer_counter = 0u;
        } else if (address >= 0xff10u && address <= 0xff3fu) {
            gb->io_registers[address - 0xff00u] = value;
            cupid_gb_apu_on_write(gb, (uint8_t)(address - 0xff00u), value);
        } else {
            gb->io_registers[address - 0xff00u] = value;
        }
        return;
    }

    if (address >= 0xff80u && address <= 0xfffeu) {
        gb->high_ram[address - 0xff80u] = value;
        return;
    }

    if (address == 0xffffu) {
        gb->interrupt_enable = value;
    }
}

/* --- step --- */

bool cupid_gb_step(CupidGb *gb)
{
    uint8_t cycles;

    if (gb == 0 || !gb->loaded) {
        return false;
    }

    if (cupid_gb_service_interrupt(gb)) {
        return true;
    }

    if (gb->cpu.stopped) {
        cupid_gb_tick(gb, CUPID_GB_HALT_CYCLES);
        return true;
    }

    if (gb->cpu.halted) {
        /* Check for a pending interrupt first. If IME=1 and one is pending
         * the ISR dispatch (5 M-cycles) happens without any extra HALT cycle.
         * If IME=0 (or no pending interrupt) just advance 1 M-cycle; the
         * service_interrupt call still clears halted when IF&IE != 0. */
        if (cupid_gb_service_interrupt(gb)) {
            return true;
        }
        cupid_gb_tick(gb, CUPID_GB_HALT_CYCLES);
        return true;
    }

    cycles = cupid_gb_execute_unprefixed(gb, cupid_gb_fetch_u8(gb));
    if (cycles > 0u) {
        cupid_gb_tick(gb, cycles);
    }
    if (cycles > 0u && gb->cpu.ime_delay > 0u) {
        gb->cpu.ime_delay = (uint8_t)(gb->cpu.ime_delay - 1u);
        if (gb->cpu.ime_delay == 0u) {
            gb->cpu.ime = true;
        }
    }

    return cycles > 0u;
}
