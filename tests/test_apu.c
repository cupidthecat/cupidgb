#include "cupid/gb/apu.h"
#include "cupid/gb/timer.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void prepare(CupidGb *gb, bool cgb, bool double_speed) {
    *gb = (CupidGb){0};
    gb->model = cgb ? CUPID_GB_MODEL_CGB : CUPID_GB_MODEL_DMG_ABC;
    gb->cgb_mode = cgb;
    gb->double_speed = double_speed;
    cupid_gb_write_u8(gb, 0xff26, 0x80);
    cupid_gb_write_u8(gb, 0xff17, 0xf0);
    cupid_gb_write_u8(gb, 0xff16, 62); /* Length two. */
    cupid_gb_write_u8(gb, 0xff19, 0xc0);
    assert((cupid_gb_read_u8(gb, 0xff26) & 2) != 0);
}

static void test_divider_clock(bool double_speed) {
    CupidGb gb;
    unsigned period = double_speed ? 4096 : 2048;
    prepare(&gb, double_speed, double_speed);
    cupid_gb_tick(&gb, (uint16_t)(period - 1));
    assert(gb.apu.ch2_len == 2);
    cupid_gb_tick(&gb, 1);
    assert(gb.apu.ch2_len == 1);
    cupid_gb_tick(&gb, (uint16_t)period);
    assert(gb.apu.ch2_len == 1);
    cupid_gb_tick(&gb, (uint16_t)period);
    assert(gb.apu.ch2_len == 0);
    assert((cupid_gb_read_u8(&gb, 0xff26) & 2) == 0);
}

static void test_div_reset_edge(void) {
    CupidGb gb;
    prepare(&gb, false, false);
    cupid_gb_tick(&gb, 1024); /* DIV bit 12 high, no falling edge yet. */
    assert(gb.apu.ch2_len == 2);
    cupid_gb_write_u8(&gb, 0xff04, 0);
    assert(gb.apu.ch2_len == 1);
    cupid_gb_write_u8(&gb, 0xff04, 0);
    assert(gb.apu.ch2_len == 1); /* A low-to-low DIV reset cannot clock length. */
}

static void test_power_on_high_div(void) {
    CupidGb gb = {0};
    gb.io_registers[4] = 0x10;
    cupid_gb_write_u8(&gb, 0xff26, 0x80);
    cupid_gb_write_u8(&gb, 0xff17, 0xf0);
    cupid_gb_write_u8(&gb, 0xff16, 62);
    cupid_gb_write_u8(&gb, 0xff19, 0xc0);
    assert(gb.apu.ch2_len == 1); /* Enabling length in this phase clocks once. */
    cupid_gb_write_u8(&gb, 0xff04, 0);
    assert(gb.apu.ch2_len == 1); /* First edge after power-on in the high half is skipped. */
    cupid_gb_tick(&gb, 2048);
    assert(gb.apu.ch2_len == 0);
}

static void test_power_counter_reset(bool cgb) {
    CupidGb gb;
    prepare(&gb, cgb, false);
    cupid_gb_write_u8(&gb, 0xff26, 0);
    assert(gb.apu.ch2_len == 0);
    cupid_gb_write_u8(&gb, 0xff16, 60);
    cupid_gb_write_u8(&gb, 0xff20, 60);
    assert(gb.apu.ch2_len == (cgb ? 0 : 4));
    assert(gb.apu.ch4_len == (cgb ? 0 : 4));
    cupid_gb_write_u8(&gb, 0xff26, 0x80);
    assert(gb.apu.ch2_len == (cgb ? 0 : 4));
    assert(gb.apu.ch4_len == (cgb ? 0 : 4));
}

static void test_apu_ticks_do_not_clock_length(void) {
    CupidGb gb;
    prepare(&gb, false, false);
    cupid_gb_tick_apu(&gb, 32768);
    assert(gb.apu.ch2_len == 2); /* The frequency clock cannot substitute for DIV edges. */
}

static void test_power_cycle_resets_sound_but_preserves_stream(bool color) {
    CupidGb gb;
    prepare(&gb, color, false);
    cupid_gb_write_u8(&gb, 0xff30, 0xa5);
    cupid_gb_write_u8(&gb, 0xff16, 0xc0);
    cupid_gb_write_u8(&gb, 0xff18, 0xff);
    cupid_gb_write_u8(&gb, 0xff19, 0x87);
    cupid_gb_tick_apu(&gb, 95); /* Sample before the duty wrap, including trigger delay. */
    assert(gb.apu.ch2_duty_pos != 0);
    uint32_t count = gb.apu.buf_write;
    uint32_t fraction = gb.apu.sample_acc;
    int16_t left = gb.apu.buf[0];
    int64_t partial = gb.apu.mix_left;
    int64_t capacitor = gb.apu.highpass_left;
    cupid_gb_write_u8(&gb, 0xff26, 0);
    assert(gb.apu.ch2_duty_pos == 0);
    assert(gb.apu.ch2_freq == 0);
    assert(!gb.apu.ch2_on && !gb.apu.ch2_dac);
    assert(gb.io_registers[0x16] == 0);
    assert(gb.io_registers[0x30] == 0xa5);
    assert(gb.apu.buf_write == count && gb.apu.buf[0] == left);
    assert(gb.apu.sample_acc == fraction);
    assert(gb.apu.mix_left == partial && gb.apu.highpass_left == capacitor);
    cupid_gb_write_u8(&gb, 0xff26, 0x80);
    assert(gb.apu.ch2_duty_pos == 0);
    assert(!gb.apu.ch2_on && !gb.apu.ch2_dac);
    assert(gb.io_registers[0x30] == 0xa5);
    assert(gb.apu.buf_write == count && gb.apu.sample_acc == fraction);
    assert(gb.apu.mix_left == partial && gb.apu.highpass_left == capacitor);
}

static void test_cgb_compatibility_uses_color_hardware(void) {
    CupidGb gb;
    prepare(&gb, true, false);
    gb.cgb_mode = false; /* A monochrome cartridge changes video mode, not APU hardware. */
    gb.apu.ch3_on = true;
    gb.apu.ch3_current_byte = 3;
    gb.apu.ch3_wave_access_ticks = 0;
    gb.io_registers[0x33] = 0x5a;
    assert(cupid_gb_read_u8(&gb, 0xff3f) == 0x5a);
    cupid_gb_write_u8(&gb, 0xff30, 0xa5);
    assert(gb.io_registers[0x33] == 0xa5);
    cupid_gb_write_u8(&gb, 0xff26, 0);
    cupid_gb_write_u8(&gb, 0xff16, 60);
    cupid_gb_write_u8(&gb, 0xff20, 60);
    cupid_gb_write_u8(&gb, 0xff26, 0x80);
    assert(gb.apu.ch2_len == 0);
    assert(gb.apu.ch4_len == 0);
}

