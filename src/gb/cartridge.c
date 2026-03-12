/**
 * @file cartridge.c
 * @brief ROM/RAM parsing, MBC bank-switching, and save-RAM persistence.
 *
 * Handles cartridge header parsing, ROM/RAM loading, MBC bank-switching
 * logic, and save-RAM (.sav) persistence. Shared between Game Boy (DMG)
 * and Game Boy Color (CGB) — both use the same cartridge header format
 * and MBC controllers.
 *
 * Supported MBC types: ROM-only, MBC1 (including multicart), MBC2, MBC3
 * (with optional RTC), MBC5, MBC6, MBC7, MMM01, HuC1, and HuC3.
 */

#include "cupid/gb/cartridge.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cupid/gb/gb.h"
#include "cupid/gbc/cgb.h"
#include "cupid/common/log.h"

// Header field offsets

enum {
    CUPID_GB_HEADER_START    = 0x0134,
    CUPID_GB_HEADER_END      = 0x014c,
    CUPID_GB_LOGO_START      = 0x0104,
    CUPID_GB_LOGO_END        = 0x0133,
    CUPID_GB_TITLE_START     = 0x0134,
    CUPID_GB_TITLE_END       = 0x0143,
    CUPID_GB_CGB_FLAG        = 0x0143,
    CUPID_GB_NEW_LICENSEE_0  = 0x0144,
    CUPID_GB_NEW_LICENSEE_1  = 0x0145,
    CUPID_GB_SGB_FLAG        = 0x0146,
    CUPID_GB_CARTRIDGE_TYPE  = 0x0147,
    CUPID_GB_ROM_SIZE        = 0x0148,
    CUPID_GB_RAM_SIZE        = 0x0149,
    CUPID_GB_DESTINATION_CODE = 0x014a,
    CUPID_GB_OLD_LICENSEE_CODE = 0x014b,
    CUPID_GB_HEADER_CHECKSUM = 0x014d
};

static const uint8_t cupid_gb_nintendo_logo[] = {
    0xceu, 0xedu, 0x66u, 0x66u, 0xccu, 0x0du, 0x00u, 0x0bu,
    0x03u, 0x73u, 0x00u, 0x83u, 0x00u, 0x0cu, 0x00u, 0x0du,
    0x00u, 0x08u, 0x11u, 0x1fu, 0x88u, 0x89u, 0x00u, 0x0eu,
    0xdcu, 0xccu, 0x6eu, 0xe6u, 0xddu, 0xddu, 0xd9u, 0x99u,
    0xbbu, 0xbbu, 0x67u, 0x63u, 0x6eu, 0x0eu, 0xecu, 0xccu,
    0xddu, 0xdcu, 0x99u, 0x9fu, 0xbbu, 0xb9u, 0x33u, 0x3eu
};

enum {
    CUPID_GB_SGB_PACKET_START = 0x0104,
    CUPID_GB_SGB_PACKET_END = 0x0158,
    CUPID_GB_SGB_PACKET_BLOCK_SIZE = 14,
    CUPID_GB_SGB_REFERENCE_DIV_COUNTER = 24,
    CUPID_GB_SGB_REFERENCE_STREAM_POPCOUNT = 288
};

// Small helpers (file-local)

/**
 * @brief Converts a RAM size code from the cartridge header to a byte count.
 *
 * @param code The RAM size code read from header offset 0x0149.
 *
 * @return The corresponding RAM size in bytes, or 0 for unrecognised codes.
 */
static size_t cupid_gb_ram_size_from_code(uint8_t code)
{
    switch (code) {
    case 0x00u:
        return 0u;
    case 0x01u:
        return 2u * 1024u;
    case 0x02u:
        return 8u * 1024u;
    case 0x03u:
        return 32u * 1024u;
    case 0x04u:
        return 128u * 1024u;
    case 0x05u:
        return 64u * 1024u;
    default:
        return 0u;
    }
}

/**
 * @brief Counts the number of set bits in an 8-bit value.
 *
 * @param value The byte to count set bits in.
 *
 * @return The number of bits set to 1 in @p value.
 */
static uint8_t cupid_gb_popcount8(uint8_t value)
{
    uint8_t count = 0u;

    while (value != 0u) {
        count = (uint8_t)(count + (value & 0x01u));
        value >>= 1u;
    }

    return count;
}

/**
 * @brief Approximates the DIV counter value after the SGB boot ROM finishes.
 *
 * The SGB boot ROM transmits the cartridge header as packet data to the
 * SNES before jumping to 0x0100. The time spent doing so — and therefore
 * the resulting DIV phase — depends on the number of 1-bits in that packet
 * stream. This function replicates that calculation so HLE boot mode lands
 * on the correct post-boot DIV phase for timing-sensitive tests.
 *
 * @param rom_data Pointer to the full ROM data buffer.
 * @param rom_size Size of the ROM data in bytes.
 *
 * @return The estimated DIV counter value (6-bit, 0–63) at the point the
 *         boot ROM hands off to the cartridge.
 */
