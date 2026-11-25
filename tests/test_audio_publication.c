/**
 * @file    test_audio_publication.c
 * @brief   Audio publication boundaries after live native-state restoration.
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

#include "cupid/gb/apu.h"
#include "cupid/gb/state.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct PublicationFixture {
    CupidGb *gb;
    CupidGbAudioOutput receiving;
} PublicationFixture;

static const CupidGbModel models[] = {CUPID_GB_MODEL_DMG_ABC, CUPID_GB_MODEL_CGB,
                                      CUPID_GB_MODEL_SGB};

static int16_t routed_voltage(uint8_t sample) {
    return (int16_t)((15 - 2 * sample) * 8);
}

static bool quiet_boundary(const CupidGb *gb) {
    const CupidGbApu *apu = &gb->apu;
    return apu->ch1_timer > 4 && apu->ch2_timer > 4 && apu->ch3_timer > 4 &&
           apu->noise_timing.remaining > 4 && !apu->sweep_timing.startup_ticks &&
           !apu->sweep_timing.shift_ticks && !apu->sweep_timing.restart_hold &&
           apu->output.sample_acc + 4 * cupid_gb_apu_get_sample_rate(gb) <
               cupid_gb_base_clock_hz(gb) &&
           apu->output.render_cycles + 4 < apu->output.max_span;
}

static void wait_for_active_samples(CupidGb *gb) {
    for (unsigned cycles = 0; cycles < 32768; ++cycles) {
        cupid_gb_tick_apu(gb, 1);
        if (gb->apu.dac_sample[0] && gb->apu.dac_sample[1] && gb->apu.dac_sample[2] &&
            gb->apu.dac_sample[3] && quiet_boundary(gb))
            return;
    }
    assert(!"Active audio fixture did not reach a quiet output boundary");
}

static PublicationFixture create_fixture(CupidGbModel model) {
    PublicationFixture fixture = {0};
    fixture.gb = calloc(1, sizeof(*fixture.gb));
    assert(fixture.gb);
    CupidGb *gb = fixture.gb;
    uint8_t rom[32768] = {0};
    rom[0x100] = 0x18;
    rom[0x101] = 0xfe;
    memcpy(rom + 0x134, "AUDIO EVENTS", 12);
    rom[0x143] = model == CUPID_GB_MODEL_CGB ? 0x80 : 0;
    rom[0x146] = 3;
    for (unsigned offset = 0x134; offset <= 0x14c; ++offset)
        rom[0x14d] = (uint8_t)(rom[0x14d] - rom[offset] - 1);
    cupid_gb_set_model(gb, model);
    cupid_gb_set_power_on_seed(gb, 17);
    assert(cupid_gb_load_rom(gb, rom, sizeof(rom)));
    assert(cupid_gb_apu_set_sample_rate(gb, 44100));
    assert(cupid_gb_apu_set_filter_mode(gb, CUPID_GB_AUDIO_FILTER_OFF));
    cupid_gb_write_u8(gb, 0xff26, 0);
    cupid_gb_write_u8(gb, 0xff04, 0);
    cupid_gb_write_u8(gb, 0xff26, 0x80);
    cupid_gb_write_u8(gb, 0xff24, 0x77);
    cupid_gb_write_u8(gb, 0xff25, 0xf0);
    cupid_gb_write_u8(gb, 0xff10, 0);
    cupid_gb_write_u8(gb, 0xff11, 0x80);
    cupid_gb_write_u8(gb, 0xff12, 0xa0);
    cupid_gb_write_u8(gb, 0xff13, 0xe0);
    cupid_gb_write_u8(gb, 0xff14, 0x87);
    cupid_gb_write_u8(gb, 0xff16, 0x80);
    cupid_gb_write_u8(gb, 0xff17, 0xb0);
    cupid_gb_write_u8(gb, 0xff18, 0xe0);
    cupid_gb_write_u8(gb, 0xff19, 0x87);
    for (unsigned offset = 0; offset < 16; ++offset)
        cupid_gb_write_u8(gb, (uint16_t)(0xff30 + offset), 0xbb);
    cupid_gb_write_u8(gb, 0xff1a, 0x80);
    cupid_gb_write_u8(gb, 0xff1c, 0x20);
    cupid_gb_write_u8(gb, 0xff1d, 0xc0);
    cupid_gb_write_u8(gb, 0xff1e, 0x87);
    cupid_gb_write_u8(gb, 0xff21, 0xc0);
    cupid_gb_write_u8(gb, 0xff22, 0x05);
    cupid_gb_write_u8(gb, 0xff23, 0x80);
    wait_for_active_samples(gb);
    gb->apu.fs_step = gb->apu.fs_skip_div_event = 0;
    return fixture;
}

static void destroy_fixture(PublicationFixture *fixture) {
    cupid_gb_cleanup(fixture->gb);
    free(fixture->gb);
}

static void restore_with_receiving_routing(PublicationFixture *fixture, bool cached) {
    CupidGb *gb = fixture->gb;
    const size_t size = cupid_gb_state_size(gb);
    uint8_t *state = malloc(size);
    assert(state);
    size_t written = 0;
    assert(cupid_gb_save_state(gb, state, size, &written) == CUPID_GB_STATE_OK);
    assert(written == size && !gb->apu.pending_cycles);
    const uint8_t samples[4] = {gb->apu.dac_sample[0], gb->apu.dac_sample[1], gb->apu.dac_sample[2],
                                gb->apu.dac_sample[3]};

    /* The receiving stream owns right-routed reconstruction and queued samples.
     * Loading restores left-routed guest registers without replaying that write. */
    cupid_gb_write_u8(gb, 0xff25, 0x0f);
    memcpy(&fixture->receiving, &gb->apu.output, sizeof(fixture->receiving));
    fixture->receiving.sample_fraction = 0;
    fixture->receiving.fraction_half = false;
    if (cached) {
        CupidGbStateContext context = {0};
        assert(cupid_gb_state_context_init(&context, gb) == CUPID_GB_STATE_OK);
        assert(cupid_gb_load_state_live_with_context(gb, state, size, &context) ==
               CUPID_GB_STATE_OK);
    } else {
        assert(cupid_gb_load_state_live(gb, state, size) == CUPID_GB_STATE_OK);
    }
    free(state);
    assert(gb->io_registers[0x25] == 0xf0);
    assert(memcmp(samples, gb->apu.dac_sample, sizeof(samples)) == 0);
    assert(memcmp(&fixture->receiving, &gb->apu.output, sizeof(fixture->receiving)) == 0);
    for (unsigned channel = 0; channel < 4; ++channel) {
        assert(gb->apu.output.channels[channel].input[0] == 0);
        assert(gb->apu.output.channels[channel].input[1] == routed_voltage(samples[channel]));
    }
}

