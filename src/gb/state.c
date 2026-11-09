#include "cupid/gb/state.h"

#include "cupid/common/sha256.h"
#include "cupid/gb/apu.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

enum {
    STATE_HEADER_SIZE = 128,
    STATE_VERSION = 3,
    STATE_VERSION_V2 = 2,
    STATE_WIRE_SIZE_V2 = 300518,
    STATE_WIRE_SIZE_V3 = STATE_WIRE_SIZE_V2 + 50
};

static const uint8_t state_magic[8] = {'C', 'U', 'P', 'I', 'D', 'G', 'B', 0};

typedef struct StateIo {
    const uint8_t *input;
    uint8_t *output;
    size_t offset;
    size_t capacity;
    bool invalid;
} StateIo;

static bool reserve(StateIo *io, size_t count) {
    if (io->invalid || io->offset > io->capacity || count > io->capacity - io->offset) {
        io->invalid = true;
        return false;
    }
    return true;
}

static void bytes(StateIo *io, uint8_t *value, size_t count) {
    if (!reserve(io, count)) {
        return;
    }
    if (io->input) {
        memcpy(value, io->input + io->offset, count);
    } else if (io->output) {
        memcpy(io->output + io->offset, value, count);
    }
    io->offset += count;
}

static void byte(StateIo *io, uint8_t *value) {
    bytes(io, value, 1);
}

static void boolean(StateIo *io, bool *value) {
    uint8_t raw = io->input ? 0 : (uint8_t)*value;
    byte(io, &raw);
    if (io->input) {
        if (raw > 1) {
            io->invalid = true;
        } else {
            *value = raw != 0;
        }
    }
}

static void word(StateIo *io, uint16_t *value) {
    uint8_t raw[2] = {(uint8_t)*value, (uint8_t)(*value >> 8)};
    bytes(io, raw, sizeof(raw));
    if (io->input) {
        *value = (uint16_t)((uint16_t)raw[0] | (uint16_t)((uint16_t)raw[1] << 8));
    }
}

static void double_word(StateIo *io, uint32_t *value) {
    uint8_t raw[4];
    for (unsigned i = 0; i < 4; ++i) {
        raw[i] = (uint8_t)(*value >> (i * 8));
    }
    bytes(io, raw, sizeof(raw));
    if (io->input) {
        *value = 0;
        for (unsigned i = 0; i < 4; ++i) {
            *value |= (uint32_t)raw[i] << (i * 8);
        }
    }
}

static void quad_word(StateIo *io, uint64_t *value) {
    uint8_t raw[8];
    for (unsigned i = 0; i < 8; ++i) {
        raw[i] = (uint8_t)(*value >> (i * 8));
    }
    bytes(io, raw, sizeof(raw));
    if (io->input) {
        *value = 0;
        for (unsigned i = 0; i < 8; ++i) {
            *value |= (uint64_t)raw[i] << (i * 8);
        }
    }
}

static void signed_quad_word(StateIo *io, int64_t *value) {
    uint64_t raw = (uint64_t)*value;
    quad_word(io, &raw);
    if (io->input) {
        *value = raw <= INT64_MAX ? (int64_t)raw : -1 - (int64_t)(UINT64_MAX - raw);
    }
}

static void size_value(StateIo *io, size_t *value) {
    uint64_t raw = (uint64_t)*value;
    quad_word(io, &raw);
    if (io->input) {
        if (raw > SIZE_MAX) {
            io->invalid = true;
        } else {
            *value = (size_t)raw;
        }
    }
}

static void words(StateIo *io, uint16_t *values, size_t count) {
    /* Check the entire wire range before touching an array. Each element still has
     * an explicit byte order, including when the state buffer is unaligned. */
    if (count > SIZE_MAX / 2) {
        io->invalid = true;
        return;
    }
    const size_t byte_count = count * 2;
    if (!reserve(io, byte_count)) {
        return;
    }
    if (io->input) {
        const uint8_t *source = io->input + io->offset;
        for (size_t i = 0; i < count; ++i) {
            values[i] =
                (uint16_t)((uint16_t)source[i * 2] | (uint16_t)((uint16_t)source[i * 2 + 1] << 8));
        }
    } else if (io->output) {
        uint8_t *destination = io->output + io->offset;
        for (size_t i = 0; i < count; ++i) {
            destination[i * 2] = (uint8_t)values[i];
            destination[i * 2 + 1] = (uint8_t)(values[i] >> 8);
        }
    }
    io->offset += byte_count;
}