static uint16_t cupid_gb_sgb_boot_div_counter(const uint8_t *rom_data, size_t rom_size)
{
    size_t offset = CUPID_GB_SGB_PACKET_START;
    unsigned stream_popcount = 0u;
    int adjusted_counter;

    while (offset < CUPID_GB_SGB_PACKET_END) {
        uint8_t checksum = 0u;
        size_t block_index;

        for (block_index = 0u; block_index < CUPID_GB_SGB_PACKET_BLOCK_SIZE; ++block_index) {
            uint8_t value = 0u;

            if (offset <= 0x014fu && offset < rom_size) {
                value = rom_data[offset];
            }

            checksum = (uint8_t)(checksum + value);
            stream_popcount += cupid_gb_popcount8(value);
            offset += 1u;
        }

        stream_popcount += cupid_gb_popcount8(checksum);
    }

    adjusted_counter = CUPID_GB_SGB_REFERENCE_DIV_COUNTER -
                       ((int)stream_popcount - CUPID_GB_SGB_REFERENCE_STREAM_POPCOUNT);

    while (adjusted_counter < 0) {
        adjusted_counter += 64;
    }

    return (uint16_t)(adjusted_counter & 0x3f);
}

/**
 * @brief Converts a ROM size code from the cartridge header to a bank count.
 *
 * @param code The ROM size code read from header offset 0x0148.
 *
 * @return The number of 16 KiB ROM banks, or 0 for unrecognised codes.
 */
static size_t cupid_gb_rom_bank_count_from_code(uint8_t code)
{
    switch (code) {
    case 0x00u:
        return 2u;
    case 0x01u:
        return 4u;
    case 0x02u:
        return 8u;
    case 0x03u:
        return 16u;
    case 0x04u:
        return 32u;
    case 0x05u:
        return 64u;
    case 0x06u:
        return 128u;
    case 0x07u:
        return 256u;
    case 0x08u:
        return 512u;
    case 0x52u:
        return 72u;
    case 0x53u:
        return 80u;
    case 0x54u:
        return 96u;
    default:
        return 0u;
    }
}

/**
 * @brief Converts a RAM size code from the cartridge header to a bank count.
 *
 * @param code The RAM size code read from header offset 0x0149.
 *
 * @return The number of 8 KiB RAM banks, or 0 for unrecognised codes.
 */
static size_t cupid_gb_ram_bank_count_from_code(uint8_t code)
{
    switch (code) {
    case 0x00u:
        return 0u;
    case 0x01u:
    case 0x02u:
        return 1u;
    case 0x03u:
        return 4u;
    case 0x04u:
        return 16u;
    case 0x05u:
        return 8u;
    default:
        return 0u;
    }
}

/**
 * @brief Identifies the MBC type from the cartridge type byte.
 *
 * @param cartridge_type The cartridge type byte read from header offset 0x0147.
 *
 * @return The corresponding @ref CupidGbMbcType enum value, or
 *         @ref CUPID_GB_MBC_UNKNOWN for unrecognised values.
 */
static CupidGbMbcType cupid_gb_detect_mbc_type(uint8_t cartridge_type)
{
    switch (cartridge_type) {
    case 0x00u:
    case 0x08u: /* ROM+RAM */
    case 0x09u: /* ROM+RAM+BATTERY */
        return CUPID_GB_MBC_NONE;
    case 0x01u:
    case 0x02u:
    case 0x03u:
        return CUPID_GB_MBC1;
    case 0x05u:
    case 0x06u:
        return CUPID_GB_MBC2;
    case 0x0bu:
    case 0x0cu:
    case 0x0du:
        return CUPID_GB_MMM01;
    case 0x0fu:
    case 0x10u:
    case 0x11u:
    case 0x12u:
    case 0x13u:
        return CUPID_GB_MBC3;
    case 0x19u:
    case 0x1au:
    case 0x1bu:
    case 0x1cu:
    case 0x1du:
    case 0x1eu:
        return CUPID_GB_MBC5;
    case 0x20u:
        return CUPID_GB_MBC6;
    case 0x22u:
        return CUPID_GB_MBC7;
    case 0xfeu:
        return CUPID_GB_HUC3;
    case 0xffu:
        return CUPID_GB_HUC1;
    default:
        return CUPID_GB_MBC_UNKNOWN;
    }
}

