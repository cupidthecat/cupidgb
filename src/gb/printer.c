#include "cupid/gb/printer.h"

#include <stdlib.h>
#include <string.h>

enum {
    PRINTER_TILE_BYTES = 16,
    PRINTER_TILE_ROW_BYTES = CUPID_GB_PRINTER_WIDTH / 8 * PRINTER_TILE_BYTES,
    PRINTER_DATA_BYTES = PRINTER_TILE_ROW_BYTES * 2,
    PRINTER_MAX_ROWS = 200,
    PRINTER_BASE_CYCLES_PER_TICK = 256,
    PRINTER_INIT = 1,
    PRINTER_PRINT = 2,
    PRINTER_DATA = 4,
    PRINTER_CHECKSUM_ERROR = 1,
    PRINTER_PRINTING = 6,
    PRINTER_DONE = 4,
    PRINTER_BUFFERED = 8,
};

typedef enum PrinterPhase {
    PRINTER_SYNC_FIRST,
    PRINTER_SYNC_SECOND,
    PRINTER_HEADER,
    PRINTER_PAYLOAD,
    PRINTER_CHECKSUM,
    PRINTER_ALIVE,
    PRINTER_STATUS,
} PrinterPhase;

struct CupidGbPrinter {
    CupidGbPrinterImageCallback on_image;
    CupidGbPrinterDoneCallback on_done;
    void *user_data;
    PrinterPhase phase;
    uint8_t header[4];
    uint8_t payload[PRINTER_DATA_BYTES];
    uint16_t index;
    uint16_t payload_size;
    uint16_t decoded_size;
    uint16_t checksum;
    uint16_t received_checksum;
    uint8_t rle_remaining;
    bool rle_repeat;
    uint8_t status;
    uint8_t rows;
    uint8_t pixels[PRINTER_MAX_ROWS * CUPID_GB_PRINTER_WIDTH];
    uint8_t incoming;
    uint8_t incoming_bits;
    uint8_t reply;
    uint8_t reply_bit;
    bool output;
    bool clock_seen;
    uint64_t last_cycle;
    uint16_t sub_tick;
    uint64_t idle_ticks;
    uint32_t remaining_ticks;
};

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
    CupidGbPrinterImageCallback image = printer->on_image;
    CupidGbPrinterDoneCallback done = printer->on_done;
    void *user_data = printer->user_data;
    memset(printer, 0, sizeof(*printer));
    printer->on_image = image;
    printer->on_done = done;
    printer->user_data = user_data;
}

void cupid_gb_printer_update(CupidGbPrinter *printer, const CupidGb *gb) {
    if (!printer || !gb) {
        return;
    }
    const uint64_t now = gb->elapsed_cycles;
    if (!printer->clock_seen || now < printer->last_cycle) {
        printer->clock_seen = true;
        printer->last_cycle = now;
        printer->sub_tick = 0;
        return;
    }
    const uint64_t elapsed = now - printer->last_cycle;
    printer->last_cycle = now;
    const unsigned fraction =
        printer->sub_tick + (unsigned)(elapsed % PRINTER_BASE_CYCLES_PER_TICK);
    const uint64_t ticks =
        elapsed / PRINTER_BASE_CYCLES_PER_TICK + fraction / PRINTER_BASE_CYCLES_PER_TICK;
    printer->sub_tick = (uint16_t)(fraction % PRINTER_BASE_CYCLES_PER_TICK);
    if (printer->phase != PRINTER_SYNC_FIRST || printer->incoming_bits) {
        printer->idle_ticks += ticks;
    }
    if (printer->remaining_ticks && ticks >= printer->remaining_ticks) {
        printer->remaining_ticks = 0;
        if (printer->on_done) {
            printer->on_done(printer->user_data);
        }
    } else if (printer->remaining_ticks) {
        printer->remaining_ticks -= (uint32_t)ticks;
    }
}

static void decode(CupidGbPrinter *printer, uint8_t byte) {
    const unsigned available = PRINTER_DATA_BYTES - printer->decoded_size;
    if (!available) {
        return;
    }
    if (!(printer->header[1] & 1u)) {
        printer->payload[printer->decoded_size++] = byte;
        return;
    }
    /* The decoder retains a partial run across packet boundaries. */
    if (!printer->rle_remaining) {
        printer->rle_repeat = (byte & 0x80u) != 0;
        printer->rle_remaining = (uint8_t)((byte & 0x7fu) + 1u + printer->rle_repeat);
        return;
    }
    if (!printer->rle_repeat) {
        printer->payload[printer->decoded_size++] = byte;
        --printer->rle_remaining;
        return;
    }
    const unsigned count = printer->rle_remaining < available ? printer->rle_remaining : available;
    memset(printer->payload + printer->decoded_size, byte, count);
    printer->decoded_size += (uint16_t)count;
    printer->rle_remaining = 0;
}

