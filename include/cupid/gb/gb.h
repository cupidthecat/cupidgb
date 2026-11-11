#ifndef CUPID_GB_GB_H
#define CUPID_GB_GB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cupid/gb/sgb.h"

#define CUPID_GB_MAX_ROM_SIZE (8u * 1024u * 1024u)
#define CUPID_GB_MAX_RAM_SIZE (128u * 1024u)
#define CUPID_GB_MAX_BOOT_ROM_SIZE 0x0900u /* CGB boot ROM is 2304 bytes */
#define CUPID_GB_SCREEN_WIDTH 160u
#define CUPID_GB_SCREEN_HEIGHT 144u

/* APU sample buffer: ~2.8 frames at 44100 Hz / 60 fps */
#define CUPID_GB_APU_SAMPLE_RATE 44100u
#define CUPID_GB_APU_BUF_FRAMES 2048u

/*
 * All 4 Game Boy audio channels + frame sequencer state.
 * ch*_len  = internal length counter (counts DOWN; channel stops at 0).
 * ch*_freq = current 11-bit channel frequency.
 * ch*_timer = T-cycle countdown until next event (duty step / wave pos / LFSR).
 * buf[]    = int16_t stereo samples, L then R, generated each tick.
 * buf_write = number of stereo frames currently waiting in buf[].
 */
typedef struct CupidGbPulseTiming {
    uint8_t start_delay; /* Remaining trigger settling time, 0..12 base T-cycles. */
    uint8_t edge_age;    /* Base T-cycles since a frequency edge; 4 means older. */
    bool clock_started;
    bool output_high;
    bool output_blocked;
    bool envelope_clock;
    bool envelope_locked;
    bool envelope_lock_pending;
} CupidGbPulseTiming;

typedef struct CupidGbSweepTiming {
    uint8_t startup_ticks; /* Remaining 1 MHz setup ticks, 0..3. */
    uint8_t shift_ticks;   /* Remaining 1 MHz calculation ticks, 0..7. */
    uint8_t restart_hold;  /* Base clocks holding the sweep input after trigger, 0..8. */
    uint16_t delta;
    uint16_t completed_delta;
    bool zero_shift_pending;
    bool zero_shift_armed;
} CupidGbSweepTiming;

typedef struct CupidGbNoiseTiming {
    uint16_t divider_counter; /* Ordinary edges mask to 14 bits; DAC-off carries retain all 16. */
    uint8_t clock_phase;      /* Power-relative base-clock phase, 0..7. */
    uint8_t remaining;        /* Remaining 2 MHz prescaler ticks, 0..34. */
    bool foreground;
    bool background;
    bool started_dac_off;
    bool reload_edge;
    bool counter_stepped;
    bool output_high;
    bool last_shift_narrow;
    bool pre_shift_bit7;
} CupidGbNoiseTiming;