/**
 * @brief Computes the header checksum for a ROM image at a given base offset.
 *
 * Implements the standard Game Boy header checksum algorithm over bytes
 * 0x0134–0x014C, relative to @p base_offset.
 *
 * @param rom_data    Pointer to the ROM data buffer.
 * @param rom_size    Size of the ROM data in bytes.
 * @param base_offset Byte offset of the start of the ROM header within
 *                    @p rom_data (0 for the primary header).
 *
 * @return The computed 8-bit checksum, or 0 if @p rom_data is NULL or the
 *         ROM is too small to contain the checksum byte.
 */
static uint8_t cupid_gb_compute_header_checksum_at(const uint8_t *rom_data,
                                                   size_t rom_size,
                                                   size_t base_offset)
{
    size_t index;
    uint8_t checksum = 0u;

    if (rom_data == 0 || base_offset + CUPID_GB_HEADER_CHECKSUM >= rom_size) {
        return 0u;
    }

    for (index = base_offset + CUPID_GB_HEADER_START;
         index <= base_offset + CUPID_GB_HEADER_END;
         ++index) {
        checksum = (uint8_t)(checksum - rom_data[index] - 1u);
    }

    return checksum;
}

/**
 * @brief Computes the header checksum for the primary ROM header.
 *
 * Convenience wrapper around @ref cupid_gb_compute_header_checksum_at
 * with base_offset of 0.
 *
 * @param rom_data Pointer to the ROM data buffer.
 *
 * @return The computed 8-bit header checksum.
 */
static uint8_t cupid_gb_compute_header_checksum(const uint8_t *rom_data)
{
    return cupid_gb_compute_header_checksum_at(rom_data,
                                               CUPID_GB_MAX_ROM_SIZE,
                                               0u);
}

/**
 * @brief Checks whether a ROM image contains the Nintendo logo bitmap.
 *
 * Compares the 48-byte logo region at 0x0104–0x0133 (relative to
 * @p base_offset) against the official reference bitmap.
 *
 * @param rom_data    Pointer to the ROM data buffer.
 * @param rom_size    Size of the ROM data in bytes.
 * @param base_offset Byte offset of the ROM header within @p rom_data.
 *
 * @return `true` if the logo matches exactly, `false` otherwise.
 */
static bool cupid_gb_has_nintendo_logo(const uint8_t *rom_data,
                                       size_t rom_size,
                                       size_t base_offset)
{
    size_t logo_offset = base_offset + CUPID_GB_LOGO_START;

    if (rom_data == 0 || logo_offset + sizeof(cupid_gb_nintendo_logo) > rom_size) {
        return false;
    }

    return memcmp(&rom_data[logo_offset],
                  cupid_gb_nintendo_logo,
                  sizeof(cupid_gb_nintendo_logo)) == 0;
}

/**
 * @brief Detects whether a MBC1 cartridge uses a multicart banking layout.
 *
 * A MBC1 multicart partitions its 64-bank ROM into four 16-bank sub-ROMs,
 * each with its own valid Nintendo logo and header checksum at a 256 KiB
 * boundary. All four sub-ROM headers must pass validation for detection
 * to succeed.
 *
 * @param rom_data Pointer to the ROM data buffer.
 * @param rom_size Size of the ROM data in bytes.
 * @param header   Pointer to the already-parsed cartridge header.
 *
 * @return `true` if a MBC1 multicart layout is detected, `false` otherwise.
 *
 * @note Returns `false` if @p rom_data or @p header is NULL, if the MBC
 *       type is not MBC1, or if the ROM does not have exactly 64 banks.
 */
static bool cupid_gb_detect_mbc1_multicart(const uint8_t *rom_data,
                                           size_t rom_size,
                                           const CupidGbCartridgeHeader *header)
{
    size_t index;

    if (rom_data == 0 || header == 0) {
        return false;
    }

    if (header->mbc_type != CUPID_GB_MBC1 || header->rom_bank_count != 64u) {
        return false;
    }

    for (index = 0u; index < 4u; ++index) {
        size_t base_offset = index * 0x40000u;

        if (base_offset + CUPID_GB_HEADER_CHECKSUM >= rom_size) {
            return false;
        }

        if (!cupid_gb_has_nintendo_logo(rom_data, rom_size, base_offset)) {
            return false;
        }

        if (cupid_gb_compute_header_checksum_at(rom_data, rom_size, base_offset) !=
            rom_data[base_offset + CUPID_GB_HEADER_CHECKSUM]) {
            return false;
        }
    }

    return true;
}

// MBC bank helpers (used by memory map in gb.c)

/**
 * @brief Returns the effective ROM bank number for a given address region.
 *
 * Computes the physical 16 KiB ROM bank that should be mapped into the
 * requested address region, accounting for MBC type, banking mode, and
 * multicart layout. The result is always wrapped modulo the available
 * bank count.
 *
 * @param gb           Pointer to the Game Boy state.
 * @param lower_region `true` for the fixed lower region (0x0000–0x3FFF),
 *                     `false` for the switchable upper region (0x4000–0x7FFF).
 *
 * @return The effective ROM bank index, or 0 if no banks are present.
 */