/* The order below is the v2 wire schema. Every scalar has an explicit width and every
 * array has an explicit element encoding. Never replace this with a structure dump:
 * host padding, pointer widths, and bool representations are not part of the format.
 * New hardware state requires a format revision and continuation tests.
 */
static void state_fields(StateIo *io, CupidGb *gb) {
#define U8(field) byte(io, &gb->field)
#define BOOL(field) boolean(io, &gb->field)
#define U16(field) word(io, &gb->field)
#define U32(field) double_word(io, &gb->field)
#define U64(field) quad_word(io, &gb->field)
#define SIZE(field) size_value(io, &gb->field)
#define BYTES(field) bytes(io, gb->field, sizeof(gb->field))
#define WORDS(field) words(io, gb->field, sizeof(gb->field) / sizeof(gb->field[0]))
    U8(cpu.a);
    U8(cpu.f);
    U8(cpu.b);
    U8(cpu.c);
    U8(cpu.d);
    U8(cpu.e);
    U8(cpu.h);
    U8(cpu.l);
    U16(cpu.sp);
    U16(cpu.pc);
    U8(cpu.ime_delay);
    BOOL(cpu.ime);
    BOOL(cpu.halt_bug);
    BOOL(cpu.halted);
    BOOL(cpu.stopped);

    BYTES(video_ram);
    BYTES(cartridge_ram);
    BYTES(work_ram);
    BYTES(object_attribute_memory);
    BYTES(io_registers);
    BYTES(high_ram);
    BYTES(frame_buffer);
    WORDS(frame_buffer_color);
    U8(interrupt_enable);
    U8(interrupt_flags);
    SIZE(current_rom_bank);
    SIZE(current_ram_bank);
    U8(mbc1_bank_low5);
    U8(mbc1_bank_high2);
    BOOL(mbc1_multicart);
    BOOL(ram_enabled);
    BOOL(mbc1_ram_banking_mode);
    U8(rtc.s);
    U8(rtc.m);
    U8(rtc.h);
    U8(rtc.dl);
    U8(rtc.dh);
    U8(rtc.ls);
    U8(rtc.lm);
    U8(rtc.lh);
    U8(rtc.ldl);
    U8(rtc.ldh);
    U8(rtc.latch_prev);
    signed_quad_word(io, &gb->rtc.base_time);
    U8(rtc_register);
    U16(mbc5_rom_bank);
    U16(mbc7_sensor_x);
    U16(mbc7_sensor_y);
    BOOL(mbc7_latch);
    BOOL(mmm01_locked);
    U8(mmm01_bank_base);
    BOOL(huc1_ir_mode);
    U8(huc3_mode);
    U8(huc3_value);
    BOOL(serial_master_clock);
    U64(elapsed_cycles);
    U8(serial_bits_remaining);
    U8(serial_tx_latch);
    U16(div_counter);
    U16(timer_counter);
    U8(tima_overflow_delay);
    BOOL(tima_reload_just_happened);
    U16(dma_source_base);
    U16(dma_restart_source_base);
    U8(dma_index);
    U8(dma_start_delay);
    U8(dma_restart_delay);
    BOOL(dma_active);
    BOOL(dma_restart_pending);
    U16(ppu_counter);
    BOOL(cgb_mode);
    U8(cgb_vram_bank);
    U8(cgb_wram_bank);
    BYTES(cgb_bg_palette_ram);
    BYTES(cgb_obj_palette_ram);
    U16(hdma_source);
    U16(hdma_destination);
    U8(hdma_blocks_remaining);
    BOOL(hdma_active);
    BOOL(double_speed);
    BOOL(speed_switch_armed);
    BOOL(speed_phase);
    U8(joypad);
    U8(window_line_counter);
    U8(ppu_lcd_warmup_lines);
    U8(stat_irq_delay);
    BOOL(ppu_lcd_startup);
    BOOL(ppu_line_boundary_hold);
    BOOL(stat_irq_line);
    BOOL(frame_ready);

    BOOL(sgb.enabled);
    U8(sgb.player_count);
    U8(sgb.current_player);
    U8(sgb.mask_mode);
    BYTES(sgb.command);
    U16(sgb.command_write_index);
    BOOL(sgb.ready_for_pulse);
    BOOL(sgb.ready_for_write);
    BOOL(sgb.ready_for_stop);
    BYTES(sgb.attribute_map);
    BYTES(sgb.attribute_files);
    WORDS(sgb.screen_palettes);
    WORDS(sgb.ram_palettes);
    BYTES(sgb.border_tiles);
    WORDS(sgb.border_map);
    WORDS(sgb.border_palettes);
    BYTES(sgb.freeze_buffer);

    U8(apu.fs_step);
    U8(apu.fs_skip_div_event);
    BOOL(apu.apu_on);
    BOOL(apu.ch1_dac);
    BOOL(apu.ch1_on);
    U8(apu.ch1_duty);
    U8(apu.ch1_duty_pos);
    U8(apu.ch1_len);
    BOOL(apu.ch1_len_en);
    U8(apu.ch1_vol);
    BOOL(apu.ch1_env_add);
    U8(apu.ch1_env_period);
    U8(apu.ch1_env_timer);
    U16(apu.ch1_freq);
    U16(apu.ch1_timer);
    BOOL(apu.ch1_sweep_en);
    U8(apu.ch1_sweep_timer);
    U8(apu.ch1_sweep_period);
    BOOL(apu.ch1_sweep_neg);
    U8(apu.ch1_sweep_shift);
    U16(apu.ch1_sweep_shadow);
    BOOL(apu.ch1_sweep_subtracted);
    BOOL(apu.ch2_dac);
    BOOL(apu.ch2_on);
    U8(apu.ch2_duty);
    U8(apu.ch2_duty_pos);
    U8(apu.ch2_len);
    BOOL(apu.ch2_len_en);
    U8(apu.ch2_vol);
    BOOL(apu.ch2_env_add);
    U8(apu.ch2_env_period);
    U8(apu.ch2_env_timer);
    U16(apu.ch2_freq);
    U16(apu.ch2_timer);
    BOOL(apu.ch3_dac);
    BOOL(apu.ch3_on);
    U16(apu.ch3_len);
    BOOL(apu.ch3_len_en);
    U8(apu.ch3_out_level);
    U16(apu.ch3_freq);
    U16(apu.ch3_active_freq);
    U16(apu.ch3_timer);
    U8(apu.ch3_wave_pos);
    U8(apu.ch3_current_byte);
    BOOL(apu.ch3_started);
    U8(apu.ch3_wave_access_ticks);
    BOOL(apu.ch3_freq_pending);
    U8(apu.ch3_sample);
    BOOL(apu.ch4_dac);
    BOOL(apu.ch4_on);
    U8(apu.ch4_len);
    BOOL(apu.ch4_len_en);
    U8(apu.ch4_vol);
    BOOL(apu.ch4_env_add);
    U8(apu.ch4_env_period);
    U8(apu.ch4_env_timer);
    U8(apu.ch4_div_code);
    U8(apu.ch4_shift);
    BOOL(apu.ch4_width7);
    U32(apu.ch4_timer);
    U16(apu.ch4_lfsr);
    U32(apu.sample_acc);
    /* Signed PCM is stored as explicit two's-complement 16-bit little-endian samples. */
    for (size_t i = 0; i < sizeof(gb->apu.buf) / sizeof(gb->apu.buf[0]); ++i) {
        uint16_t raw = (uint16_t)gb->apu.buf[i];
        word(io, &raw);
        if (io->input) {
            gb->apu.buf[i] = raw <= INT16_MAX ? (int16_t)raw : (int16_t)((int32_t)raw - 65536);
        }
    }
    U32(apu.buf_write);
    BOOL(boot_rom_enabled);
    U32(apu.sample_rate);
    signed_quad_word(io, &gb->apu.mix_left);
    signed_quad_word(io, &gb->apu.mix_right);
    signed_quad_word(io, &gb->apu.highpass_left);
    signed_quad_word(io, &gb->apu.highpass_right);
    U32(apu.highpass_rate);
    BOOL(apu.highpass_disabled);
    U16(pixels.dot);
    U8(pixels.line);
    U8(pixels.x);
    BYTES(pixels.colors);
    BYTES(pixels.palettes);
    BYTES(pixels.priorities);
    U8(pixels.read);
    U8(pixels.count);
    U8(pixels.phase);
    U8(pixels.tile);
    U8(pixels.attributes);
    U8(pixels.low);
    U8(pixels.high);
    U8(pixels.column);
    U8(pixels.discard);
    U8(pixels.delay);
    U16(pixels.address);
    BYTES(pixels.objects);
    BYTES(pixels.object_x);
    BYTES(pixels.object_y);
    U8(pixels.object_count);
    U16(pixels.fetched_objects);
    U8(pixels.object_delay);
    U8(pixels.object_index);
    U8(pixels.object_low);
    U8(pixels.object_high);
    U8(pixels.object_attributes);
    U16(pixels.object_address);
    BYTES(pixels.sprite_colors);
    BYTES(pixels.sprite_palettes);
    BYTES(pixels.sprite_flags);
    WORDS(pixels.sprite_priorities);
    BOOL(pixels.initialized);
    BOOL(pixels.active);
    BOOL(pixels.window_armed);
    BOOL(pixels.window_active);
    BOOL(pixels.window_used);
    BOOL(pixels.lyc_irq);
    BOOL(cgb_object_priority_x);
    BOOL(cpu.just_halted);
#undef U8
#undef BOOL
#undef U16
#undef U32
#undef U64
#undef SIZE
#undef BYTES
#undef WORDS
}