typedef struct CupidGbApu {
    /* Frame sequencer */
    uint8_t fs_step;           /* 0-7 */
    uint8_t fs_skip_div_event; /* 1: skip first edge; 2: resume without advancing phase. */

    bool apu_on; /* NR52 bit 7 */

    /* CH1 – square wave with frequency sweep */
    bool ch1_dac;
    bool ch1_on;
    uint8_t ch1_duty;     /* 0-3 */
    uint8_t ch1_duty_pos; /* 0-7 */
    uint8_t ch1_len;
    bool ch1_len_en;
    uint8_t ch1_vol;
    bool ch1_env_add;
    uint8_t ch1_env_period;
    uint8_t ch1_env_timer;
    uint16_t ch1_freq; /* current live frequency (11-bit) */
    uint16_t ch1_timer;
    bool ch1_sweep_en;
    uint8_t ch1_sweep_timer;
    uint8_t ch1_sweep_period;
    bool ch1_sweep_neg;
    uint8_t ch1_sweep_shift;
    uint16_t ch1_sweep_shadow;
    bool ch1_sweep_subtracted;

    /* CH2 – square wave (no sweep) */
    bool ch2_dac;
    bool ch2_on;
    uint8_t ch2_duty;
    uint8_t ch2_duty_pos;
    uint8_t ch2_len;
    bool ch2_len_en;
    uint8_t ch2_vol;
    bool ch2_env_add;
    uint8_t ch2_env_period;
    uint8_t ch2_env_timer;
    uint16_t ch2_freq;
    uint16_t ch2_timer;

    /* CH3 – arbitrary wave */
    bool ch3_dac;
    bool ch3_on;
    uint16_t ch3_len; /* counts down from 256; fits in 9 bits */
    bool ch3_len_en;
    uint8_t ch3_out_level; /* 0=mute 1=100% 2=50% 3=25% */
    uint16_t ch3_freq;
    uint16_t ch3_active_freq; /* currently active period source */
    uint16_t ch3_timer;
    uint8_t ch3_wave_pos;          /* 0-31 nibble index */
    uint8_t ch3_current_byte;      /* 0-15 wave RAM byte currently exposed to CPU */
    bool ch3_started;              /* true once the first post-trigger sample has been fetched */
    uint8_t ch3_wave_access_ticks; /* DMG-only access window after a wave fetch */
    bool ch3_freq_pending;         /* apply ch3_freq after the next wave RAM fetch */
    uint8_t ch3_sample;            /* current 4-bit output */

    /* CH4 – noise (LFSR) */
    bool ch4_dac;
    bool ch4_on;
    uint8_t ch4_len;
    bool ch4_len_en;
    uint8_t ch4_vol;
    bool ch4_env_add;
    uint8_t ch4_env_period;
    uint8_t ch4_env_timer;
    uint8_t ch4_div_code; /* 0-7 */
    uint8_t ch4_shift;    /* 0-15; high shifts do not clock the noise generator. */
    bool ch4_width7;      /* true = 7-bit LFSR */
    uint32_t ch4_timer;
    uint16_t ch4_lfsr; /* 15-bit shift register */

    /* Hardware clock/output latches; native v3 appends these after its v2 prefix. */
    uint8_t clock_phase;    /* Power-relative base-clock phase, 0..3. */
    uint8_t envelope_delay; /* Deferred color double-speed envelope edge, 0..2 base clocks. */
    CupidGbPulseTiming pulse_timing[2];
    CupidGbSweepTiming sweep_timing;
    CupidGbNoiseTiming noise_timing;

    /* Output */
    uint32_t sample_acc;
    int16_t buf[CUPID_GB_APU_BUF_FRAMES * 2u]; /* L/R interleaved */
    uint32_t buf_write;                        /* number of stereo frames written */
    uint32_t sample_rate; /* Zero selects the default rate for zero-initialized callers. */
    int64_t mix_left, mix_right;
    int64_t highpass_left, highpass_right;
    uint32_t highpass_rate;
    bool highpass_disabled;
} CupidGbApu;

typedef enum CupidGbMbcType {
    CUPID_GB_MBC_NONE = 0,
    CUPID_GB_MBC1,
    CUPID_GB_MBC2,
    CUPID_GB_MBC3,
    CUPID_GB_MBC5,
    CUPID_GB_MBC6,
    CUPID_GB_MBC7,
    CUPID_GB_MMM01,
    CUPID_GB_HUC1,
    CUPID_GB_HUC3,
    CUPID_GB_MBC_UNKNOWN,
    CUPID_GB_CAMERA,
    CUPID_GB_TPP1
} CupidGbMbcType;

typedef struct CupidGbCamera {
    uint8_t registers[0x36];
    uint8_t clock_phase;
    bool mapped;
    uint32_t busy_tcycles;
    uint32_t capture_number;
} CupidGbCamera;

/* MBC3 real-time clock registers */
typedef struct CupidGbRtc {
    uint8_t s, m, h, dl, dh;      /* live registers */
    uint8_t ls, lm, lh, ldl, ldh; /* latched copies */
    uint8_t latch_prev;           /* last value written to latch port */
    int64_t base_time;            /* system clock snapshot for elapsed-time calc */
} CupidGbRtc;

