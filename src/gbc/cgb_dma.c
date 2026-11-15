/* Color DMA transactions advance all guest clock domains through the common scheduler. */
#include "cupid/gbc/cgb.h"

#include "cgb_internal.h"
#include "cupid/gb/gb.h"
#include "cupid/gb/ppu.h"
#include "cupid/gb/timer.h"

/* Zero remaining blocks encodes the completed value FF. Cancellation keeps the
 * newly written count but releases the active request, producing an idle count. */
uint8_t cupid_cgb_dma_status(const CupidGb *gb) {
    return (uint8_t)((gb->hdma_active || gb->hdma_pending ? 0 : 0x80u) |
                     ((gb->hdma_blocks_remaining - 1u) & 0x7fu));
}

void cupid_cgb_run_pending_dma(CupidGb *gb) {
    if (!gb || gb->model != CUPID_GB_MODEL_CGB || !gb->hdma_pending)
        return;

    const size_t bank = gb->cgb_vram_bank ? 0x2000u : 0;
    unsigned slot = 0;
    gb->hdma_running = true;
    /* Every byte uses two LCD dots. Normal speed adds a final two-dot slot so
     * the complete stall ends on a CPU machine-cycle boundary in either speed. */
    cupid_gb_tick_color_dma_slot(gb, true);
    ++slot;
    while (gb->hdma_pending && gb->hdma_blocks_remaining) {
        gb->hdma_vram_fetch = UINT16_MAX;
        const uint16_t source = gb->hdma_source++;
        /* The VRAM and echo ranges have no source driver during color DMA. */
        const bool driven = source < 0x8000u || (source >= 0xa000u && source < 0xe000u);
        const uint8_t bus_value =
            driven ? cupid_gb_read_bus_u8(gb, source) : cupid_gb_sample_cartridge_bus(gb);
        const unsigned oam_address = source & 0xffu;
        if ((gb->dma_active || gb->dma_start_delay) && (gb->double_speed || gb->dma_phase == 2u) &&
            oam_address < 160u)
            gb->object_attribute_memory[oam_address] = bus_value;
        cupid_gb_tick_color_dma_slot(gb, gb->double_speed || !(slot & 1u));
        ++slot;
        /* A fetch on the shared VRAM port can pull destination address lines low.
         * Slots without a fetch leave the programmed destination unchanged. */
        const size_t destination = gb->hdma_destination & gb->hdma_vram_fetch & 0x1fffu;
        ++gb->hdma_destination;
        gb->video_ram[bank + destination] = bus_value;
        if (cupid_gb_lcd_enabled(gb) && gb->pixels.line < 144 &&
            (gb->io_registers[CUPID_GB_IO_STAT] & 3u) == CUPID_GB_PPU_MODE_TRANSFER)
            gb->video_ram[(bank ^ 0x2000u) + destination] = bus_value;

        if (!(gb->hdma_destination & 15u)) {
            --gb->hdma_blocks_remaining;
            if (!gb->hdma_blocks_remaining || !gb->hdma_destination) {
                gb->hdma_active = false;
                gb->hdma_pending = false;
                gb->hdma_hblank_delay = 0;
            } else if (gb->hdma_active) {
                gb->hdma_pending = false;
            }
        }
    }
    gb->hdma_running = false;
    if (!gb->double_speed)
        cupid_gb_tick_color_dma_slot(gb, !(slot & 1u));
    gb->io_registers[0x55u] = cupid_cgb_dma_status(gb);
}

void cupid_cgb_tick_hdma(CupidGb *gb) {
    if (gb && gb->hdma_active && gb->hdma_blocks_remaining)
        /* The request settles three dots after the final pixel, independently
         * of the readable STAT mode and its interrupt delay. */
        gb->hdma_hblank_delay = 3;
}

void cupid_cgb_advance_hdma(CupidGb *gb) {
    if (gb && gb->hdma_hblank_delay && !--gb->hdma_hblank_delay && gb->hdma_active &&
        !gb->cpu.halted && !gb->cpu.stopped)
        gb->hdma_pending = true;
}

void cupid_cgb_hdma_lcd_off(CupidGb *gb, uint8_t previous_mode) {
    if (!gb)
        return;
    gb->hdma_hblank_delay = 0;
    if (gb->hdma_active && previous_mode)
        gb->hdma_pending = true;
}

void cupid_cgb_hdma_sleep(CupidGb *gb) {
    if (gb)
        gb->hdma_allow_on_wake = (gb->io_registers[CUPID_GB_IO_STAT] & 3u) != 0;
}

void cupid_cgb_hdma_wake(CupidGb *gb) {
    if (gb && gb->hdma_active && gb->hdma_allow_on_wake &&
        !(gb->io_registers[CUPID_GB_IO_STAT] & 3u))
        gb->hdma_pending = true;
}