/* The complete v2 payload remains a prefix. New physical latches have their own
 * explicit extension, independent of C structure padding and host ABI changes. */
static void state_extension_v3(StateIo *io, CupidGb *gb) {
    boolean(io, &gb->hdma_pending);
    byte(io, &gb->hdma_hblank_delay);
    boolean(io, &gb->hdma_allow_on_wake);
    byte(io, &gb->apu.clock_phase);
    byte(io, &gb->apu.envelope_delay);
    for (unsigned channel = 0; channel < 2; ++channel) {
        CupidGbPulseTiming *timing = &gb->apu.pulse_timing[channel];
        byte(io, &timing->start_delay);
        byte(io, &timing->edge_age);
        boolean(io, &timing->clock_started);
        boolean(io, &timing->output_high);
        boolean(io, &timing->output_blocked);
        boolean(io, &timing->envelope_clock);
        boolean(io, &timing->envelope_locked);
        boolean(io, &timing->envelope_lock_pending);
    }
    CupidGbSweepTiming *sweep = &gb->apu.sweep_timing;
    byte(io, &sweep->startup_ticks);
    byte(io, &sweep->shift_ticks);
    byte(io, &sweep->restart_hold);
    word(io, &sweep->delta);
    word(io, &sweep->completed_delta);
    boolean(io, &sweep->zero_shift_pending);
    boolean(io, &sweep->zero_shift_armed);
    CupidGbNoiseTiming *noise = &gb->apu.noise_timing;
    word(io, &noise->divider_counter);
    byte(io, &noise->clock_phase);
    byte(io, &noise->remaining);
    boolean(io, &noise->foreground);
    boolean(io, &noise->background);
    boolean(io, &noise->started_dac_off);
    boolean(io, &noise->reload_edge);
    boolean(io, &noise->counter_stepped);
    boolean(io, &noise->output_high);
    boolean(io, &noise->last_shift_narrow);
    boolean(io, &noise->pre_shift_bit7);
    double_word(io, &gb->speed_switch_halt_cycles);
    byte(io, &gb->speed_switch_toggle_cycles);
    byte(io, &gb->speed_switch_freeze_cycles);
    byte(io, &gb->stop_divider_hold_cycles);
    byte(io, &gb->speed_switch_alignment);
}