static void assert_queue_preserved(const PublicationFixture *fixture) {
    const CupidGbAudioOutput *output = &fixture->gb->apu.output;
    assert(output->buf_write == fixture->receiving.buf_write);
    assert(memcmp(output->buf, fixture->receiving.buf,
                  output->buf_write * 2 * sizeof(output->buf[0])) == 0);
}

static void assert_no_publication(const PublicationFixture *fixture) {
    assert(memcmp(fixture->receiving.channels, fixture->gb->apu.output.channels,
                  sizeof(fixture->receiving.channels)) == 0);
    assert_queue_preserved(fixture);
}

static void assert_publication(const PublicationFixture *fixture, unsigned mask) {
    const CupidGbApu *apu = &fixture->gb->apu;
    for (unsigned channel = 0; channel < 4; ++channel) {
        const CupidGbAudioChannel *history = &apu->output.channels[channel];
        if (mask & (1u << channel)) {
            assert(history->input[0] == routed_voltage(apu->dac_sample[channel]));
            assert(history->input[1] == 0);
            assert(memcmp(history, &fixture->receiving.channels[channel], sizeof(*history)) != 0);
        } else {
            assert(memcmp(history, &fixture->receiving.channels[channel], sizeof(*history)) == 0);
        }
    }
    assert_queue_preserved(fixture);
}

