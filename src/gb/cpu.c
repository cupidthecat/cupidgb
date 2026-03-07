/* =========================================================================
 * SM83 CPU – shared between Game Boy (DMG) and Game Boy Color (CGB)
 *   Register helpers, ALU operations, instruction decode/execute,
 *   interrupt service routine.
 * ========================================================================= */

#include "cupid/gb/cpu.h"

#include "cupid/gb/gb.h"
#include "cupid/gb/ppu.h"
#include "cupid/gb/timer.h"
#include "cupid/common/log.h"

/* ---------- register helpers ---------- */

static bool cupid_gb_get_flag(const CupidGbCpu *cpu, uint8_t mask)
{
    return (cpu->f & mask) != 0u;
}

static void cupid_gb_set_flag(CupidGbCpu *cpu, uint8_t mask, bool enabled)
{
    if (enabled) {
        cpu->f = (uint8_t)(cpu->f | mask);
    } else {
        cpu->f = (uint8_t)(cpu->f & (uint8_t)(~mask));
    }
}

static uint16_t cupid_gb_get_bc(const CupidGbCpu *cpu)
{
    return (uint16_t)(((uint16_t)cpu->b << 8u) | cpu->c);
}

static uint16_t cupid_gb_get_de(const CupidGbCpu *cpu)
{
    return (uint16_t)(((uint16_t)cpu->d << 8u) | cpu->e);
}

static uint16_t cupid_gb_get_hl(const CupidGbCpu *cpu)
{
    return (uint16_t)(((uint16_t)cpu->h << 8u) | cpu->l);
}

static uint16_t cupid_gb_get_af(const CupidGbCpu *cpu)
{
    return (uint16_t)(((uint16_t)cpu->a << 8u) | cpu->f);
}

static void cupid_gb_set_bc(CupidGbCpu *cpu, uint16_t value)
{
    cpu->b = (uint8_t)(value >> 8u);
    cpu->c = (uint8_t)(value & 0x00ffu);
}

static void cupid_gb_set_de(CupidGbCpu *cpu, uint16_t value)
{
    cpu->d = (uint8_t)(value >> 8u);
    cpu->e = (uint8_t)(value & 0x00ffu);
}

static void cupid_gb_set_hl(CupidGbCpu *cpu, uint16_t value)
{
    cpu->h = (uint8_t)(value >> 8u);
    cpu->l = (uint8_t)(value & 0x00ffu);
}

static void cupid_gb_set_af(CupidGbCpu *cpu, uint16_t value)
{
    cpu->a = (uint8_t)(value >> 8u);
    cpu->f = (uint8_t)(value & 0x00f0u);
}

/* ---------- DMG OAM corruption bug ---------- */

static bool cupid_gb_oam_bug_active(const CupidGb *gb, uint16_t address, uint16_t *row_offset)
{
    uint8_t ly;

    if (gb == 0 || gb->cgb_mode || !cupid_gb_lcd_enabled(gb)) {
        return false;
    }

    if (address < 0xfe00u || address >= 0xff00u) {
        return false;
    }

    ly = gb->io_registers[CUPID_GB_IO_LY];
    if (ly >= CUPID_GB_PPU_VISIBLE_SCANLINES || gb->ppu_counter >= CUPID_GB_PPU_OAM_CYCLES) {
        return false;
    }

    if (row_offset != 0) {
        *row_offset = (uint16_t)(gb->ppu_counter * 8u);
    }
    return true;
}

static uint16_t cupid_gb_oam_bug_get_word(const CupidGb *gb, uint16_t row_offset, uint8_t word_index)
{
    size_t offset = (size_t)row_offset + (size_t)word_index * 2u;

    return (uint16_t)((uint16_t)gb->object_attribute_memory[offset]
                      | (uint16_t)((uint16_t)gb->object_attribute_memory[offset + 1u] << 8u));
}

static void cupid_gb_oam_bug_set_word(CupidGb *gb, uint16_t row_offset, uint8_t word_index, uint16_t value)
{
    size_t offset = (size_t)row_offset + (size_t)word_index * 2u;

    gb->object_attribute_memory[offset] = (uint8_t)(value & 0x00ffu);
    gb->object_attribute_memory[offset + 1u] = (uint8_t)(value >> 8u);
}

static void cupid_gb_oam_bug_copy_row(CupidGb *gb, uint16_t dst_row_offset, uint16_t src_row_offset)
{
    size_t index;

    for (index = 0u; index < 8u; ++index) {
        gb->object_attribute_memory[(size_t)dst_row_offset + index] =
            gb->object_attribute_memory[(size_t)src_row_offset + index];
    }
}

static void cupid_gb_trigger_oam_bug_write(CupidGb *gb, uint16_t address)
{
    uint16_t row_offset;
    uint16_t a;
    uint16_t b;
    uint16_t c;
    size_t index;

    if (!cupid_gb_oam_bug_active(gb, address, &row_offset) || row_offset < 8u || row_offset >= 0x00a0u) {
        return;
    }

    a = cupid_gb_oam_bug_get_word(gb, row_offset, 0u);
    b = cupid_gb_oam_bug_get_word(gb, (uint16_t)(row_offset - 8u), 0u);
    c = cupid_gb_oam_bug_get_word(gb, (uint16_t)(row_offset - 4u), 0u);

    cupid_gb_oam_bug_set_word(gb, row_offset, 0u, (uint16_t)(((a ^ c) & (b ^ c)) ^ c));
    for (index = 0u; index < 6u; ++index) {
        gb->object_attribute_memory[(size_t)row_offset + 2u + index] =
            gb->object_attribute_memory[(size_t)row_offset - 6u + index];
    }
}

void cupid_gb_trigger_oam_bug_write_access(CupidGb *gb, uint16_t address)
{
    cupid_gb_trigger_oam_bug_write(gb, address);
}

static void cupid_gb_trigger_oam_bug_read(CupidGb *gb, uint16_t address)
{
    uint16_t row_offset;
    uint16_t a;
    uint16_t b;
    uint16_t c;

    if (!cupid_gb_oam_bug_active(gb, address, &row_offset) || row_offset < 8u || row_offset >= 0x00a0u) {
        return;
    }

    a = cupid_gb_oam_bug_get_word(gb, row_offset, 0u);
    b = cupid_gb_oam_bug_get_word(gb, (uint16_t)(row_offset - 8u), 0u);
    c = cupid_gb_oam_bug_get_word(gb, (uint16_t)(row_offset - 8u), 2u);

    cupid_gb_oam_bug_copy_row(gb, row_offset, (uint16_t)(row_offset - 8u));
    cupid_gb_oam_bug_set_word(gb, row_offset, 0u, (uint16_t)(b | (a & c)));
}

static void cupid_gb_trigger_oam_bug_read_increment(CupidGb *gb, uint16_t address)
{
    uint16_t row_offset;

    if (!cupid_gb_oam_bug_active(gb, address, &row_offset) || row_offset < 8u || row_offset >= 0x00a0u) {
        return;
    }

    if (row_offset >= 16u && row_offset < 0x0098u) {
        uint16_t a = cupid_gb_oam_bug_get_word(gb, (uint16_t)(row_offset - 16u), 0u);
        uint16_t b = cupid_gb_oam_bug_get_word(gb, (uint16_t)(row_offset - 8u), 0u);
        uint16_t c = cupid_gb_oam_bug_get_word(gb, row_offset, 0u);
        uint16_t d = cupid_gb_oam_bug_get_word(gb, (uint16_t)(row_offset - 8u), 2u);
        uint16_t glitch = (uint16_t)((b & (a | c | d)) | (a & c & d));

        cupid_gb_oam_bug_set_word(gb, (uint16_t)(row_offset - 8u), 0u, glitch);
        cupid_gb_oam_bug_copy_row(gb, row_offset, (uint16_t)(row_offset - 8u));
        cupid_gb_oam_bug_copy_row(gb, (uint16_t)(row_offset - 16u), (uint16_t)(row_offset - 8u));
    }

    cupid_gb_trigger_oam_bug_read(gb, address);
}

/* ---------- fetch / push / pop ---------- */

