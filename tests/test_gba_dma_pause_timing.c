#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "cupid/gba/gba.h"

typedef struct DmaPauseRomExpectation {
    const char *path;
    uint32_t expected_r12;
} DmaPauseRomExpectation;

typedef struct DmaCheckpoint {
    uint32_t step;
    uint32_t pc_before;
    uint32_t pc_after;
    uint32_t dot_before;
    uint32_t dot_after;
    uint16_t timer0_before;
    uint16_t timer0_after;
    uint32_t mem40_before;
    uint32_t mem40_after;
    uint32_t r2_before;
    uint32_t r2_after;
    uint8_t pending_before;
    uint8_t pending_after;
    uint8_t depth_before;
    uint8_t depth_after;
} DmaCheckpoint;

#define CUPID_DMA_CHECKPOINT_MAX 256u

typedef struct DmaTraceLog {
    DmaCheckpoint entries[CUPID_DMA_CHECKPOINT_MAX];
    uint32_t count;
} DmaTraceLog;

typedef struct DmaEvent {
    uint32_t step;
    uint32_t pc_before;
    uint32_t pc_after;
    uint32_t dot_before;
    uint32_t dot_after;
    uint16_t dispstat_before;
    uint16_t dispstat_after;
    uint8_t pending_before;
    uint8_t pending_after;
    uint8_t depth_before;
    uint8_t depth_after;
    uint16_t ctrl_before[4];
    uint16_t ctrl_after[4];
    uint8_t active_before;
    uint8_t active_after;
} DmaEvent;

#define CUPID_DMA_EVENT_MAX 512u

typedef struct DmaEventLog {
    DmaEvent entries[CUPID_DMA_EVENT_MAX];
    uint32_t count;
} DmaEventLog;

static bool cupid_dma_trace_should_capture(uint32_t pc_before,
                                           uint32_t pc_after,
                                           uint32_t mem40_before,
                                           uint32_t mem40_after)
{
    if (mem40_before != mem40_after) {
        return true;
    }

    if ((pc_before >= 0x08000240u && pc_before <= 0x08000270u) ||
        (pc_after >= 0x08000240u && pc_after <= 0x08000270u)) {
        return true;
    }

    return false;
}

static void cupid_dma_trace_append(DmaTraceLog *trace,
                                   uint32_t step,
                                   uint32_t pc_before,
                                   uint32_t pc_after,
                                   uint32_t dot_before,
                                   uint32_t dot_after,
                                   uint16_t timer0_before,
                                   uint16_t timer0_after,
                                   uint32_t mem40_before,
                                   uint32_t mem40_after,
                                   uint32_t r2_before,
                                   uint32_t r2_after,
                                   uint8_t pending_before,
                                   uint8_t pending_after,
                                   uint8_t depth_before,
                                   uint8_t depth_after)
{
    DmaCheckpoint *entry;

    if (trace == 0 || trace->count >= CUPID_DMA_CHECKPOINT_MAX) {
        return;
    }

    entry = &trace->entries[trace->count++];
    entry->step = step;
    entry->pc_before = pc_before;
    entry->pc_after = pc_after;
    entry->dot_before = dot_before;
    entry->dot_after = dot_after;
    entry->timer0_before = timer0_before;
    entry->timer0_after = timer0_after;
    entry->mem40_before = mem40_before;
    entry->mem40_after = mem40_after;
    entry->r2_before = r2_before;
    entry->r2_after = r2_after;
    entry->pending_before = pending_before;
    entry->pending_after = pending_after;
    entry->depth_before = depth_before;
    entry->depth_after = depth_after;
}

static void cupid_dma_trace_print(const char *path, const DmaTraceLog *trace)
{
    uint32_t i;

    if (trace == 0) {
        return;
    }

    fprintf(stderr, "checkpoint trace for %s (entries=%u)\n", path, trace->count);
    for (i = 0u; i < trace->count; ++i) {
        const DmaCheckpoint *entry = &trace->entries[i];
        fprintf(stderr,
                "step=%u pc=%08X->%08X dot=%u->%u t0=%04X->%04X mem40=%08X->%08X r2=%08X->%08X pm=%02X->%02X depth=%u->%u\n",
                entry->step,
                entry->pc_before,
                entry->pc_after,
                entry->dot_before,
                entry->dot_after,
                entry->timer0_before,
                entry->timer0_after,
                entry->mem40_before,
                entry->mem40_after,
                entry->r2_before,
                entry->r2_after,
                entry->pending_before,
                entry->pending_after,
                entry->depth_before,
                entry->depth_after);
    }
}

