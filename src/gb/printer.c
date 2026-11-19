/**
 * @file    printer.c
 * @brief   Serial printer packet handling, image generation, and print-job timing.
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

#include "printer_internal.h"

#include <stdlib.h>
#include <string.h>

CupidGbPrinter *cupid_gb_printer_create(CupidGbPrinterImageCallback image,
                                        CupidGbPrinterDoneCallback done, void *user_data) {
    CupidGbPrinter *printer = calloc(1, sizeof(*printer));
    if (printer) {
        printer->on_image = image;
        printer->on_done = done;
        printer->user_data = user_data;
    }
    return printer;
}

void cupid_gb_printer_destroy(CupidGbPrinter *printer) {
    free(printer);
}

void cupid_gb_printer_reset(CupidGbPrinter *printer) {
    if (!printer) {
        return;
    }
    memset(&printer->state, 0, sizeof(printer->state));
}

void cupid_gb_printer_update(CupidGbPrinter *printer, const CupidGb *gb) {
    if (!printer || !gb) {
        return;
    }
    CupidGbPrinterState *state = &printer->state;
    const uint64_t now = gb->elapsed_cycles;
    if (!state->clock_seen || now < state->last_cycle) {
        state->clock_seen = true;
        state->last_cycle = now;
        state->sub_tick = 0;
        return;
    }
    const uint64_t elapsed = now - state->last_cycle;
    state->last_cycle = now;
    const unsigned fraction = state->sub_tick + (unsigned)(elapsed % PRINTER_BASE_CYCLES_PER_TICK);
    const uint64_t ticks =
        elapsed / PRINTER_BASE_CYCLES_PER_TICK + fraction / PRINTER_BASE_CYCLES_PER_TICK;
    state->sub_tick = (uint16_t)(fraction % PRINTER_BASE_CYCLES_PER_TICK);
    if (state->phase != PRINTER_SYNC_FIRST || state->incoming_bits) {
        state->idle_ticks += ticks;
    }
    if (state->remaining_ticks && ticks >= state->remaining_ticks) {
        state->remaining_ticks = 0;
        if (printer->on_done) {
            printer->on_done(printer->user_data);
        }
    } else if (state->remaining_ticks) {
        state->remaining_ticks -= (uint32_t)ticks;
    }
}

static void decode(CupidGbPrinterState *state, uint8_t byte) {
    const unsigned available = PRINTER_DATA_BYTES - state->decoded_size;
    if (!available) {
        return;
    }
    if (!(state->header[1] & 1u)) {
        state->payload[state->decoded_size++] = byte;
        return;
    }
    /* The decoder retains a partial run across packet boundaries. */
    if (!state->rle_remaining) {
        state->rle_repeat = (byte & 0x80u) != 0;
        state->rle_remaining = (uint8_t)((byte & 0x7fu) + 1u + state->rle_repeat);
        return;
    }
    if (!state->rle_repeat) {
        state->payload[state->decoded_size++] = byte;
        --state->rle_remaining;
        return;
    }
    const unsigned count = state->rle_remaining < available ? state->rle_remaining : available;
    memset(state->payload + state->decoded_size, byte, count);
    state->decoded_size += (uint16_t)count;
    state->rle_remaining = 0;
}

static void append_tiles(CupidGbPrinterState *state, const uint8_t *tiles) {
    for (unsigned y = 0; y < 16 && state->rows < PRINTER_MAX_ROWS; ++y, ++state->rows) {
        uint8_t *scanline = state->pixels + state->rows * CUPID_GB_PRINTER_WIDTH;
        const uint8_t *planes = tiles + (y / 8) * PRINTER_TILE_ROW_BYTES + (y % 8) * 2;
        for (unsigned x = 0; x < CUPID_GB_PRINTER_WIDTH; ++x) {
            const unsigned tile = (x / 8) * PRINTER_TILE_BYTES;
            const unsigned shift = 7 - x % 8;
            scanline[x] = (uint8_t)(((planes[tile] >> shift) & 1u) |
                                    (((planes[tile + 1] >> shift) & 1u) << 1));
        }
    }
}

static void print_job(CupidGbPrinter *printer, uint32_t clock_hz) {
    CupidGbPrinterState *state = &printer->state;
    const uint8_t *parameters = state->payload;
    state->status = PRINTER_PRINTING;
    const uint32_t ticks = state->rows * clock_hz / (PRINTER_BASE_CYCLES_PER_TICK * 8u);
    state->remaining_ticks = ticks ? ticks : 1;
    if (printer->on_image) {
        const unsigned palette = parameters[2];
        for (unsigned i = 0; i < (unsigned)state->rows * CUPID_GB_PRINTER_WIDTH; ++i) {
            const unsigned shade = (palette >> (2 * state->pixels[i])) & 3u;
            state->pixels[i] = (uint8_t)(255u - 85u * shade);
        }
        const CupidGbPrinterImage image = {
            .pixels = state->pixels,
            .height = state->rows,
            .top_margin = parameters[1] >> 4,
            .bottom_margin = parameters[1] & 0xfu,
            .exposure = parameters[3] & 0x7fu,
        };
        printer->on_image(&image, printer->user_data);
    }
    state->rows = 0;
}