typedef struct CupidGbTpp1 {
    uint16_t rom_bank;
    uint8_t ram_bank;
    uint8_t mode;
    uint8_t status;  /* Rumble strength, RTC running, and RTC overflow. */
    uint8_t live[4]; /* Weeks, packed weekday/hour, minutes, seconds. */
    uint8_t latched[4];
    int64_t rtc_base_time;
} CupidGbTpp1;

typedef struct CupidGbCpu {
    uint8_t a;
    uint8_t f;
    uint8_t b;
    uint8_t c;
    uint8_t d;
    uint8_t e;
    uint8_t h;
    uint8_t l;
    uint16_t sp;
    uint16_t pc;
    uint8_t ime_delay;
    bool ime;
    bool halt_bug;
    bool halted;
    bool just_halted; /* The first idle cycle polls before the clock advances. */
    bool stopped;
} CupidGbCpu;

typedef struct CupidGbCartridgeHeader {
    char title[17];
    uint8_t cgb_flag;
    uint8_t new_licensee_code[2];
    uint8_t sgb_flag;
    uint8_t cartridge_type;
    uint8_t rom_size_code;
    uint8_t ram_size_code;
    uint8_t destination_code;
    uint8_t old_licensee_code;
    uint8_t header_checksum;
    size_t rom_bank_count;
    size_t ram_bank_count;
    CupidGbMbcType mbc_type;
    bool header_checksum_valid;
    bool has_battery;
    bool has_timer;
    bool has_rumble;
} CupidGbCartridgeHeader;

typedef enum CupidGbModel {
    CUPID_GB_MODEL_DMG_ABC = 0,
    /* Native state model tags are persistent. Do not renumber existing values. */
    CUPID_GB_MODEL_DMG0 = 1,
    CUPID_GB_MODEL_MGB = 2,
    CUPID_GB_MODEL_CGB = 3,
    CUPID_GB_MODEL_SGB = 4,
    CUPID_GB_MODEL_SGB2 = 5
} CupidGbModel;

/* Pixel queues retain fetched data across CPU writes and save/rewind boundaries.
 * Palette lookup happens when a pixel leaves the queue, rather than at HBlank. */
typedef struct CupidGbPixels {
    uint16_t dot;
    uint8_t line, x;
    uint8_t colors[8], palettes[8], priorities[8];
    uint8_t read, count, phase, tile, attributes, low, high, column, discard, delay;
    uint16_t address;
    uint8_t objects[10], object_x[10], object_y[10], object_count;
    uint16_t fetched_objects;
    uint8_t object_delay, object_index, object_low, object_high, object_attributes;
    uint16_t object_address;
    uint8_t sprite_colors[8], sprite_palettes[8], sprite_flags[8];
    uint16_t sprite_priorities[8];
    bool initialized, active, window_armed, window_active, window_used, lyc_irq;
} CupidGbPixels;

typedef struct CupidGbMbc7 {
    uint16_t command;
    uint16_t output;
    uint8_t data_bits;
    uint8_t pins;
    bool registers_enabled;
    bool programming_enabled;
    /* Live host input in sensor counts relative to neutral. */
    int16_t motion_x;
    int16_t motion_y;
} CupidGbMbc7;

typedef struct CupidGbHuc3 {
    int64_t rtc_base_time;
    uint16_t minutes, days;
    uint16_t alarm_minutes, alarm_days;
    uint16_t ir_warmup, ir_decay;
    uint8_t index, flags;
    bool alarm_enabled, ir_output, ir_detected;
} CupidGbHuc3;

struct CupidGb;
typedef struct CupidGbCheats CupidGbCheats;
typedef void (*CupidGbSerialOutputCallback)(const struct CupidGb *gb, uint8_t transmitted,
                                            void *user_data);
typedef void (*CupidGbSerialLaunchCallback)(const struct CupidGb *gb, bool outgoing,
                                            void *user_data);
typedef bool (*CupidGbSerialSampleCallback)(const struct CupidGb *gb, void *user_data);

