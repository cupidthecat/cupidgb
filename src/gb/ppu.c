#include "cupid/gb/ppu.h"

#include "cupid/gb/cpu.h"
#include "cupid/gbc/cgb.h"

#include <string.h>

bool cupid_gb_lcd_enabled(const CupidGb *gb) {
    return (gb->io_registers[CUPID_GB_IO_LCDC] & 0x80) != 0;
}

void cupid_gb_set_ppu_mode(CupidGb *gb, uint8_t mode) {
    gb->io_registers[CUPID_GB_IO_STAT] =
        (uint8_t)((gb->io_registers[CUPID_GB_IO_STAT] & 0xfc) | (mode & 3));
}

static void update_stat_irq(CupidGb *gb, bool write, bool half_dot, bool stat_write, int forced) {
    bool coincidence;
    bool irq_signal;
    uint8_t stat;
    uint8_t mode;

    if (gb == 0) {
        return;
    }

    stat = gb->io_registers[CUPID_GB_IO_STAT];

    if (!cupid_gb_lcd_enabled(gb)) {
        stat = (uint8_t)((stat & 0xfcu) | CUPID_GB_PPU_MODE_HBLANK);
        gb->io_registers[CUPID_GB_IO_STAT] = stat;
        /* LCD power-off freezes both comparison and interrupt latches. An
         * unchanged LYC condition at power-on must not create a second edge. */
        gb->stat_irq_delay = 0u;
        return;
    }

    const CupidGbPixels *p = &gb->pixels;
    bool compare_valid = !p->initialized || gb->ppu_lcd_startup || p->dot != 0 || p->line == 0 ||
                         (stat & 3u) == CUPID_GB_PPU_MODE_OAM;
    if (p->initialized && p->line < 143 && gb->io_registers[CUPID_GB_IO_LY] == p->line + 1u)
        compare_valid = false;
    unsigned compare_line = gb->io_registers[CUPID_GB_IO_LY];
    if (p->initialized && p->line == 143 && p->dot >= 455)
        compare_valid = false;
    if (p->initialized && p->line == 153) {
        unsigned first = gb->model == CUPID_GB_MODEL_CGB ? 1u : 3u;
        compare_valid = (p->dot >= first && p->dot < 5) || p->dot >= 9;
        compare_line = p->dot < 9 ? 153u : 0u;
    }
    coincidence = compare_valid && compare_line == gb->io_registers[CUPID_GB_IO_LYC];
    if (compare_valid)
        gb->pixels.lyc_irq = coincidence;
    /* Monochrome hardware clears the readable bit during invalid comparison
     * phases while retaining the previous comparison input to the IRQ line. */
    if (compare_valid || gb->model != CUPID_GB_MODEL_CGB) {
        if (coincidence)
            stat = (uint8_t)(stat | 0x04u);
        else
            stat = (uint8_t)(stat & 0xfbu);
    }

    gb->io_registers[CUPID_GB_IO_STAT] = stat;
    /* The line-tail interrupt input is available to register writes before its
     * next automatic latch edge. Recomputing that latch on every intervening
     * dot would fabricate a new HBlank edge after a STAT write. */
    unsigned line_length =
        gb->ppu_lcd_startup ? (gb->model == CUPID_GB_MODEL_CGB ? 452u : 453u) : 456u;
    if (!write && forced == -2 && p->initialized && p->line < 143 && !p->active &&
        p->x == CUPID_GB_SCREEN_WIDTH && p->dot >= line_length - 3 && p->dot < line_length)
        return;
    mode = (uint8_t)(stat & 0x03u);
    int irq_mode = mode;
    if (p->initialized && !gb->ppu_lcd_startup) {
        if (mode == CUPID_GB_PPU_MODE_OAM)
            irq_mode = -1;
        if (p->dot == 0 && p->line < 144)
            irq_mode = mode == CUPID_GB_PPU_MODE_OAM ? -1 : p->line == 0 ? 1 : 2;
        unsigned length = 456;
        if (write && p->line < 143 && !p->active && p->x == CUPID_GB_SCREEN_WIDTH &&
            (p->dot >= length - 3 || (half_dot && gb->double_speed && p->dot == length - 4)))
            irq_mode = CUPID_GB_PPU_MODE_OAM;
        if (write && stat_write && gb->model == CUPID_GB_MODEL_CGB && gb->double_speed &&
            p->line < 144 && p->dot == 2 && mode == CUPID_GB_PPU_MODE_OAM && !half_dot)
            irq_mode = CUPID_GB_PPU_MODE_OAM;
    } else if (write && gb->ppu_lcd_startup) {
        unsigned length = gb->model == CUPID_GB_MODEL_CGB ? 452 : 453;
        if (!p->active && p->x == CUPID_GB_SCREEN_WIDTH &&
            (p->dot >= length - 3 || (half_dot && gb->double_speed && p->dot == length - 4)))
            irq_mode = CUPID_GB_PPU_MODE_OAM;
    }
    if (forced != -2)
        irq_mode = forced;
    bool startup_blank = gb->ppu_lcd_startup && !p->active && p->x == 0;
    irq_signal =
        (gb->pixels.lyc_irq && (stat & 0x40u) != 0u) ||
        (irq_mode == CUPID_GB_PPU_MODE_HBLANK && !gb->stat_irq_delay && (stat & 0x08u) != 0u) ||
        (irq_mode == CUPID_GB_PPU_MODE_VBLANK && (stat & 0x10u) != 0u) ||
        (irq_mode == CUPID_GB_PPU_MODE_OAM && (stat & 0x20u) != 0u);
    if (startup_blank)
        irq_signal = gb->pixels.lyc_irq && (stat & 0x40u) != 0u;

    if (irq_signal && !gb->stat_irq_line) {
        /* A coincidence edge is independent of the pending HBlank transition.
         * Clearing its delay here can leave double-speed mode 3 latched after
         * the last pixel, with no future transition available to release it. */
        cupid_gb_request_interrupt(gb, CUPID_GB_INTERRUPT_LCD_STAT);
    }

    gb->stat_irq_line = irq_signal;
}