uint8_t cupid_gb_fetch_u8(CupidGb *gb)
{
    uint8_t value = cupid_gb_read_u8(gb, gb->cpu.pc);
    if (gb->cpu.halt_bug) {
        gb->cpu.halt_bug = false;
    } else {
        gb->cpu.pc = (uint16_t)(gb->cpu.pc + 1u);
    }
    return value;
}

static uint16_t cupid_gb_fetch_u16(CupidGb *gb)
{
    uint16_t low = cupid_gb_fetch_u8(gb);
    uint16_t high = cupid_gb_fetch_u8(gb);
    return (uint16_t)(low | (uint16_t)(high << 8u));
}

/* ---------- interrupts ---------- */

void cupid_gb_request_interrupt(CupidGb *gb, uint8_t mask)
{
    gb->interrupt_flags = (uint8_t)((gb->interrupt_flags | mask) & 0x1fu);
}

bool cupid_gb_service_interrupt(CupidGb *gb)
{
    static const uint16_t vectors[5] = {0x40u, 0x48u, 0x50u, 0x58u, 0x60u};
    uint8_t pending;
    uint8_t bit;
    uint16_t pc_save;

    pending = (uint8_t)(gb->interrupt_enable & gb->interrupt_flags & 0x1fu);
    if (pending == 0u) {
        return false;
    }

    gb->cpu.halted = false;
    if (!gb->cpu.ime) {
        return false;
    }

    for (bit = 0u; bit < 5u; ++bit) {
        uint8_t mask = (uint8_t)(1u << bit);
        if ((pending & mask) != 0u) {
            gb->cpu.ime = false;
            gb->cpu.ime_delay = 0u;

            pc_save = gb->cpu.pc;

            /* M1, M2: two internal (no-op) cycles */
            cupid_gb_tick(gb, 2u);
            cupid_gb_trigger_oam_bug_write(gb, gb->cpu.sp);

            /* M3: push PC high byte */
            gb->cpu.sp = (uint16_t)(gb->cpu.sp - 1u);
            cupid_gb_write_u8(gb, gb->cpu.sp, (uint8_t)(pc_save >> 8u));
            cupid_gb_tick(gb, 1u);

            /* IE may have been modified by the upper-byte push. Re-evaluate
             * the highest-priority pending interrupt before committing the
             * dispatch. If none remain, the dispatch is cancelled and the
             * final jump target becomes $0000. */
            pending = (uint8_t)(gb->interrupt_enable & gb->interrupt_flags & 0x1fu);
            if (pending != 0u) {
                for (bit = 0u; bit < 5u; ++bit) {
                    uint8_t current_mask = (uint8_t)(1u << bit);
                    if ((pending & current_mask) != 0u) {
                        gb->interrupt_flags =
                            (uint8_t)(gb->interrupt_flags & (uint8_t)(~current_mask));
                        break;
                    }
                }
            } else {
                bit = 0xffu;
            }

            /* M4: push PC low byte */
            gb->cpu.sp = (uint16_t)(gb->cpu.sp - 1u);
            cupid_gb_write_u8(gb, gb->cpu.sp, (uint8_t)(pc_save & 0x00ffu));
            cupid_gb_tick(gb, 1u);

            /* M5: load PC from vector */
            gb->cpu.pc = (bit < 5u) ? vectors[bit] : 0x0000u;
            cupid_gb_tick(gb, 1u);

            return true;
        }
    }

    return false;
}

/* ---------- register read/write by index ---------- */

static uint8_t cupid_gb_read_reg8(CupidGb *gb, uint8_t index)
{
    switch (index & 0x07u) {
    case 0u:
        return gb->cpu.b;
    case 1u:
        return gb->cpu.c;
    case 2u:
        return gb->cpu.d;
    case 3u:
        return gb->cpu.e;
    case 4u:
        return gb->cpu.h;
    case 5u:
        return gb->cpu.l;
    case 6u:
        return cupid_gb_read_u8(gb, cupid_gb_get_hl(&gb->cpu));
    default:
        return gb->cpu.a;
    }
}

static void cupid_gb_write_reg8(CupidGb *gb, uint8_t index, uint8_t value)
{
    switch (index & 0x07u) {
    case 0u:
        gb->cpu.b = value;
        break;
    case 1u:
        gb->cpu.c = value;
        break;
    case 2u:
        gb->cpu.d = value;
        break;
    case 3u:
        gb->cpu.e = value;
        break;
    case 4u:
        gb->cpu.h = value;
        break;
    case 5u:
        gb->cpu.l = value;
        break;
    case 6u:
        cupid_gb_write_u8(gb, cupid_gb_get_hl(&gb->cpu), value);
        break;
    default:
        gb->cpu.a = value;
        break;
    }
}

static uint16_t cupid_gb_read_pair(const CupidGbCpu *cpu, uint8_t pair)
{
    switch (pair & 0x03u) {
    case 0u:
        return cupid_gb_get_bc(cpu);
    case 1u:
        return cupid_gb_get_de(cpu);
    case 2u:
        return cupid_gb_get_hl(cpu);
    default:
        return cpu->sp;
    }
}

static void cupid_gb_write_pair(CupidGbCpu *cpu, uint8_t pair, uint16_t value)
{
    switch (pair & 0x03u) {
    case 0u:
        cupid_gb_set_bc(cpu, value);
        break;
    case 1u:
        cupid_gb_set_de(cpu, value);
        break;
    case 2u:
        cupid_gb_set_hl(cpu, value);
        break;
    default:
        cpu->sp = value;
        break;
    }
}

static bool cupid_gb_check_condition(const CupidGbCpu *cpu, uint8_t condition)
{
    switch (condition & 0x03u) {
    case 0u:
        return !cupid_gb_get_flag(cpu, CUPID_GB_FLAG_Z);
    case 1u:
        return cupid_gb_get_flag(cpu, CUPID_GB_FLAG_Z);
    case 2u:
        return !cupid_gb_get_flag(cpu, CUPID_GB_FLAG_C);
    default:
        return cupid_gb_get_flag(cpu, CUPID_GB_FLAG_C);
    }
}

/* ---------- ALU ---------- */

static uint8_t cupid_gb_inc8(CupidGbCpu *cpu, uint8_t value)
{
    uint8_t result = (uint8_t)(value + 1u);
    bool carry = cupid_gb_get_flag(cpu, CUPID_GB_FLAG_C);

    cpu->f = 0u;
    cupid_gb_set_flag(cpu, CUPID_GB_FLAG_Z, result == 0u);
    cupid_gb_set_flag(cpu, CUPID_GB_FLAG_H, (value & 0x0fu) == 0x0fu);
    cupid_gb_set_flag(cpu, CUPID_GB_FLAG_C, carry);
    return result;
}

static uint8_t cupid_gb_dec8(CupidGbCpu *cpu, uint8_t value)
{
    uint8_t result = (uint8_t)(value - 1u);
    bool carry = cupid_gb_get_flag(cpu, CUPID_GB_FLAG_C);

    cpu->f = CUPID_GB_FLAG_N;
    cupid_gb_set_flag(cpu, CUPID_GB_FLAG_Z, result == 0u);
    cupid_gb_set_flag(cpu, CUPID_GB_FLAG_H, (value & 0x0fu) == 0u);
    cupid_gb_set_flag(cpu, CUPID_GB_FLAG_C, carry);
    return result;
}

static void cupid_gb_add_a(CupidGbCpu *cpu, uint8_t value)
{
    uint16_t result = (uint16_t)cpu->a + value;

    cpu->f = 0u;
    cupid_gb_set_flag(cpu, CUPID_GB_FLAG_Z, (uint8_t)result == 0u);
    cupid_gb_set_flag(cpu, CUPID_GB_FLAG_H, ((cpu->a & 0x0fu) + (value & 0x0fu)) > 0x0fu);
    cupid_gb_set_flag(cpu, CUPID_GB_FLAG_C, result > 0x00ffu);
    cpu->a = (uint8_t)result;
}