static bool valid_model(CupidGbModel model) {
    return model >= CUPID_GB_MODEL_DMG_ABC && model <= CUPID_GB_MODEL_SGB2;
}

static bool ready(const CupidGb *gb) {
    return gb && gb->loaded && gb->rom && gb->rom_size >= 0x150 && gb->rom_size <= UINT32_MAX &&
           gb->cartridge_ram_size <= CUPID_GB_MAX_RAM_SIZE;
}

static bool valid_hardware_prefix(const CupidGb *gb) {
    const CupidGbApu *a = &gb->apu;
    const CupidGbPixels *p = &gb->pixels;
    const int64_t area_limit = INT64_C(16320) * a->sample_acc;
    if ((a->sample_rate && (a->sample_rate < 8000 || a->sample_rate > 192000)) ||
        a->highpass_rate > UINT32_C(1073741824) || a->mix_left < -area_limit ||
        a->mix_left > area_limit || a->mix_right < -area_limit || a->mix_right > area_limit ||
        a->highpass_left < -INT64_C(1069547520) || a->highpass_left > INT64_C(1069547520) ||
        a->highpass_right < -INT64_C(1069547520) || a->highpass_right > INT64_C(1069547520) ||
        p->dot >= 456 || p->line >= 154 || p->x > 160 || p->read >= 8 || p->count > 8 ||
        p->phase > 6 || p->column > 31 || p->discard > 7 || p->delay > 12 || p->address >= 0x4000 ||
        p->object_count > 10 || p->fetched_objects > 0x3ff || p->object_delay > 11 ||
        p->object_index >= 10 || p->object_address >= 0x3fff)
        return false;
    /* Map fetches read matching attributes from the second VRAM bank. A tile-data
     * address is valid in other fetch phases, but would cross VRAM's end here. */
    if (p->phase == 1 && (p->address < 0x1800 || p->address >= 0x2000))
        return false;
    for (unsigned i = 0; i < 8; ++i) {
        if (p->colors[i] > 3 || p->palettes[i] > 7 || p->priorities[i] > 1 ||
            p->sprite_colors[i] > 3 || p->sprite_palettes[i] > 7 || p->sprite_flags[i] > 1)
            return false;
    }
    for (unsigned i = 0; i < 10; ++i) {
        if (p->objects[i] >= 40)
            return false;
    }
    if (!valid_model(gb->model) || (gb->cpu.f & 15u) || gb->cpu.ime_delay > 1 ||
        gb->div_counter >= 64 || gb->serial_bits_remaining > 8 || gb->tima_overflow_delay > 1 ||
        gb->dma_index > 160 || gb->dma_start_delay > 2 || gb->dma_restart_delay > 2 ||
        gb->ppu_counter >= 114 || gb->ppu_lcd_warmup_lines > 2 || gb->stat_irq_delay > 1 ||
        gb->cgb_vram_bank > 1 || gb->cgb_wram_bank > 7 ||
        ((gb->cgb_mode || gb->double_speed) && gb->model != CUPID_GB_MODEL_CGB) ||
        gb->mbc1_bank_low5 > 31 || gb->mbc1_bank_high2 > 3 || gb->mbc5_rom_bank > 511 ||
        gb->current_rom_bank > 511 || gb->current_ram_bank > 255 ||
        (gb->boot_rom_enabled &&
         (!gb->boot_rom_size || gb->boot_rom_size > CUPID_GB_MAX_BOOT_ROM_SIZE))) {
        return false;
    }
    /* Bounds below protect every array index derived from restored APU/SGB state. RTC
     * register bytes and raw I/O remain unrestricted: the current bus accepts all byte values. */
    if (a->fs_step > 7 || a->fs_skip_div_event > 2 || a->ch1_duty > 3 || a->ch2_duty > 3 ||
        a->ch1_duty_pos > 7 || a->ch2_duty_pos > 7 || a->ch1_len > 64 || a->ch2_len > 64 ||
        a->ch3_len > 256 || a->ch4_len > 64 || a->ch1_vol > 15 || a->ch2_vol > 15 ||
        a->ch4_vol > 15 || a->ch1_env_period > 7 || a->ch2_env_period > 7 ||
        a->ch4_env_period > 7 || a->ch1_env_timer > 8 || a->ch2_env_timer > 8 ||
        a->ch4_env_timer > 8 || a->ch1_freq > 2047 || a->ch2_freq > 2047 || a->ch3_freq > 2047 ||
        a->ch3_active_freq > 2047 || a->ch1_sweep_shadow > 2047 || a->ch1_sweep_timer > 8 ||
        a->ch1_sweep_period > 7 || a->ch1_sweep_shift > 7 || a->ch3_out_level > 3 ||
        a->ch3_wave_pos > 31 || a->ch3_current_byte > 15 || a->ch3_wave_access_ticks > 1 ||
        a->ch3_sample > 15 || a->ch4_div_code > 7 || a->ch4_shift > 15 || a->ch4_lfsr > 0x7fff ||
        a->sample_acc >= cupid_gb_base_clock_hz(gb) || a->buf_write > CUPID_GB_APU_BUF_FRAMES ||
        (gb->sgb.player_count != 1 && gb->sgb.player_count != 2 && gb->sgb.player_count != 4) ||
        gb->sgb.current_player >= gb->sgb.player_count || gb->sgb.mask_mode > 3 ||
        gb->sgb.command_write_index > sizeof(gb->sgb.command) * 8) {
        return false;
    }
    for (size_t i = 0; i < sizeof(gb->frame_buffer); ++i) {
        if (gb->frame_buffer[i] > 3 || gb->sgb.freeze_buffer[i] > 3) {
            return false;
        }
    }
    for (size_t i = 0; i < sizeof(gb->sgb.attribute_map); ++i) {
        if (gb->sgb.attribute_map[i] > 3) {
            return false;
        }
    }
    return true;
}

