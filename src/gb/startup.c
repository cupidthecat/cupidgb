#include "cupid/gb/startup.h"

#include "startup_internal.h"

#include <stddef.h>
#include <string.h>
#include <time.h>

#define STARTUP_NOISE_STRIDE UINT64_C(0x9e3779b97f4a7c15)

typedef struct StartupNoise {
    uint64_t counter;
    uint64_t bytes;
    unsigned available;
} StartupNoise;

static uint64_t mix_word(uint64_t word) {
    word = (word ^ (word >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    word = (word ^ (word >> 27)) * UINT64_C(0x94d049bb133111eb);
    return word ^ (word >> 31);
}

static uint8_t next_byte(StartupNoise *noise) {
    if (!noise->available) {
        noise->counter += STARTUP_NOISE_STRIDE;
        noise->bytes = mix_word(noise->counter);
        noise->available = 8;
    }
    const uint8_t result = (uint8_t)noise->bytes;
    noise->bytes >>= 8;
    --noise->available;
    return result;
}

static uint8_t biased_byte(StartupNoise *noise, unsigned draws, bool high) {
    uint8_t result = next_byte(noise);
    while (--draws) {
        const uint8_t other = next_byte(noise);
        result = high ? (uint8_t)(result | other) : (uint8_t)(result & other);
    }
    return result;
}

static uint64_t fresh_seed(const CupidGb *gb) {
    struct timespec now = {0};
    if (timespec_get(&now, TIME_UTC) != TIME_UTC)
        now.tv_sec = time(NULL);
    return mix_word((uint64_t)now.tv_sec ^ ((uint64_t)now.tv_nsec << 32) ^ (uint64_t)(uintptr_t)gb ^
                    (uint64_t)clock());
}

static StartupNoise fill_memory(CupidGb *gb) {
    StartupNoise noise = {gb->power_on.generation_seed, 0, 0};
    const bool color = gb->model == CUPID_GB_MODEL_CGB;
    const bool pocket = gb->model == CUPID_GB_MODEL_MGB;
    const bool sgb2 = gb->model == CUPID_GB_MODEL_SGB2;
    const size_t work_bytes = color ? sizeof(gb->work_ram) : 0x2000u;

    memset(gb->video_ram, 0, sizeof(gb->video_ram));
    memset(gb->work_ram + work_bytes, 0, sizeof(gb->work_ram) - work_bytes);
    for (size_t address = 0; address < work_bytes; ++address) {
        if (color || pocket)
            gb->work_ram[address] = next_byte(&noise);
        else if (sgb2)
            gb->work_ram[address] = (uint8_t)(0x55u ^ biased_byte(&noise, 3, false));
        else
            gb->work_ram[address] = biased_byte(&noise, 2, (address & 0x100u) == 0);
    }
    for (size_t address = 0; address < sizeof(gb->high_ram); ++address)
        gb->high_ram[address] =
            color ? next_byte(&noise) : biased_byte(&noise, 3, (address & 1u) != 0);

    if (color)
        memset(gb->object_attribute_memory, 0, sizeof(gb->object_attribute_memory));
    else
        for (size_t address = 0; address < sizeof(gb->object_attribute_memory); ++address)
            gb->object_attribute_memory[address] = biased_byte(&noise, 3, (address & 2u) == 0);
    return noise;
}

static StartupNoise noise_after_memory(const CupidGb *gb) {
    const bool color = gb->model == CUPID_GB_MODEL_CGB;
    const bool pocket = gb->model == CUPID_GB_MODEL_MGB;
    const unsigned work_draws = color || pocket ? 1 : gb->model == CUPID_GB_MODEL_SGB2 ? 3 : 2;
    const size_t work_bytes = color ? sizeof(gb->work_ram) : 0x2000u;
    const size_t bytes = work_bytes * work_draws + sizeof(gb->high_ram) * (color ? 1u : 3u) +
                         (color ? 0 : sizeof(gb->object_attribute_memory) * 3u);
    /* Re-entry reaches the same IO portion without rewriting or walking live RAM.
     * The stream counter advances once per eight bytes, independently of host endian. */
    StartupNoise noise = {gb->power_on.generation_seed + STARTUP_NOISE_STRIDE * (bytes / 8u), 0, 0};
    for (size_t remainder = bytes % 8u; remainder; --remainder)
        (void)next_byte(&noise);
    return noise;
}

static void fill_wave(CupidGb *gb, StartupNoise *noise, bool firmware) {
    const bool color = gb->model == CUPID_GB_MODEL_CGB;
    /* Monochrome power-on noise ends at FF3E; FF3F starts at zero. Color
     * firmware initializes wave RAM itself, while HLE supplies its 00/FF ramp. */
    for (unsigned address = 0; address < 16; ++address) {
        uint8_t value = 0;
        if (!color && address < 15)
            value =
                biased_byte(noise, gb->model == CUPID_GB_MODEL_MGB ? 2 : 3, (address & 1u) == 0);
        else if (color && !firmware)
            value = address & 1u ? 0xff : 0;
        gb->io_registers[0x30u + address] = value;
    }
}

void cupid_gb_startup_memory(CupidGb *gb) {
    gb->power_on.generation_seed = gb->power_on.fixed_seed ? gb->power_on.seed : fresh_seed(gb);
    StartupNoise noise = fill_memory(gb);
    fill_wave(gb, &noise, false);
    /* The HLE handoff represents firmware that already cleared physical WRAM
     * bank 2. Initial firmware execution reconstructs the raw generation first. */
    if (gb->model == CUPID_GB_MODEL_CGB)
        memset(gb->work_ram + 0x2000u, 0, 0x1000u);
}

void cupid_gb_startup_boot_io(CupidGb *gb) {
    StartupNoise noise = noise_after_memory(gb);
    fill_wave(gb, &noise, true);
    if (gb->model == CUPID_GB_MODEL_CGB) {
        for (size_t address = 0; address < sizeof(gb->cgb_bg_palette_ram); ++address) {
            gb->cgb_bg_palette_ram[address] = next_byte(&noise);
            gb->cgb_obj_palette_ram[address] = next_byte(&noise);
        }
    } else {
        memset(gb->cgb_bg_palette_ram, 0, sizeof(gb->cgb_bg_palette_ram));
        memset(gb->cgb_obj_palette_ram, 0, sizeof(gb->cgb_obj_palette_ram));
    }
}

void cupid_gb_start_boot_rom(CupidGb *gb) {
    if (!gb || !gb->boot_rom_size)
        return;
    (void)fill_memory(gb);
    cupid_gb_enter_boot_rom(gb);
}

void cupid_gb_set_power_on_seed(CupidGb *gb, uint64_t seed) {
    if (gb) {
        gb->power_on.seed = seed;
        gb->power_on.fixed_seed = true;
    }
}

void cupid_gb_clear_power_on_seed(CupidGb *gb) {
    if (gb) {
        gb->power_on.seed = 0;
        gb->power_on.fixed_seed = false;
    }
}

uint64_t cupid_gb_get_power_on_seed(const CupidGb *gb) {
    return gb ? gb->power_on.generation_seed : 0;
}
