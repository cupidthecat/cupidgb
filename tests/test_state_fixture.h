/**
 * @file    test_state_fixture.h
 * @brief   Frozen portable-state wire fixtures and historical audio bytes.
 * @author  cupidthecat
 * @date    2026-11-16
 *
 * Copyright (C) 2026 cupidthecat
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://gnu.org>.
 */

#ifndef CUPID_TEST_STATE_FIXTURE_H
#define CUPID_TEST_STATE_FIXTURE_H

static void print_fixture(const char *path) {
    CupidGb *gb = calloc(1, sizeof(*gb));
    assert(gb);
    prepare(gb, CUPID_GB_MODEL_CGB, true);
    /* The frozen wire fixture supplies its historical bytes explicitly.
     * Startup variation is covered separately from the codec contract. */
    memset(gb->high_ram, 0, sizeof(gb->high_ram));
    memset(gb->cgb_obj_palette_ram, 0, sizeof(gb->cgb_obj_palette_ram));
    for (size_t index = 0; index < sizeof(gb->cgb_bg_palette_ram); index += 2) {
        gb->cgb_bg_palette_ram[index] = 0xff;
        gb->cgb_bg_palette_ram[index + 1] = 0x7f;
    }
    /* Preserve the original divider phase of this compatibility fixture. */
    gb->div_counter = 0;
    gb->double_speed = true;
    cupid_gb_write_u8(gb, 0xff40, 0);
    prepare_phase(gb, 2);
    /* Keep the historical in-flight OAM prefix of this compatibility fixture.
     * The current scheduler completes its last source read one M-cycle earlier. */
    gb->object_attribute_memory[21] = 0;
    gb->dma_source_base = 0xc000;
    gb->dma_index = 21;
    gb->dma_start_delay = 0;
    gb->dma_restart_delay = 2;
    gb->dma_active = true;
    /* Historical writers advanced audio hardware eagerly at this boundary. */
    cupid_gb_apu_run(gb, true);
    size_t length;
    uint8_t *bytes = encode(gb, &length);
    const size_t v11_length = v11_hardware_size(gb, length);
    assert(v11_length == 300600 && bytes[8] == 14 && !bytes[v11_length - 28] &&
           !bytes[v11_length - 27]);
    /* These bytes came from the checked original v4 fixture. Sound synthesis
     * can evolve without rewriting the historical portability contract. */
    const int16_t legacy_samples[] = {4080, 4063, 4047, 4031, 4015, 3999,
                                      3983, 3967, 3951, 3935, 3920, 3904};
    put_u32(bytes + 292148, 1970952);
    memset(bytes + 292152, 0, CUPID_GB_APU_BUF_FRAMES * 4);
    for (unsigned frame = 0; frame < sizeof(legacy_samples) / sizeof(legacy_samples[0]); ++frame)
        for (unsigned side = 0; side < 2; ++side) {
            const size_t offset = 292152 + frame * 4 + side * 2;
            bytes[offset] = (uint8_t)legacy_samples[frame];
            bytes[offset + 1] = (uint8_t)((uint16_t)legacy_samples[frame] >> 8);
        }
    put_u32(bytes + 300344, 12);
    put_u32(bytes + 300349, 0);
    const int64_t legacy_history[] = {8041484160, 8041484160, 12515083, 12515083};
    for (unsigned field = 0; field < 4; ++field)
        for (unsigned byte = 0; byte < 8; ++byte)
            bytes[300353 + field * 8 + byte] =
                (uint8_t)((uint64_t)legacy_history[field] >> (8 * byte));
    put_u32(bytes + 300385, UINT32_C(1069461147));
    bytes[300389] = 0;
    length = v11_length - 28;
    put_u32(bytes + 8, 4);
    put_u32(bytes + 12, (uint32_t)length);
    update_integrity(bytes, length);
    char sha256[65];
    digest_hex(bytes, length, sha256);
    printf("CUPIDGB_STATE_FIXTURE size=%zu sha256=%s\n", length, sha256);
    fflush(stdout);
    assert(length == 300572);
    assert(strcmp(sha256, "652af4f40504c86dd59eff09f0997f5b60b654816a0908d7802704f981b43be5") == 0);
    /* The older wire payloads remain byte-for-byte compatible when wrapped
     * in their original headers. Added fields cannot shift either prefix. */
    put_u32(bytes + 8, 3);
    put_u32(bytes + 12, 300568);
    update_integrity(bytes, 300568);
    char legacy_sha256[65];
    digest_hex(bytes, 300568, legacy_sha256);
    assert(strcmp(legacy_sha256,
                  "e4fdd3d7ebfdd04aec94ce7b85f0f2d7d83de1416242fa89f26ccf5dfaf71eb2") == 0);
    put_u32(bytes + 8, 4);
    put_u32(bytes + 12, (uint32_t)length);
    update_integrity(bytes, length);
    if (path) {
        FILE *file = fopen(path, "wb");
        assert(file && fwrite(bytes, 1, length, file) == length && fclose(file) == 0);
    }
    free(bytes);
    cupid_gb_cleanup(gb);
    free(gb);
}

#endif