typedef struct CupidGbSerialLink {
    CupidGbSerialLaunchCallback launch;
    CupidGbSerialSampleCallback sample;
    void *user_data;
} CupidGbSerialLink;

typedef void (*CupidGbRumbleCallback)(const struct CupidGb *gb, bool enabled, uint64_t base_cycles,
                                      void *user_data);

typedef struct CupidGb {
    CupidGbCpu cpu;
    CupidGbModel model;
    CupidGbCartridgeHeader header;
    const CupidGbCheats *cheats; /* Borrowed host configuration, outside native snapshots. */
    uint8_t *rom;
    size_t rom_size;
    uint8_t video_ram[0x4000];
    uint8_t cartridge_ram[CUPID_GB_MAX_RAM_SIZE];
    size_t cartridge_ram_size;
    uint8_t work_ram[0x8000];
    uint8_t object_attribute_memory[0x00a0];
    uint8_t io_registers[0x0080];
    uint8_t high_ram[0x007f];
    uint8_t frame_buffer[CUPID_GB_SCREEN_WIDTH * CUPID_GB_SCREEN_HEIGHT];
    uint16_t frame_buffer_color[CUPID_GB_SCREEN_WIDTH * CUPID_GB_SCREEN_HEIGHT];
    uint8_t interrupt_enable;
    uint8_t interrupt_flags;
    size_t rom_bank_count;
    size_t ram_bank_count;
    size_t current_rom_bank;
    size_t current_ram_bank;
    uint8_t mbc1_bank_low5;
    uint8_t mbc1_bank_high2;
    bool mbc1_multicart;
    bool ram_enabled;
    bool mbc1_ram_banking_mode;
    /* MBC3 real-time clock */
    CupidGbRtc rtc;
    CupidGbTpp1 tpp1;
    uint8_t rtc_register; /* 0 = normal RAM; 0x08-0x0C = RTC reg */
    /* MBC5 9-bit ROM bank */
    uint16_t mbc5_rom_bank;
    bool mbc5_rumble_on;
    /* MBC7 accelerometer */
    uint16_t mbc7_sensor_x;
    uint16_t mbc7_sensor_y;
    bool mbc7_latch;
    CupidGbMbc7 mbc7;
    /* MMM01 */
    bool mmm01_locked;
    uint8_t mmm01_bank_base; /* Retained for the legacy native state prefix. */
    uint8_t mmm01_rom_select, mmm01_bank_select, mmm01_mode, mmm01_ram_mask;
    bool mmm01_menu_first; /* Immutable physical ROM dump layout. */
    /* HuC1 */
    bool huc1_ir_mode;
    /* HuC3 */
    uint8_t huc3_mode;
    uint8_t huc3_value;
    CupidGbHuc3 huc3;
    bool huc3_ir_input; /* Live host signal, outside native snapshots. */
    CupidGbCamera camera;
    bool serial_master_clock;
    uint64_t cartridge_bus_expiry; /* Half-dot deadline on the shared main bus. */
    uint8_t cartridge_bus_data;
    uint64_t elapsed_cycles; /* Base-speed T-cycles, including LCD-off execution. */
    uint8_t serial_bits_remaining;
    uint8_t serial_tx_latch;
    uint16_t div_counter;
    uint16_t timer_counter;
    uint8_t tima_overflow_delay; /* M-cycles remaining until TMA reload + timer IRQ */
    bool tima_reload_just_happened;
    uint16_t dma_source_base;
    uint16_t dma_restart_source_base;
    uint8_t dma_index;
    uint8_t dma_start_delay;
    uint8_t dma_restart_delay;
    bool dma_active;
    bool dma_restart_pending;
    uint8_t dma_phase; /* CPU T-cycle phase of the four-cycle OAM source slot. */
    uint16_t ppu_counter;
    bool cgb_mode;
    bool cgb_object_priority_x; /* Latched independently of FF6C readback after boot. */
    uint8_t cgb_vram_bank;
    uint8_t cgb_wram_bank;
    uint8_t cgb_bg_palette_ram[0x40];
    uint8_t cgb_obj_palette_ram[0x40];
    uint16_t hdma_vram_fetch; /* Last fetch in the current DMA byte slot. */
    uint16_t hdma_source;
    uint16_t hdma_destination;
    uint8_t hdma_blocks_remaining;
    bool hdma_active;
    /* Ready CPU work is distinct from an armed HBlank request. A false active
     * flag with pending work denotes a general transfer. */
    bool hdma_pending;
    bool hdma_running;         /* Transient during the synchronous CPU stall. */
    uint8_t hdma_hblank_delay; /* Remaining LCD dots before a block becomes ready. */
    bool hdma_allow_on_wake;   /* Sleep began outside mode 0. */
    bool double_speed;
    bool speed_switch_armed;
    bool speed_phase;
    uint32_t speed_switch_halt_cycles; /* CPU T-cycles remaining in the timed wait. */
    uint8_t speed_switch_toggle_cycles;
    uint8_t speed_switch_freeze_cycles;
    uint8_t stop_divider_hold_cycles;
    /* LCD clock phase in half-dot units modulo eight, measured from LCD enable.
     * LCD power transitions reset this origin; frozen CPU clocks do not advance it. */
    uint8_t speed_switch_alignment;
    uint8_t joypad;              /* button state: bits 0-3 dpad, 4-7 action; 0=pressed */
    uint8_t joypad_extra[3];     /* SGB players 2..4, with the same active-low layout. */
    uint8_t joyp_select_delay;   /* Remaining base dots before monochrome row release. */
    uint8_t joyp_select_value;   /* Requested select bits while old contacts settle. */
    uint8_t window_line_counter; /* internal window scanline counter */
    uint8_t ppu_lcd_warmup_lines;
    uint8_t stat_irq_delay;
    bool ppu_lcd_startup;
    bool ppu_line_boundary_hold;
    bool stat_irq_line;
    bool frame_ready;
    bool lcd_frame_odd;   /* Current handheld LCD row polarity. */
    bool lcd_first_frame; /* The first handheld frame after enable is blank or repeated. */
    uint32_t display_cycles_since_frame; /* Base dots since the last presentation event. */
    uint32_t lcd_repeat_countdown;       /* Remaining CGB first-frame repeat delay. */
    uint8_t display_frame_type;          /* Presentation event, separate from VBlank IRQs. */
    bool loaded;
    uint64_t vblank_events;            /* Host observation count; excluded from guest snapshots. */
    uint64_t battery_write_generation; /* Host dirty tracking; excluded from guest snapshots. */
    char save_path[260];
    CupidGbSgb sgb;
    CupidGbApu apu;
    CupidGbPixels pixels;
    /* Boot ROM overlay */
    uint8_t boot_rom[CUPID_GB_MAX_BOOT_ROM_SIZE];
    size_t boot_rom_size;  /* 0x0100 for DMG, 0x0900 for CGB */
    bool boot_rom_enabled; /* true while boot ROM is mapped */
    /* Host registrations are excluded from hardware snapshots. */
    CupidGbSerialOutputCallback serial_output_callback;
    void *serial_output_user_data;
    CupidGbSerialLink serial_link;
    CupidGbRumbleCallback rumble_callback;
    void *rumble_user_data;
} CupidGb;