static void cupid_gb_adc_a(CupidGbCpu *cpu, uint8_t value)
{
    uint8_t carry = cupid_gb_get_flag(cpu, CUPID_GB_FLAG_C) ? 1u : 0u;
    unsigned int result = (unsigned int)cpu->a + (unsigned int)value + (unsigned int)carry;

    cpu->f = 0u;
    cupid_gb_set_flag(cpu, CUPID_GB_FLAG_Z, (uint8_t)result == 0u);
    cupid_gb_set_flag(cpu,
                      CUPID_GB_FLAG_H,
                      ((cpu->a & 0x0fu) + (value & 0x0fu) + carry) > 0x0fu);
    cupid_gb_set_flag(cpu, CUPID_GB_FLAG_C, result > 0x00ffu);
    cpu->a = (uint8_t)result;
}

static void cupid_gb_sub_a(CupidGbCpu *cpu, uint8_t value)
{
    cpu->f = CUPID_GB_FLAG_N;
    cupid_gb_set_flag(cpu, CUPID_GB_FLAG_Z, cpu->a == value);
    cupid_gb_set_flag(cpu, CUPID_GB_FLAG_H, (cpu->a & 0x0fu) < (value & 0x0fu));
    cupid_gb_set_flag(cpu, CUPID_GB_FLAG_C, cpu->a < value);
    cpu->a = (uint8_t)(cpu->a - value);
}

static void cupid_gb_sbc_a(CupidGbCpu *cpu, uint8_t value)
{
    uint8_t carry = cupid_gb_get_flag(cpu, CUPID_GB_FLAG_C) ? 1u : 0u;
    uint16_t subtrahend = (uint16_t)value + carry;

    cpu->f = CUPID_GB_FLAG_N;
    cupid_gb_set_flag(cpu, CUPID_GB_FLAG_Z, cpu->a == (uint8_t)subtrahend);
    cupid_gb_set_flag(cpu,
                      CUPID_GB_FLAG_H,
                      (cpu->a & 0x0fu) < (uint8_t)((value & 0x0fu) + carry));
    cupid_gb_set_flag(cpu, CUPID_GB_FLAG_C, cpu->a < subtrahend);
    cpu->a = (uint8_t)(cpu->a - subtrahend);
}

static void cupid_gb_and_a(CupidGbCpu *cpu, uint8_t value)
{
    cpu->a = (uint8_t)(cpu->a & value);
    cpu->f = 0u;
    cupid_gb_set_flag(cpu, CUPID_GB_FLAG_Z, cpu->a == 0u);
    cupid_gb_set_flag(cpu, CUPID_GB_FLAG_H, true);
}

static void cupid_gb_xor_a(CupidGbCpu *cpu, uint8_t value)
{
    cpu->a = (uint8_t)(cpu->a ^ value);
    cpu->f = 0u;
    cupid_gb_set_flag(cpu, CUPID_GB_FLAG_Z, cpu->a == 0u);
}

static void cupid_gb_or_a(CupidGbCpu *cpu, uint8_t value)
{
    cpu->a = (uint8_t)(cpu->a | value);
    cpu->f = 0u;
    cupid_gb_set_flag(cpu, CUPID_GB_FLAG_Z, cpu->a == 0u);
}

static void cupid_gb_cp_a(CupidGbCpu *cpu, uint8_t value)
{
    cpu->f = CUPID_GB_FLAG_N;
    cupid_gb_set_flag(cpu, CUPID_GB_FLAG_Z, cpu->a == value);
    cupid_gb_set_flag(cpu, CUPID_GB_FLAG_H, (cpu->a & 0x0fu) < (value & 0x0fu));
    cupid_gb_set_flag(cpu, CUPID_GB_FLAG_C, cpu->a < value);
}

static void cupid_gb_add_hl(CupidGbCpu *cpu, uint16_t value)
{
    uint32_t result = (uint32_t)cupid_gb_get_hl(cpu) + value;
    bool zero = cupid_gb_get_flag(cpu, CUPID_GB_FLAG_Z);

    cpu->f = 0u;
    cupid_gb_set_flag(cpu,
                      CUPID_GB_FLAG_H,
                      ((cupid_gb_get_hl(cpu) & 0x0fffu) + (value & 0x0fffu)) > 0x0fffu);
    cupid_gb_set_flag(cpu, CUPID_GB_FLAG_C, result > 0x0000ffffu);
    cupid_gb_set_flag(cpu, CUPID_GB_FLAG_Z, zero);
    cupid_gb_set_hl(cpu, (uint16_t)result);
}

static uint16_t cupid_gb_add_sp_offset(CupidGbCpu *cpu, int8_t offset)
{
    uint16_t base   = cpu->sp;
    uint8_t  sp_lo  = (uint8_t)(base & 0x00ffu);
    uint8_t  off8   = (uint8_t)offset; /* reinterpret signed bits as unsigned byte */
    uint16_t sum8   = (uint16_t)sp_lo + (uint16_t)off8; /* 8-bit + 8-bit addition */

    /* Z and N always 0; H and C from the low-byte addition only */
    cpu->f = 0u;
    cupid_gb_set_flag(cpu, CUPID_GB_FLAG_H, ((sp_lo ^ off8 ^ (uint8_t)sum8) & 0x10u) != 0u);
    cupid_gb_set_flag(cpu, CUPID_GB_FLAG_C, sum8 > 0x00ffu);

    return (uint16_t)(base + (int16_t)offset);
}

/* ---------- shift / rotate / bit ---------- */

static uint8_t cupid_gb_rlc(CupidGbCpu *cpu, uint8_t value, bool zero_flag)
{
    uint8_t carry = (uint8_t)(value >> 7u);
    uint8_t result = (uint8_t)((value << 1u) | carry);

    cpu->f = 0u;
    cupid_gb_set_flag(cpu, CUPID_GB_FLAG_Z, zero_flag && result == 0u);
    cupid_gb_set_flag(cpu, CUPID_GB_FLAG_C, carry != 0u);
    return result;
}

static uint8_t cupid_gb_rrc(CupidGbCpu *cpu, uint8_t value, bool zero_flag)
{
    uint8_t carry = (uint8_t)(value & 0x01u);
    uint8_t result = (uint8_t)((value >> 1u) | (uint8_t)(carry << 7u));

    cpu->f = 0u;
    cupid_gb_set_flag(cpu, CUPID_GB_FLAG_Z, zero_flag && result == 0u);
    cupid_gb_set_flag(cpu, CUPID_GB_FLAG_C, carry != 0u);
    return result;
}

static uint8_t cupid_gb_rl(CupidGbCpu *cpu, uint8_t value, bool zero_flag)
{
    uint8_t old_carry = cupid_gb_get_flag(cpu, CUPID_GB_FLAG_C) ? 1u : 0u;
    uint8_t new_carry = (uint8_t)(value >> 7u);
    uint8_t result = (uint8_t)((value << 1u) | old_carry);

    cpu->f = 0u;
    cupid_gb_set_flag(cpu, CUPID_GB_FLAG_Z, zero_flag && result == 0u);
    cupid_gb_set_flag(cpu, CUPID_GB_FLAG_C, new_carry != 0u);
    return result;
}

static uint8_t cupid_gb_rr(CupidGbCpu *cpu, uint8_t value, bool zero_flag)
{
    uint8_t old_carry = cupid_gb_get_flag(cpu, CUPID_GB_FLAG_C) ? 1u : 0u;
    uint8_t new_carry = (uint8_t)(value & 0x01u);
    uint8_t result = (uint8_t)((value >> 1u) | (uint8_t)(old_carry << 7u));

    cpu->f = 0u;
    cupid_gb_set_flag(cpu, CUPID_GB_FLAG_Z, zero_flag && result == 0u);
    cupid_gb_set_flag(cpu, CUPID_GB_FLAG_C, new_carry != 0u);
    return result;
}

static uint8_t cupid_gb_sla(CupidGbCpu *cpu, uint8_t value)
{
    uint8_t carry = (uint8_t)(value >> 7u);
    uint8_t result = (uint8_t)(value << 1u);

    cpu->f = 0u;
    cupid_gb_set_flag(cpu, CUPID_GB_FLAG_Z, result == 0u);
    cupid_gb_set_flag(cpu, CUPID_GB_FLAG_C, carry != 0u);
    return result;
}