void cupid_gb_update_stat_irq(CupidGb *gb) {
    update_stat_irq(gb, false, false, false, -2);
}

void cupid_gb_update_stat_write(CupidGb *gb, bool half_dot, bool stat_write) {
    update_stat_irq(gb, true, half_dot, stat_write, -2);
}

bool cupid_gb_stat_boundary(const CupidGb *gb) {
    return gb->pixels.initialized && !gb->ppu_lcd_startup && gb->pixels.line < 144 &&
           gb->pixels.dot == 0 && !gb->pixels.active &&
           (gb->io_registers[CUPID_GB_IO_STAT] & 3u) != CUPID_GB_PPU_MODE_OAM;
}

void cupid_gb_sync_stat_half(CupidGb *gb) {
    cupid_gb_sync_lcd_startup(gb);
    if (gb->model != CUPID_GB_MODEL_CGB || !gb->double_speed || !cupid_gb_lcd_enabled(gb) ||
        !gb->pixels.initialized)
        return;
    const CupidGbPixels *p = &gb->pixels;
    unsigned length = gb->ppu_lcd_startup ? 452u : 456u;
    if (p->line < 143 && p->dot == length - 1u && !p->active && p->x == CUPID_GB_SCREEN_WIDTH) {
        /* LY and its interrupt input settle between pipeline dots. Keep the
         * comparison frozen until the following OAM-mode edge; the next whole
         * dot still owns the line-counter and fetch-pipeline transition. */
        gb->io_registers[CUPID_GB_IO_LY] = (uint8_t)(p->line + 1u);
        cupid_gb_set_ppu_mode(gb, CUPID_GB_PPU_MODE_HBLANK);
        update_stat_irq(gb, false, true, false, 2);
    } else if (!gb->ppu_lcd_startup && p->line < 144 && p->dot == 0 &&
               (gb->io_registers[CUPID_GB_IO_STAT] & 3u) != CUPID_GB_PPU_MODE_OAM) {
        cupid_gb_set_ppu_mode(gb, CUPID_GB_PPU_MODE_OAM);
        update_stat_irq(gb, false, true, false, 2);
        update_stat_irq(gb, false, true, false, -1);
    }
}

// PPU reset / DMA

/**
 * @brief Resets the PPU to its LCD power-on state.
 *
 * Sets the LCD dot counter and LY to zero and clears frame-ready and
 * window-line counters. The first line starts in HBlank without an
 * OAM scan; later lines use the normal OAM and transfer phases.
 *
 * Call this when the LCD transitions from disabled to enabled (LCDC bit 7
 * goes from 0 to 1).
 *
 * @param gb Pointer to the Game Boy state.
 *
 * @note Does nothing if @p gb is NULL.
 */