size_t cupid_gb_effective_rom_bank(const CupidGb *gb, bool lower_region)
{
    size_t bank;

    switch (gb->header.mbc_type) {
    case CUPID_GB_MBC1:
    case CUPID_GB_HUC1: {
        size_t high = (size_t)(gb->mbc1_bank_high2 & 0x03u);
        size_t low = (size_t)(gb->mbc1_bank_low5 & 0x1fu);

        if (gb->mbc1_multicart) {
            if (lower_region) {
                bank = gb->mbc1_ram_banking_mode ? (high << 4u) : 0u;
            } else {
                bank = (high << 4u) | (low & 0x0fu);
            }
            break;
        }

        if (low == 0u) {
            low = 1u;
        }

        if (lower_region) {
            bank = gb->mbc1_ram_banking_mode ? (high << 5u) : 0u;
        } else {
            bank = low | (high << 5u);
        }
        break;
    }
    case CUPID_GB_MBC2: {
        size_t b = gb->current_rom_bank & 0x0fu;
        if (b == 0u) { b = 1u; }
        bank = lower_region ? 0u : b;
        break;
    }
    case CUPID_GB_MBC5: {
        bank = lower_region ? 0u : (size_t)(gb->mbc5_rom_bank & 0x01ffu);
        break;
    }
    case CUPID_GB_MMM01: {
        if (!gb->mmm01_locked) {
            bank = lower_region ? 0u : gb->current_rom_bank;
        } else {
            size_t b = gb->current_rom_bank;
            if (b == 0u) { b = 1u; }
            bank = lower_region ? (size_t)gb->mmm01_bank_base
                                : ((size_t)gb->mmm01_bank_base + b);
        }
        break;
    }
    case CUPID_GB_MBC_NONE:
        bank = lower_region ? 0u : 1u;
        break;
    default:
        /* MBC3, MBC6, MBC7, HuC3, unknown */
        bank = lower_region ? 0u : gb->current_rom_bank;
        break;
    }

    if (gb->rom_bank_count == 0u) {
        return 0u;
    }

    return bank % gb->rom_bank_count;
}

/**
 * @brief Returns the effective cartridge RAM bank number.
 *
 * Computes the physical 8 KiB RAM bank that should be mapped into the
 * cartridge RAM window (0xA000–0xBFFF), accounting for MBC type and
 * banking mode. The result is always wrapped modulo the available bank
 * count.
 *
 * @param gb Pointer to the Game Boy state.
 *
 * @return The effective RAM bank index, or 0 if no RAM banks are present.
 */
size_t cupid_gb_effective_ram_bank(const CupidGb *gb)
{
    if (gb->ram_bank_count == 0u) {
        return 0u;
    }

    if (gb->header.mbc_type == CUPID_GB_MBC1 ||
        gb->header.mbc_type == CUPID_GB_HUC1) {
        if (gb->mbc1_ram_banking_mode) {
            return (size_t)(gb->mbc1_bank_high2 & 0x03u) % gb->ram_bank_count;
        }

        return 0u;
    }

    return gb->current_ram_bank % gb->ram_bank_count;
}

// Header parsing

/**
 * @brief Parses the cartridge header from a ROM image.
 *
 * Extracts title, CGB/SGB flags, cartridge type, ROM/RAM size codes,
 * MBC type, battery/timer/rumble capability flags, and validates the
 * header checksum.
 *
 * @param rom_data Pointer to the ROM data buffer.
 * @param rom_size Size of the ROM data in bytes.
 * @param header   Output pointer to a @ref CupidGbCartridgeHeader struct
 *                 to populate.
 *
 * @return `true` on success, `false` if any pointer is NULL or the ROM is
 *         too small to contain the full header.
 */