static void test_sweep_zero_period_and_shadow(void) {
    CupidGb gb;
    prepare(&gb, false, false);
    cupid_gb_write_u8(&gb, 0xff10, 0x01);
    cupid_gb_write_u8(&gb, 0xff12, 0xf0);
    cupid_gb_write_u8(&gb, 0xff13, 0);
    cupid_gb_write_u8(&gb, 0xff14, 0x82);
    for (unsigned i = 0; i < 64; ++i) {
        cupid_gb_apu_div_event(&gb);
    }
    assert(gb.apu.ch1_on);
    assert(gb.apu.ch1_freq == 0x200);
    cupid_gb_write_u8(&gb, 0xff10, 0x12);
    cupid_gb_write_u8(&gb, 0xff13, 4);
    cupid_gb_write_u8(&gb, 0xff14, 0x80);
    cupid_gb_write_u8(&gb, 0xff13, 0);
    assert(gb.apu.ch1_sweep_shadow == 4);
    for (unsigned i = 0; i < 3; ++i) {
        cupid_gb_apu_div_event(&gb);
    }
    assert(gb.apu.ch1_freq == 5);
    assert(gb.apu.ch1_sweep_shadow == 5);
}

static void tick_audio(CupidGb *gb, uint32_t cycles) {
    while (cycles) {
        uint16_t batch = cycles > 32768u ? 32768u : (uint16_t)cycles;
        cupid_gb_tick_apu(gb, batch);
        cycles -= batch;
    }
}

static void test_pulse_pitch(void) {
    const uint16_t frequencies[] = {0, 1024, 2040, 2047};
    for (unsigned channel = 0; channel < 2; ++channel) {
        for (unsigned f = 0; f < sizeof(frequencies) / sizeof(frequencies[0]); ++f) {
            CupidGb gb = {0};
            uint16_t frequency = frequencies[f];
            uint32_t period = (2048u - frequency) * 4u;
            uint16_t base = channel ? 0xff16u : 0xff11u;
            cupid_gb_write_u8(&gb, 0xff26, 0x80);
            cupid_gb_write_u8(&gb, base, 0x80);
            cupid_gb_write_u8(&gb, (uint16_t)(base + 1), 0xf0);
            cupid_gb_write_u8(&gb, (uint16_t)(base + 2), (uint8_t)frequency);
            cupid_gb_write_u8(&gb, (uint16_t)(base + 3), (uint8_t)(0x80u | (frequency >> 8)));
            /* Start after the first reload: this checks sustained pitch, not trigger latency. */
            tick_audio(&gb, channel ? gb.apu.ch2_timer : gb.apu.ch1_timer);
            uint8_t start = channel ? gb.apu.ch2_duty_pos : gb.apu.ch1_duty_pos;
            for (unsigned edge = 1; edge <= 17; ++edge) {
                tick_audio(&gb, period - 1);
                assert((channel ? gb.apu.ch2_duty_pos : gb.apu.ch1_duty_pos) ==
                       ((start + edge - 1) & 7));
                tick_audio(&gb, 1);
                assert((channel ? gb.apu.ch2_duty_pos : gb.apu.ch1_duty_pos) ==
                       ((start + edge) & 7));
            }
        }
    }
}

static void test_noise_clock(void) {
    const uint32_t divisors[] = {8, 16, 32, 48, 64, 80, 96, 112};
    for (unsigned divisor = 0; divisor < 8; ++divisor) {
        for (unsigned shift = 0; shift < 4; ++shift) {
            CupidGb gb = {0};
            uint32_t period = divisors[divisor] << shift;
            cupid_gb_write_u8(&gb, 0xff26, 0x80);
            cupid_gb_write_u8(&gb, 0xff21, 0xf0);
            cupid_gb_write_u8(&gb, 0xff22, (uint8_t)((shift << 4) | divisor));
            cupid_gb_write_u8(&gb, 0xff23, 0x80);
            tick_audio(&gb, gb.apu.ch4_timer);
            uint16_t previous = gb.apu.ch4_lfsr;
            for (unsigned edge = 0; edge < 24; ++edge) {
                tick_audio(&gb, period - 1);
                assert(gb.apu.ch4_lfsr == previous);
                tick_audio(&gb, 1);
                assert(gb.apu.ch4_lfsr != previous);
                previous = gb.apu.ch4_lfsr;
            }
        }
    }
    for (unsigned shift = 14; shift <= 15; ++shift) {
        CupidGb gb = {0};
        cupid_gb_write_u8(&gb, 0xff26, 0x80);
        cupid_gb_write_u8(&gb, 0xff21, 0xf0);
        cupid_gb_write_u8(&gb, 0xff22, (uint8_t)(shift << 4));
        cupid_gb_write_u8(&gb, 0xff23, 0x80);
        tick_audio(&gb, 1u << 21);
        assert(gb.apu.ch4_on);
        assert(gb.apu.ch4_lfsr == 0x7fff);
    }
}

static void test_partial_drain(void) {
    CupidGb gb = {0};
    int16_t output[12];
    const int16_t frames[] = {1, -1, 2, -2, 3, -3, 4, -4};
    memcpy(gb.apu.buf, frames, sizeof(frames));
    gb.apu.buf_write = 4;
    assert(cupid_gb_apu_drain(&gb, output, 0) == 0);
    assert(gb.apu.buf_write == 4);
    assert(cupid_gb_apu_drain(&gb, output, 1) == 1);
    assert(memcmp(output, frames, 2 * sizeof(int16_t)) == 0);
    assert(gb.apu.buf_write == 3);
    assert(cupid_gb_apu_drain(&gb, output, 6) == 3);
    assert(memcmp(output, frames + 2, 6 * sizeof(int16_t)) == 0);
    assert(gb.apu.buf_write == 0);
    assert(cupid_gb_apu_drain(&gb, output, 6) == 0);
}

static void test_stop_oscillator(bool color) {
    CupidGb gb;
    prepare(&gb, color, false);
    gb.cpu.stopped = true;
    gb.apu.ch2_timer = 4;
    gb.apu.ch2_freq = 2047;
    uint8_t duty_position = gb.apu.ch2_duty_pos;
    tick_audio(&gb, 100);
    assert(gb.apu.ch2_duty_pos == ((duty_position + (color ? 25 : 0)) & 7));
    assert(gb.apu.buf_write == 1); /* STOP does not stop the host's audio stream. */
}

static void prepare_dc(CupidGb *gb) {
    *gb = (CupidGb){0};
    cupid_gb_write_u8(gb, 0xff26, 0x80);
    cupid_gb_write_u8(gb, 0xff12, 0xf0); /* DAC connected, channel not triggered: code zero. */
    cupid_gb_write_u8(gb, 0xff24, 0x70);
    cupid_gb_write_u8(gb, 0xff25, 0x10);
}

static void test_dac_transfer_and_pan(void) {
    CupidGb gb;
    int16_t output[8];
    prepare_dc(&gb);
    cupid_gb_apu_set_highpass(&gb, false);
    tick_audio(&gb, 96);
    assert(cupid_gb_apu_drain(&gb, output, 4) == 1);
    assert(output[0] == 4080 && output[1] == 0);
    /* Sample 15 has the opposite polarity to code zero. */
    gb.apu.ch1_on = true;
    gb.apu.ch1_duty = 0;
    gb.apu.ch1_duty_pos = 7;
    gb.apu.ch1_vol = 15;
    gb.apu.ch1_timer = 8192;
    tick_audio(&gb, 192);
    assert(cupid_gb_apu_drain(&gb, output, 4) == 2);
    assert(output[2] == -4080 && output[3] == 0);
    cupid_gb_write_u8(&gb, 0xff25, 1);
    tick_audio(&gb, 192);
    assert(cupid_gb_apu_drain(&gb, output, 4) == 2);
    assert(output[2] == 0 && output[3] == -510); /* NR50 zero still means gain one. */
    cupid_gb_write_u8(&gb, 0xff12, 0);
    tick_audio(&gb, 192);
    assert(cupid_gb_apu_drain(&gb, output, 4) == 2);
    assert(output[2] == 0 && output[3] == 0);
}

