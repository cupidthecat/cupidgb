#include "cupid/gb/printer.h"
#include "cupid/gb/timer.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

typedef struct Capture {
    uint8_t pixels[CUPID_GB_PRINTER_WIDTH * 200];
    uint8_t height;
    uint8_t top;
    uint8_t bottom;
    uint8_t exposure;
    unsigned pages;
    unsigned completed;
} Capture;

static void on_image(const CupidGbPrinterImage *image, void *user_data) {
    Capture *capture = user_data;
    capture->height = image->height;
    capture->top = image->top_margin;
    capture->bottom = image->bottom_margin;
    capture->exposure = image->exposure;
    memcpy(capture->pixels, image->pixels, (size_t)image->height * CUPID_GB_PRINTER_WIDTH);
    ++capture->pages;
}

static void on_done(void *user_data) {
    Capture *capture = user_data;
    ++capture->completed;
}

static uint8_t send_byte(CupidGb *gb, CupidGbPrinter *printer, uint8_t outgoing) {
    cupid_gb_write_u8(gb, 0xff01, outgoing);
    cupid_gb_write_u8(gb, 0xff02, 0x83);
    cupid_gb_tick(gb, 32);
    cupid_gb_printer_update(printer, gb);
    assert((cupid_gb_read_u8(gb, 0xff02) & 0x80u) == 0);
    return cupid_gb_read_u8(gb, 0xff01);
}

static uint8_t send_packet(CupidGb *gb, CupidGbPrinter *printer, uint8_t command, bool compressed,
                           const uint8_t *payload, uint16_t length, bool corrupt) {
    uint8_t header[] = {0x88, 0x33, command, compressed, (uint8_t)length, (uint8_t)(length >> 8)};
    unsigned checksum = 0;
    for (unsigned i = 0; i < sizeof(header); ++i) {
        assert(send_byte(gb, printer, header[i]) == 0);
        if (i >= 2) {
            checksum += header[i];
        }
    }
    for (unsigned i = 0; i < length; ++i) {
        assert(send_byte(gb, printer, payload[i]) == 0);
        checksum += payload[i];
    }
    checksum ^= corrupt;
    assert(send_byte(gb, printer, (uint8_t)checksum) == 0);
    assert(send_byte(gb, printer, (uint8_t)(checksum >> 8)) == 0);
    assert(send_byte(gb, printer, 0) == 0x81);
    return send_byte(gb, printer, 0);
}

int main(void) {
    Capture capture = {0};
    CupidGb *gb = calloc(1, sizeof(*gb));
    CupidGbPrinter *printer = cupid_gb_printer_create(on_image, on_done, &capture);
    assert(gb && printer);
    gb->model = CUPID_GB_MODEL_CGB;
    gb->cgb_mode = true;
    cupid_gb_set_serial_link(gb, cupid_gb_printer_link(printer));
    assert(send_packet(gb, printer, 0x0f, false, NULL, 0, true) == 1);
    assert(send_packet(gb, printer, 0x01, false, NULL, 0, false) == 0);

    uint8_t tiles[640] = {0};
    tiles[0] = 0xa0;
    tiles[1] = 0x60;
    tiles[16] = 0x80;
    tiles[17] = 0x80;
    tiles[320] = 0x01;
    const uint8_t rle[] = {1, 0xa0, 0x60, 0xff, 0, 0xff, 0, 0xff, 0, 0xff, 0, 0xf8, 0};
    const uint8_t parameters[] = {1, 0x21, 0xe4, 0xa0};
    assert(send_packet(gb, printer, 0x04, false, tiles, sizeof(tiles), false) == 0);
    assert(send_packet(gb, printer, 0x04, true, rle, sizeof(rle), false) == 8);
    assert(send_packet(gb, printer, 0x02, false, parameters, sizeof(parameters), true) == 9);
    assert(capture.pages == 0);
    assert(send_packet(gb, printer, 0x02, false, parameters, sizeof(parameters), false) == 8);
    assert(capture.pages == 1 && capture.height == 32 && capture.top == 2 && capture.bottom == 1);
    assert(capture.exposure == 0x20);
    assert(capture.pixels[0] == 0xaa && capture.pixels[1] == 0x55 && capture.pixels[2] == 0);
    assert(capture.pixels[3] == 0xff && capture.pixels[8] == 0);
    assert(capture.pixels[8 * CUPID_GB_PRINTER_WIDTH + 7] == 0xaa);
    assert(capture.pixels[16 * CUPID_GB_PRINTER_WIDTH + 1] == 0x55);
    assert(capture.pixels[16 * CUPID_GB_PRINTER_WIDTH + 8] == 0xff);

    assert(send_packet(gb, printer, 0x0f, false, NULL, 0, false) == 6);
    const uint64_t second = cupid_gb_base_clock_hz(gb);
    gb->elapsed_cycles += 3 * second;
    cupid_gb_printer_update(printer, gb);
    assert(capture.completed == 0);
    gb->elapsed_cycles -= second;
    cupid_gb_printer_update(printer, gb);
    assert(capture.completed == 0);
    gb->elapsed_cycles += second;
    cupid_gb_printer_update(printer, gb);
    assert(capture.completed == 1);
    assert(send_packet(gb, printer, 0x0f, false, NULL, 0, false) == 4);

    assert(send_packet(gb, printer, 0x02, false, parameters, sizeof(parameters), false) == 4);
    assert(capture.pages == 2 && capture.height == 0);
    gb->elapsed_cycles += 256;
    cupid_gb_printer_update(printer, gb);
    assert(capture.completed == 2);

    assert(send_byte(gb, printer, 0x88) == 0);
    assert(send_byte(gb, printer, 0x33) == 0);
    cupid_gb_printer_reset(printer);
    assert(send_packet(gb, printer, 0x0f, false, NULL, 0, false) == 0);

    const uint8_t truncated_run[] = {0x80};
    const uint8_t next_run[] = {0xa0, 0xff, 0, 0xff, 0, 0xff, 0, 0xff, 0, 0xf8, 0};
    assert(send_packet(gb, printer, 0x04, true, truncated_run, sizeof(truncated_run), false) == 0);
    assert(send_packet(gb, printer, 0x04, true, next_run, sizeof(next_run), false) == 0);
    assert(send_packet(gb, printer, 0x02, false, parameters, sizeof(parameters), false) == 8);
    assert(capture.pages == 3 && capture.height == 16 && capture.pixels[0] == 0);

    cupid_gb_printer_reset(printer);
    for (unsigned i = 0; i < 13; ++i) {
        assert(send_packet(gb, printer, 0x04, true, rle, sizeof(rle), false) == (i ? 8 : 0));
    }
    assert(send_packet(gb, printer, 0x02, false, parameters, sizeof(parameters), false) == 8);
    assert(capture.pages == 4 && capture.height == 200);
    assert(capture.pixels[192 * CUPID_GB_PRINTER_WIDTH] == 0xaa);

    cupid_gb_set_serial_link(gb, (CupidGbSerialLink){0});
    assert(send_byte(gb, printer, 0x88) == 0xff);
    cupid_gb_printer_destroy(printer);
    cupid_gb_cleanup(gb);
    free(gb);
    return 0;
}