bool cupid_gb_parse_header(const uint8_t *rom_data,
                           size_t rom_size,
                           CupidGbCartridgeHeader *header)
{
    size_t rom_index;
    size_t title_index = 0u;

    if (rom_data == 0 || header == 0 || rom_size <= CUPID_GB_HEADER_CHECKSUM) {
        return false;
    }

    memset(header, 0, sizeof(*header));

    for (rom_index = CUPID_GB_TITLE_START; rom_index <= CUPID_GB_TITLE_END; ++rom_index) {
        uint8_t value = rom_data[rom_index];

        if (value == 0u) {
            break;
        }

        header->title[title_index] = (char)value;
        ++title_index;
    }

    header->title[title_index] = '\0';
    header->cgb_flag = rom_data[CUPID_GB_CGB_FLAG];
    header->new_licensee_code[0] = rom_data[CUPID_GB_NEW_LICENSEE_0];
    header->new_licensee_code[1] = rom_data[CUPID_GB_NEW_LICENSEE_1];
    header->sgb_flag = rom_data[CUPID_GB_SGB_FLAG];
    header->cartridge_type = rom_data[CUPID_GB_CARTRIDGE_TYPE];
    header->rom_size_code = rom_data[CUPID_GB_ROM_SIZE];
    header->ram_size_code = rom_data[CUPID_GB_RAM_SIZE];
    header->destination_code = rom_data[CUPID_GB_DESTINATION_CODE];
    header->old_licensee_code = rom_data[CUPID_GB_OLD_LICENSEE_CODE];
    header->header_checksum = rom_data[CUPID_GB_HEADER_CHECKSUM];
    header->rom_bank_count = cupid_gb_rom_bank_count_from_code(header->rom_size_code);
    header->ram_bank_count = cupid_gb_ram_bank_count_from_code(header->ram_size_code);
    header->mbc_type = cupid_gb_detect_mbc_type(header->cartridge_type);
    header->header_checksum_valid =
        cupid_gb_compute_header_checksum(rom_data) == header->header_checksum;

    /* Detect battery-backed SRAM */
    switch (header->cartridge_type) {
    case 0x03u: /* MBC1+RAM+BATTERY */
    case 0x06u: /* MBC2+BATTERY */
    case 0x09u: /* ROM+RAM+BATTERY */
    case 0x0du: /* MMM01+RAM+BATTERY */
    case 0x0fu: /* MBC3+TIMER+BATTERY */
    case 0x10u: /* MBC3+TIMER+RAM+BATTERY */
    case 0x13u: /* MBC3+RAM+BATTERY */
    case 0x1bu: /* MBC5+RAM+BATTERY */
    case 0x1eu: /* MBC5+RUMBLE+RAM+BATTERY */
    case 0x22u: /* MBC7+SENSOR+RUMBLE+RAM+BATTERY */
    case 0xfeu: /* HuC3 (battery-backed RTC/RAM) */
    case 0xffu: /* HuC1+RAM+BATTERY */
        header->has_battery = true;
        break;
    default:
        header->has_battery = false;
        break;
    }

    /* Detect MBC3 real-time clock */
    switch (header->cartridge_type) {
    case 0x0fu: /* MBC3+TIMER+BATTERY */
    case 0x10u: /* MBC3+TIMER+RAM+BATTERY */
        header->has_timer = true;
        break;
    default:
        header->has_timer = false;
        break;
    }

    /* Detect rumble motor */
    switch (header->cartridge_type) {
    case 0x1cu: /* MBC5+RUMBLE */
    case 0x1du: /* MBC5+RUMBLE+RAM */
    case 0x1eu: /* MBC5+RUMBLE+RAM+BATTERY */
    case 0x22u: /* MBC7+SENSOR+RUMBLE+RAM+BATTERY */
        header->has_rumble = true;
        break;
    default:
        header->has_rumble = false;
        break;
    }

    return true;
}

// ROM loading

/**
 * @brief Loads a Game Boy ROM from a file path.
 *
 * Opens the file at @p path, reads its entire contents into a temporary
 * buffer, and delegates to @ref cupid_gb_load_rom. The buffer is freed
 * before returning.
 *
 * @param gb   Pointer to an initialized @ref CupidGb instance.
 * @param path Null-terminated path to the ROM file.
 *
 * @return `true` if the ROM was read and loaded successfully,
 *         `false` on any I/O or parse error.
 *
 * @note Returns `false` if @p gb or @p path is NULL.
 */
bool cupid_gb_load_rom_file(CupidGb *gb, const char *path)
{
    FILE *file;
    long file_size;
    size_t bytes_read;
    uint8_t *buffer;
    bool loaded;

    if (gb == 0 || path == 0) {
        return false;
    }

    file = fopen(path, "rb");
    if (file == 0) {
        cupid_log_errorf("Failed to open ROM file: %s", path);
        return false;
    }

    if (fseek(file, 0L, SEEK_END) != 0) {
        fclose(file);
        cupid_log_error("Failed to seek ROM file.");
        return false;
    }

    file_size = ftell(file);
    if (file_size <= 0L || (size_t)file_size > CUPID_GB_MAX_ROM_SIZE) {
        fclose(file);
        cupid_log_error("ROM file size is invalid or too large.");
        return false;
    }

    if (fseek(file, 0L, SEEK_SET) != 0) {
        fclose(file);
        cupid_log_error("Failed to rewind ROM file.");
        return false;
    }

    buffer = (uint8_t *)malloc((size_t)file_size);
    if (buffer == 0) {
        fclose(file);
        cupid_log_error("Failed to allocate ROM buffer.");
        return false;
    }

    bytes_read = fread(buffer, 1u, (size_t)file_size, file);
    fclose(file);

    if (bytes_read != (size_t)file_size) {
        free(buffer);
        cupid_log_error("Failed to read the full ROM file.");
        return false;
    }

    loaded = cupid_gb_load_rom(gb, buffer, (size_t)file_size);
    free(buffer);
    return loaded;
}