static void set_length(CupidGb *gb, unsigned channel, unsigned length) {
    static const uint8_t registers[] = {0x14, 0x19, 0x1e, 0x23};
    gb->io_registers[registers[channel]] |= 0x40;
    switch (channel) {
        case 0:
            gb->apu.ch1_len = (uint8_t)length;
            gb->apu.ch1_len_en = true;
            break;
        case 1:
            gb->apu.ch2_len = (uint8_t)length;
            gb->apu.ch2_len_en = true;
            break;
        case 2:
            gb->apu.ch3_len = (uint16_t)length;
            gb->apu.ch3_len_en = true;
            break;
        default:
            gb->apu.ch4_len = (uint8_t)length;
            gb->apu.ch4_len_en = true;
            break;
    }
}

static void test_idle_divider(void) {
    for (unsigned model = 0; model < sizeof(models) / sizeof(*models); ++model)
        for (unsigned phase = 0; phase < 8; ++phase)
            for (unsigned lengths = 0; lengths < 2; ++lengths) {
                PublicationFixture fixture = create_fixture(models[model]);
                CupidGb *gb = fixture.gb;
                gb->apu.fs_step = (uint8_t)phase;
                if (lengths)
                    for (unsigned channel = 0; channel < 4; ++channel)
                        set_length(gb, channel, 2);
                restore_with_receiving_routing(&fixture, lengths != 0);
                cupid_gb_apu_div_event(gb);
                assert(gb->apu.fs_step == ((phase + 1) & 7));
                if (lengths) {
                    const unsigned expected = phase & 1 ? 2 : 1;
                    assert(gb->apu.ch1_len == expected && gb->apu.ch2_len == expected &&
                           gb->apu.ch3_len == expected && gb->apu.ch4_len == expected);
                }
                assert_no_publication(&fixture);
                assert(memcmp(&fixture.receiving, &gb->apu.output, sizeof(fixture.receiving)) == 0);
                destroy_fixture(&fixture);
            }
}

static void test_length_expiry(void) {
    for (unsigned model = 0; model < sizeof(models) / sizeof(*models); ++model)
        for (unsigned channel = 0; channel < 4; ++channel) {
            PublicationFixture fixture = create_fixture(models[model]);
            set_length(fixture.gb, channel, 1);
            restore_with_receiving_routing(&fixture, (channel & 1) != 0);
            cupid_gb_apu_div_event(fixture.gb);
            assert((cupid_gb_read_u8(fixture.gb, 0xff26) & (1u << channel)) == 0);
            assert(fixture.gb->apu.dac_sample[channel] == 0);
            assert_publication(&fixture, 1u << channel);
            destroy_fixture(&fixture);
        }
}

static void set_envelope(CupidGb *gb, unsigned channel, uint8_t period, uint8_t countdown) {
    static const uint8_t registers[] = {0x12, 0x17, 0, 0x21};
    assert(channel != 2);
    gb->io_registers[registers[channel]] =
        (uint8_t)((gb->io_registers[registers[channel]] & 0xf0) | period);
    switch (channel) {
        case 0:
            gb->apu.ch1_env_period = period;
            gb->apu.ch1_env_timer = countdown;
            gb->apu.ch1_env_add = false;
            break;
        case 1:
            gb->apu.ch2_env_period = period;
            gb->apu.ch2_env_timer = countdown;
            gb->apu.ch2_env_add = false;
            break;
        default:
            gb->apu.ch4_env_period = period;
            gb->apu.ch4_env_timer = countdown;
            gb->apu.ch4_env_add = false;
            break;
    }
}