static void test_highpass_dc_and_disconnect(void) {
    CupidGb gb;
    int16_t output[CUPID_GB_APU_BUF_FRAMES * 2];
    prepare_dc(&gb);
    tick_audio(&gb, 96000);
    uint32_t count = cupid_gb_apu_drain(&gb, output, CUPID_GB_APU_BUF_FRAMES);
    assert(count > 1000);
    assert(output[0] == 4080 && output[1] == 0);
    for (uint32_t i = 1; i < count; ++i) {
        assert(output[i * 2] <= output[(i - 1) * 2]);
        assert(output[i * 2] >= 0);
        assert(output[i * 2 + 1] == 0);
    }
    assert(output[(count - 1) * 2] < 100);
    int64_t capacitor = gb.apu.highpass_left;
    cupid_gb_write_u8(&gb, 0xff12, 0);
    tick_audio(&gb, 960);
    count = cupid_gb_apu_drain(&gb, output, CUPID_GB_APU_BUF_FRAMES);
    assert(count >= 10);
    assert(gb.apu.highpass_left == capacitor); /* Disconnected DMG capacitor holds charge. */
    for (uint32_t i = 0; i < count * 2; ++i) {
        assert(output[i] == 0);
    }
    gb.model = CUPID_GB_MODEL_CGB;
    tick_audio(&gb, 960);
    assert(gb.apu.highpass_left < capacitor); /* Color hardware keeps the output connected. */
}

static void test_duty_phase(void) {
    const char *waveforms[] = {"00000001", "10000001", "10000111", "01111110"};
    CupidGb gb;
    int16_t output[8];
    prepare_dc(&gb);
    cupid_gb_apu_set_highpass(&gb, false);
    cupid_gb_write_u8(&gb, 0xff17, 0xf0);
    cupid_gb_write_u8(&gb, 0xff25, 0x30);
    gb.apu.ch1_on = true;
    gb.apu.ch2_on = true;
    gb.apu.ch1_vol = 15;
    gb.apu.ch2_vol = 15;
    for (unsigned phase = 0; phase < 8; ++phase) {
        for (unsigned duty = 0; duty < 4; ++duty) {
            gb.apu.ch1_duty_pos = (uint8_t)phase;
            gb.apu.ch2_duty_pos = (uint8_t)phase;
            gb.apu.ch1_duty = (uint8_t)duty;
            gb.apu.ch2_duty = (uint8_t)duty;
            gb.apu.ch1_timer = 8192;
            gb.apu.ch2_timer = 8192;
            tick_audio(&gb, 192);
            uint32_t count = cupid_gb_apu_drain(&gb, output, 4);
            assert(count >= 2);
            assert(output[(count - 1) * 2] == (waveforms[duty][phase] == '1' ? -8160 : 8160));
            assert(output[(count - 1) * 2 + 1] == 0);
        }
    }
}

static void test_sample_rates_and_batching(void) {
    const uint32_t rates[] = {8000, 22050, 44100, 48000, 96000, 192000};
    for (unsigned r = 0; r < sizeof(rates) / sizeof(rates[0]); ++r) {
        CupidGb gb = {0};
        int16_t output[CUPID_GB_APU_BUF_FRAMES * 2];
        uint32_t count = 0;
        uint32_t cycles = 4194304;
        assert(cupid_gb_apu_set_sample_rate(&gb, rates[r]));
        assert(cupid_gb_apu_get_sample_rate(&gb) == rates[r]);
        while (cycles) {
            uint16_t batch = cycles > 8192 ? 8192 : (uint16_t)cycles;
            tick_audio(&gb, batch);
            count += cupid_gb_apu_drain(&gb, output, CUPID_GB_APU_BUF_FRAMES);
            cycles -= batch;
        }
        assert(count == rates[r]);
        assert(gb.apu.sample_acc == 0);
        assert(gb.apu.mix_left == 0 && gb.apu.mix_right == 0);
        assert(!cupid_gb_apu_set_sample_rate(&gb, 7999));
        assert(!cupid_gb_apu_set_sample_rate(&gb, 192001));
        assert(cupid_gb_apu_get_sample_rate(&gb) == rates[r]);
    }

    CupidGb one;
    CupidGb batch;
    prepare_dc(&one);
    cupid_gb_write_u8(&one, 0xff11, 0x80);
    cupid_gb_write_u8(&one, 0xff13, 0xf8);
    cupid_gb_write_u8(&one, 0xff14, 0x87);
    batch = one;
    for (unsigned i = 0; i < 12000; ++i) {
        cupid_gb_tick_apu(&one, 1);
    }
    tick_audio(&batch, 12000);
    assert(one.apu.buf_write == batch.apu.buf_write);
    assert(memcmp(one.apu.buf, batch.apu.buf, one.apu.buf_write * 2 * sizeof(int16_t)) == 0);
    assert(one.apu.mix_left == batch.apu.mix_left);
    assert(one.apu.sample_acc == batch.apu.sample_acc);
    assert(one.apu.highpass_left == batch.apu.highpass_left);
    assert(one.apu.ch1_duty_pos == batch.apu.ch1_duty_pos);
}

static void test_physical_model_clocks(void) {
    assert(cupid_gb_base_clock_hz(NULL) == 4194304u);
    uint32_t handheld_filter = 0;
    for (unsigned model = CUPID_GB_MODEL_DMG_ABC; model <= CUPID_GB_MODEL_SGB2; ++model) {
        CupidGb gb = {0};
        gb.model = (CupidGbModel)model;
        uint32_t clock_hz = model == CUPID_GB_MODEL_SGB ? 4295454u : 4194304u;
        assert(cupid_gb_base_clock_hz(&gb) == clock_hz);
        gb.double_speed = true;
        assert(cupid_gb_base_clock_hz(&gb) == clock_hz);
        assert(cupid_gb_apu_set_sample_rate(&gb, 48000));
        if (model == CUPID_GB_MODEL_DMG_ABC) {
            handheld_filter = gb.apu.highpass_rate;
        }
        assert(model == CUPID_GB_MODEL_SGB ? gb.apu.highpass_rate < handheld_filter
                                           : gb.apu.highpass_rate == handheld_filter);
        uint32_t count = 0, cycles = clock_hz;
        int16_t output[CUPID_GB_APU_BUF_FRAMES * 2];
        while (cycles) {
            uint16_t chunk = cycles > 8192 ? 8192 : (uint16_t)cycles;
            tick_audio(&gb, chunk);
            count += cupid_gb_apu_drain(&gb, output, CUPID_GB_APU_BUF_FRAMES);
            cycles -= chunk;
        }
        assert(count == 48000 && gb.apu.sample_acc == 0);
    }

    CupidGb one, batch;
    prepare_dc(&one);
    one.model = CUPID_GB_MODEL_SGB;
    cupid_gb_apu_set_highpass(&one, false);
    batch = one;
    for (unsigned dot = 0; dot < 12000; ++dot) {
        cupid_gb_tick_apu(&one, 1);
    }
    tick_audio(&batch, 12000);
    assert(one.apu.buf_write && one.apu.buf_write == batch.apu.buf_write);
    assert(memcmp(one.apu.buf, batch.apu.buf, one.apu.buf_write * 2 * sizeof(int16_t)) == 0);
    assert(one.apu.sample_acc == batch.apu.sample_acc);
    assert(one.apu.mix_left == batch.apu.mix_left && one.apu.mix_right == batch.apu.mix_right);
    for (unsigned frame = 0; frame < one.apu.buf_write; ++frame) {
        /* Clock changes affect pitch and sample boundaries, not the DAC's DC gain. */
        assert(one.apu.buf[frame * 2] == 4080 && one.apu.buf[frame * 2 + 1] == 0);
    }
}