static void append_tiles(CupidGbPrinter *printer, const uint8_t *tiles) {
    for (unsigned y = 0; y < 16 && printer->rows < PRINTER_MAX_ROWS; ++y, ++printer->rows) {
        uint8_t *scanline = printer->pixels + printer->rows * CUPID_GB_PRINTER_WIDTH;
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
    const uint8_t *parameters = printer->payload;
    printer->status = PRINTER_PRINTING;
    const uint32_t ticks = printer->rows * clock_hz / (PRINTER_BASE_CYCLES_PER_TICK * 8u);
    printer->remaining_ticks = ticks ? ticks : 1;
    if (printer->on_image) {
        const unsigned palette = parameters[2];
        for (unsigned i = 0; i < (unsigned)printer->rows * CUPID_GB_PRINTER_WIDTH; ++i) {
            const unsigned shade = (palette >> (2 * printer->pixels[i])) & 3u;
            printer->pixels[i] = (uint8_t)(255u - 85u * shade);
        }
        const CupidGbPrinterImage image = {
            .pixels = printer->pixels,
            .height = printer->rows,
            .top_margin = parameters[1] >> 4,
            .bottom_margin = parameters[1] & 0xfu,
            .exposure = parameters[3] & 0x7fu,
        };
        printer->on_image(&image, printer->user_data);
    }
    printer->rows = 0;
}

static unsigned command(const CupidGbPrinter *printer) {
    return printer->header[0] & 0xfu;
}

static void execute(CupidGbPrinter *printer, uint32_t clock_hz) {
    const unsigned id = command(printer);
    if (id == PRINTER_INIT) {
        printer->status = 0;
        printer->rows = 0;
        return;
    }
    if (id == PRINTER_PRINT && printer->decoded_size == 4) {
        print_job(printer, clock_hz);
    } else if (id == PRINTER_DATA && printer->decoded_size == PRINTER_DATA_BYTES) {
        append_tiles(printer, printer->payload);
        printer->status = PRINTER_BUFFERED;
    }
}

static void receive(CupidGbPrinter *printer, uint8_t byte, uint32_t clock_hz) {
    printer->reply = 0;
    printer->reply_bit = 0;
    switch (printer->phase) {
        case PRINTER_SYNC_FIRST:
            if (byte == 0x88) {
                printer->phase = PRINTER_SYNC_SECOND;
                printer->status &= (uint8_t)~PRINTER_CHECKSUM_ERROR;
                printer->checksum = 0;
                printer->decoded_size = 0;
            }
            break;
        case PRINTER_SYNC_SECOND:
            if (byte == 0x33) {
                printer->phase = PRINTER_HEADER;
                printer->index = 0;
            } else if (byte != 0x88) {
                printer->phase = PRINTER_SYNC_FIRST;
            }
            break;
        case PRINTER_HEADER:
            printer->header[printer->index++] = byte;
            printer->checksum += byte;
            if (printer->index == sizeof(printer->header)) {
                printer->payload_size =
                    (uint16_t)(printer->header[2] | ((printer->header[3] & 3u) << 8));
                printer->phase = printer->payload_size ? PRINTER_PAYLOAD : PRINTER_CHECKSUM;
                printer->index = 0;
            }
            break;
        case PRINTER_PAYLOAD:
            decode(printer, byte);
            printer->checksum += byte;
            if (++printer->index == printer->payload_size) {
                printer->phase = PRINTER_CHECKSUM;
                printer->index = 0;
            }
            break;
        case PRINTER_CHECKSUM:
            if (printer->index++ == 0) {
                printer->received_checksum = byte;
            } else {
                printer->received_checksum |= (uint16_t)((unsigned)byte << 8);
                if (printer->checksum != printer->received_checksum) {
                    printer->status |= PRINTER_CHECKSUM_ERROR;
                }
                printer->phase = PRINTER_ALIVE;
                printer->reply = 0x81;
            }
            break;
        case PRINTER_ALIVE:
            if (command(printer) != PRINTER_INIT ||
                printer->checksum != printer->received_checksum) {
                if (printer->status == PRINTER_PRINTING && !printer->remaining_ticks) {
                    printer->status = PRINTER_DONE;
                }
                printer->reply = printer->status;
            }
            printer->phase = PRINTER_STATUS;
            break;
        case PRINTER_STATUS:
            printer->phase = PRINTER_SYNC_FIRST;
            if (printer->checksum == printer->received_checksum) {
                execute(printer, clock_hz);
            }
            break;
    }
}

static void launch(const CupidGb *gb, bool outgoing, void *user_data) {
    CupidGbPrinter *printer = user_data;
    cupid_gb_printer_update(printer, gb);
    const uint32_t clock_hz = cupid_gb_base_clock_hz(gb);
    if (printer->idle_ticks > clock_hz) {
        printer->phase = PRINTER_SYNC_FIRST;
        printer->incoming_bits = 0;
        printer->incoming = 0;
    }
    printer->idle_ticks = 0;
    printer->incoming = (uint8_t)(2u * printer->incoming + outgoing);
    if (++printer->incoming_bits == 8) {
        receive(printer, printer->incoming, clock_hz);
        printer->incoming_bits = 0;
        printer->incoming = 0;
    }
}

static bool sample(const CupidGb *gb, void *user_data) {
    (void)gb;
    CupidGbPrinter *printer = user_data;
    const bool incoming = printer->output;
    /* The printer prepares the next output bit on the edge where the console
     * samples the currently driven bit. The byte parser runs at bit launch. */
    printer->output = printer->reply_bit < 8 && (printer->reply & (0x80u >> printer->reply_bit));
    if (printer->reply_bit < 8) {
        ++printer->reply_bit;
    }
    return incoming;
}

CupidGbSerialLink cupid_gb_printer_link(CupidGbPrinter *printer) {
    return printer ? (CupidGbSerialLink){launch, sample, printer} : (CupidGbSerialLink){0};
}