static void test_envelope_steps(void) {
    for (unsigned model = 0; model < sizeof(models) / sizeof(*models); ++model)
        for (unsigned channel = 0; channel < 4; ++channel) {
            if (channel == 2 || cupid_gb_model_is_cgb(models[model]))
                continue;
            for (unsigned expires = 0; expires < 2; ++expires) {
                PublicationFixture fixture = create_fixture(models[model]);
                CupidGb *gb = fixture.gb;
                gb->apu.fs_step = 6;
                set_envelope(gb, channel, 2, expires ? 1 : 2);
                cupid_gb_apu_div_event(gb);
                cupid_gb_apu_div_secondary_event(gb);
                const uint8_t previous = gb->apu.dac_sample[channel];
                restore_with_receiving_routing(&fixture, expires != 0);
                cupid_gb_apu_div_event(gb);
                assert(gb->apu.dac_sample[channel] == previous - expires);
                if (expires)
                    assert_publication(&fixture, 1u << channel);
                else
                    assert_no_publication(&fixture);
                destroy_fixture(&fixture);
            }
        }
}

static void run_color_envelope(CupidGb *gb, unsigned delivery) {
    switch (delivery) {
        case 0:
            cupid_gb_apu_div_event(gb);
            break;
        case 1:
            cupid_gb_apu_finish_divider_period(gb);
            break;
        default:
            cupid_gb_tick_apu(gb, 2);
            break;
    }
}

static void test_color_envelope(void) {
    /* Test each public delivery route: ordinary DIV, completion before another
     * divider boundary, and two explicit base clocks in double-speed mode. */
    for (unsigned delivery = 0; delivery < 3; ++delivery)
        for (unsigned channel = 0; channel < 4; ++channel)
            for (unsigned condition = 0; condition < 5; ++condition) {
                if (channel == 2)
                    continue;
                PublicationFixture fixture = create_fixture(CUPID_GB_MODEL_CGB);
                CupidGb *gb = fixture.gb;
                gb->apu.fs_step = 1;
                gb->double_speed = delivery != 0;
                gb->apu.envelope_delay = delivery ? 2 : 0;
                set_envelope(gb, channel, condition == 1 ? 0 : 1, 1);
                CupidGbEnvelopeGate *gate = channel == 3 ? &gb->apu.noise_timing.envelope
                                                         : &gb->apu.pulse_timing[channel].envelope;
                gate->clock = condition != 0;
                gate->locked = condition == 2;
                gate->lock_pending = condition == 3;
                const uint8_t previous = gb->apu.dac_sample[channel];
                restore_with_receiving_routing(&fixture, (condition & 1) != 0);
                run_color_envelope(gb, delivery);
                assert(!gate->clock);
                assert(!gb->apu.envelope_delay);
                if (condition == 4) {
                    assert(gb->apu.dac_sample[channel] == previous - 1);
                    assert_publication(&fixture, 1u << channel);
                } else {
                    assert(gb->apu.dac_sample[channel] == previous);
                    assert_no_publication(&fixture);
                }
                destroy_fixture(&fixture);
            }
}

static void test_sweep(void) {
    const CupidGbModel monochrome[] = {CUPID_GB_MODEL_DMG_ABC, CUPID_GB_MODEL_SGB};
    for (unsigned model = 0; model < sizeof(monochrome) / sizeof(*monochrome); ++model)
        for (unsigned overflow = 0; overflow < 2; ++overflow) {
            PublicationFixture fixture = create_fixture(monochrome[model]);
            CupidGb *gb = fixture.gb;
            gb->apu.fs_step = 2;
            gb->io_registers[0x10] = 0x11;
            gb->apu.ch1_sweep_en = true;
            gb->apu.ch1_sweep_period = gb->apu.ch1_sweep_timer = 1;
            gb->apu.ch1_sweep_shift = 1;
            gb->apu.ch1_sweep_neg = false;
            gb->apu.ch1_sweep_shadow = overflow ? 1200 : 512;
            restore_with_receiving_routing(&fixture, overflow != 0);
            cupid_gb_apu_div_event(gb);
            if (overflow) {
                assert(!gb->apu.ch1_on && gb->apu.ch1_freq == 1800);
                assert(gb->apu.dac_sample[0] == 0);
                assert_publication(&fixture, 1);
            } else {
                assert(gb->apu.ch1_on && gb->apu.ch1_freq == 768);
                assert_no_publication(&fixture);
            }
            destroy_fixture(&fixture);
        }
}