static void test_fast_pulse_integration(void) {
    CupidGb gb;
    int16_t output[CUPID_GB_APU_BUF_FRAMES * 2];
    prepare_dc(&gb);
    cupid_gb_apu_set_highpass(&gb, false);
    cupid_gb_write_u8(&gb, 0xff11, 0x80); /* 50 percent duty, 131072 Hz fundamental. */
    cupid_gb_write_u8(&gb, 0xff13, 0xff);
    cupid_gb_write_u8(&gb, 0xff14, 0x87);
    tick_audio(&gb, 32768);
    uint32_t count = cupid_gb_apu_drain(&gb, output, CUPID_GB_APU_BUF_FRAMES);
    assert(count > 300);
    /* Each output window contains nearly three complete pulse periods.
     * Point sampling would produce full-amplitude 4080-valued aliases instead.
     */
    for (uint32_t i = 1; i < count; ++i) {
        assert(output[i * 2] > -1400 && output[i * 2] < 1400);
        assert(output[i * 2 + 1] == 0);
    }
}

static void test_pcm_taps(void) {
    CupidGb gb = {0};
    gb.model = CUPID_GB_MODEL_CGB;
    gb.cgb_mode = false; /* Digital taps belong to the hardware, not the cartridge mode. */
    cupid_gb_write_u8(&gb, 0xff26, 0x80);
    gb.apu.ch1_on = true;
    gb.apu.ch1_dac = true;
    gb.apu.ch1_vol = 5;
    gb.apu.ch1_duty = 0;
    gb.apu.ch1_duty_pos = 7;
    gb.apu.ch2_on = true;
    gb.apu.ch2_dac = true;
    gb.apu.ch2_vol = 12;
    gb.apu.ch2_duty = 1;
    gb.apu.ch2_duty_pos = 0;
    gb.apu.ch3_on = true;
    gb.apu.ch3_dac = true;
    gb.apu.ch3_sample = 13;
    gb.apu.ch3_out_level = 3;
    gb.apu.ch4_on = true;
    gb.apu.ch4_dac = true;
    gb.apu.ch4_vol = 9;
    gb.apu.ch4_lfsr = 0;
    cupid_gb_apu_import_legacy(&gb); /* Materialize the synthetic fixture's digital latches. */
    assert(cupid_gb_apu_read_pcm12(&gb) == 0xc5);
    assert(cupid_gb_apu_read_pcm34(&gb) == 0x93);
    for (unsigned color_mode = 0; color_mode < 2; ++color_mode) {
        gb.cgb_mode = color_mode != 0;
        /* Software reads the memory bus, so direct tap tests alone cannot
         * establish that the hardware registers are actually reachable. */
        assert(cupid_gb_read_u8(&gb, 0xff76) == 0xc5);
        assert(cupid_gb_read_u8(&gb, 0xff77) == 0x93);
    }
    cupid_gb_write_u8(&gb, 0xff24, 0x77);
    cupid_gb_write_u8(&gb, 0xff25, 0xff);
    cupid_gb_apu_set_highpass(&gb, false);
    assert(cupid_gb_apu_read_pcm12(&gb) == 0xc5);
    assert(cupid_gb_apu_read_pcm34(&gb) == 0x93);
    const uint8_t wave_outputs[] = {0, 13, 6, 3};
    for (unsigned level = 0; level < 4; ++level) {
        cupid_gb_write_u8(&gb, 0xff1c, (uint8_t)(level << 5));
        assert(cupid_gb_apu_read_pcm34(&gb) == (0x90 | wave_outputs[level]));
    }
    gb.apu.ch1_on = false;
    gb.apu.ch4_lfsr = 1;
    assert(cupid_gb_apu_read_pcm34(&gb) == 0x93); /* The sample is held between shift edges. */
    cupid_gb_apu_import_legacy(&gb);
    assert(cupid_gb_apu_read_pcm12(&gb) == 0xc0);
    assert(cupid_gb_apu_read_pcm34(&gb) == 3);
    cupid_gb_write_u8(&gb, 0xff17, 0);
    cupid_gb_write_u8(&gb, 0xff1a, 0);
    assert(cupid_gb_apu_read_pcm12(&gb) == 0);
    assert(cupid_gb_apu_read_pcm34(&gb) == 0);
    for (unsigned model = CUPID_GB_MODEL_DMG_ABC; model <= CUPID_GB_MODEL_SGB2; ++model) {
        gb.model = (CupidGbModel)model;
        assert(cupid_gb_apu_read_pcm12(&gb) == (model == CUPID_GB_MODEL_CGB ? 0 : 0xff));
        assert(cupid_gb_apu_read_pcm34(&gb) == (model == CUPID_GB_MODEL_CGB ? 0 : 0xff));
        assert(cupid_gb_read_u8(&gb, 0xff76) == (model == CUPID_GB_MODEL_CGB ? 0 : 0xff));
        assert(cupid_gb_read_u8(&gb, 0xff77) == (model == CUPID_GB_MODEL_CGB ? 0 : 0xff));
    }
}

static void set_length_state(CupidGb *gb, unsigned channel, uint16_t count, bool enable) {
    switch (channel) {
        case 0:
            gb->apu.ch1_len = (uint8_t)count;
            gb->apu.ch1_len_en = enable;
            gb->apu.ch1_on = count != 0;
            break;
        case 1:
            gb->apu.ch2_len = (uint8_t)count;
            gb->apu.ch2_len_en = enable;
            gb->apu.ch2_on = count != 0;
            break;
        case 2:
            gb->apu.ch3_len = count;
            gb->apu.ch3_len_en = enable;
            gb->apu.ch3_on = count != 0;
            break;
        default:
            gb->apu.ch4_len = (uint8_t)count;
            gb->apu.ch4_len_en = enable;
            gb->apu.ch4_on = count != 0;
            break;
    }
}

static uint16_t get_length_count(const CupidGb *gb, unsigned channel) {
    const uint16_t counts[] = {gb->apu.ch1_len, gb->apu.ch2_len, gb->apu.ch3_len, gb->apu.ch4_len};
    return counts[channel];
}