/* Optional host observation for one hardware step. This output is not stored
 * in the hardware instance or native state. Address is the actual opcode fetch
 * address, the interrupted PC, or the idle PC according to the event. */
typedef enum CupidGbStepEvent {
    CUPID_GB_STEP_IDLE = 0,
    CUPID_GB_STEP_INSTRUCTION,
    CUPID_GB_STEP_INTERRUPT
} CupidGbStepEvent;

typedef struct CupidGbStepTrace {
    CupidGbStepEvent event;
    uint16_t address;
    uint8_t opcode;
} CupidGbStepTrace;

void cupid_gb_init(CupidGb *gb);
void cupid_gb_cleanup(CupidGb *gb);
/* Optional observer of completed internal-clock bytes. Called synchronously after
 * SB shifts, SC clears its start bit, and the serial interrupt becomes pending.
 * The observer must not mutate or reenter the emulator. Registration survives
 * init, ROM loading, boot resets, and state loading; cleanup clears it. NULL
 * disables observation. The byte is SB latched when SC starts the transfer;
 * this observation does not provide a bit-level cable endpoint. The core never
 * writes serial bytes to a host stream. */
void cupid_gb_set_serial_output_callback(CupidGb *gb, CupidGbSerialOutputCallback callback,
                                         void *user_data);