static void test_configuration_writes(void) {
    static const struct {
        uint8_t reg;
        uint8_t value;
    } writes[] = {{0x10, 0},    {0x11, 0x81}, {0x16, 0x81}, {0x13, 0xe1},
                  {0x18, 0xe1}, {0x1b, 0x12}, {0x1d, 0xc1}, {0x20, 0x12}};

    for (unsigned model = 0; model < sizeof(models) / sizeof(*models); ++model)
        for (unsigned index = 0; index < sizeof(writes) / sizeof(*writes); ++index) {
            PublicationFixture fixture = create_fixture(models[model]);
            restore_with_receiving_routing(&fixture, (index & 1) != 0);
            cupid_gb_write_u8(fixture.gb, (uint16_t)(0xff00 + writes[index].reg),
                              writes[index].value);
            assert_no_publication(&fixture);
            assert(memcmp(&fixture.receiving, &fixture.gb->apu.output, sizeof(fixture.receiving)) ==
                   0);
            destroy_fixture(&fixture);
        }
}

static void test_mixer_writes(void) {
    for (unsigned model = 0; model < sizeof(models) / sizeof(*models); ++model)
        for (unsigned reg = 0x24; reg <= 0x25; ++reg) {
            PublicationFixture fixture = create_fixture(models[model]);
            restore_with_receiving_routing(&fixture, reg == 0x25);
            /* Even a repeated mixer value is an explicit routing publication. */
            cupid_gb_write_u8(fixture.gb, (uint16_t)(0xff00 + reg), fixture.gb->io_registers[reg]);
            assert_publication(&fixture, 15);
            destroy_fixture(&fixture);
        }
}

static void test_envelope_length_order(void) {
    PublicationFixture fixture = create_fixture(CUPID_GB_MODEL_CGB);
    CupidGb *gb = fixture.gb;
    cupid_gb_write_u8(gb, 0xff12, 0x09);
    cupid_gb_write_u8(gb, 0xff14, 0x87);
    bool ready = false;
    for (unsigned cycles = 0; cycles < 32768; ++cycles) {
        cupid_gb_tick_apu(gb, 1);
        if (!gb->apu.pulse_timing[0].output_blocked && gb->apu.pulse_timing[0].output_high &&
            quiet_boundary(gb)) {
            ready = true;
            break;
        }
    }
    assert(ready && gb->apu.ch1_on && gb->apu.ch1_vol == 0 && gb->apu.dac_sample[0] == 0);
    set_length(gb, 0, 1);
    gb->apu.fs_step = 0;
    gb->apu.pulse_timing[0].envelope.clock = true;
    gb->apu.pulse_timing[0].envelope.locked = false;
    gb->apu.pulse_timing[0].envelope.lock_pending = false;
    restore_with_receiving_routing(&fixture, true);
    cupid_gb_apu_div_event(gb);
    /* The envelope publishes code one before length expiry publishes zero.
     * Combining both into a final zero would incorrectly skip the routing edge. */
    assert(gb->apu.ch1_vol == 1 && !gb->apu.ch1_on && gb->apu.dac_sample[0] == 0);
    assert_publication(&fixture, 1);
    destroy_fixture(&fixture);
}