static unsigned command(const CupidGbPrinterState *state) {
    return state->header[0] & 0xfu;
}

static void execute(CupidGbPrinter *printer, uint32_t clock_hz) {
    CupidGbPrinterState *state = &printer->state;
    const unsigned id = command(state);
    if (id == PRINTER_INIT) {
        state->status = 0;
        state->rows = 0;
        return;
    }
    if (id == PRINTER_PRINT && state->decoded_size == 4) {
        print_job(printer, clock_hz);
    } else if (id == PRINTER_DATA && state->decoded_size == PRINTER_DATA_BYTES) {
        append_tiles(state, state->payload);
        state->status = PRINTER_BUFFERED;
    }
}

static void receive(CupidGbPrinter *printer, uint8_t byte, uint32_t clock_hz) {
    CupidGbPrinterState *state = &printer->state;
    state->reply = 0;
    state->reply_bit = 0;
    switch (state->phase) {
        case PRINTER_SYNC_FIRST:
            if (byte == 0x88) {
                state->phase = PRINTER_SYNC_SECOND;
                state->status &= (uint8_t)~PRINTER_CHECKSUM_ERROR;
                state->checksum = 0;
                state->decoded_size = 0;
            }
            break;
        case PRINTER_SYNC_SECOND:
            if (byte == 0x33) {
                state->phase = PRINTER_HEADER;
                state->index = 0;
            } else if (byte != 0x88) {
                state->phase = PRINTER_SYNC_FIRST;
            }
            break;
        case PRINTER_HEADER:
            state->header[state->index++] = byte;
            state->checksum += byte;
            if (state->index == sizeof(state->header)) {
                state->payload_size = (uint16_t)(state->header[2] | ((state->header[3] & 3u) << 8));
                state->phase = state->payload_size ? PRINTER_PAYLOAD : PRINTER_CHECKSUM;
                state->index = 0;
            }
            break;
        case PRINTER_PAYLOAD:
            decode(state, byte);
            state->checksum += byte;
            if (++state->index == state->payload_size) {
                state->phase = PRINTER_CHECKSUM;
                state->index = 0;
            }
            break;
        case PRINTER_CHECKSUM:
            if (state->index++ == 0) {
                state->received_checksum = byte;
            } else {
                state->received_checksum |= (uint16_t)((unsigned)byte << 8);
                if (state->checksum != state->received_checksum) {
                    state->status |= PRINTER_CHECKSUM_ERROR;
                }
                state->phase = PRINTER_ALIVE;
                state->reply = 0x81;
            }
            break;
        case PRINTER_ALIVE:
            if (command(state) != PRINTER_INIT || state->checksum != state->received_checksum) {
                if (state->status == PRINTER_PRINTING && !state->remaining_ticks) {
                    state->status = PRINTER_DONE;
                }
                state->reply = state->status;
            }
            state->phase = PRINTER_STATUS;
            break;
        case PRINTER_STATUS:
            state->phase = PRINTER_SYNC_FIRST;
            if (state->checksum == state->received_checksum) {
                execute(printer, clock_hz);
            }
            break;
    }
}

static void launch(const CupidGb *gb, bool outgoing, void *user_data) {
    CupidGbPrinter *printer = user_data;
    cupid_gb_printer_update(printer, gb);
    CupidGbPrinterState *state = &printer->state;
    const uint32_t clock_hz = cupid_gb_base_clock_hz(gb);
    if (state->idle_ticks > clock_hz) {
        state->phase = PRINTER_SYNC_FIRST;
        state->incoming_bits = 0;
        state->incoming = 0;
    }
    state->idle_ticks = 0;
    state->incoming = (uint8_t)(2u * state->incoming + outgoing);
    if (++state->incoming_bits == 8) {
        receive(printer, state->incoming, clock_hz);
        state->incoming_bits = 0;
        state->incoming = 0;
    }
}

static bool sample(const CupidGb *gb, void *user_data) {
    (void)gb;
    CupidGbPrinter *printer = user_data;
    CupidGbPrinterState *state = &printer->state;
    const bool incoming = state->output;
    /* The printer prepares the next output bit on the edge where the console
     * samples the currently driven bit. The byte parser runs at bit launch. */
    state->output = state->reply_bit < 8 && (state->reply & (0x80u >> state->reply_bit));
    if (state->reply_bit < 8) {
        ++state->reply_bit;
    }
    return incoming;
}

CupidGbSerialLink cupid_gb_printer_link(CupidGbPrinter *printer) {
    return printer ? (CupidGbSerialLink){.launch = launch,
                                         .sample = sample,
                                         .user_data = printer,
                                         .device = CUPID_GB_SERIAL_DEVICE_PRINTER}
                   : (CupidGbSerialLink){0};
}

CupidGbPrinter *cupid_gb_printer_attached(const CupidGb *gb) {
    if (!gb || gb->serial_link.device != CUPID_GB_SERIAL_DEVICE_PRINTER ||
        gb->serial_link.launch != launch || gb->serial_link.sample != sample)
        return NULL;
    /* A tag alone cannot turn arbitrary cable context into owned device data. */
    return gb->serial_link.user_data;
}