static void test_length_control_matrix(void) {
    const uint16_t registers[] = {0xff14, 0xff19, 0xff1e, 0xff23};
    for (unsigned startup = 0; startup < 2; ++startup) {
        for (unsigned edges = 0; edges < 10; ++edges) {
            for (unsigned channel = 0; channel < 4; ++channel) {
                for (unsigned count = 0; count < 3; ++count) {
                    for (unsigned controls = 0; controls < 8; ++controls) {
                        CupidGb gb = {0};
                        bool old_enable = (controls & 1) != 0;
                        bool enable = (controls & 2) != 0;
                        bool trigger = (controls & 4) != 0;
                        bool extra =
                            startup && edges < 2 ? true : ((edges - (startup ? 1u : 0u)) & 1u) != 0;
                        gb.model = CUPID_GB_MODEL_CGB;
                        gb.io_registers[0x04] = startup ? 0x10 : 0;
                        cupid_gb_write_u8(&gb, 0xff26, 0x80);
                        cupid_gb_write_u8(&gb, 0xff12, 0xf0);
                        cupid_gb_write_u8(&gb, 0xff17, 0xf0);
                        cupid_gb_write_u8(&gb, 0xff1a, 0x80);
                        cupid_gb_write_u8(&gb, 0xff21, 0xf0);
                        for (unsigned edge = 0; edge < edges; ++edge) {
                            cupid_gb_apu_div_event(&gb);
                        }
                        set_length_state(&gb, channel, (uint16_t)count, old_enable);
                        unsigned consumed = extra && enable && !old_enable && count ? 1u : 0u;
                        unsigned expected = count - consumed;
                        bool active = expected != 0;
                        if (trigger) {
                            if (!expected) {
                                expected =
                                    (channel == 2 ? 256u : 64u) - (extra && enable ? 1u : 0u);
                            }
                            active = true;
                        }
                        cupid_gb_write_u8(&gb, registers[channel],
                                          (uint8_t)((enable ? 0x40 : 0) | (trigger ? 0x80 : 0)));
                        assert(get_length_count(&gb, channel) == expected);
                        assert(((cupid_gb_read_u8(&gb, 0xff26) >> channel) & 1u) == active);
                    }
                }
            }
        }
    }
}

static void test_power_preserves_output_pipeline(bool color) {
    CupidGb gb;
    prepare_dc(&gb);
    gb.model = color ? CUPID_GB_MODEL_CGB : CUPID_GB_MODEL_DMG_ABC;
    assert(cupid_gb_apu_set_sample_rate(&gb, 48000));
    cupid_gb_write_u8(&gb, 0xff11, 0x80);
    cupid_gb_write_u8(&gb, 0xff14, 0x87);
    tick_audio(&gb, 200);
    for (unsigned byte = 0; byte < 16; ++byte) {
        cupid_gb_write_u8(&gb, (uint16_t)(0xff30 + byte), (uint8_t)(byte * 13));
    }
    CupidGbApu pipeline = gb.apu;
    assert(pipeline.buf_write != 0 && pipeline.sample_acc != 0);
    cupid_gb_write_u8(&gb, 0xff26, 0);
    assert(gb.apu.sample_rate == pipeline.sample_rate);
    assert(gb.apu.sample_acc == pipeline.sample_acc);
    assert(gb.apu.mix_left == pipeline.mix_left);
    assert(gb.apu.highpass_left == pipeline.highpass_left);
    assert(gb.apu.highpass_rate == pipeline.highpass_rate);
    assert(gb.apu.buf_write == pipeline.buf_write);
    assert(memcmp(gb.apu.buf, pipeline.buf, sizeof(pipeline.buf)) == 0);
    assert(gb.apu.ch1_freq == 0 && gb.apu.ch1_timer == 0 && gb.apu.ch1_duty_pos == 0);
    assert(gb.apu.ch1_vol == 0 && !gb.apu.ch1_dac && !gb.apu.ch1_on);
    for (unsigned reg = 0x10; reg < 0x26; ++reg) {
        assert(gb.io_registers[reg] == 0);
    }
    for (unsigned byte = 0; byte < 16; ++byte) {
        assert(cupid_gb_read_u8(&gb, (uint16_t)(0xff30 + byte)) == byte * 13);
    }
    cupid_gb_write_u8(&gb, 0xff11, 63);
    cupid_gb_write_u8(&gb, 0xff16, 62);
    cupid_gb_write_u8(&gb, 0xff1b, 253);
    cupid_gb_write_u8(&gb, 0xff20, 60);
    cupid_gb_write_u8(&gb, 0xff26, 0x80);
    for (unsigned channel = 0; channel < 4; ++channel) {
        assert(get_length_count(&gb, channel) == (color ? 0 : channel + 1));
    }
    assert(gb.apu.sample_acc == pipeline.sample_acc);
    assert(gb.apu.mix_left == pipeline.mix_left);
    assert(gb.apu.highpass_left == pipeline.highpass_left);
    assert(gb.apu.buf_write == pipeline.buf_write);
}

static void test_color_pulse_clock_and_held_duty(void) {
    for (unsigned channel = 0; channel < 2; ++channel) {
        for (unsigned phase = 0; phase < 4; phase += 2) {
            CupidGb gb = {0};
            gb.model = CUPID_GB_MODEL_CGB;
            uint16_t duty = channel ? 0xff16 : 0xff11;
            uint16_t envelope = channel ? 0xff17 : 0xff12;
            uint16_t low = channel ? 0xff18 : 0xff13;
            uint16_t high = channel ? 0xff19 : 0xff14;
            unsigned shift = channel * 4;
            cupid_gb_write_u8(&gb, 0xff26, 0x80);
            cupid_gb_tick_apu(&gb, 100);
            assert(gb.apu.ch1_duty_pos == 0 && gb.apu.ch2_duty_pos == 0);
            cupid_gb_tick_apu(&gb, (uint16_t)phase);
            cupid_gb_write_u8(&gb, duty, 0);
            cupid_gb_write_u8(&gb, envelope, 0xf0);
            cupid_gb_write_u8(&gb, low, 0xff);
            cupid_gb_write_u8(&gb, high, 0x87);
            unsigned first_high = 36 + phase;
            cupid_gb_tick_apu(&gb, (uint16_t)(first_high - 1));
            assert(((cupid_gb_read_u8(&gb, 0xff76) >> shift) & 15) == 0);
            cupid_gb_tick_apu(&gb, 1);
            assert(((cupid_gb_read_u8(&gb, 0xff76) >> shift) & 15) == 15);
            cupid_gb_write_u8(&gb, duty, 0xc0); /* New duty is low at phase seven. */
            assert(((cupid_gb_read_u8(&gb, 0xff76) >> shift) & 15) == 15);
            cupid_gb_tick_apu(&gb, 3);
            assert(((cupid_gb_read_u8(&gb, 0xff76) >> shift) & 15) == 15);
            cupid_gb_tick_apu(&gb, 1);
            assert(((cupid_gb_read_u8(&gb, 0xff76) >> shift) & 15) == 0);
            cupid_gb_write_u8(&gb, envelope, 0);
            unsigned index = channel ? gb.apu.ch2_duty_pos : gb.apu.ch1_duty_pos;
            cupid_gb_tick_apu(&gb, 100);
            assert((channel ? gb.apu.ch2_duty_pos : gb.apu.ch1_duty_pos) == index);
        }
    }
}