static bool cupid_dma_event_window(uint32_t step,
                                   uint32_t pc_before,
                                   uint32_t pc_after)
{
    (void)pc_before;
    (void)pc_after;

    if (step <= 420u) {
        return true;
    }
    return false;
}

static void cupid_dma_event_append(DmaEventLog *log,
                                   uint32_t step,
                                   uint32_t pc_before,
                                   uint32_t pc_after,
                                   uint32_t dot_before,
                                   uint32_t dot_after,
                                   uint16_t dispstat_before,
                                   uint16_t dispstat_after,
                                   uint8_t pending_before,
                                   uint8_t pending_after,
                                   uint8_t depth_before,
                                   uint8_t depth_after,
                                   const uint16_t ctrl_before[4],
                                   const uint16_t ctrl_after[4],
                                   uint8_t active_before,
                                   uint8_t active_after)
{
    DmaEvent *entry;
    uint32_t channel;

    if (log == 0 || log->count >= CUPID_DMA_EVENT_MAX) {
        return;
    }

    entry = &log->entries[log->count++];
    entry->step = step;
    entry->pc_before = pc_before;
    entry->pc_after = pc_after;
    entry->dot_before = dot_before;
    entry->dot_after = dot_after;
    entry->dispstat_before = dispstat_before;
    entry->dispstat_after = dispstat_after;
    entry->pending_before = pending_before;
    entry->pending_after = pending_after;
    entry->depth_before = depth_before;
    entry->depth_after = depth_after;
    entry->active_before = active_before;
    entry->active_after = active_after;
    for (channel = 0u; channel < 4u; ++channel) {
        entry->ctrl_before[channel] = ctrl_before[channel];
        entry->ctrl_after[channel] = ctrl_after[channel];
    }
}

static void cupid_dma_event_print(const char *path, const DmaEventLog *log)
{
    uint32_t i;

    if (log == 0) {
        return;
    }

    fprintf(stderr, "dma event trace for %s (entries=%u)\n", path, log->count);
    for (i = 0u; i < log->count; ++i) {
        const DmaEvent *entry = &log->entries[i];
        fprintf(stderr,
                "step=%u pc=%08X->%08X dot=%u->%u ds=%04X->%04X pm=%02X->%02X depth=%u->%u act=%X->%X c0=%04X->%04X c1=%04X->%04X c2=%04X->%04X c3=%04X->%04X\n",
                entry->step,
                entry->pc_before,
                entry->pc_after,
                entry->dot_before,
                entry->dot_after,
                entry->dispstat_before,
                entry->dispstat_after,
                entry->pending_before,
                entry->pending_after,
                entry->depth_before,
                entry->depth_after,
                entry->active_before,
                entry->active_after,
                entry->ctrl_before[0],
                entry->ctrl_after[0],
                entry->ctrl_before[1],
                entry->ctrl_after[1],
                entry->ctrl_before[2],
                entry->ctrl_after[2],
                entry->ctrl_before[3],
                entry->ctrl_after[3]);
    }
}

static bool load_gba_rom_fixture(CupidGba *gba, const char *path)
{
    static const char *prefixes[] = {
        "",
        "../",
    };
    unsigned int i;

    for (i = 0u; i < sizeof(prefixes) / sizeof(prefixes[0]); ++i) {
        char candidate[256];
        int written = snprintf(candidate, sizeof(candidate), "%s%s", prefixes[i], path);

        if (written > 0 && (size_t)written < sizeof(candidate) &&
            cupid_gba_load_rom_file(gba, candidate)) {
            return true;
        }
    }

    return false;
}