static uint8_t cupid_gb_sra(CupidGbCpu *cpu, uint8_t value)
{
    uint8_t carry = (uint8_t)(value & 0x01u);
    uint8_t result = (uint8_t)((value >> 1u) | (value & 0x80u));

    cpu->f = 0u;
    cupid_gb_set_flag(cpu, CUPID_GB_FLAG_Z, result == 0u);
    cupid_gb_set_flag(cpu, CUPID_GB_FLAG_C, carry != 0u);
    return result;
}

static uint8_t cupid_gb_swap(CupidGbCpu *cpu, uint8_t value)
{
    uint8_t result = (uint8_t)((value << 4u) | (value >> 4u));

    cpu->f = 0u;
    cupid_gb_set_flag(cpu, CUPID_GB_FLAG_Z, result == 0u);
    return result;
}

static uint8_t cupid_gb_srl(CupidGbCpu *cpu, uint8_t value)
{
    uint8_t carry = (uint8_t)(value & 0x01u);
    uint8_t result = (uint8_t)(value >> 1u);

    cpu->f = 0u;
    cupid_gb_set_flag(cpu, CUPID_GB_FLAG_Z, result == 0u);
    cupid_gb_set_flag(cpu, CUPID_GB_FLAG_C, carry != 0u);
    return result;
}

static void cupid_gb_bit(CupidGbCpu *cpu, uint8_t bit, uint8_t value)
{
    bool carry = cupid_gb_get_flag(cpu, CUPID_GB_FLAG_C);

    cpu->f = 0u;
    cupid_gb_set_flag(cpu, CUPID_GB_FLAG_Z, (value & (uint8_t)(1u << bit)) == 0u);
    cupid_gb_set_flag(cpu, CUPID_GB_FLAG_H, true);
    cupid_gb_set_flag(cpu, CUPID_GB_FLAG_C, carry);
}

static void cupid_gb_daa(CupidGbCpu *cpu)
{
    uint8_t correction = 0u;
    bool carry = cupid_gb_get_flag(cpu, CUPID_GB_FLAG_C);

    if (!cupid_gb_get_flag(cpu, CUPID_GB_FLAG_N)) {
        if (cupid_gb_get_flag(cpu, CUPID_GB_FLAG_H) || (cpu->a & 0x0fu) > 0x09u) {
            correction |= 0x06u;
        }
        if (carry || cpu->a > 0x99u) {
            correction |= 0x60u;
            carry = true;
        }
        cpu->a = (uint8_t)(cpu->a + correction);
    } else {
        if (cupid_gb_get_flag(cpu, CUPID_GB_FLAG_H)) {
            correction |= 0x06u;
        }
        if (carry) {
            correction |= 0x60u;
        }
        cpu->a = (uint8_t)(cpu->a - correction);
    }

    cupid_gb_set_flag(cpu, CUPID_GB_FLAG_Z, cpu->a == 0u);
    cupid_gb_set_flag(cpu, CUPID_GB_FLAG_H, false);
    cupid_gb_set_flag(cpu, CUPID_GB_FLAG_C, carry);
}

/* ---------- instruction decode ---------- */

static uint8_t cupid_gb_unsupported_opcode(CupidGb *gb, uint16_t opcode, bool prefixed)
{
    if (prefixed) {
        cupid_log_errorf("Unsupported Game Boy CB opcode 0x%02X at PC=0x%04X",
                         (unsigned int)opcode,
                         (unsigned int)(gb->cpu.pc - 1u));
    } else {
        cupid_log_errorf("Unsupported Game Boy opcode 0x%02X at PC=0x%04X",
                         (unsigned int)opcode,
                         (unsigned int)(gb->cpu.pc - 1u));
    }

    gb->cpu.halted = true;
    return 0u;
}

static uint8_t cupid_gb_execute_cb(CupidGb *gb, uint8_t opcode)
{
    uint8_t reg   = (uint8_t)(opcode & 0x07u);
    bool    is_hl = (reg == 6u);
    uint8_t value;

    /* M1=CB opcode already fetched before execute_unprefixed was called.
     * M2=sub-opcode was fetched inside case 0xCB before we arrive here.
     * For (HL) ops the data read must happen at M3, so advance two cycles. */
    if (is_hl) {
        cupid_gb_tick(gb, 2u);
    }
    value = cupid_gb_read_reg8(gb, reg); /* M3 for HL, immediate for regs */

    /* For read-modify-write (HL) ops the write must happen at M4. */
    if (opcode <= 0x07u) {
        value = cupid_gb_rlc(&gb->cpu, value, true);
        if (is_hl) { cupid_gb_tick(gb, 1u); }
        cupid_gb_write_reg8(gb, reg, value);
        return is_hl ? 1u : 2u;  /* HL: 2+1+1=4 total; reg: 2 total */
    }
    if (opcode <= 0x0fu) {
        value = cupid_gb_rrc(&gb->cpu, value, true);
        if (is_hl) { cupid_gb_tick(gb, 1u); }
        cupid_gb_write_reg8(gb, reg, value);
        return is_hl ? 1u : 2u;
    }
    if (opcode <= 0x17u) {
        value = cupid_gb_rl(&gb->cpu, value, true);
        if (is_hl) { cupid_gb_tick(gb, 1u); }
        cupid_gb_write_reg8(gb, reg, value);
        return is_hl ? 1u : 2u;
    }
    if (opcode <= 0x1fu) {
        value = cupid_gb_rr(&gb->cpu, value, true);
        if (is_hl) { cupid_gb_tick(gb, 1u); }
        cupid_gb_write_reg8(gb, reg, value);
        return is_hl ? 1u : 2u;
    }
    if (opcode <= 0x27u) {
        value = cupid_gb_sla(&gb->cpu, value);
        if (is_hl) { cupid_gb_tick(gb, 1u); }
        cupid_gb_write_reg8(gb, reg, value);
        return is_hl ? 1u : 2u;
    }
    if (opcode <= 0x2fu) {
        value = cupid_gb_sra(&gb->cpu, value);
        if (is_hl) { cupid_gb_tick(gb, 1u); }
        cupid_gb_write_reg8(gb, reg, value);
        return is_hl ? 1u : 2u;
    }
    if (opcode <= 0x37u) {
        value = cupid_gb_swap(&gb->cpu, value);
        if (is_hl) { cupid_gb_tick(gb, 1u); }
        cupid_gb_write_reg8(gb, reg, value);
        return is_hl ? 1u : 2u;
    }
    if (opcode <= 0x3fu) {
        value = cupid_gb_srl(&gb->cpu, value);
        if (is_hl) { cupid_gb_tick(gb, 1u); }
        cupid_gb_write_reg8(gb, reg, value);
        return is_hl ? 1u : 2u;
    }
    if (opcode <= 0x7fu) {
        /* BIT: read-only, no write */
        cupid_gb_bit(&gb->cpu, (uint8_t)((opcode - 0x40u) >> 3u), value);
        return is_hl ? 1u : 2u;  /* HL: 2+1=3 total; reg: 2 total */
    }
    if (opcode <= 0xbfu) {
        value = (uint8_t)(value & (uint8_t)(~(uint8_t)(1u << ((opcode - 0x80u) >> 3u))));
        if (is_hl) { cupid_gb_tick(gb, 1u); }
        cupid_gb_write_reg8(gb, reg, value);
        return is_hl ? 1u : 2u;
    }
    value = (uint8_t)(value | (uint8_t)(1u << ((opcode - 0xc0u) >> 3u)));
    if (is_hl) { cupid_gb_tick(gb, 1u); }
    cupid_gb_write_reg8(gb, reg, value);
    return is_hl ? 1u : 2u;
}

