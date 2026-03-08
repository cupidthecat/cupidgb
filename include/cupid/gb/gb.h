#ifndef CUPID_GB_GB_H
#define CUPID_GB_GB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cupid/gb/sgb.h"

#define CUPID_GB_MAX_ROM_SIZE    (8u * 1024u * 1024u)
#define CUPID_GB_MAX_RAM_SIZE    (128u * 1024u)
#define CUPID_GB_SCREEN_WIDTH    160u
#define CUPID_GB_SCREEN_HEIGHT   144u

/* APU sample buffer: ~2.8 frames at 44100 Hz / 60 fps */
#define CUPID_GB_APU_SAMPLE_RATE 44100u
#define CUPID_GB_APU_BUF_FRAMES  2048u

/*
 * All 4 Game Boy audio channels + frame sequencer state.
 * ch*_len  = internal length counter (counts DOWN; channel stops at 0).
 * ch*_freq = current 11-bit channel frequency.
 * ch*_timer = T-cycle countdown until next event (duty step / wave pos / LFSR).
 * buf[]    = int16_t stereo samples, L then R, generated each tick.
 * buf_write = number of stereo frames currently waiting in buf[].
 */
typedef struct CupidGbApu {
    /* Frame sequencer */
    uint16_t fs_counter;  /* T-cycles until next frame-sequencer tick */
    uint8_t  fs_step;     /* 0-7 */

    bool apu_on;          /* NR52 bit 7 */

    /* CH1 – square wave with frequency sweep */
    bool     ch1_dac;
    bool     ch1_on;
    uint8_t  ch1_duty;        /* 0-3 */
    uint8_t  ch1_duty_pos;    /* 0-7 */
    uint8_t  ch1_len;
    bool     ch1_len_en;
    uint8_t  ch1_vol;
    bool     ch1_env_add;
    uint8_t  ch1_env_period;
    uint8_t  ch1_env_timer;
    uint16_t ch1_freq;        /* current live frequency (11-bit) */
    uint16_t ch1_timer;
    bool     ch1_sweep_en;
    uint8_t  ch1_sweep_timer;
    uint8_t  ch1_sweep_period;
    bool     ch1_sweep_neg;
    uint8_t  ch1_sweep_shift;
    uint16_t ch1_sweep_shadow;
    bool     ch1_sweep_subtracted;

    /* CH2 – square wave (no sweep) */
    bool     ch2_dac;
    bool     ch2_on;
    uint8_t  ch2_duty;
    uint8_t  ch2_duty_pos;
    uint8_t  ch2_len;
    bool     ch2_len_en;
    uint8_t  ch2_vol;
    bool     ch2_env_add;
    uint8_t  ch2_env_period;
    uint8_t  ch2_env_timer;
    uint16_t ch2_freq;
    uint16_t ch2_timer;

    /* CH3 – arbitrary wave */
    bool     ch3_dac;
    bool     ch3_on;
    uint16_t ch3_len;         /* counts down from 256; fits in 9 bits */
    bool     ch3_len_en;
    uint8_t  ch3_out_level;   /* 0=mute 1=100% 2=50% 3=25% */
    uint16_t ch3_freq;
    uint16_t ch3_active_freq; /* currently active period source */
    uint16_t ch3_timer;
    uint8_t  ch3_wave_pos;    /* 0-31 nibble index */
    uint8_t  ch3_current_byte;/* 0-15 wave RAM byte currently exposed to CPU */
    bool     ch3_started;     /* true once the first post-trigger sample has been fetched */
    uint8_t  ch3_wave_access_ticks; /* DMG-only access window after a wave fetch */
    bool     ch3_freq_pending;/* apply ch3_freq after the next wave RAM fetch */
    uint8_t  ch3_sample;      /* current 4-bit output */

    /* CH4 – noise (LFSR) */
    bool     ch4_dac;
    bool     ch4_on;
    uint8_t  ch4_len;
    bool     ch4_len_en;
    uint8_t  ch4_vol;
    bool     ch4_env_add;
    uint8_t  ch4_env_period;
    uint8_t  ch4_env_timer;
    uint8_t  ch4_div_code;    /* 0-7 */
    uint8_t  ch4_shift;       /* 0-13 */
    bool     ch4_width7;      /* true = 7-bit LFSR */
    uint32_t ch4_timer;
    uint16_t ch4_lfsr;        /* 15-bit shift register */

    /* Output */
    uint32_t sample_acc;
    int16_t  buf[CUPID_GB_APU_BUF_FRAMES * 2u]; /* L/R interleaved */
    uint32_t buf_write;       /* number of stereo frames written */
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
    CUPID_GB_MBC_UNKNOWN
} CupidGbMbcType;