static bool valid_hardware_extension(const CupidGb *gb) {
    if (gb->hdma_blocks_remaining > 128 || gb->hdma_hblank_delay > 3 || gb->apu.clock_phase > 3 ||
        gb->apu.envelope_delay > 2 || gb->apu.sweep_timing.startup_ticks > 3 ||
        gb->apu.sweep_timing.shift_ticks > 7 || gb->apu.sweep_timing.restart_hold > 8 ||
        gb->apu.sweep_timing.delta > 2047 || gb->apu.sweep_timing.completed_delta > 2047 ||
        gb->apu.noise_timing.clock_phase > 7 || gb->apu.noise_timing.remaining > 34 ||
        gb->speed_switch_halt_cycles > 131080 || gb->speed_switch_toggle_cycles > 6 ||
        gb->speed_switch_freeze_cycles > 5 || gb->stop_divider_hold_cycles > 4 ||
        gb->speed_switch_alignment > 7 || (gb->hdma_hblank_delay && !gb->hdma_active) ||
        ((gb->hdma_pending || gb->hdma_active) && !gb->hdma_blocks_remaining)) {
        return false;
    }
    for (unsigned channel = 0; channel < 2; ++channel) {
        const CupidGbPulseTiming *timing = &gb->apu.pulse_timing[channel];
        if (timing->start_delay > 12 || timing->edge_age > 4) {
            return false;
        }
    }
    return true;
}