void cupid_gb_reset_ppu(CupidGb *gb) {
    if (gb == 0) {
        return;
    }

    /* Power cycling the LCD resets the fetch pipeline, but the comparison and
     * combined STAT latches stay frozen until startup reaches its refresh edge. */
    const bool previous_lyc_irq = gb->pixels.lyc_irq;
    memset(&gb->pixels, 0, sizeof(gb->pixels));
    gb->pixels.lyc_irq = previous_lyc_irq;
    gb->pixels.initialized = true;
    cupid_gb_pixels_begin_line(gb);
    gb->ppu_counter = 0u;
    gb->stat_irq_delay = 0u;
    gb->frame_ready = false;
    gb->lcd_first_frame = gb->model == CUPID_GB_MODEL_CGB;
    gb->ppu_lcd_warmup_lines = 2u;
    gb->ppu_lcd_startup = true;
    gb->ppu_line_boundary_hold = false;
    gb->window_line_counter = 0u;
    gb->io_registers[CUPID_GB_IO_LY] = 0u;
    cupid_gb_set_ppu_mode(gb, CUPID_GB_PPU_MODE_HBLANK);
}

void cupid_gb_sync_lcd_startup(CupidGb *gb) {
    if (gb && gb->model == CUPID_GB_MODEL_CGB && cupid_gb_lcd_enabled(gb) && gb->ppu_lcd_startup &&
        gb->pixels.dot == 0)
        cupid_gb_update_stat_irq(gb);
}

/**
 * @brief Arms an OAM DMA transfer from `source_high << 8`.
 *
 * A 2-T-cycle start delay is applied before the DMA becomes active.
 * If a DMA is already running, the new request is queued as a restart
 * and takes effect 2 T-cycles later via @ref cupid_gb_tick_dma.
 *
 * @param gb          Pointer to the Game Boy state.
 * @param source_high The high byte of the source address (written to FF46).
 *                    Values 0xE0–0xFF are remapped to 0xC0–0xDF.
 *
 * @note Does nothing if @p gb is NULL.
 */
void cupid_gb_run_dma_transfer(CupidGb *gb, uint8_t source_high) {
    uint16_t source_base;

    if (gb == 0) {
        return;
    }

    if (source_high >= 0xe0u) {
        source_high = (uint8_t)(source_high - 0x20u);
    }

    source_base = (uint16_t)((uint16_t)source_high << 8u);

    if (gb->dma_active) {
        gb->dma_restart_source_base = source_base;
        gb->dma_restart_delay = 2u;
        gb->dma_restart_pending = true;
        return;
    }

    gb->dma_source_base = source_base;
    gb->dma_index = 0u;
    gb->dma_start_delay = 2u;
    gb->dma_restart_delay = 0u;
    gb->dma_restart_pending = false;
    gb->dma_active = false;
}

/**
 * @brief Advances the OAM DMA state machine by one M-cycle.
 *
 * Each call handles at most one byte of DMA data. Manages the start
 * delay, restart delay, active transfer, and completion. Must be called
 * once per M-cycle from the main tick path.
 *
 * @param gb Pointer to the Game Boy state.
 *
 * @note Does nothing if @p gb is NULL.
 */
void cupid_gb_tick_dma(CupidGb *gb) {
    if (gb == 0 || gb->cpu.halted || gb->cpu.stopped) {
        return;
    }

    if (gb->dma_restart_pending && gb->dma_restart_delay > 0u) {
        gb->dma_restart_delay = (uint8_t)(gb->dma_restart_delay - 1u);
        if (gb->dma_restart_delay == 0u) {
            gb->dma_source_base = gb->dma_restart_source_base;
            gb->dma_index = 0u;
            gb->dma_restart_pending = false;
            return;
        }
    } else if (gb->dma_start_delay > 0u) {
        gb->dma_start_delay = (uint8_t)(gb->dma_start_delay - 1u);
        if (gb->dma_start_delay == 0u) {
            gb->dma_active = true;
            return;
        } else {
            return;
        }
    }

    if (!gb->dma_active) {
        return;
    }

    if (gb->dma_index < sizeof(gb->object_attribute_memory)) {
        gb->object_attribute_memory[gb->dma_index] =
            cupid_gb_read_bus_u8(gb, (uint16_t)(gb->dma_source_base + gb->dma_index));
        gb->dma_index = (uint8_t)(gb->dma_index + 1u);
    }

    if (gb->dma_index >= sizeof(gb->object_attribute_memory)) {
        gb->dma_active = false;
    }
}