/* Internal-clock transfers launch the first bit when armed and launch each
 * subsequent bit after sampling on the falling clock edge. Missing samples
 * read high. The host may clock a peer from sample, but must not reenter gb.
 * Registrations survive reset and state loading; cleanup detaches the link.
 * Transport state, including an already launched bit, belongs to the host. */
void cupid_gb_set_serial_link(CupidGb *gb, CupidGbSerialLink link);
/* Read an external-clock peer's outgoing bit before delivering the incoming
 * bit. An idle peer drives low and a peer using its own clock reads high.
 * Clock requests shift SB only while an external-clock transfer is armed. */
bool cupid_gb_serial_external_bit(const CupidGb *gb);
void cupid_gb_serial_external_clock(CupidGb *gb, bool incoming);
/* MBC5 motor transitions are observed at the current base-cycle timestamp.
 * Callbacks must not mutate or reenter the core. Registration survives resets
 * and state loading; state loading emits no callback. Resynchronize host output
 * with the getter after loading a state, resetting, or changing cartridges. */
void cupid_gb_set_rumble_callback(CupidGb *gb, CupidGbRumbleCallback callback, void *user_data);
bool cupid_gb_rumble_enabled(const CupidGb *gb);
void cupid_gb_set_model(CupidGb *gb, CupidGbModel model);
/* Physical base-speed T-cycles per second, independent of CGB double speed.
 * SGB uses its NTSC host-derived clock; other current models use 4194304 Hz.
 * A NULL instance returns the ordinary handheld clock. */
uint32_t cupid_gb_base_clock_hz(const CupidGb *gb);
const char *cupid_gb_model_name(CupidGbModel model);
bool cupid_gb_parse_header(const uint8_t *rom_data, size_t rom_size,
                           CupidGbCartridgeHeader *header);
bool cupid_gb_load_rom_file(CupidGb *gb, const char *path);
bool cupid_gb_load_rom(CupidGb *gb, const uint8_t *rom_data, size_t rom_size);
bool cupid_gb_step(CupidGb *gb);
bool cupid_gb_step_observed(CupidGb *gb, CupidGbStepTrace *trace);
uint8_t cupid_gb_read_u8(const CupidGb *gb, uint16_t address);

/* Guest accesses drive the cartridge bus; the const read API is passive. */
enum { CUPID_GB_CARTRIDGE_BUS_HALF_DOTS = 12 };

uint8_t cupid_gb_read_bus_u8(CupidGb *gb, uint16_t address);
uint8_t cupid_gb_sample_cartridge_bus(const CupidGb *gb);
void cupid_gb_write_u8(CupidGb *gb, uint16_t address, uint8_t value);
const char *cupid_gb_cartridge_type_name(uint8_t cartridge_type);
const char *cupid_gb_mbc_name(CupidGbMbcType mbc_type);

/*
 * Drain generated audio samples into `out` (int16_t stereo, L then R).
 * Returns the number of stereo frames written (max `max_frames`).
 * Call once per video frame after cupid_gb_step returns frame_ready.
 */
uint32_t cupid_gb_apu_drain(CupidGb *gb, int16_t *out, uint32_t max_frames);

void cupid_gb_set_save_path(CupidGb *gb, const char *rom_path);
void cupid_gb_load_save(CupidGb *gb);
void cupid_gb_save(CupidGb *gb);

/* Boot ROM support: load an external boot ROM dump for authentic startup */
bool cupid_gb_load_boot_rom_file(CupidGb *gb, const char *path);
void cupid_gb_clear_boot_rom(CupidGb *gb);
void cupid_gb_enter_boot_rom(CupidGb *gb);

#endif
