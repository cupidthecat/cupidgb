#ifndef CUPID_GB_PRINTER_H
#define CUPID_GB_PRINTER_H

#include "cupid/gb/gb.h"

#define CUPID_GB_PRINTER_WIDTH 160

typedef struct CupidGbPrinter CupidGbPrinter;

typedef struct CupidGbPrinterImage {
    /* Borrowed row-major grayscale pixels, valid for the duration of the callback. */
    const uint8_t *pixels;
    uint8_t height;
    /* Raw margin nibbles from the print command. */
    uint8_t top_margin;
    uint8_t bottom_margin;
    uint8_t exposure;
} CupidGbPrinterImage;

typedef void (*CupidGbPrinterImageCallback)(const CupidGbPrinterImage *image, void *user_data);
typedef void (*CupidGbPrinterDoneCallback)(void *user_data);

CupidGbPrinter *cupid_gb_printer_create(CupidGbPrinterImageCallback image,
                                        CupidGbPrinterDoneCallback done, void *user_data);
/* Reset protocol state and pending jobs while keeping host registrations. */
void cupid_gb_printer_reset(CupidGbPrinter *printer);
/* Detach the link from its core before destroying the printer. */
void cupid_gb_printer_destroy(CupidGbPrinter *printer);
CupidGbSerialLink cupid_gb_printer_link(CupidGbPrinter *printer);
/* Advance the accessory after a guest execution slice. Serial callbacks also
 * synchronize it before receiving data. The printer has its own state and is
 * independent of the attached console's save state. */
void cupid_gb_printer_update(CupidGbPrinter *printer, const CupidGb *gb);

#endif