/**
 * @brief Loads a Game Boy ROM from a memory buffer.
 *
 * Copies the ROM into the emulator, parses the cartridge header, sets
 * CGB/SGB mode flags, initialises all MBC state, and optionally applies
 * compatibility palettes. Must be called on an already-initialized
 * @ref CupidGb instance.
 *
 * @param gb       Pointer to an initialized @ref CupidGb instance.
 * @param rom_data Pointer to the ROM data buffer.
 * @param rom_size Size of the ROM data in bytes.
 *
 * @return `true` if the ROM was loaded and validated successfully,
 *         `false` on any validation or size error.
 *
 * @note Returns `false` if @p gb or @p rom_data is NULL.
 * @note An invalid header checksum is non-fatal; a warning is logged and
 *       loading continues.
 */
bool cupid_gb_load_rom(CupidGb *gb, const uint8_t *rom_data, size_t rom_size)
{
    if (gb == 0 || rom_data == 0) {
        return false;
    }

    if (rom_size < 0x0150u || rom_size > CUPID_GB_MAX_ROM_SIZE) {
        cupid_log_error("Game Boy ROM size is out of supported range.");
        return false;
    }

    cupid_gb_init(gb);
    if (gb->rom == 0) {
        return false;
    }
    memcpy(gb->rom, rom_data, rom_size);
    gb->rom_size = rom_size;

    if (!cupid_gb_parse_header(rom_data, rom_size, &gb->header)) {
        cupid_log_error("Failed to parse Game Boy cartridge header.");
        return false;
    }

    /* 0xC0 cartridges require CGB mode. 0x80 cartridges enter CGB mode only
     * when the user selected the CGB hardware profile. */
    gb->cgb_mode = (gb->header.cgb_flag & 0xc0u) == 0xc0u ||
                   ((gb->header.cgb_flag & 0x80u) != 0u && gb->model == CUPID_GB_MODEL_CGB);
    gb->sgb.enabled = (gb->model == CUPID_GB_MODEL_SGB || gb->model == CUPID_GB_MODEL_SGB2) &&
                      gb->header.sgb_flag == 0x03u;
    gb->double_speed = false;
    gb->speed_switch_armed = false;
    gb->speed_phase = false;
    gb->io_registers[0x4du] = 0x00u;
    gb->io_registers[0x4fu] = (uint8_t)(0xfeu | (gb->cgb_vram_bank & 0x01u));
    gb->io_registers[0x70u] = (uint8_t)(0xf8u | gb->cgb_wram_bank);
    if (gb->model == CUPID_GB_MODEL_SGB || gb->model == CUPID_GB_MODEL_SGB2) {
        /* The SGB boot ROM spends ROM-dependent time transmitting header
         * packets before it jumps to 0x0100. In HLE mode, approximate the
         * resulting DIV phase from that packet stream so timing tests such as
         * boot_div-S and boot_div2-S land on the same post-boot phase as the
         * real boot ROM. */
        gb->div_counter = cupid_gb_sgb_boot_div_counter(rom_data, rom_size);
    }
    if (gb->cgb_mode) {
        gb->cpu.a = 0x11u;
        gb->io_registers[0x4du] = 0x00u;
    } else if (gb->model == CUPID_GB_MODEL_CGB) {
        cupid_cgb_apply_compatibility_palette(gb);
    } else if (gb->sgb.enabled) {
        cupid_gb_sgb_apply_compatibility_palette(gb);
    }

    if (gb->header.rom_bank_count == 0u) {
        cupid_log_error("Unsupported Game Boy ROM size code.");
        return false;
    }

    if (gb->header.mbc_type == CUPID_GB_MBC_UNKNOWN) {
        cupid_log_errorf("Unrecognised cartridge type 0x%02X; attempting ROM-only mode.",
                         gb->header.cartridge_type);
        gb->header.mbc_type = CUPID_GB_MBC_NONE;
    }

    if (rom_size < gb->header.rom_bank_count * 0x4000u) {
        cupid_log_error("ROM file is smaller than the header-declared bank count.");
        return false;
    }

    gb->cartridge_ram_size = cupid_gb_ram_size_from_code(gb->header.ram_size_code);
    if (gb->cartridge_ram_size > CUPID_GB_MAX_RAM_SIZE) {
        cupid_log_error("Game Boy cartridge RAM size exceeds emulator limits.");
        return false;
    }

    gb->rom_bank_count = gb->header.rom_bank_count;
    gb->ram_bank_count = gb->header.ram_bank_count;
    gb->current_rom_bank = gb->rom_bank_count > 1u ? 1u : 0u;
    gb->current_ram_bank = 0u;
    gb->mbc1_bank_low5 = 1u;
    gb->mbc1_bank_high2 = 0u;
    gb->mbc1_multicart = cupid_gb_detect_mbc1_multicart(rom_data, rom_size, &gb->header);
    gb->ram_enabled = gb->header.mbc_type == CUPID_GB_MBC_NONE;
    gb->mbc1_ram_banking_mode = false;
    gb->mbc5_rom_bank = 1u;
    gb->rtc_register = 0u;
    gb->rtc.base_time = 0;
    gb->mbc7_sensor_x = 0x81d0u;
    gb->mbc7_sensor_y = 0x81d0u;
    gb->mbc7_latch = false;
    gb->mmm01_locked = false;
    gb->mmm01_bank_base = 0u;
    gb->huc1_ir_mode = false;
    gb->huc3_mode = 0u;
    gb->huc3_value = 0u;

    /* MBC2 has 512x4-bit built-in RAM regardless of header RAM size */
    if (gb->header.mbc_type == CUPID_GB_MBC2) {
        gb->cartridge_ram_size = 512u;
        gb->ram_bank_count = 1u;
    }
    /* MBC7 uses cartridge RAM region for EEPROM mapping */
    if (gb->header.mbc_type == CUPID_GB_MBC7) {
        if (gb->cartridge_ram_size < 256u) {
            gb->cartridge_ram_size = 256u;
        }
        if (gb->ram_bank_count == 0u) {
            gb->ram_bank_count = 1u;
        }
    }

    gb->loaded = true;

    cupid_log_infof("Loaded Game Boy ROM: %s",
                    gb->header.title[0] != '\0' ? gb->header.title : "<untitled>");
    if (gb->mbc1_multicart) {
        cupid_log_info("Detected MBC1 multicart banking layout.");
    }
    if (!gb->header.header_checksum_valid) {
        cupid_log_error("ROM header checksum is invalid; continuing for development purposes.");
    }

    return true;
}

