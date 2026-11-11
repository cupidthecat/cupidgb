#include "cupid/gb/mbc7.h"

#include <string.h>

enum {
    EEPROM_DO = 0x01,
    EEPROM_DI = 0x02,
    EEPROM_CLOCK = 0x40,
    EEPROM_SELECT = 0x80,
    EEPROM_START = 0x400,
    EEPROM_WORDS = CUPID_GB_MBC7_EEPROM_SIZE / 2,
    SENSOR_RESET = 0x8000,
    SENSOR_NEUTRAL = 0x81d0,
};

static uint16_t read_word(const CupidGb *gb, unsigned index) {
    const uint8_t *word = gb->cartridge_ram + index * 2;
    return (uint16_t)((unsigned)word[1] * 256u + word[0]);
}

static void store_word(CupidGb *gb, unsigned index, uint16_t value) {
    if (read_word(gb, index) == value)
        return;
    uint8_t *word = gb->cartridge_ram + index * 2;
    word[0] = (uint8_t)value;
    word[1] = (uint8_t)(value >> 8);
    ++gb->battery_write_generation;
}

static void fill_eeprom(CupidGb *gb, uint16_t value) {
    for (unsigned index = 0; index < EEPROM_WORDS; ++index)
        store_word(gb, index, value);
}

void cupid_gb_mbc7_reset_serial(CupidGb *gb) {
    gb->mbc7.command = 0;
    gb->mbc7.output = UINT16_MAX;
    gb->mbc7.data_bits = 0;
    gb->mbc7.pins = EEPROM_DO;
    gb->mbc7.registers_enabled = false;
    gb->mbc7.programming_enabled = false;
}

void cupid_gb_mbc7_reset(CupidGb *gb) {
    gb->mbc7_sensor_x = SENSOR_RESET;
    gb->mbc7_sensor_y = SENSOR_RESET;
    gb->mbc7_latch = true;
    cupid_gb_mbc7_reset_serial(gb);
    memset(gb->cartridge_ram, 0xff, CUPID_GB_MBC7_EEPROM_SIZE);
}

void cupid_gb_mbc7_set_motion(CupidGb *gb, int16_t x, int16_t y) {
    if (gb) {
        gb->mbc7.motion_x = x;
        gb->mbc7.motion_y = y;
    }
}

static bool register_window(const CupidGb *gb, uint16_t address) {
    return gb->ram_enabled && gb->mbc7.registers_enabled && address < 0xb000u;
}

uint8_t cupid_gb_mbc7_read(const CupidGb *gb, uint16_t address) {
    if (!register_window(gb, address))
        return 0xff;
    switch ((address / 16u) % 16u) {
        case 2:
            return (uint8_t)gb->mbc7_sensor_x;
        case 3:
            return (uint8_t)(gb->mbc7_sensor_x >> 8);
        case 4:
            return (uint8_t)gb->mbc7_sensor_y;
        case 5:
            return (uint8_t)(gb->mbc7_sensor_y >> 8);
        case 6:
            return 0;
        case 8:
            return gb->mbc7.pins;
        default:
            return 0xff;
    }
}

/* The start bit and 10 following bits frame each command. The high address
 * line does not reach the 128-word EEPROM array. */
static void dispatch_command(CupidGb *gb) {
    CupidGbMbc7 *device = &gb->mbc7;
    const unsigned opcode = (device->command >> 8) & 3u;
    const unsigned index = device->command % EEPROM_WORDS;
    const unsigned control = (device->command >> 6) & 3u;
    if (opcode == 2u) {
        device->output = read_word(gb, index);
    } else if (opcode == 1u || (opcode == 0u && control == 1u)) {
        if (device->programming_enabled) {
            if (opcode)
                store_word(gb, index, 0);
            else
                fill_eeprom(gb, 0);
        }
        device->data_bits = 16;
        return;
    } else if (opcode == 3u) {
        if (device->programming_enabled) {
            store_word(gb, index, UINT16_MAX);
            device->output = 0x3fff;
        }
    } else if (control == 2u) {
        if (device->programming_enabled) {
            fill_eeprom(gb, UINT16_MAX);
            device->output = 0x00ff;
        }
    } else {
        device->programming_enabled = control == 3u;
    }
    device->command = 0;
}

static void clock_eeprom(CupidGb *gb) {
    CupidGbMbc7 *device = &gb->mbc7;
    const bool input = (device->pins & EEPROM_DI) != 0;
    device->pins |= (uint8_t)(device->output >> 15);
    device->output = (uint16_t)((unsigned)device->output * 2u + 1u);
    if (!device->data_bits) {
        device->command = (uint16_t)((unsigned)device->command * 2u + input);
        if (device->command & EEPROM_START)
            dispatch_command(gb);
        return;
    }

    /* Model the observed data phase independently of the initial erase.
     * A bulk transfer erases all words but shifts into the first 127. */
    --device->data_bits;
    device->pins |= EEPROM_DO;
    if (input) {
        const uint16_t bit = (uint16_t)(1u << device->data_bits);
        if (device->command & 0x100u) {
            const unsigned index = device->command % EEPROM_WORDS;
            store_word(gb, index, read_word(gb, index) | bit);
        } else {
            for (unsigned index = 0; index + 1u < EEPROM_WORDS; ++index)
                store_word(gb, index, read_word(gb, index) | bit);
        }
    }
    if (!device->data_bits) {
        device->command = 0;
        device->output = 0x3fff;
    }
}

void cupid_gb_mbc7_write(CupidGb *gb, uint16_t address, uint8_t value) {
    if (!register_window(gb, address))
        return;
    switch ((address / 16u) % 16u) {
        case 0:
            if (value == 0x55) {
                gb->mbc7_latch = true;
                gb->mbc7_sensor_x = gb->mbc7_sensor_y = SENSOR_RESET;
            }
            break;
        case 1:
            if (value == 0xaa) {
                gb->mbc7_latch = false;
                gb->mbc7_sensor_x = (uint16_t)(SENSOR_NEUTRAL + gb->mbc7.motion_x);
                gb->mbc7_sensor_y = (uint16_t)(SENSOR_NEUTRAL + gb->mbc7.motion_y);
            }
            break;
        case 8: {
            CupidGbMbc7 *device = &gb->mbc7;
            const bool clock =
                (value & (EEPROM_SELECT | EEPROM_CLOCK)) == (EEPROM_SELECT | EEPROM_CLOCK) &&
                !(device->pins & EEPROM_CLOCK);
            device->pins = (uint8_t)((device->pins & EEPROM_DO) |
                                     (value & (EEPROM_SELECT | EEPROM_CLOCK | EEPROM_DI)));
            if (clock) {
                device->pins &= (uint8_t)~EEPROM_DO;
                clock_eeprom(gb);
            }
            break;
        }
        default:
            break;
    }
}