/* LCD dots use the base clock; the caller converts double-speed CPU time.
 * Partial machine-cycle calls preserve pixel queues, bus locks, and IRQ delays. */
static void tick_ppu(CupidGb *gb, uint32_t dots, uint8_t *midpoint) {
    if (!gb)
        return;
    CupidGbPixels *p = &gb->pixels;
    const bool lcd_enabled = cupid_gb_lcd_enabled(gb);
    const bool stopped = gb->cpu.stopped && gb->model != CUPID_GB_MODEL_CGB;
    const bool sgb = gb->sgb.enabled;
    if (sgb) {
        cupid_gb_sgb_clock(gb, dots, !lcd_enabled || stopped);
    }
    if (!lcd_enabled) {
        const bool previous_lyc_irq = p->lyc_irq;
        memset(p, 0, sizeof(*p));
        p->lyc_irq = previous_lyc_irq;
        gb->ppu_counter = 0;
        gb->frame_ready = false;
        gb->ppu_lcd_warmup_lines = 0;
        gb->ppu_lcd_startup = false;
        gb->ppu_line_boundary_hold = false;
        gb->window_line_counter = 0;
        gb->io_registers[CUPID_GB_IO_LY] = 0;
        cupid_gb_set_ppu_mode(gb, CUPID_GB_PPU_MODE_HBLANK);
        cupid_gb_update_stat_irq(gb);
        return;
    }
    if (stopped)
        return;
    if (!p->initialized) {
        p->initialized = true;
        p->line = gb->io_registers[CUPID_GB_IO_LY];
        p->dot = (uint16_t)(gb->ppu_counter * 4);
        cupid_gb_pixels_begin_line(gb);
        if (p->line < 144 && !gb->ppu_lcd_startup) {
            for (unsigned index = 0; index < 40 && index * 2 < p->dot; ++index)
                cupid_gb_pixels_scan_object(gb, index);
        }
        if (p->line < 144 && p->dot >= 80) {
            cupid_gb_pixels_begin_transfer(gb);
            for (unsigned dot = 80; dot < p->dot && p->active; ++dot)
                if (cupid_gb_pixels_tick(gb))
                    p->active = false;
        }
    }
    while (dots) {
        for (unsigned subcycle = 0; subcycle < 4 && dots; ++subcycle, --dots) {
            cupid_cgb_advance_hdma(gb);
            cupid_gb_sync_lcd_startup(gb);
            if (gb->stat_irq_delay) {
                --gb->stat_irq_delay;
                /* Double-speed hardware keeps mode 3 visible for the final
                 * LCD dot; mode 0 and its interrupt input then change together. */
                if (gb->double_speed && !p->active && p->x == CUPID_GB_SCREEN_WIDTH &&
                    (gb->io_registers[CUPID_GB_IO_STAT] & 3u) == CUPID_GB_PPU_MODE_TRANSFER)
                    cupid_gb_set_ppu_mode(gb, CUPID_GB_PPU_MODE_HBLANK);
                cupid_gb_update_stat_irq(gb);
            }
            unsigned transfer =
                gb->ppu_lcd_startup ? (gb->model == CUPID_GB_MODEL_CGB ? 78u : 79u) : 80u;
            if (p->line < 144) {
                if (p->dot == 0) {
                    cupid_gb_pixels_begin_line(gb);
                    if (!gb->ppu_lcd_startup) {
                        bool already_visible =
                            (gb->io_registers[CUPID_GB_IO_STAT] & 3u) == CUPID_GB_PPU_MODE_OAM;
                        cupid_gb_set_ppu_mode(gb, CUPID_GB_PPU_MODE_OAM);
                        if (!already_visible)
                            update_stat_irq(gb, false, false, false, 2);
                        update_stat_irq(gb, false, false, false, -1);
                    }
                }
                if (!gb->ppu_lcd_startup && p->dot < 80 && (p->dot & 1))
                    cupid_gb_pixels_scan_object(gb, p->dot >> 1);
                if (p->dot == transfer) {
                    cupid_gb_pixels_begin_transfer(gb);
                    cupid_gb_set_ppu_mode(gb, CUPID_GB_PPU_MODE_TRANSFER);
                    cupid_gb_update_stat_irq(gb);
                }
                if (p->active && cupid_gb_pixels_tick(gb)) {
                    p->active = false;
                    if (p->window_used)
                        ++gb->window_line_counter;
                    if (!gb->double_speed)
                        cupid_gb_set_ppu_mode(gb, CUPID_GB_PPU_MODE_HBLANK);
                    /* At normal speed, mode bits precede the interrupt by one dot. */
                    gb->stat_irq_delay = 1;
                    cupid_gb_update_stat_irq(gb);
                    cupid_cgb_tick_hdma(gb);
                }
            } else if (p->line == 144 && p->dot == 1) {
                /* LY changes before VBlank's interrupt request. Keeping this phase
                 * explicit avoids treating a following DI as an instruction exception. */
                gb->frame_ready = true;
                if (!sgb && !gb->lcd_first_frame)
                    gb->lcd_frame_odd = !gb->lcd_frame_odd;
                gb->lcd_first_frame = false;
                if (sgb) {
                    cupid_gb_sgb_vblank(gb, dots - 1);
                }
                cupid_gb_set_ppu_mode(gb, CUPID_GB_PPU_MODE_VBLANK);
                cupid_gb_request_interrupt(gb, CUPID_GB_INTERRUPT_VBLANK);
                if (!gb->stat_irq_line && (gb->io_registers[CUPID_GB_IO_STAT] & 0x20))
                    cupid_gb_request_interrupt(gb, CUPID_GB_INTERRUPT_LCD_STAT);
                cupid_gb_update_stat_irq(gb);
            } else if (p->line == 153 && p->dot == (gb->model == CUPID_GB_MODEL_CGB ? 4 : 2)) {
                gb->io_registers[CUPID_GB_IO_LY] = 0;
                cupid_gb_update_stat_irq(gb);
            }
            ++p->dot;
            if (p->line >= 143 && p->line < 153 && p->dot == 455) {
                gb->io_registers[CUPID_GB_IO_LY] = p->line + 1;
                if (p->line == 143 && !gb->stat_irq_line &&
                    (gb->io_registers[CUPID_GB_IO_STAT] & 0x20))
                    cupid_gb_request_interrupt(gb, CUPID_GB_INTERRUPT_LCD_STAT);
            }
            unsigned length =
                gb->ppu_lcd_startup ? (gb->model == CUPID_GB_MODEL_CGB ? 452u : 453u) : 456u;
            if (p->dot == length) {
                p->dot = 0;
                ++p->line;
                gb->ppu_lcd_startup = false;
                gb->ppu_lcd_warmup_lines = 0;
                gb->ppu_line_boundary_hold = false;
                if (p->line == 154) {
                    p->line = 0;
                    if (gb->model != CUPID_GB_MODEL_CGB)
                        cupid_gb_set_ppu_mode(gb, CUPID_GB_PPU_MODE_HBLANK);
                    p->window_armed = false;
                    gb->window_line_counter = 0;
                    gb->frame_ready = false;
                }
                gb->io_registers[CUPID_GB_IO_LY] = p->line;
                cupid_gb_update_stat_irq(gb);
            }
            gb->ppu_counter = p->dot / 4;
            /* Monochrome startup holds the frozen comparison through the first
             * dot. CGB starts refreshing on its first advancing clock edge. */
            if (!gb->ppu_lcd_startup || gb->model == CUPID_GB_MODEL_CGB || p->dot != 1)
                cupid_gb_update_stat_irq(gb);
            if (midpoint && subcycle == 1) {
                *midpoint = gb->interrupt_enable & gb->interrupt_flags & 0x1f;
            }
        }
    }
}

void cupid_gb_tick_ppu(CupidGb *gb, uint16_t cycles) {
    tick_ppu(gb, (uint32_t)cycles * 4, 0);
}

void cupid_gb_tick_ppu_dots(CupidGb *gb, uint16_t dots) {
    tick_ppu(gb, dots, 0);
}

uint8_t cupid_gb_tick_ppu_halt(CupidGb *gb) {
    uint8_t pending = gb ? gb->interrupt_enable & gb->interrupt_flags & 0x1f : 0;
    tick_ppu(gb, 4, &pending);
    return pending;
}