static bool valid_hardware(const CupidGb *gb) {
    return valid_hardware_prefix(gb) && valid_hardware_extension(gb);
}

size_t cupid_gb_state_size(const CupidGb *gb) {
    if (!ready(gb)) {
        return 0;
    }
    /* Every version stores fixed-capacity fields. Encoding and loading
     * still check their final offset against this size, so schema drift fails closed. */
    return STATE_WIRE_SIZE_V3;
}

static void put_u32(uint8_t *data, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) {
        data[i] = (uint8_t)(value >> (i * 8));
    }
}

static uint32_t get_u32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static void integrity(const uint8_t *data, size_t length, uint8_t digest[32]) {
    cupid_sha256_parts(data, 96, data + STATE_HEADER_SIZE, length - STATE_HEADER_SIZE, digest);
}

static bool overlaps(const void *a, size_t a_size, const void *b, size_t b_size) {
    uintptr_t first = (uintptr_t)a, second = (uintptr_t)b;
    return first <= second ? second - first < a_size : first - second < b_size;
}

static CupidGbStateResult save_state(const CupidGb *gb, uint8_t *data, size_t capacity,
                                     size_t *written, const uint8_t *rom_digest) {
    if (written) {
        *written = 0;
    }
    if (!ready(gb)) {
        return CUPID_GB_STATE_NO_ROM;
    }
    size_t size = cupid_gb_state_size(gb);
    if (!data || capacity < size) {
        return CUPID_GB_STATE_BUFFER_TOO_SMALL;
    }
    if (overlaps(data, size, gb, sizeof(*gb)) || overlaps(data, size, gb->rom, gb->rom_size) ||
        !valid_hardware(gb)) {
        return CUPID_GB_STATE_INVALID;
    }
    memset(data, 0, STATE_HEADER_SIZE);
    memcpy(data, state_magic, sizeof(state_magic));
    put_u32(data + 8, STATE_VERSION);
    put_u32(data + 12, (uint32_t)size);
    put_u32(data + 16, (uint32_t)gb->model);
    put_u32(data + 20, (uint32_t)gb->rom_size);
    put_u32(data + 24, gb->boot_rom_enabled ? (uint32_t)gb->boot_rom_size : 0);
    put_u32(data + 28, gb->boot_rom_enabled ? 1 : 0);
    if (rom_digest) {
        memcpy(data + 32, rom_digest, 32);
    } else {
        cupid_sha256(gb->rom, gb->rom_size, data + 32);
    }
    if (gb->boot_rom_enabled) {
        cupid_sha256(gb->boot_rom, gb->boot_rom_size, data + 64);
    }
    StateIo io = {NULL, data, STATE_HEADER_SIZE, size, false};
    state_fields(&io, (CupidGb *)gb);
    state_extension_v3(&io, (CupidGb *)gb);
    if (io.invalid || io.offset != size) {
        return CUPID_GB_STATE_INVALID;
    }
    integrity(data, size, data + 96);
    if (written) {
        *written = size;
    }
    return CUPID_GB_STATE_OK;
}

