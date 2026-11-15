#ifndef CUPID_GB_PRINTER_INTERNAL_H
#define CUPID_GB_PRINTER_INTERNAL_H

#include "cupid/gb/printer.h"

enum {
    PRINTER_TILE_BYTES = 16,
    PRINTER_TILE_ROW_BYTES = CUPID_GB_PRINTER_WIDTH / 8 * PRINTER_TILE_BYTES,
    PRINTER_DATA_BYTES = PRINTER_TILE_ROW_BYTES * 2,
    PRINTER_MAX_ROWS = 200,
    PRINTER_BASE_CYCLES_PER_TICK = 256,
    PRINTER_STATE_WIRE_SIZE = 32687,
    PRINTER_STATE_CODEC_VERSION = 1,
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

/* Only device history lives here. The wire codec gives each field an explicit
 * width and never includes structure padding or the host registrations below. */
typedef struct CupidGbPrinterState {
    uint8_t phase;
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
} CupidGbPrinterState;

struct CupidGbPrinter {
    CupidGbPrinterImageCallback on_image;
    CupidGbPrinterDoneCallback on_done;
    void *user_data;
    CupidGbPrinterState state;
};

CupidGbPrinter *cupid_gb_printer_attached(const CupidGb *gb);
void cupid_gb_printer_state_clear(CupidGbPrinterState *state, uint64_t cycle);
bool cupid_gb_printer_state_valid(const CupidGbPrinterState *state, const CupidGb *gb);
bool cupid_gb_printer_state_encode(const CupidGbPrinterState *state, uint8_t *data, size_t length);
bool cupid_gb_printer_state_decode(CupidGbPrinterState *state, const uint8_t *data, size_t length);

#endif