/* MBC3 real-time clock registers */
typedef struct CupidGbRtc {
    uint8_t s, m, h, dl, dh;       /* live registers */
    uint8_t ls, lm, lh, ldl, ldh;  /* latched copies */
    uint8_t latch_prev;             /* last value written to latch port */
    int64_t base_time;              /* system clock snapshot for elapsed-time calc */
} CupidGbRtc;

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
    CUPID_GB_MODEL_DMG0,
    CUPID_GB_MODEL_MGB,
    CUPID_GB_MODEL_CGB,
    CUPID_GB_MODEL_SGB,
    CUPID_GB_MODEL_SGB2
} CupidGbModel;

typedef struct CupidGb {
    CupidGbCpu cpu;
    CupidGbModel model;
    CupidGbCartridgeHeader header;
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
    uint8_t rtc_register;       /* 0 = normal RAM; 0x08-0x0C = RTC reg */
    /* MBC5 9-bit ROM bank */
    uint16_t mbc5_rom_bank;
    /* MBC7 accelerometer */
    uint16_t mbc7_sensor_x;
    uint16_t mbc7_sensor_y;
    bool     mbc7_latch;
    /* MMM01 */
    bool     mmm01_locked;
    uint8_t  mmm01_bank_base;
    /* HuC1 */
    bool     huc1_ir_mode;
    /* HuC3 */
    uint8_t  huc3_mode;
    uint8_t  huc3_value;
    uint8_t  serial_counter;
    uint8_t  serial_bits_remaining;
    uint8_t  serial_tx_latch;
    uint16_t div_counter;
    uint16_t timer_counter;
    uint8_t  tima_overflow_delay; /* M-cycles remaining until TMA reload + timer IRQ */
    bool     tima_reload_just_happened;
    uint16_t dma_source_base;
    uint16_t dma_restart_source_base;
    uint8_t  dma_index;
    uint8_t  dma_start_delay;
    uint8_t  dma_restart_delay;
    bool     dma_active;
    bool     dma_restart_pending;
    uint16_t ppu_counter;
    bool cgb_mode;
    uint8_t cgb_vram_bank;
    uint8_t cgb_wram_bank;
    uint8_t cgb_bg_palette_ram[0x40];
    uint8_t cgb_obj_palette_ram[0x40];
    uint16_t hdma_source;
    uint16_t hdma_destination;
    uint8_t hdma_blocks_remaining;
    bool hdma_active;
    bool double_speed;
    bool speed_switch_armed;
    bool speed_phase;
    uint8_t joypad;          /* button state: bits 0-3 dpad, 4-7 action; 0=pressed */
    uint8_t window_line_counter; /* internal window scanline counter */
    uint8_t ppu_lcd_warmup_lines;
    uint8_t stat_irq_delay;
    bool ppu_lcd_startup;
    bool ppu_line_boundary_hold;
    bool stat_irq_line;
    bool frame_ready;
    bool loaded;
    char save_path[260];
    CupidGbSgb sgb;
    CupidGbApu apu;
} CupidGb;

void cupid_gb_init(CupidGb *gb);
void cupid_gb_cleanup(CupidGb *gb);
void cupid_gb_set_model(CupidGb *gb, CupidGbModel model);
const char *cupid_gb_model_name(CupidGbModel model);
bool cupid_gb_parse_header(const uint8_t *rom_data,
                           size_t rom_size,
                           CupidGbCartridgeHeader *header);
bool cupid_gb_load_rom_file(CupidGb *gb, const char *path);
bool cupid_gb_load_rom(CupidGb *gb, const uint8_t *rom_data, size_t rom_size);
bool cupid_gb_step(CupidGb *gb);
uint8_t cupid_gb_read_u8(const CupidGb *gb, uint16_t address);
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

#endif