static CupidGbStateResult load_state(CupidGb *gb, const uint8_t *data, size_t length,
                                     const uint8_t *rom_digest) {
    if (!ready(gb)) {
        return CUPID_GB_STATE_NO_ROM;
    }
    if (!data || length < STATE_HEADER_SIZE || memcmp(data, state_magic, sizeof(state_magic))) {
        return CUPID_GB_STATE_INVALID;
    }
    const uint32_t version = get_u32(data + 8);
    if (version != STATE_VERSION && version != STATE_VERSION_V2) {
        return CUPID_GB_STATE_UNSUPPORTED_VERSION;
    }
    const size_t wire_size = version == STATE_VERSION_V2 ? STATE_WIRE_SIZE_V2 : STATE_WIRE_SIZE_V3;
    if (length != get_u32(data + 12) || length != wire_size) {
        return CUPID_GB_STATE_INVALID;
    }
    uint8_t digest[32];
    integrity(data, length, digest);
    if (memcmp(digest, data + 96, sizeof(digest))) {
        return CUPID_GB_STATE_INVALID;
    }
    if (get_u32(data + 16) != (uint32_t)gb->model) {
        return CUPID_GB_STATE_WRONG_MODEL;
    }
    if (get_u32(data + 20) != gb->rom_size) {
        return CUPID_GB_STATE_WRONG_ROM;
    }
    if (rom_digest) {
        memcpy(digest, rom_digest, sizeof(digest));
    } else {
        cupid_sha256(gb->rom, gb->rom_size, digest);
    }
    if (memcmp(digest, data + 32, sizeof(digest))) {
        return CUPID_GB_STATE_WRONG_ROM;
    }
    uint32_t boot_length = get_u32(data + 24), flags = get_u32(data + 28);
    if (flags > 1 || (!flags && boot_length)) {
        return CUPID_GB_STATE_INVALID;
    }
    if (flags) {
        if (!boot_length || boot_length > CUPID_GB_MAX_BOOT_ROM_SIZE ||
            boot_length != gb->boot_rom_size) {
            return CUPID_GB_STATE_WRONG_BOOT_ROM;
        }
        cupid_sha256(gb->boot_rom, gb->boot_rom_size, digest);
        if (memcmp(digest, data + 64, sizeof(digest))) {
            return CUPID_GB_STATE_WRONG_BOOT_ROM;
        }
    } else {
        const uint8_t empty[32] = {0};
        if (memcmp(data + 64, empty, sizeof(empty))) {
            return CUPID_GB_STATE_INVALID;
        }
    }
    CupidGb *candidate = malloc(sizeof(*candidate));
    if (!candidate) {
        return CUPID_GB_STATE_OUT_OF_MEMORY;
    }
    /* Host resources, cartridge metadata, and immutable inputs come only from the live
     * instance. Decode into separate storage and never run bus writes while loading. */
    *candidate = *gb;
    StateIo io = {data, NULL, STATE_HEADER_SIZE, length, false};
    state_fields(&io, candidate);
    if (version == STATE_VERSION) {
        state_extension_v3(&io, candidate);
    }
    if (io.invalid || io.offset != length || candidate->boot_rom_enabled != (flags != 0) ||
        !valid_hardware_prefix(candidate)) {
        free(candidate);
        return CUPID_GB_STATE_INVALID;
    }
    if (version == STATE_VERSION_V2) {
        /* Earlier states have no pending-DMA or power-relative audio history.
         * Initialize only the new latches after validating every legacy index;
         * never inherit their values from the target's unrelated live timeline. */
        candidate->hdma_pending = false;
        candidate->hdma_hblank_delay = 0;
        candidate->hdma_allow_on_wake = false;
        candidate->speed_switch_halt_cycles = 0;
        candidate->speed_switch_toggle_cycles = 0;
        candidate->speed_switch_freeze_cycles = 0;
        candidate->stop_divider_hold_cycles = 0;
        candidate->speed_switch_alignment = 0;
        cupid_gb_apu_import_legacy(candidate);
    }
    if (!valid_hardware_extension(candidate)) {
        free(candidate);
        return CUPID_GB_STATE_INVALID;
    }
    if (candidate->model == CUPID_GB_MODEL_SGB && candidate->apu.highpass_rate) {
        /* Older SGB snapshots used the handheld clock for this derived host filter
         * coefficient. Refresh it at the same rate without dropping queued samples
         * or the partially integrated frame. Keep the zero/default rate sentinel. */
        uint32_t configured_rate = candidate->apu.sample_rate;
        cupid_gb_apu_set_sample_rate(candidate, cupid_gb_apu_get_sample_rate(candidate));
        candidate->apu.sample_rate = configured_rate;
    }
    *gb = *candidate;
    free(candidate);
    return CUPID_GB_STATE_OK;
}