static void test_frequency_envelope_order(void) {
    PublicationFixture fixture = create_fixture(CUPID_GB_MODEL_CGB);
    CupidGb *gb = fixture.gb;
    gb->double_speed = true;
    cupid_gb_write_u8(gb, 0xff12, 0x11);
    cupid_gb_write_u8(gb, 0xff14, 0x87);
    bool ready = false;
    for (unsigned cycles = 0; cycles < 32768; ++cycles) {
        cupid_gb_tick_apu(gb, 1);
        const CupidGbApu *apu = &gb->apu;
        /* Duty two rises when position four advances to five. Leave the other
         * channels and the host sample boundary outside these two base clocks. */
        if (apu->ch1_duty_pos == 4 && apu->ch1_timer == 2 && apu->ch2_timer > 2 &&
            apu->ch3_timer > 2 && apu->noise_timing.remaining > 2 &&
            !apu->pulse_timing[0].output_blocked && !apu->pulse_timing[0].output_high &&
            apu->output.sample_acc + 2 * cupid_gb_apu_get_sample_rate(gb) <
                cupid_gb_base_clock_hz(gb) &&
            apu->output.render_cycles + 2 < apu->output.max_span) {
            ready = true;
            break;
        }
    }
    assert(ready && gb->apu.ch1_on && gb->apu.ch1_vol == 1 && gb->apu.dac_sample[0] == 0);
    gb->apu.envelope_delay = 2;
    gb->apu.pulse_timing[0].envelope.clock = true;
    gb->apu.pulse_timing[0].envelope.locked = false;
    gb->apu.pulse_timing[0].envelope.lock_pending = false;
    restore_with_receiving_routing(&fixture, true);
    cupid_gb_tick_apu(gb, 1);
    assert(gb->apu.ch1_vol == 1 && gb->apu.dac_sample[0] == 0);
    assert_no_publication(&fixture);
    cupid_gb_tick_apu(gb, 1);
    /* The oscillator first publishes code one, then the delayed envelope
     * publishes zero at the same offset. A final-sample-only update loses both. */
    assert(gb->apu.ch1_on && gb->apu.ch1_vol == 0 && gb->apu.dac_sample[0] == 0);
    assert(gb->apu.pulse_timing[0].output_high && !gb->apu.envelope_delay);
    assert_publication(&fixture, 1);
    destroy_fixture(&fixture);
}

static void test_noise_register_edges(void) {
    static const uint8_t unchanged_selector[] = {0x05, 0x06, 0x0d};
    for (unsigned index = 0; index < sizeof(unchanged_selector); ++index) {
        PublicationFixture fixture = create_fixture(CUPID_GB_MODEL_CGB);
        restore_with_receiving_routing(&fixture, (index & 1) != 0);
        cupid_gb_write_u8(fixture.gb, 0xff22, unchanged_selector[index]);
        assert_no_publication(&fixture);
        assert(memcmp(&fixture.receiving, &fixture.gb->apu.output, sizeof(fixture.receiving)) == 0);
        destroy_fixture(&fixture);
    }

    const CupidGbModel monochrome[] = {CUPID_GB_MODEL_DMG_ABC, CUPID_GB_MODEL_SGB};
    for (unsigned model = 0; model < sizeof(monochrome) / sizeof(*monochrome); ++model) {
        PublicationFixture fixture = create_fixture(monochrome[model]);
        CupidGb *gb = fixture.gb;
        bool ready = false;
        for (unsigned cycles = 0; cycles < 131072; ++cycles) {
            cupid_gb_tick_apu(gb, 1);
            /* This reachable divider selection produces two shifts during one
             * repeated write. Their held tap sequence is zero, one, then zero. */
            if ((gb->apu.noise_timing.divider_counter & 0x89u) == 0x81u &&
                (gb->apu.ch4_lfsr & 7u) == 5u && !gb->apu.noise_timing.reload_edge) {
                ready = true;
                break;
            }
        }
        assert(ready && gb->apu.ch4_on && gb->apu.dac_sample[3] == 0);
        assert(!gb->apu.noise_timing.output_high);
        restore_with_receiving_routing(&fixture, model != 0);
        cupid_gb_write_u8(gb, 0xff22, 0x05);
        assert(gb->apu.ch4_on && gb->apu.dac_sample[3] == 0);
        assert(!gb->apu.noise_timing.output_high);
        assert_publication(&fixture, 8);
        destroy_fixture(&fixture);
    }
}