static uint32_t run_dma_pause_rom(const char *path, DmaTraceLog *trace, DmaEventLog *events)
{
    CupidGba gba = {0};
    uint32_t last_pc = 0xffffffffu;
    uint32_t same_pc_count = 0u;
    uint32_t steps;
    uint32_t result;

    cupid_gba_init(&gba);
    cupid_gba_load_bios_file(&gba, "bootroms/gba_bios.bin");
    assert(load_gba_rom_fixture(&gba, path));

    for (steps = 0u; steps < 8000000u; ++steps) {
        uint16_t ctrl_before[4];
        uint16_t ctrl_after[4];
        uint8_t active_before = 0u;
        uint8_t active_after = 0u;
        uint32_t pc_before = gba.cpu.r[15] & ~1u;
        uint32_t dot_before = gba.ppu_dot;
        uint16_t dispstat_before = gba.display_status;
        uint16_t timer0_before = gba.timers[0].counter;
        uint32_t mem40_before = cupid_gba_read_u32(&gba, 0x03000040u);
        uint32_t r2_before = gba.cpu.r[2];
        uint8_t pending_before = gba.dma_pending_mask;
        uint8_t depth_before = gba.dma_frame_depth;
        uint32_t pc;
        uint32_t channel;

        for (channel = 0u; channel < 4u; ++channel) {
            ctrl_before[channel] = gba.dma[channel].control;
            if (gba.dma[channel].active) {
                active_before = (uint8_t)(active_before | (uint8_t)(1u << channel));
            }
        }

        cupid_gba_step(&gba);
        pc = gba.cpu.r[15] & ~1u;

        for (channel = 0u; channel < 4u; ++channel) {
            ctrl_after[channel] = gba.dma[channel].control;
            if (gba.dma[channel].active) {
                active_after = (uint8_t)(active_after | (uint8_t)(1u << channel));
            }
        }

        if (cupid_dma_trace_should_capture(pc_before,
                                           pc,
                                           mem40_before,
                                           cupid_gba_read_u32(&gba, 0x03000040u))) {
            cupid_dma_trace_append(trace,
                                   steps,
                                   pc_before,
                                   pc,
                                   dot_before,
                                   gba.ppu_dot,
                                   timer0_before,
                                   gba.timers[0].counter,
                                   mem40_before,
                                   cupid_gba_read_u32(&gba, 0x03000040u),
                                   r2_before,
                                   gba.cpu.r[2],
                                   pending_before,
                                   gba.dma_pending_mask,
                                   depth_before,
                                   gba.dma_frame_depth);
        }

        if (events != 0 &&
            cupid_dma_event_window(steps, pc_before, pc) &&
            (pending_before != gba.dma_pending_mask ||
             depth_before != gba.dma_frame_depth ||
             active_before != active_after ||
             ctrl_before[0] != ctrl_after[0] ||
             ctrl_before[1] != ctrl_after[1] ||
             ctrl_before[2] != ctrl_after[2] ||
             ctrl_before[3] != ctrl_after[3])) {
            cupid_dma_event_append(events,
                                   steps,
                                   pc_before,
                                   pc,
                                   dot_before,
                                   gba.ppu_dot,
                                   dispstat_before,
                                   gba.display_status,
                                   pending_before,
                                   gba.dma_pending_mask,
                                   depth_before,
                                   gba.dma_frame_depth,
                                   ctrl_before,
                                   ctrl_after,
                                   active_before,
                                   active_after);
        }

        if (pc == last_pc) {
            same_pc_count++;
        } else {
            last_pc = pc;
            same_pc_count = 0u;
        }

        if (same_pc_count >= 64u && (gba.display_control & 0x7u) == 4u) {
            break;
        }
    }

    if (!(same_pc_count >= 64u && (gba.display_control & 0x7u) == 4u)) {
        fprintf(stderr,
                "%s did not reach idle loop after %u steps (pc=%08X r12=%u)\n",
                path,
                steps,
                gba.cpu.r[15],
                gba.cpu.r[12]);
        assert(0);
    }

    result = gba.cpu.r[12];
    cupid_gba_cleanup(&gba);
    return result;
}

int main(void)
{
    static const DmaPauseRomExpectation expectations[] = {
        { "gba-tests-master/DMA/DMA_pause_timing_mid_1.gba", 0u },
        { "gba-tests-master/DMA/DMA_pause_timing_mid_2.gba", 0u },
    };
    unsigned int i;

    for (i = 0u; i < sizeof(expectations) / sizeof(expectations[0]); ++i) {
        DmaTraceLog trace = {0};
        DmaEventLog events = {0};
        uint32_t observed = run_dma_pause_rom(expectations[i].path, &trace, &events);

        if (observed != expectations[i].expected_r12) {
            fprintf(stderr,
                    "%s expected r12=%u but got r12=%u\n",
                    expectations[i].path,
                    expectations[i].expected_r12,
                    observed);
            cupid_dma_trace_print(expectations[i].path, &trace);
            cupid_dma_event_print(expectations[i].path, &events);
        }

        assert(observed == expectations[i].expected_r12);
    }

    return 0;
}