static void test_color_envelope_edges(void) {
    for (unsigned channel = 0; channel < 2; ++channel) {
        for (unsigned speed = 0; speed < 2; ++speed) {
            CupidGb gb = {0};
            gb.model = CUPID_GB_MODEL_CGB;
            gb.double_speed = speed != 0;
            uint16_t envelope = channel ? 0xff17 : 0xff12;
            uint16_t high = channel ? 0xff19 : 0xff14;
            cupid_gb_write_u8(&gb, 0xff26, 0x80);
            cupid_gb_write_u8(&gb, envelope, 0xf0);
            cupid_gb_write_u8(&gb, high, 0x80);
            cupid_gb_write_u8(&gb, envelope, 0xf8);
            assert((channel ? gb.apu.ch2_vol : gb.apu.ch1_vol) == 0);
            cupid_gb_write_u8(&gb, envelope, 0xf8);
            assert((channel ? gb.apu.ch2_vol : gb.apu.ch1_vol) == 1);
            cupid_gb_write_u8(&gb, envelope, 0xf1);
            assert((channel ? gb.apu.ch2_vol : gb.apu.ch1_vol) == 14);
            cupid_gb_write_u8(&gb, high, 0x80);
            for (unsigned edge = 0; edge < 7; ++edge) {
                cupid_gb_apu_div_event(&gb);
            }
            cupid_gb_apu_div_secondary_event(&gb);
            assert((channel ? gb.apu.ch2_vol : gb.apu.ch1_vol) == 15);
            cupid_gb_apu_div_event(&gb);
            if (speed) {
                cupid_gb_tick_apu(&gb, 1);
                assert((channel ? gb.apu.ch2_vol : gb.apu.ch1_vol) == 15);
                cupid_gb_tick_apu(&gb, 1);
            }
            assert((channel ? gb.apu.ch2_vol : gb.apu.ch1_vol) == 14);

            cupid_gb_write_u8(&gb, envelope, 0xf9);
            cupid_gb_write_u8(&gb, high, 0x80);
            for (unsigned edge = 0; edge < 7; ++edge) {
                cupid_gb_apu_div_event(&gb);
            }
            cupid_gb_apu_div_secondary_event(&gb);
            cupid_gb_write_u8(&gb, envelope, 0xf1);
            assert((channel ? gb.apu.ch2_vol : gb.apu.ch1_vol) == 1);
            cupid_gb_apu_div_event(&gb);
            cupid_gb_tick_apu(&gb, 2);
            assert((channel ? gb.apu.ch2_vol : gb.apu.ch1_vol) == 1);
            cupid_gb_write_u8(&gb, 0xff26, 0);
            assert(gb.apu.envelope_delay == 0);
            assert(!gb.apu.pulse_timing[channel].envelope_locked);
            assert(gb.apu.pulse_timing[channel].edge_age == 4);
        }
    }
}

static void test_color_frequency_reload_window(void) {
    for (unsigned age = 0; age <= 4; age += 2) {
        CupidGb gb = {0};
        gb.model = CUPID_GB_MODEL_CGB;
        cupid_gb_write_u8(&gb, 0xff26, 0x80);
        cupid_gb_write_u8(&gb, 0xff12, 0xf0);
        cupid_gb_write_u8(&gb, 0xff13, 0xfc);
        cupid_gb_write_u8(&gb, 0xff14, 0x87);
        cupid_gb_tick_apu(&gb, (uint16_t)(104 + age));
        assert(gb.apu.ch1_duty_pos == 6);
        cupid_gb_write_u8(&gb, 0xff14, 0);
        assert(cupid_gb_read_u8(&gb, 0xff76) == 0);
        cupid_gb_tick_apu(&gb, 32);
        assert(cupid_gb_read_u8(&gb, 0xff76) == (age <= 2 ? 0 : 15));
    }
}

static void test_color_sweep_live_frequency_and_latency(void) {
    CupidGb gb = {0};
    gb.model = CUPID_GB_MODEL_CGB;
    cupid_gb_write_u8(&gb, 0xff26, 0x80);
    cupid_gb_write_u8(&gb, 0xff10, 0x1f);
    cupid_gb_write_u8(&gb, 0xff12, 0x80);
    cupid_gb_write_u8(&gb, 0xff13, 0xff);
    cupid_gb_write_u8(&gb, 0xff14, 0x87);
    cupid_gb_tick_apu(&gb, 64);
    for (unsigned edge = 0; edge < 3; ++edge) {
        cupid_gb_apu_div_event(&gb);
    }
    assert(gb.apu.ch1_freq == 0x7f0);
    assert(gb.io_registers[0x13] == 0xff); /* Sweep output is not the software write latch. */
    cupid_gb_write_u8(&gb, 0xff14, 0x87);
    assert(gb.apu.ch1_freq == 0x7f0);

    cupid_gb_write_u8(&gb, 0xff26, 0);
    cupid_gb_write_u8(&gb, 0xff26, 0x80);
    cupid_gb_write_u8(&gb, 0xff10, 0x17);
    cupid_gb_write_u8(&gb, 0xff12, 0x80);
    cupid_gb_write_u8(&gb, 0xff13, 0xff);
    cupid_gb_write_u8(&gb, 0xff14, 0x87);
    assert(cupid_gb_read_u8(&gb, 0xff26) & 1);
    cupid_gb_tick_apu(&gb, 37);
    assert(cupid_gb_read_u8(&gb, 0xff26) & 1);
    cupid_gb_tick_apu(&gb, 1);
    assert(!(cupid_gb_read_u8(&gb, 0xff26) & 1));
}

static void test_color_sweep_overflow_mixer_edge(void) {
    for (unsigned sample_boundary = 0; sample_boundary < 2; ++sample_boundary) {
        CupidGb batch = {0};
        batch.model = CUPID_GB_MODEL_CGB;
        cupid_gb_write_u8(&batch, 0xff26, 0x80);
        cupid_gb_write_u8(&batch, 0xff10, 0x17);
        cupid_gb_write_u8(&batch, 0xff11, 0x80);
        cupid_gb_write_u8(&batch, 0xff12, 0x80);
        cupid_gb_write_u8(&batch, 0xff13, 0xff);
        cupid_gb_write_u8(&batch, 0xff14, 0x87);
        cupid_gb_write_u8(&batch, 0xff24, 0x77);
        cupid_gb_write_u8(&batch, 0xff25, 0xff);
        cupid_gb_tick_apu(&batch, 36);
        if (sample_boundary) {
            batch.apu.sample_acc = cupid_gb_base_clock_hz(&batch) - 2 * CUPID_GB_APU_SAMPLE_RATE;
        }
        CupidGb split = batch;
        cupid_gb_tick_apu(&batch, 4);
        for (unsigned dot = 0; dot < 4; ++dot) {
            cupid_gb_tick_apu(&split, 1);
        }
        assert(!batch.apu.ch1_on && !split.apu.ch1_on);
        /* Channel shutdown changes the mixer on its own base-clock edge,
         * including partial integration and a host sample crossing that edge. */
        assert(memcmp(&batch.apu, &split.apu, sizeof(batch.apu)) == 0);
    }
}