uint8_t cupid_gb_execute_unprefixed(CupidGb *gb, uint8_t opcode)
{
    if ((opcode & 0xc0u) == 0x40u) {
        if (opcode == 0x76u) {
            uint8_t pending = (uint8_t)(gb->interrupt_enable & gb->interrupt_flags & 0x1fu);

            if (!gb->cpu.ime && pending != 0u) {
                gb->cpu.halt_bug = true;
            } else {
                gb->cpu.halted = true;
            }
            return 1u;
        }

        {
            uint8_t dst = (uint8_t)((opcode >> 3u) & 0x07u);
            uint8_t src = (uint8_t)(opcode & 0x07u);
            /* LD r,(HL) or LD (HL),r: memory access must be at M2 */
            if (src == 6u || dst == 6u) { cupid_gb_tick(gb, 1u); }
            cupid_gb_write_reg8(gb, dst, cupid_gb_read_reg8(gb, src));
            return 1u; /* HL variant: 1+1=2 total; reg: 1 total */
        }
    }

    if ((opcode & 0xf8u) == 0x80u) {
        if ((opcode & 0x07u) == 0x06u) { cupid_gb_tick(gb, 1u); }
        cupid_gb_add_a(&gb->cpu, cupid_gb_read_reg8(gb, (uint8_t)(opcode & 0x07u)));
        return 1u;
    }
    if ((opcode & 0xf8u) == 0x88u) {
        if ((opcode & 0x07u) == 0x06u) { cupid_gb_tick(gb, 1u); }
        cupid_gb_adc_a(&gb->cpu, cupid_gb_read_reg8(gb, (uint8_t)(opcode & 0x07u)));
        return 1u;
    }
    if ((opcode & 0xf8u) == 0x90u) {
        if ((opcode & 0x07u) == 0x06u) { cupid_gb_tick(gb, 1u); }
        cupid_gb_sub_a(&gb->cpu, cupid_gb_read_reg8(gb, (uint8_t)(opcode & 0x07u)));
        return 1u;
    }
    if ((opcode & 0xf8u) == 0x98u) {
        if ((opcode & 0x07u) == 0x06u) { cupid_gb_tick(gb, 1u); }
        cupid_gb_sbc_a(&gb->cpu, cupid_gb_read_reg8(gb, (uint8_t)(opcode & 0x07u)));
        return 1u;
    }
    if ((opcode & 0xf8u) == 0xa0u) {
        if ((opcode & 0x07u) == 0x06u) { cupid_gb_tick(gb, 1u); }
        cupid_gb_and_a(&gb->cpu, cupid_gb_read_reg8(gb, (uint8_t)(opcode & 0x07u)));
        return 1u;
    }
    if ((opcode & 0xf8u) == 0xa8u) {
        if ((opcode & 0x07u) == 0x06u) { cupid_gb_tick(gb, 1u); }
        cupid_gb_xor_a(&gb->cpu, cupid_gb_read_reg8(gb, (uint8_t)(opcode & 0x07u)));
        return 1u;
    }
    if ((opcode & 0xf8u) == 0xb0u) {
        if ((opcode & 0x07u) == 0x06u) { cupid_gb_tick(gb, 1u); }
        cupid_gb_or_a(&gb->cpu, cupid_gb_read_reg8(gb, (uint8_t)(opcode & 0x07u)));
        return 1u;
    }
    if ((opcode & 0xf8u) == 0xb8u) {
        if ((opcode & 0x07u) == 0x06u) { cupid_gb_tick(gb, 1u); }
        cupid_gb_cp_a(&gb->cpu, cupid_gb_read_reg8(gb, (uint8_t)(opcode & 0x07u)));
        return 1u;
    }

    switch (opcode) {
    case 0x00u:
        return 1u;
    case 0x01u:
    case 0x11u:
    case 0x21u:
    case 0x31u:
        cupid_gb_write_pair(&gb->cpu, (uint8_t)((opcode >> 4u) & 0x03u), cupid_gb_fetch_u16(gb));
        return 3u;
    case 0x02u:
        cupid_gb_tick(gb, 1u);
        cupid_gb_write_u8(gb, cupid_gb_get_bc(&gb->cpu), gb->cpu.a);
        return 1u;
    case 0x03u:
    case 0x13u:
    case 0x23u:
    case 0x33u: {
        uint8_t pair = (uint8_t)((opcode >> 4u) & 0x03u);
        uint16_t value = cupid_gb_read_pair(&gb->cpu, pair);

        cupid_gb_tick(gb, 1u);
        cupid_gb_trigger_oam_bug_write(gb, value);
        cupid_gb_write_pair(&gb->cpu, pair, (uint16_t)(value + 1u));
        return 1u;
    }
    case 0x04u:
    case 0x0cu:
    case 0x14u:
    case 0x1cu:
    case 0x24u:
    case 0x2cu:
    case 0x34u:
    case 0x3cu: {
        uint8_t reg = (uint8_t)((opcode >> 3u) & 0x07u);
        uint8_t val;
        if (reg == 6u) { cupid_gb_tick(gb, 1u); }  /* (HL) read at M2 */
        val = cupid_gb_inc8(&gb->cpu, cupid_gb_read_reg8(gb, reg));
        if (reg == 6u) { cupid_gb_tick(gb, 1u); }  /* (HL) write at M3 */
        cupid_gb_write_reg8(gb, reg, val);
        return 1u; /* (HL): 1+1+1=3 total; reg: 1 total */
    }
    case 0x05u:
    case 0x0du:
    case 0x15u:
    case 0x1du:
    case 0x25u:
    case 0x2du:
    case 0x35u:
    case 0x3du: {
        uint8_t reg = (uint8_t)((opcode >> 3u) & 0x07u);
        uint8_t val;
        if (reg == 6u) { cupid_gb_tick(gb, 1u); }  /* (HL) read at M2 */
        val = cupid_gb_dec8(&gb->cpu, cupid_gb_read_reg8(gb, reg));
        if (reg == 6u) { cupid_gb_tick(gb, 1u); }  /* (HL) write at M3 */
        cupid_gb_write_reg8(gb, reg, val);
        return 1u; /* (HL): 1+1+1=3 total; reg: 1 total */
    }
    case 0x06u:
    case 0x0eu:
    case 0x16u:
    case 0x1eu:
    case 0x26u:
    case 0x2eu:
    case 0x3eu:
        cupid_gb_write_reg8(gb, (uint8_t)((opcode >> 3u) & 0x07u), cupid_gb_fetch_u8(gb));
        return 2u;
    case 0x36u: {  /* LD (HL),n: M1=opcode, M2=fetch n, M3=write(HL) */
        uint8_t n = cupid_gb_fetch_u8(gb);
        cupid_gb_tick(gb, 2u);
        cupid_gb_write_u8(gb, cupid_gb_get_hl(&gb->cpu), n);
        return 1u;
    }
    case 0x07u:
        gb->cpu.a = cupid_gb_rlc(&gb->cpu, gb->cpu.a, false);
        return 1u;
    case 0x08u: {  /* LD (nn),SP: M1=opcode M2/M3=fetch nn M4=write lo M5=write hi */
        uint16_t address = cupid_gb_fetch_u16(gb);
        cupid_gb_tick(gb, 3u);
        cupid_gb_write_u8(gb, address, (uint8_t)(gb->cpu.sp & 0x00ffu));
        cupid_gb_tick(gb, 1u);
        cupid_gb_write_u8(gb, (uint16_t)(address + 1u), (uint8_t)(gb->cpu.sp >> 8u));
        return 1u;
    }
    case 0x09u:
    case 0x19u:
    case 0x29u:
    case 0x39u:
        cupid_gb_add_hl(&gb->cpu, cupid_gb_read_pair(&gb->cpu, (uint8_t)((opcode >> 4u) & 0x03u)));
        return 2u;
    case 0x0au:
        cupid_gb_tick(gb, 1u);
        gb->cpu.a = cupid_gb_read_u8(gb, cupid_gb_get_bc(&gb->cpu));
        return 1u;
    case 0x0bu:
    case 0x1bu:
    case 0x2bu:
    case 0x3bu: {
        uint8_t pair = (uint8_t)((opcode >> 4u) & 0x03u);
        uint16_t value = cupid_gb_read_pair(&gb->cpu, pair);

        cupid_gb_tick(gb, 1u);
        cupid_gb_trigger_oam_bug_write(gb, value);
        cupid_gb_write_pair(&gb->cpu, pair, (uint16_t)(value - 1u));
        return 1u;
    }
    case 0x0fu:
        gb->cpu.a = cupid_gb_rrc(&gb->cpu, gb->cpu.a, false);
        return 1u;
    case 0x10u:
        (void)cupid_gb_fetch_u8(gb); /* STOP's padding byte */
        if (gb->cgb_mode && gb->speed_switch_armed) {
            gb->double_speed = !gb->double_speed;
            gb->speed_switch_armed = false;
            gb->speed_phase = false;
            gb->io_registers[0x4du] = (uint8_t)(gb->double_speed ? 0x80u : 0x00u);
            gb->cpu.stopped = false;
            return 1u;
        }
        gb->cpu.stopped = true;
        return 1u;
    case 0x12u:
        cupid_gb_write_u8(gb, cupid_gb_get_de(&gb->cpu), gb->cpu.a);
        cupid_gb_tick(gb, 1u);
        return 1u;
    case 0x17u:
        gb->cpu.a = cupid_gb_rl(&gb->cpu, gb->cpu.a, false);
        return 1u;
    case 0x18u: {
        int8_t offset = (int8_t)cupid_gb_fetch_u8(gb);
        gb->cpu.pc = (uint16_t)(gb->cpu.pc + offset);
        return 3u;
    }
    case 0x1au:
        gb->cpu.a = cupid_gb_read_u8(gb, cupid_gb_get_de(&gb->cpu));
        cupid_gb_tick(gb, 1u);
        return 1u;
    case 0x1fu:
        gb->cpu.a = cupid_gb_rr(&gb->cpu, gb->cpu.a, false);
        return 1u;
    case 0x20u:
    case 0x28u:
    case 0x30u:
    case 0x38u: {
        int8_t offset = (int8_t)cupid_gb_fetch_u8(gb);
        if (cupid_gb_check_condition(&gb->cpu, (uint8_t)((opcode >> 3u) & 0x03u))) {
            gb->cpu.pc = (uint16_t)(gb->cpu.pc + offset);
            return 3u;
        }
        return 2u;
    }
    case 0x22u: {
        uint16_t hl = cupid_gb_get_hl(&gb->cpu);
        cupid_gb_tick(gb, 1u);
        cupid_gb_write_u8(gb, hl, gb->cpu.a);
        cupid_gb_trigger_oam_bug_write(gb, hl);
        cupid_gb_set_hl(&gb->cpu, (uint16_t)(hl + 1u));
        return 1u;
    }
    case 0x27u:
        cupid_gb_daa(&gb->cpu);
        return 1u;
    case 0x2au: {
        uint16_t hl = cupid_gb_get_hl(&gb->cpu);
        cupid_gb_tick(gb, 1u);
        gb->cpu.a = cupid_gb_read_u8(gb, hl);
        cupid_gb_trigger_oam_bug_read_increment(gb, hl);
        cupid_gb_set_hl(&gb->cpu, (uint16_t)(hl + 1u));
        return 1u;
    }
    case 0x2fu:
        gb->cpu.a = (uint8_t)(gb->cpu.a ^ 0xffu);
        cupid_gb_set_flag(&gb->cpu, CUPID_GB_FLAG_N, true);
        cupid_gb_set_flag(&gb->cpu, CUPID_GB_FLAG_H, true);
        return 1u;
    case 0x32u: {
        uint16_t hl = cupid_gb_get_hl(&gb->cpu);
        cupid_gb_tick(gb, 1u);
        cupid_gb_write_u8(gb, hl, gb->cpu.a);
        cupid_gb_trigger_oam_bug_write(gb, hl);
        cupid_gb_set_hl(&gb->cpu, (uint16_t)(hl - 1u));
        return 1u;
    }
    case 0x37u:
        cupid_gb_set_flag(&gb->cpu, CUPID_GB_FLAG_N, false);
        cupid_gb_set_flag(&gb->cpu, CUPID_GB_FLAG_H, false);
        cupid_gb_set_flag(&gb->cpu, CUPID_GB_FLAG_C, true);
        return 1u;
    case 0x3au: {
        uint16_t hl = cupid_gb_get_hl(&gb->cpu);
        cupid_gb_tick(gb, 1u);
        gb->cpu.a = cupid_gb_read_u8(gb, hl);
        cupid_gb_trigger_oam_bug_read_increment(gb, hl);
        cupid_gb_set_hl(&gb->cpu, (uint16_t)(hl - 1u));
        return 1u;
    }
    case 0x3fu:
        cupid_gb_set_flag(&gb->cpu, CUPID_GB_FLAG_N, false);
        cupid_gb_set_flag(&gb->cpu, CUPID_GB_FLAG_H, false);
        cupid_gb_set_flag(&gb->cpu,
                          CUPID_GB_FLAG_C,
                          !cupid_gb_get_flag(&gb->cpu, CUPID_GB_FLAG_C));
        return 1u;
    case 0xc0u:
    case 0xc8u:
    case 0xd0u:
    case 0xd8u:
        /* RET cc: taken M1=opcode M2=internal M3=read_lo M4=read_hi M5=internal */
        if (cupid_gb_check_condition(&gb->cpu, (uint8_t)((opcode >> 3u) & 0x03u))) {
            uint8_t lo, hi;
            cupid_gb_tick(gb, 1u);  /* M2 */
            lo = cupid_gb_read_u8(gb, gb->cpu.sp);
            gb->cpu.sp = (uint16_t)(gb->cpu.sp + 1u);
            cupid_gb_tick(gb, 1u);  /* M3 */
            hi = cupid_gb_read_u8(gb, gb->cpu.sp);
            gb->cpu.sp = (uint16_t)(gb->cpu.sp + 1u);
            gb->cpu.pc = (uint16_t)((uint16_t)lo | (uint16_t)(hi << 8u));
            return 3u;  /* M1+M4+M5 */
        }
        return 2u;
    case 0xc1u: {
        /* POP BC: M1=opcode M2=read_lo M3=read_hi */
        uint8_t lo, hi;
        cupid_gb_tick(gb, 1u);  /* M1 */
        cupid_gb_trigger_oam_bug_read_increment(gb, gb->cpu.sp);
        lo = cupid_gb_read_u8(gb, gb->cpu.sp);
        gb->cpu.sp = (uint16_t)(gb->cpu.sp + 1u);
        cupid_gb_tick(gb, 1u);  /* M2 */
        cupid_gb_trigger_oam_bug_read_increment(gb, gb->cpu.sp);
        hi = cupid_gb_read_u8(gb, gb->cpu.sp);
        gb->cpu.sp = (uint16_t)(gb->cpu.sp + 1u);
        cupid_gb_set_bc(&gb->cpu, (uint16_t)((uint16_t)lo | (uint16_t)(hi << 8u)));
        return 1u;  /* M3 */
    }
    case 0xc2u:
    case 0xcau:
    case 0xd2u:
    case 0xdau: {
        uint16_t address;

        address = (uint16_t)cupid_gb_read_u8(gb, gb->cpu.pc);
        gb->cpu.pc = (uint16_t)(gb->cpu.pc + 1u);
        cupid_gb_tick(gb, 1u);
        address |= (uint16_t)(cupid_gb_read_u8(gb, gb->cpu.pc) << 8u);
        gb->cpu.pc = (uint16_t)(gb->cpu.pc + 1u);

        if (cupid_gb_check_condition(&gb->cpu, (uint8_t)((opcode >> 3u) & 0x03u))) {
            cupid_gb_tick(gb, 1u);
            gb->cpu.pc = address;
            return 1u;
        }
        return 2u;
    }
    case 0xc3u: {
        uint16_t address;

        address = (uint16_t)cupid_gb_read_u8(gb, gb->cpu.pc);
        gb->cpu.pc = (uint16_t)(gb->cpu.pc + 1u);
        cupid_gb_tick(gb, 1u);
        address |= (uint16_t)(cupid_gb_read_u8(gb, gb->cpu.pc) << 8u);
        gb->cpu.pc = (uint16_t)(gb->cpu.pc + 1u);
        cupid_gb_tick(gb, 1u);
        gb->cpu.pc = address;
        return 1u;
    }
    case 0xc4u:
    case 0xccu:
    case 0xd4u:
    case 0xdcu: {
        /* CALL cc,nn: taken M1=opcode M2=lo M3=hi M4=internal M5=push_hi M6=push_lo */
        uint16_t address;

        address = (uint16_t)cupid_gb_read_u8(gb, gb->cpu.pc);
        gb->cpu.pc = (uint16_t)(gb->cpu.pc + 1u);
        cupid_gb_tick(gb, 1u);  /* M2 */
        address |= (uint16_t)(cupid_gb_read_u8(gb, gb->cpu.pc) << 8u);
        gb->cpu.pc = (uint16_t)(gb->cpu.pc + 1u);

        if (cupid_gb_check_condition(&gb->cpu, (uint8_t)((opcode >> 3u) & 0x03u))) {
            cupid_gb_tick(gb, 1u);  /* M3 */
            cupid_gb_tick(gb, 1u);  /* M4 */
            cupid_gb_trigger_oam_bug_write(gb, gb->cpu.sp);
            gb->cpu.sp = (uint16_t)(gb->cpu.sp - 1u);
            cupid_gb_write_u8(gb, gb->cpu.sp, (uint8_t)(gb->cpu.pc >> 8u));
            cupid_gb_tick(gb, 1u);  /* M5 */
            gb->cpu.sp = (uint16_t)(gb->cpu.sp - 1u);
            cupid_gb_write_u8(gb, gb->cpu.sp, (uint8_t)(gb->cpu.pc & 0xffu));
            gb->cpu.pc = address;
            return 1u;  /* M6 */
        }
        return 2u;      /* M1+M3 */
    }
    case 0xc5u: {
        /* PUSH BC: M1=opcode M2=internal M3=write_hi M4=write_lo */
        uint16_t val = cupid_gb_get_bc(&gb->cpu);
        cupid_gb_tick(gb, 1u);  /* M1 */
        cupid_gb_trigger_oam_bug_write(gb, gb->cpu.sp);
        gb->cpu.sp = (uint16_t)(gb->cpu.sp - 1u);
        cupid_gb_write_u8(gb, gb->cpu.sp, (uint8_t)(val >> 8u));
        cupid_gb_tick(gb, 1u);  /* M2 */
        gb->cpu.sp = (uint16_t)(gb->cpu.sp - 1u);
        cupid_gb_write_u8(gb, gb->cpu.sp, (uint8_t)(val & 0xffu));
        return 1u;  /* M3 */
    }
    case 0xc6u:
        cupid_gb_add_a(&gb->cpu, cupid_gb_fetch_u8(gb));
        return 2u;
    case 0xc7u:
    case 0xcfu:
    case 0xd7u:
    case 0xdfu:
    case 0xe7u:
    case 0xefu:
    case 0xf7u:
    case 0xffu: {
        /* RST: M1=opcode M2=internal M3=write_hi M4=write_lo */
        uint16_t ret_addr = gb->cpu.pc;
        cupid_gb_tick(gb, 1u);  /* M1 */
        cupid_gb_trigger_oam_bug_write(gb, gb->cpu.sp);
        gb->cpu.sp = (uint16_t)(gb->cpu.sp - 1u);
        cupid_gb_write_u8(gb, gb->cpu.sp, (uint8_t)(ret_addr >> 8u));
        cupid_gb_tick(gb, 1u);  /* M2 */
        gb->cpu.sp = (uint16_t)(gb->cpu.sp - 1u);
        cupid_gb_write_u8(gb, gb->cpu.sp, (uint8_t)(ret_addr & 0xffu));
        gb->cpu.pc = (uint16_t)(opcode & 0x38u);
        return 1u;  /* M3 */
    }
    case 0xc9u: {
        /* RET: M1=opcode M2=read_lo M3=read_hi M4=internal */
        uint8_t lo, hi;
        cupid_gb_trigger_oam_bug_read_increment(gb, gb->cpu.sp);
        lo = cupid_gb_read_u8(gb, gb->cpu.sp);
        gb->cpu.sp = (uint16_t)(gb->cpu.sp + 1u);
        cupid_gb_tick(gb, 1u);  /* M2 */
        cupid_gb_trigger_oam_bug_read_increment(gb, gb->cpu.sp);
        hi = cupid_gb_read_u8(gb, gb->cpu.sp);
        gb->cpu.sp = (uint16_t)(gb->cpu.sp + 1u);
        gb->cpu.pc = (uint16_t)((uint16_t)lo | (uint16_t)(hi << 8u));
        return 3u;  /* M1+M3+M4 */
    }
    case 0xcbu:
        return cupid_gb_execute_cb(gb, cupid_gb_fetch_u8(gb));
    case 0xcdu: {
        /* CALL nn: M1=opcode M2=addr_lo M3=addr_hi M4=internal M5=push_hi M6=push_lo */
        uint16_t target;

        target = (uint16_t)cupid_gb_read_u8(gb, gb->cpu.pc);
        gb->cpu.pc = (uint16_t)(gb->cpu.pc + 1u);
        cupid_gb_tick(gb, 1u);  /* M2 */
        target |= (uint16_t)(cupid_gb_read_u8(gb, gb->cpu.pc) << 8u);
        gb->cpu.pc = (uint16_t)(gb->cpu.pc + 1u);

        /* gb->cpu.pc now points past operand = return address */
        cupid_gb_tick(gb, 1u);  /* M3 */
        cupid_gb_tick(gb, 1u);  /* M4 */
        cupid_gb_trigger_oam_bug_write(gb, gb->cpu.sp);
        gb->cpu.sp = (uint16_t)(gb->cpu.sp - 1u);
        cupid_gb_write_u8(gb, gb->cpu.sp, (uint8_t)(gb->cpu.pc >> 8u));
        cupid_gb_tick(gb, 1u);  /* M5 */
        gb->cpu.sp = (uint16_t)(gb->cpu.sp - 1u);
        cupid_gb_write_u8(gb, gb->cpu.sp, (uint8_t)(gb->cpu.pc & 0xffu));
        gb->cpu.pc = target;
        return 1u;  /* M6 */
    }
    case 0xceu:
        cupid_gb_adc_a(&gb->cpu, cupid_gb_fetch_u8(gb));
        return 2u;
    case 0xd1u: {
        /* POP DE: M1=opcode M2=read_lo M3=read_hi */
        uint8_t lo, hi;
        cupid_gb_tick(gb, 1u);  /* M1 */
        cupid_gb_trigger_oam_bug_read_increment(gb, gb->cpu.sp);
        lo = cupid_gb_read_u8(gb, gb->cpu.sp);
        gb->cpu.sp = (uint16_t)(gb->cpu.sp + 1u);
        cupid_gb_tick(gb, 1u);  /* M2 */
        cupid_gb_trigger_oam_bug_read_increment(gb, gb->cpu.sp);
        hi = cupid_gb_read_u8(gb, gb->cpu.sp);
        gb->cpu.sp = (uint16_t)(gb->cpu.sp + 1u);
        cupid_gb_set_de(&gb->cpu, (uint16_t)((uint16_t)lo | (uint16_t)(hi << 8u)));
        return 1u;  /* M3 */
    }
    case 0xd3u:
    case 0xdbu:
    case 0xddu:
    case 0xe3u:
    case 0xe4u:
    case 0xebu:
    case 0xecu:
    case 0xedu:
    case 0xf4u:
    case 0xfcu:
    case 0xfdu:
        return cupid_gb_unsupported_opcode(gb, opcode, false);
    case 0xd5u: {
        /* PUSH DE: M1=opcode M2=internal M3=write_hi M4=write_lo */
        uint16_t val = cupid_gb_get_de(&gb->cpu);
        cupid_gb_tick(gb, 1u);  /* M1 */
        cupid_gb_trigger_oam_bug_write(gb, gb->cpu.sp);
        gb->cpu.sp = (uint16_t)(gb->cpu.sp - 1u);
        cupid_gb_write_u8(gb, gb->cpu.sp, (uint8_t)(val >> 8u));
        cupid_gb_tick(gb, 1u);  /* M2 */
        gb->cpu.sp = (uint16_t)(gb->cpu.sp - 1u);
        cupid_gb_write_u8(gb, gb->cpu.sp, (uint8_t)(val & 0xffu));
        return 1u;  /* M3 */
    }
    case 0xd6u:
        cupid_gb_sub_a(&gb->cpu, cupid_gb_fetch_u8(gb));
        return 2u;
    case 0xd9u: {
        /* RETI: M1=opcode M2=read_lo M3=read_hi M4=internal */
        uint8_t lo, hi;
        cupid_gb_trigger_oam_bug_read_increment(gb, gb->cpu.sp);
        lo = cupid_gb_read_u8(gb, gb->cpu.sp);
        gb->cpu.sp = (uint16_t)(gb->cpu.sp + 1u);
        cupid_gb_tick(gb, 1u);  /* M2 */
        cupid_gb_trigger_oam_bug_read_increment(gb, gb->cpu.sp);
        hi = cupid_gb_read_u8(gb, gb->cpu.sp);
        gb->cpu.sp = (uint16_t)(gb->cpu.sp + 1u);
        gb->cpu.pc = (uint16_t)((uint16_t)lo | (uint16_t)(hi << 8u));
        gb->cpu.ime = true;
        gb->cpu.ime_delay = 0u;
        return 3u;  /* M1+M3+M4 */
    }
    case 0xdeu:
        cupid_gb_sbc_a(&gb->cpu, cupid_gb_fetch_u8(gb));
        return 2u;
    case 0xe0u: {  /* LDH (n),A: M1=opcode M2=fetch n M3=write */
        uint8_t n = cupid_gb_fetch_u8(gb);
        cupid_gb_tick(gb, 2u);
        cupid_gb_write_u8(gb, (uint16_t)(0xff00u + n), gb->cpu.a);
        return 1u;
    }
    case 0xe1u: {
        /* POP HL: M1=opcode M2=read_lo M3=read_hi */
        uint8_t lo, hi;
        cupid_gb_tick(gb, 1u);  /* M1 */
        cupid_gb_trigger_oam_bug_read_increment(gb, gb->cpu.sp);
        lo = cupid_gb_read_u8(gb, gb->cpu.sp);
        gb->cpu.sp = (uint16_t)(gb->cpu.sp + 1u);
        cupid_gb_tick(gb, 1u);  /* M2 */
        cupid_gb_trigger_oam_bug_read_increment(gb, gb->cpu.sp);
        hi = cupid_gb_read_u8(gb, gb->cpu.sp);
        gb->cpu.sp = (uint16_t)(gb->cpu.sp + 1u);
        cupid_gb_set_hl(&gb->cpu, (uint16_t)((uint16_t)lo | (uint16_t)(hi << 8u)));
        return 1u;  /* M3 */
    }
    case 0xe2u:
        cupid_gb_tick(gb, 1u);
        cupid_gb_write_u8(gb, (uint16_t)(0xff00u + gb->cpu.c), gb->cpu.a);
        return 1u;
    case 0xe5u: {
        /* PUSH HL: M1=opcode M2=internal M3=write_hi M4=write_lo */
        uint16_t val = cupid_gb_get_hl(&gb->cpu);
        cupid_gb_tick(gb, 1u);  /* M1 */
        cupid_gb_trigger_oam_bug_write(gb, gb->cpu.sp);
        gb->cpu.sp = (uint16_t)(gb->cpu.sp - 1u);
        cupid_gb_write_u8(gb, gb->cpu.sp, (uint8_t)(val >> 8u));
        cupid_gb_tick(gb, 1u);  /* M2 */
        gb->cpu.sp = (uint16_t)(gb->cpu.sp - 1u);
        cupid_gb_write_u8(gb, gb->cpu.sp, (uint8_t)(val & 0xffu));
        return 1u;  /* M3 */
    }
    case 0xe6u:
        cupid_gb_and_a(&gb->cpu, cupid_gb_fetch_u8(gb));
        return 2u;
    case 0xe8u:
        gb->cpu.sp = cupid_gb_add_sp_offset(&gb->cpu, (int8_t)cupid_gb_fetch_u8(gb));
        return 4u;
    case 0xe9u:
        gb->cpu.pc = cupid_gb_get_hl(&gb->cpu);
        return 1u;
    case 0xeau: {  /* LD (nn),A: M1=opcode M2/M3=fetch nn M4=write */
        uint16_t addr = cupid_gb_fetch_u16(gb);
        cupid_gb_tick(gb, 3u);
        cupid_gb_write_u8(gb, addr, gb->cpu.a);
        return 1u;
    }
    case 0xeeu:
        cupid_gb_xor_a(&gb->cpu, cupid_gb_fetch_u8(gb));
        return 2u;
    case 0xf0u: {  /* LDH A,(n): M1=opcode M2=fetch n M3=read */
        uint8_t n = cupid_gb_fetch_u8(gb);
        cupid_gb_tick(gb, 2u);
        gb->cpu.a = cupid_gb_read_u8(gb, (uint16_t)(0xff00u + n));
        return 1u;
    }
    case 0xf1u: {
        /* POP AF: M1=opcode M2=read_lo M3=read_hi */
        uint8_t lo, hi;
        cupid_gb_tick(gb, 1u);  /* M1 */
        cupid_gb_trigger_oam_bug_read_increment(gb, gb->cpu.sp);
        lo = cupid_gb_read_u8(gb, gb->cpu.sp);
        gb->cpu.sp = (uint16_t)(gb->cpu.sp + 1u);
        cupid_gb_tick(gb, 1u);  /* M2 */
        cupid_gb_trigger_oam_bug_read_increment(gb, gb->cpu.sp);
        hi = cupid_gb_read_u8(gb, gb->cpu.sp);
        gb->cpu.sp = (uint16_t)(gb->cpu.sp + 1u);
        cupid_gb_set_af(&gb->cpu, (uint16_t)((uint16_t)lo | (uint16_t)(hi << 8u)));
        return 1u;  /* M3 */
    }
    case 0xf2u:
        cupid_gb_tick(gb, 1u);
        gb->cpu.a = cupid_gb_read_u8(gb, (uint16_t)(0xff00u + gb->cpu.c));
        return 1u;
    case 0xf3u:
        gb->cpu.ime = false;
        gb->cpu.ime_delay = 0u;
        return 1u;
    case 0xf5u: {
        /* PUSH AF: M1=opcode M2=internal M3=write_hi M4=write_lo */
        uint16_t val = cupid_gb_get_af(&gb->cpu);
        cupid_gb_tick(gb, 1u);  /* M1 */
        cupid_gb_trigger_oam_bug_write(gb, gb->cpu.sp);
        gb->cpu.sp = (uint16_t)(gb->cpu.sp - 1u);
        cupid_gb_write_u8(gb, gb->cpu.sp, (uint8_t)(val >> 8u));
        cupid_gb_tick(gb, 1u);  /* M2 */
        gb->cpu.sp = (uint16_t)(gb->cpu.sp - 1u);
        cupid_gb_write_u8(gb, gb->cpu.sp, (uint8_t)(val & 0xffu));
        return 1u;  /* M3 */
    }
    case 0xf6u:
        cupid_gb_or_a(&gb->cpu, cupid_gb_fetch_u8(gb));
        return 2u;
    case 0xf8u:
        cupid_gb_set_hl(&gb->cpu, cupid_gb_add_sp_offset(&gb->cpu, (int8_t)cupid_gb_fetch_u8(gb)));
        return 3u;
    case 0xf9u:
        cupid_gb_tick(gb, 1u);
        cupid_gb_trigger_oam_bug_write(gb, cupid_gb_get_hl(&gb->cpu));
        gb->cpu.sp = cupid_gb_get_hl(&gb->cpu);
        return 1u;
    case 0xfau: {  /* LD A,(nn): M1=opcode M2/M3=fetch nn M4=read */
        uint16_t addr = cupid_gb_fetch_u16(gb);
        cupid_gb_tick(gb, 3u);
        gb->cpu.a = cupid_gb_read_u8(gb, addr);
        return 1u;
    }
    case 0xfbu:
        return 1u;
    case 0xfeu:
        cupid_gb_cp_a(&gb->cpu, cupid_gb_fetch_u8(gb));
        return 2u;
    default:
        return cupid_gb_unsupported_opcode(gb, opcode, false);
    }
}