// Name helpers

/**
 * @brief Returns the human-readable name for a cartridge type byte.
 *
 * @param cartridge_type The cartridge type byte from header offset 0x0147.
 *
 * @return A null-terminated string such as `"MBC1+RAM+BATTERY"`, or
 *         `"UNKNOWN"` for unrecognised values.
 *
 * @note The returned string is a literal and must not be modified or freed.
 */
const char *cupid_gb_cartridge_type_name(uint8_t cartridge_type)
{
    switch (cartridge_type) {
    case 0x00u: return "ROM ONLY";
    case 0x01u: return "MBC1";
    case 0x02u: return "MBC1+RAM";
    case 0x03u: return "MBC1+RAM+BATTERY";
    case 0x05u: return "MBC2";
    case 0x06u: return "MBC2+BATTERY";
    case 0x08u: return "ROM+RAM";
    case 0x09u: return "ROM+RAM+BATTERY";
    case 0x0bu: return "MMM01";
    case 0x0cu: return "MMM01+RAM";
    case 0x0du: return "MMM01+RAM+BATTERY";
    case 0x0fu: return "MBC3+TIMER+BATTERY";
    case 0x10u: return "MBC3+TIMER+RAM+BATTERY";
    case 0x11u: return "MBC3";
    case 0x12u: return "MBC3+RAM";
    case 0x13u: return "MBC3+RAM+BATTERY";
    case 0x19u: return "MBC5";
    case 0x1au: return "MBC5+RAM";
    case 0x1bu: return "MBC5+RAM+BATTERY";
    case 0x1cu: return "MBC5+RUMBLE";
    case 0x1du: return "MBC5+RUMBLE+RAM";
    case 0x1eu: return "MBC5+RUMBLE+RAM+BATTERY";
    case 0x20u: return "MBC6";
    case 0x22u: return "MBC7+SENSOR+RUMBLE+RAM+BATTERY";
    case 0xfeu: return "HuC3";
    case 0xffu: return "HuC1+RAM+BATTERY";
    default:    return "UNKNOWN";
    }
}

/**
 * @brief Returns the human-readable name for an MBC type.
 *
 * @param mbc_type The @ref CupidGbMbcType enum value to look up.
 *
 * @return A null-terminated string such as `"MBC5"`, or `"UNKNOWN"` for
 *         unrecognised values.
 *
 * @note The returned string is a literal and must not be modified or freed.
 */
const char *cupid_gb_mbc_name(CupidGbMbcType mbc_type)
{
    switch (mbc_type) {
    case CUPID_GB_MBC_NONE: return "ROM ONLY";
    case CUPID_GB_MBC1:     return "MBC1";
    case CUPID_GB_MBC2:     return "MBC2";
    case CUPID_GB_MBC3:     return "MBC3";
    case CUPID_GB_MBC5:     return "MBC5";
    case CUPID_GB_MBC6:     return "MBC6";
    case CUPID_GB_MBC7:     return "MBC7";
    case CUPID_GB_MMM01:    return "MMM01";
    case CUPID_GB_HUC1:     return "HuC1";
    case CUPID_GB_HUC3:     return "HuC3";
    default:                return "UNKNOWN";
    }
}