static void test_noise_restart_order(void) {
    const CupidGbModel monochrome[] = {CUPID_GB_MODEL_DMG_ABC, CUPID_GB_MODEL_SGB};
    for (unsigned model = 0; model < sizeof(monochrome) / sizeof(*monochrome); ++model) {
        PublicationFixture fixture = create_fixture(monochrome[model]);
        CupidGb *gb = fixture.gb;
        cupid_gb_write_u8(gb, 0xff26, 0);
        cupid_gb_write_u8(gb, 0xff04, 0);
        cupid_gb_write_u8(gb, 0xff26, 0x80);
        cupid_gb_write_u8(gb, 0xff24, 0x77);
        cupid_gb_write_u8(gb, 0xff25, 0xf0);
        cupid_gb_write_u8(gb, 0xff21, 0xc0);
        cupid_gb_write_u8(gb, 0xff22, 0x10);
        assert(gb->apu.noise_timing.clock_phase == 0);
        cupid_gb_write_u8(gb, 0xff23, 0x80);
        bool ready = false;
        for (unsigned cycles = 0; cycles < 32768; ++cycles) {
            cupid_gb_tick_apu(gb, 1);
            const CupidGbApu *apu = &gb->apu;
            /* A phase-four retrigger takes twelve base clocks. With divisor
             * zero and selector one, this boundary reaches the next rising
             * selector edge on the exact clock that completes the restart. */
            if (apu->noise_timing.clock_phase == 4 && apu->noise_timing.remaining == 2 &&
                (apu->noise_timing.divider_counter & 3u) == 3u && (apu->ch4_lfsr & 3u) == 1u &&
                apu->output.sample_acc + 12 * cupid_gb_apu_get_sample_rate(gb) <
                    cupid_gb_base_clock_hz(gb) &&
                apu->output.render_cycles + 12 < apu->output.max_span) {
                ready = true;
                break;
            }
        }
        assert(ready && gb->apu.ch4_on && gb->apu.dac_sample[3] == 0);
        cupid_gb_write_u8(gb, 0xff23, 0x80);
        assert(gb->apu.noise_timing.start_delay == 12 && gb->apu.dac_sample[3] == 0);
        restore_with_receiving_routing(&fixture, model != 0);
        cupid_gb_tick_apu(gb, 11);
        assert(gb->apu.noise_timing.start_delay == 1 && gb->apu.dac_sample[3] == 0);
        assert_no_publication(&fixture);
        cupid_gb_tick_apu(gb, 1);
        /* The intermediate high tap must publish the restored routing before
         * restart returns to zero. A lone final zero would leave it on the right. */
        assert(gb->apu.ch4_on && gb->apu.ch4_vol == 12 && gb->apu.dac_sample[3] == 0);
        assert(!gb->apu.noise_timing.start_delay && !gb->apu.noise_timing.output_high);
        assert_publication(&fixture, 8);
        destroy_fixture(&fixture);
    }
}

int main(int argc, char **argv) {
    static const struct {
        const char *name;
        void (*run)(void);
    } groups[] = {{"idle", test_idle_divider},
                  {"length", test_length_expiry},
                  {"envelope", test_envelope_steps},
                  {"color-envelope", test_color_envelope},
                  {"sweep", test_sweep},
                  {"registers", test_configuration_writes},
                  {"mixer", test_mixer_writes},
                  {"order", test_envelope_length_order},
                  {"oscillator-order", test_frequency_envelope_order},
                  {"noise-registers", test_noise_register_edges},
                  {"noise-restart", test_noise_restart_order}};

    if (argc > 2)
        return 2;
    unsigned selected = 0;
    for (unsigned index = 0; index < sizeof(groups) / sizeof(*groups); ++index) {
        if (argc == 2 && strcmp(argv[1], groups[index].name) != 0)
            continue;
        printf("Audio publication group: %s\n", groups[index].name);
        fflush(stdout);
        groups[index].run();
        ++selected;
    }
    if (!selected)
        return 2;
    puts("Audio publication checks passed");
    return 0;
}