CupidGbStateResult cupid_gb_save_state(const CupidGb *gb, uint8_t *data, size_t capacity,
                                       size_t *written) {
    return save_state(gb, data, capacity, written, NULL);
}

CupidGbStateResult cupid_gb_load_state(CupidGb *gb, const uint8_t *data, size_t length) {
    return load_state(gb, data, length, NULL);
}

CupidGbStateResult cupid_gb_state_context_init(CupidGbStateContext *context, const CupidGb *gb) {
    if (!context || (gb && overlaps(context, sizeof(*context), gb, sizeof(*gb))) ||
        (ready(gb) && overlaps(context, sizeof(*context), gb->rom, gb->rom_size))) {
        return CUPID_GB_STATE_INVALID;
    }
    memset(context, 0, sizeof(*context));
    if (!ready(gb)) {
        return CUPID_GB_STATE_NO_ROM;
    }
    if (!valid_model(gb->model)) {
        return CUPID_GB_STATE_INVALID;
    }
    context->owner = gb;
    context->rom = gb->rom;
    context->rom_size = gb->rom_size;
    context->model = gb->model;
    cupid_sha256(gb->rom, gb->rom_size, context->rom_digest);
    return CUPID_GB_STATE_OK;
}

static CupidGbStateResult check_context(const CupidGb *gb, const CupidGbStateContext *context) {
    if (!ready(gb)) {
        return CUPID_GB_STATE_NO_ROM;
    }
    if (!context || context->owner != gb || context->rom != gb->rom ||
        context->rom_size != gb->rom_size) {
        return CUPID_GB_STATE_WRONG_ROM;
    }
    return context->model == gb->model ? CUPID_GB_STATE_OK : CUPID_GB_STATE_WRONG_MODEL;
}

CupidGbStateResult cupid_gb_save_state_with_context(const CupidGb *gb, uint8_t *data,
                                                    size_t capacity, size_t *written,
                                                    const CupidGbStateContext *context) {
    CupidGbStateResult result = check_context(gb, context);
    if (result != CUPID_GB_STATE_OK) {
        if (written) {
            *written = 0;
        }
        return result;
    }
    if (data && overlaps(data, cupid_gb_state_size(gb), context, sizeof(*context))) {
        if (written) {
            *written = 0;
        }
        return CUPID_GB_STATE_INVALID;
    }
    return save_state(gb, data, capacity, written, context->rom_digest);
}

CupidGbStateResult cupid_gb_load_state_with_context(CupidGb *gb, const uint8_t *data, size_t length,
                                                    const CupidGbStateContext *context) {
    CupidGbStateResult result = check_context(gb, context);
    return result == CUPID_GB_STATE_OK ? load_state(gb, data, length, context->rom_digest) : result;
}

const char *cupid_gb_state_result_name(CupidGbStateResult result) {
    switch (result) {
        case CUPID_GB_STATE_OK:
            return "State operation succeeded";
        case CUPID_GB_STATE_INVALID:
            return "Invalid or damaged state";
        case CUPID_GB_STATE_UNSUPPORTED_VERSION:
            return "Unsupported state version";
        case CUPID_GB_STATE_WRONG_ROM:
            return "State belongs to a different cartridge";
        case CUPID_GB_STATE_WRONG_MODEL:
            return "State uses a different hardware model";
        case CUPID_GB_STATE_WRONG_BOOT_ROM:
            return "State requires the matching boot ROM";
        case CUPID_GB_STATE_BUFFER_TOO_SMALL:
            return "State buffer is too small";
        case CUPID_GB_STATE_OUT_OF_MEMORY:
            return "Cannot allocate state storage";
        case CUPID_GB_STATE_NO_ROM:
            return "No cartridge is loaded";
        default:
            return "Unknown state error";
    }
}