// Save RAM (.sav) helpers

/**
 * @brief Derives and stores the save file path from a ROM file path.
 *
 * Copies @p rom_path into the emulator's `save_path` field and replaces
 * the file extension with `.sav`. If the resulting path would exceed the
 * internal buffer, `save_path` is set to an empty string and save I/O is
 * silently disabled.
 *
 * @param gb       Pointer to the @ref CupidGb instance.
 * @param rom_path Null-terminated path to the ROM file.
 *
 * @note Does nothing if @p gb or @p rom_path is NULL.
 */
void cupid_gb_set_save_path(CupidGb *gb, const char *rom_path)
{
    size_t len;

    if (gb == 0 || rom_path == 0) {
        return;
    }

    len = strlen(rom_path);
    if (len >= sizeof(gb->save_path) - 1u) {
        gb->save_path[0] = '\0';
        return;
    }

    memcpy(gb->save_path, rom_path, len + 1u);

    /* Replace the last extension (.gb, .gbc) with .sav */
    {
        char *dot = strrchr(gb->save_path, '.');
        if (dot != 0 && (size_t)(dot - gb->save_path) + 4u < sizeof(gb->save_path)) {
            dot[1] = 's';
            dot[2] = 'a';
            dot[3] = 'v';
            dot[4] = '\0';
        }
    }
}

/**
 * @brief Loads cartridge RAM contents from the save file.
 *
 * Reads up to `cartridge_ram_size` bytes from the `.sav` file indicated
 * by `gb->save_path` into `gb->cartridge_ram`. A partial read is accepted
 * with a warning. Missing save files are silently ignored.
 *
 * @param gb Pointer to the @ref CupidGb instance.
 *
 * @note Does nothing if @p gb is NULL, `save_path` is empty, the
 *       cartridge has no battery, RAM size is zero, the header checksum
 *       is invalid, or the title is blank.
 */
void cupid_gb_load_save(CupidGb *gb)
{
    FILE *file;
    size_t bytes_read;

    if (gb == 0 || gb->save_path[0] == '\0') {
        return;
    }

    if (!gb->header.has_battery || gb->cartridge_ram_size == 0u) {
        return;
    }

    /*
     * Many hardware test ROMs advertise battery-backed RAM for result
     * reporting, but leave the title blank and/or ship with an invalid
     * header checksum. Persisting their cartridge RAM can feed stale test
     * output back into later runs and produce misleading failures.
     */
    if (!gb->header.header_checksum_valid || gb->header.title[0] == '\0') {
        return;
    }

    file = fopen(gb->save_path, "rb");
    if (file == 0) {
        return; /* No save file yet - not an error */
    }

    bytes_read = fread(gb->cartridge_ram, 1u, gb->cartridge_ram_size, file);
    fclose(file);

    if (bytes_read == gb->cartridge_ram_size) {
        cupid_log_infof("Loaded save data from %s (%zu bytes).",
                        gb->save_path, bytes_read);
    } else {
        cupid_log_errorf("Save file %s is %zu bytes; expected %zu. Loaded partial data.",
                         gb->save_path, bytes_read, gb->cartridge_ram_size);
    }
}

/**
 * @brief Writes cartridge RAM contents to the save file.
 *
 * Writes `cartridge_ram_size` bytes from `gb->cartridge_ram` to the
 * `.sav` file indicated by `gb->save_path`, creating or overwriting
 * it as needed.
 *
 * @param gb Pointer to the @ref CupidGb instance.
 *
 * @note Does nothing if @p gb is NULL, `save_path` is empty, the
 *       cartridge has no battery, RAM size is zero, the header checksum
 *       is invalid, or the title is blank.
 */
void cupid_gb_save(CupidGb *gb)
{
    FILE *file;

    if (gb == 0 || gb->save_path[0] == '\0') {
        return;
    }

    if (!gb->header.has_battery || gb->cartridge_ram_size == 0u) {
        return;
    }

    if (!gb->header.header_checksum_valid || gb->header.title[0] == '\0') {
        return;
    }

    file = fopen(gb->save_path, "wb");
    if (file == 0) {
        cupid_log_errorf("Could not write save file: %s", gb->save_path);
        return;
    }

    fwrite(gb->cartridge_ram, 1u, gb->cartridge_ram_size, file);
    fclose(file);
    cupid_log_infof("Saved game data to %s (%zu bytes).",
                    gb->save_path, gb->cartridge_ram_size);
}