static void test_color_noise_prescaler_and_reload(void) {
    static const uint8_t start_ticks[4][8] = {
        {6, 10, 10, 14, 18, 22, 26, 30},
        {7, 9, 13, 17, 21, 25, 29, 33},
        {6, 8, 12, 16, 20, 24, 28, 32},
        {7, 11, 11, 15, 19, 23, 27, 31},
    };
    for (unsigned phase = 0; phase < 4; ++phase) {
        for (unsigned divisor = 0; divisor < 8; ++divisor) {
            CupidGb gb = {0};
            gb.model = CUPID_GB_MODEL_CGB;
            cupid_gb_write_u8(&gb, 0xff26, 0x80);
            cupid_gb_tick_apu(&gb, (uint16_t)(phase * 2));
            cupid_gb_write_u8(&gb, 0xff21, 0xf0);
            cupid_gb_write_u8(&gb, 0xff22, (uint8_t)divisor);
            cupid_gb_write_u8(&gb, 0xff23, 0x80);
            assert(cupid_gb_read_u8(&gb, 0xff77) == 0);
            cupid_gb_tick_apu(&gb, (uint16_t)(start_ticks[phase][divisor] * 2 - 1));
            assert(gb.apu.noise_timing.divider_counter == 0);
            cupid_gb_tick_apu(&gb, 1);
            assert(gb.apu.noise_timing.divider_counter == 1);
            assert(gb.apu.noise_timing.remaining == (divisor ? divisor * 4 : 2));
        }
    }

    CupidGb gb = {0};
    gb.model = CUPID_GB_MODEL_CGB;
    cupid_gb_write_u8(&gb, 0xff26, 0x80);
    cupid_gb_write_u8(&gb, 0xff21, 0xf0);
    cupid_gb_write_u8(&gb, 0xff23, 0x80);
    cupid_gb_tick_apu(&gb, 12);
    cupid_gb_write_u8(&gb, 0xff22, 3);
    assert(gb.apu.noise_timing.remaining == 12); /* Reload edge sees the new divisor. */
    cupid_gb_tick_apu(&gb, 2);
    cupid_gb_write_u8(&gb, 0xff22, 0);
    assert(gb.apu.noise_timing.remaining == 11); /* A running countdown is retained. */
    cupid_gb_tick_apu(&gb, 22);
    assert(gb.apu.noise_timing.remaining == 2);
}

static void test_color_noise_retrigger_and_gates(void) {
    CupidGb gb = {0};
    gb.model = CUPID_GB_MODEL_CGB;
    cupid_gb_write_u8(&gb, 0xff26, 0x80);
    cupid_gb_write_u8(&gb, 0xff21, 0xf0);
    cupid_gb_write_u8(&gb, 0xff23, 0x80);
    cupid_gb_tick_apu(&gb, 6);
    cupid_gb_write_u8(&gb, 0xff23, 0x80);
    assert(gb.apu.ch4_lfsr == 0x7faa);
    assert(cupid_gb_read_u8(&gb, 0xff77) == 0); /* Trigger holds zero despite the LFSR seed. */
    cupid_gb_write_u8(&gb, 0xff21, 0);
    uint16_t previous = gb.apu.noise_timing.divider_counter;
    cupid_gb_tick_apu(&gb, 32);
    assert(gb.apu.noise_timing.divider_counter != previous); /* Divisor zero retains background. */
    assert(!gb.apu.ch4_on && cupid_gb_read_u8(&gb, 0xff77) == 0);

    cupid_gb_write_u8(&gb, 0xff22, 1);
    cupid_gb_write_u8(&gb, 0xff21, 0xf0);
    cupid_gb_write_u8(&gb, 0xff23, 0x80);
    cupid_gb_tick_apu(&gb, 32);
    cupid_gb_write_u8(&gb, 0xff21, 0);
    previous = gb.apu.noise_timing.divider_counter;
    cupid_gb_tick_apu(&gb, 32);
    assert(gb.apu.noise_timing.divider_counter == previous);
    cupid_gb_write_u8(&gb, 0xff21, 0xf0);
    cupid_gb_tick_apu(&gb, 32);
    assert(gb.apu.noise_timing.divider_counter == previous); /* DAC enable is not a trigger. */
    cupid_gb_write_u8(&gb, 0xff26, 0);
    assert(gb.apu.noise_timing.divider_counter == 0);
    assert(!gb.apu.noise_timing.background && !gb.apu.noise_timing.foreground);
}

static void test_color_noise_selector_edge(void) {
    CupidGb gb = {0};
    gb.model = CUPID_GB_MODEL_CGB;
    cupid_gb_write_u8(&gb, 0xff26, 0x80);
    cupid_gb_write_u8(&gb, 0xff21, 0xf0);
    cupid_gb_write_u8(&gb, 0xff22, 0x38);
    cupid_gb_write_u8(&gb, 0xff23, 0x80);
    cupid_gb_tick_apu(&gb, 12);
    assert(gb.apu.noise_timing.divider_counter == 1 && gb.apu.ch4_lfsr == 0x7fff);
    cupid_gb_write_u8(&gb, 0xff22, 8);
    assert(gb.apu.ch4_lfsr == 0x3fbf);
    cupid_gb_write_u8(&gb, 0xff22, 0x38);
    assert(gb.apu.ch4_lfsr == 0x3fbf);
    cupid_gb_write_u8(&gb, 0xff22, 8);
    assert(gb.apu.ch4_lfsr == 0x1f9f);
}

static void test_color_noise_dac_off_counter_carry(void) {
    CupidGb gb = {0};
    gb.model = CUPID_GB_MODEL_CGB;
    cupid_gb_write_u8(&gb, 0xff26, 0x80);
    cupid_gb_write_u8(&gb, 0xff21, 0xf0);
    cupid_gb_write_u8(&gb, 0xff22, 9);
    cupid_gb_write_u8(&gb, 0xff23, 0x80);
    tick_audio(&gb, 65540u * 2);
    assert(gb.apu.noise_timing.divider_counter == 0x3fff);
    assert(gb.apu.noise_timing.remaining == 2);
    assert(gb.apu.ch4_lfsr == (0x47c7 ^ 0x7fff));
    cupid_gb_write_u8(&gb, 0xff21, 0);
    assert(gb.apu.noise_timing.divider_counter == 0x4000);
    assert(!gb.apu.noise_timing.foreground && !gb.apu.noise_timing.background);
    tick_audio(&gb, 16);
    assert(gb.apu.noise_timing.divider_counter == 0x4000);
    cupid_gb_write_u8(&gb, 0xff22, 0xe9);
    assert(gb.apu.ch4_lfsr == (0x63e3 ^ 0x7fff));
    assert(cupid_gb_read_u8(&gb, 0xff77) == 0);
    cupid_gb_write_u8(&gb, 0xff22, 9);
    /* Repeated gate transitions can carry beyond 0x4000 without a normal
     * prescaler edge. The held counter wraps at its sixteen-bit storage width. */
    for (unsigned restart = 0; restart < 0xc000; ++restart) {
        cupid_gb_write_u8(&gb, 0xff21, 0xf0);
        cupid_gb_write_u8(&gb, 0xff23, 0x80);
        cupid_gb_tick_apu(&gb, 16);
        cupid_gb_write_u8(&gb, 0xff21, 0);
        if (restart < 3) {
            assert(gb.apu.noise_timing.divider_counter == 0x4001 + restart);
        }
    }
    assert(gb.apu.noise_timing.divider_counter == 0);
}

static void test_audio_chunk_splitting(void) {
    const CupidGbModel models[] = {CUPID_GB_MODEL_DMG_ABC, CUPID_GB_MODEL_CGB, CUPID_GB_MODEL_SGB};
    const uint32_t rates[] = {8000, 44100, 192000};
    const uint16_t chunks[] = {1, 2, 4, 31, 257, UINT16_MAX};
    for (unsigned model = 0; model < sizeof(models) / sizeof(models[0]); ++model) {
        for (unsigned rate = 0; rate < sizeof(rates) / sizeof(rates[0]); ++rate) {
            for (unsigned scenario = 0; scenario < 6; ++scenario) {
                CupidGb batch = {0};
                batch.model = models[model];
                assert(cupid_gb_apu_set_sample_rate(&batch, rates[rate]));
                cupid_gb_write_u8(&batch, 0xff26, 0x80);
                cupid_gb_write_u8(&batch, 0xff10, scenario == 2 ? 0x17 : 0);
                cupid_gb_write_u8(&batch, 0xff11, 0x80);
                cupid_gb_write_u8(&batch, 0xff12, 0x89);
                cupid_gb_write_u8(&batch, 0xff13, 0xff);
                cupid_gb_write_u8(&batch, 0xff14, 0x87);
                cupid_gb_write_u8(&batch, 0xff21, 0xb3);
                cupid_gb_write_u8(&batch, 0xff22, 9);
                cupid_gb_write_u8(&batch, 0xff23, 0x80);
                cupid_gb_write_u8(&batch, 0xff24, 0x77);
                cupid_gb_write_u8(&batch, 0xff25, 0xff);
                if (scenario == 0) {
                    cupid_gb_write_u8(&batch, 0xff26, 0);
                } else if (scenario == 3) {
                    batch.apu.buf_write = CUPID_GB_APU_BUF_FRAMES;
                } else if (scenario == 4) {
                    batch.cpu.stopped = true;
                    batch.apu.ch3_wave_access_ticks = 3;
                } else if (scenario == 5) {
                    batch.apu.ch1_timer = 500;
                    batch.apu.ch2_timer = 500;
                    batch.apu.ch3_wave_access_ticks = 3;
                    batch.apu.ch4_timer = 2;
                    batch.apu.ch4_shift = 14;
                }
                for (unsigned chunk = 0; chunk < sizeof(chunks) / sizeof(chunks[0]); ++chunk) {
                    CupidGb split = batch;
                    cupid_gb_tick_apu(&batch, chunks[chunk]);
                    for (unsigned dot = 0; dot < chunks[chunk]; ++dot) {
                        cupid_gb_tick_apu(&split, 1);
                    }
                    assert(memcmp(&batch.apu, &split.apu, sizeof(batch.apu)) == 0);
                }
            }
        }
    }
}

static void test_color_pulse_phantom_trigger(void) {
    for (unsigned channel = 0; channel < 2; ++channel) {
        for (unsigned speed = 0; speed < 2; ++speed) {
            CupidGb gb = {0};
            gb.model = CUPID_GB_MODEL_CGB;
            gb.cgb_mode = true;
            gb.double_speed = speed != 0;
            unsigned base = channel ? 0xff16 : 0xff11;
            uint16_t *timer = channel ? &gb.apu.ch2_timer : &gb.apu.ch1_timer;
            uint8_t *index = channel ? &gb.apu.ch2_duty_pos : &gb.apu.ch1_duty_pos;
            CupidGbPulseTiming *timing = &gb.apu.pulse_timing[channel];
            cupid_gb_write_u8(&gb, 0xff26, 0x80);
            cupid_gb_write_u8(&gb, base, 0);
            cupid_gb_write_u8(&gb, base + 1, 0xf0);
            cupid_gb_write_u8(&gb, base + 2, 0);
            cupid_gb_write_u8(&gb, base + 3, 0x83);
            assert(*index == 0); /* Cold power has no pending frequency-counter edge. */
            assert(timing->output_blocked);
            *timer = 402;
            *index = 6;
            timing->start_delay = 0;
            timing->edge_age = 4;
            cupid_gb_write_u8(&gb, base + 3, 0x83);
            assert(*index == 7);
            assert(!timing->output_blocked);
            assert(((cupid_gb_apu_read_pcm12(&gb) >> (channel * 4)) & 15) == 15);
            /* Bit 2 gates the phantom edge; a real reload suppresses a second edge. */
            *timer = 402;
            *index = 6;
            timing->start_delay = 0;
            cupid_gb_write_u8(&gb, base + 3, 0x87);
            assert(*index == 6);
            timing->edge_age = 0;
            cupid_gb_write_u8(&gb, base + 3, 0x83);
            assert(*index == 6);
            assert(timing->edge_age == 0);
            /* Inactive channels retain the last reload until their clock restarts. */
            cupid_gb_write_u8(&gb, base + 1, 0);
            cupid_gb_tick_apu(&gb, 8);
            assert(timing->edge_age == 0);
            cupid_gb_write_u8(&gb, base + 2, 0xfe);
            assert(*timer == 4104);
            cupid_gb_write_u8(&gb, base + 3, 3);
            assert(*timer == 4104);
            cupid_gb_write_u8(&gb, base + 1, 0xf0);
            cupid_gb_write_u8(&gb, base + 3, 0x83);
            assert(*index == 6);
            assert(timing->output_blocked);
            /* A suppressed retrigger at the maximum frequency has four extra base clocks. */
            cupid_gb_write_u8(&gb, base + 2, 0xff);
            cupid_gb_write_u8(&gb, base + 3, 0x87);
            assert(timing->start_delay == 10);
            assert(*timer == 12);
            /* A retrigger retains reload writes but clears the prior real duty edge. */
            cupid_gb_write_u8(&gb, 0xff26, 0);
            cupid_gb_write_u8(&gb, 0xff26, 0x80);
            cupid_gb_write_u8(&gb, base + 1, 0xf0);
            cupid_gb_write_u8(&gb, base + 2, 0xfc);
            cupid_gb_write_u8(&gb, base + 3, 0x87);
            cupid_gb_tick_apu(&gb, 24);
            assert(*index == 1 && timing->edge_age == 0);
            cupid_gb_write_u8(&gb, base + 3, 0x87);
            cupid_gb_write_u8(&gb, base + 3, 3);
            assert(*index == 1);
            assert(*timer == 4112);
        }
    }
}

int main(void) {
    test_color_pulse_phantom_trigger();
    test_color_pulse_clock_and_held_duty();
    test_color_envelope_edges();
    test_color_frequency_reload_window();
    test_color_sweep_live_frequency_and_latency();
    test_color_sweep_overflow_mixer_edge();
    test_color_noise_prescaler_and_reload();
    test_color_noise_retrigger_and_gates();
    test_color_noise_selector_edge();
    test_color_noise_dac_off_counter_carry();
    test_audio_chunk_splitting();
    test_divider_clock(false);
    test_divider_clock(true);
    test_div_reset_edge();
    test_power_on_high_div();
    test_power_counter_reset(false);
    test_power_counter_reset(true);
    test_power_cycle_resets_sound_but_preserves_stream(false);
    test_power_cycle_resets_sound_but_preserves_stream(true);
    test_apu_ticks_do_not_clock_length();
    test_cgb_compatibility_uses_color_hardware();
    test_sweep_zero_period_and_shadow();
    test_pulse_pitch();
    test_noise_clock();
    test_partial_drain();
    test_stop_oscillator(false);
    test_stop_oscillator(true);
    test_dac_transfer_and_pan();
    test_highpass_dc_and_disconnect();
    test_duty_phase();
    test_sample_rates_and_batching();
    test_physical_model_clocks();
    test_fast_pulse_integration();
    test_pcm_taps();
    test_length_control_matrix();
    test_power_preserves_output_pipeline(false);
    test_power_preserves_output_pipeline(true);
    puts("APU timing and audio regression checks passed.");
    return 0;
}
