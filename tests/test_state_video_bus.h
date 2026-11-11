/* Included after the common snapshot helpers in test_state.c. */
static void test_boot_entry_object_abort(void) {
    CupidGb *source = calloc(1, sizeof(*source)), *target = calloc(1, sizeof(*target));
    assert(source && target);
    prepare(source, CUPID_GB_MODEL_CGB, true);
    prepare(target, CUPID_GB_MODEL_CGB, true);
    for (unsigned stage = CUPID_GB_OBJECT_IDLE; stage <= CUPID_GB_OBJECT_LEGACY; ++stage) {
        source->object_fetch_stage = (CupidGbObjectFetchStage)stage;
        source->pixels.object_delay = stage == CUPID_GB_OBJECT_LEGACY ? 11
                                      : stage == CUPID_GB_OBJECT_IDLE ? 5
                                                                      : 0;
        source->pixels.object_count = 1;
        source->pixels.object_index = 0;
        source->pixels.fetched_objects = 1;
        source->pixels.active = true;
        source->pixels.line = 1;
        source->pixels.x = 0;
        source->pixels.count = 8;
        source->pixels.phase = 6;
        source->pixels.delay = 7;
        size_t length;
        uint8_t *bytes = encode(source, &length);
        free(bytes);
        /* Boot entry starts a fresh hardware transaction, including when the
         * prior countdown came from an imported execution state. */
        cupid_gb_enter_boot_rom(source);
        assert(source->object_fetch_stage == CUPID_GB_OBJECT_IDLE);
        assert(!source->pixels.object_delay);
        assert(source->oam_vram_row == UINT16_MAX && !source->oam_search_word &&
               !source->stop_ppu_vram_blocked);
        bytes = encode(source, &length);
        assert(cupid_gb_load_state(target, bytes, length) == CUPID_GB_STATE_OK);
        assert_encoded_equal(source, target);
        free(bytes);
    }
    cupid_gb_cleanup(source);
    cupid_gb_cleanup(target);
    free(source);
    free(target);
}

static void test_video_bus_history(void) {
    enum { VIDEO_HISTORY_BYTES = 6 };

    CupidGb *a = calloc(1, sizeof(*a)), *b = calloc(1, sizeof(*b));
    CupidGb *before = malloc(sizeof(*before));
    assert(a && b && before);
    prepare(a, CUPID_GB_MODEL_CGB, false);
    prepare(b, CUPID_GB_MODEL_CGB, false);
    assert(a->oam_vram_row == UINT16_MAX && !a->oam_search_word && !a->stop_ppu_vram_blocked);
    a->dma_active = true;
    a->dma_source_base = 0x8000;
    a->dma_index = 1;
    const uint16_t rows[] = {0, 1, 0x1fff, UINT16_MAX};
    for (unsigned row = 0; row < sizeof(rows) / sizeof(rows[0]); ++row) {
        a->oam_vram_row = rows[row];
        a->oam_search_word = row & 1 ? UINT16_MAX : 0x0328;
        a->stop_ppu_vram_blocked = (row & 1) != 0;
        a->double_speed = (row & 1) != 0;
        a->speed_phase = a->double_speed;
        a->io_registers[0x4d] = a->double_speed ? 0x80 : 0;
        a->cpu.halted = (row & 1) != 0;
        a->cpu.stopped = (row & 2) != 0;
        size_t length;
        uint8_t *state = encode(a, &length);
        const size_t offset = length - VIDEO_HISTORY_BYTES;
        assert(state[offset] == (uint8_t)rows[row] &&
               state[offset + 1] == (uint8_t)(rows[row] >> 8));
        assert(cupid_gb_load_state(b, state, length) == CUPID_GB_STATE_OK);
        assert(b->oam_vram_row == rows[row] && b->oam_search_word == a->oam_search_word &&
               b->stop_ppu_vram_blocked == a->stop_ppu_vram_blocked);
        assert_encoded_equal(a, b);
        const uint16_t invalid[] = {0x2000, 0xfffe};
        for (unsigned bad = 0; bad < sizeof(invalid) / sizeof(invalid[0]); ++bad) {
            state[offset] = (uint8_t)invalid[bad];
            state[offset + 1] = (uint8_t)(invalid[bad] >> 8);
            update_integrity(state, length);
            *before = *b;
            rejected_unchanged(b, state, length, CUPID_GB_STATE_INVALID);
            assert(memcmp(before, b, sizeof(*b)) == 0);
        }
        state[offset] = (uint8_t)rows[row];
        state[offset + 1] = (uint8_t)(rows[row] >> 8);
        state[offset + 4] = 2;
        update_integrity(state, length);
        *before = *b;
        rejected_unchanged(b, state, length, CUPID_GB_STATE_INVALID);
        assert(memcmp(before, b, sizeof(*b)) == 0);
        free(state);
    }
    a->cpu.halted = false;
    a->cpu.stopped = false;
    a->stop_ppu_vram_blocked = false;
    a->double_speed = false;
    a->speed_phase = false;
    a->io_registers[0x4d] = 0;
    for (unsigned unused = 0; unused < 5; ++unused) {
        a->model = unused == 0 ? CUPID_GB_MODEL_DMG_ABC : CUPID_GB_MODEL_CGB;
        a->cgb_mode = a->model == CUPID_GB_MODEL_CGB;
        a->dma_active = unused != 1 && unused != 4;
        a->dma_source_base = unused == 2 ? 0xc000 : 0x8000;
        a->dma_index = unused >= 3 ? 0 : 1;
        a->dma_start_delay = unused == 4 ? 1 : 0;
        a->oam_vram_row = 1;
        size_t length;
        uint8_t *state = encode(a, &length);
        const size_t offset = length - VIDEO_HISTORY_BYTES;
        assert(a->oam_vram_row == 1 && state[offset] == 0xff && state[offset + 1] == 0xff);
        if (unused != 0) {
            state[offset] = 0;
            state[offset + 1] = 0;
            update_integrity(state, length);
            *before = *b;
            rejected_unchanged(b, state, length, CUPID_GB_STATE_INVALID);
            assert(memcmp(before, b, sizeof(*b)) == 0);
        }
        free(state);
    }
    cupid_gb_cleanup(a);
    prepare(a, CUPID_GB_MODEL_CGB, false);
    cupid_gb_write_u8(a, 0xff40, 0);
    size_t length;
    uint8_t *state = encode(a, &length);
    const unsigned versions[] = {2, 3, 4, 6, 7, 8};
    const size_t lengths[] = {300518,
                              300568,
                              300572,
                              length - VIDEO_HISTORY_BYTES - 12,
                              length - VIDEO_HISTORY_BYTES - 11,
                              length - VIDEO_HISTORY_BYTES};
    for (unsigned version = 0; version < sizeof(versions) / sizeof(versions[0]); ++version) {
        b->oam_vram_row = 2;
        b->oam_search_word = 0x033c;
        b->stop_ppu_vram_blocked = true;
        put_u32(state + 8, versions[version]);
        put_u32(state + 12, (uint32_t)lengths[version]);
        update_integrity(state, lengths[version]);
        assert(cupid_gb_load_state(b, state, lengths[version]) == CUPID_GB_STATE_OK);
        assert(b->oam_vram_row == UINT16_MAX && !b->oam_search_word && !b->stop_ppu_vram_blocked &&
               b->object_fetch_stage == CUPID_GB_OBJECT_IDLE);
    }
    free(state);
    for (unsigned lane = 0; lane < 2; ++lane) {
        a->pixels.line = 24;
        a->pixels.object_count = 0;
        memset(a->object_attribute_memory, 0, sizeof(a->object_attribute_memory));
        a->dma_active = true;
        a->dma_index = 5;
        a->dma_source_base = 0x8000;
        a->oam_search_word = lane ? 0x033c : 0x0328;
        state = encode(a, &length);
        b->oam_search_word = lane ? 0x0328 : 0x033c;
        assert(cupid_gb_load_state(b, state, length) == CUPID_GB_STATE_OK);
        cupid_gb_pixels_scan_object(a, 1);
        cupid_gb_pixels_scan_object(b, 1);
        assert(a->pixels.object_count == (lane ? 0 : 1));
        assert_encoded_equal(a, b);
        free(state);
    }
    cupid_gb_cleanup(a);
    cupid_gb_cleanup(b);
    free(before);
    free(a);
    free(b);
}

static void test_vram_row_continuation(void) {
    CupidGb *a = calloc(1, sizeof(*a)), *b = calloc(1, sizeof(*b));
    CupidGb *expected = malloc(sizeof(*expected)), *wrong = malloc(sizeof(*wrong));
    assert(a && b && expected && wrong);
    for (unsigned options = 0; options < 4; ++options) {
        prepare(a, CUPID_GB_MODEL_CGB, false);
        prepare(b, CUPID_GB_MODEL_CGB, false);
        a->double_speed = (options & 1) != 0;
        a->speed_phase = a->double_speed;
        a->io_registers[0x4d] = a->double_speed ? 0x80 : 0;
        cupid_gb_write_u8(a, 0xff40, 0);
        for (unsigned i = 0; i < sizeof(a->video_ram); ++i) {
            a->video_ram[i] = (uint8_t)(i * 17 + (i >> 13) * 0x51 + 9);
        }
        cupid_gb_write_u8(a, 0xff40, 0x91);
        cupid_gb_tick_ppu_dots(a, 76);
        cupid_gb_write_u8(a, 0xff46, 0x80);
        CupidGbStateContext source_context, target_context;
        assert(cupid_gb_state_context_init(&source_context, a) == CUPID_GB_STATE_OK);
        assert(cupid_gb_state_context_init(&target_context, b) == CUPID_GB_STATE_OK);
        bool witnessed = false;
        for (unsigned cycle = 0; cycle < 150 && !witnessed; ++cycle) {
            cupid_gb_tick(a, 1);
            const bool fast = (options & 1) != 0;
            if (fast ? cycle != 2 : a->oam_vram_row == UINT16_MAX) {
                continue;
            }
            *expected = *a;
            cupid_gb_tick(expected, 1);
            if (!fast) {
                /* A naturally retained normal-speed row must affect the next
                 * transfer. Fast continuation does not assume the same phase. */
                *wrong = *a;
                wrong->oam_vram_row = a->oam_vram_row ^ 2u;
                cupid_gb_tick(wrong, 1);
                if (!memcmp(wrong->object_attribute_memory, expected->object_attribute_memory,
                            sizeof(wrong->object_attribute_memory))) {
                    continue;
                }
            }
            const size_t length = cupid_gb_state_size(a);
            uint8_t *state = malloc(length);
            assert(state);
            if (options & 2) {
                assert(cupid_gb_save_state_with_context(a, state, length, NULL, &source_context) ==
                       CUPID_GB_STATE_OK);
            } else {
                assert(cupid_gb_save_state(a, state, length, NULL) == CUPID_GB_STATE_OK);
            }
            b->oam_vram_row = 2;
            const CupidGbStateResult result =
                options & 2 ? cupid_gb_load_state_with_context(b, state, length, &target_context)
                            : cupid_gb_load_state(b, state, length);
            assert(result == CUPID_GB_STATE_OK && b->oam_vram_row == a->oam_vram_row);
            cupid_gb_tick(b, 1);
            assert(memcmp(b->object_attribute_memory, expected->object_attribute_memory,
                          sizeof(b->object_attribute_memory)) == 0);
            assert_encoded_equal(expected, b);
            witnessed = true;
            free(state);
        }
        assert(witnessed);
        cupid_gb_cleanup(a);
        cupid_gb_cleanup(b);
    }
    free(expected);
    free(wrong);
    free(a);
    free(b);
}

static void test_stop_video_gate_continuation(void) {
    CupidGb *a = calloc(1, sizeof(*a)), *b = calloc(1, sizeof(*b));
    CupidGb *expected = malloc(sizeof(*expected)), *wrong = malloc(sizeof(*wrong));
    assert(a && b && expected && wrong);
    prepare(a, CUPID_GB_MODEL_CGB, false);
    prepare(b, CUPID_GB_MODEL_CGB, false);
    cupid_gb_write_u8(a, 0xff40, 0);
    cupid_gb_write_u8(a, 0xff26, 0);
    cupid_gb_write_u8(a, 0xff07, 0);
    cupid_gb_write_u8(a, 0xff42, 0);
    cupid_gb_write_u8(a, 0xff43, 0);
    cupid_gb_write_u8(a, 0xff4a, 0);
    cupid_gb_write_u8(a, 0xff4b, 0xff);
    cupid_gb_write_u8(a, 0xff45, 0xff);
    cupid_gb_write_u8(a, 0xff41, 0);
    for (unsigned i = 0; i < sizeof(a->video_ram); ++i) {
        a->video_ram[i] = (uint8_t)(i * 17 + (i >> 13) * 0x51 + 9);
    }
    memset(a->object_attribute_memory, 0xcc, sizeof(a->object_attribute_memory));
    cupid_gb_write_u8(a, 0xff4f, 0);
    cupid_gb_write_u8(a, 0xff40, 0x91);
    for (unsigned warmup = 0; warmup < 19; ++warmup) {
        cupid_gb_tick(a, 1);
    }
    cupid_gb_write_u8(a, 0xff04, 0);
    cupid_gb_write_u8(a, 0xff05, 0xfc);
    cupid_gb_write_u8(a, 0xff06, 0x42);
    cupid_gb_write_u8(a, 0xff07, 5);
    cupid_gb_write_u8(a, 0xffff, 0);
    cupid_gb_write_u8(a, 0xff0f, 0);
    cupid_gb_write_u8(a, 0xff00, 0x10);
    cupid_gb_set_buttons(a, 0xff);
    memset(a->high_ram, 0, sizeof(a->high_ram));
    a->high_ram[0] = 0x10;
    a->cpu.pc = 0xff80;
    a->cpu.sp = 0xc100;
    a->cpu.b = 0;
    a->cpu.c = 0x91;
    a->cpu.d = 0xc0;
    a->cpu.e = 0x20;
    a->cpu.h = 0x80;
    a->cpu.l = 0;
    a->cpu.a = 8;
    a->cpu.f = 0;
    a->cpu.ime = false;
    a->cpu.ime_delay = 0;
    a->dma_active = true;
    a->dma_start_delay = 0;
    a->dma_source_base = 0x8000;
    a->dma_index = 0;
    a->dma_phase = 0;
    a->oam_vram_row = UINT16_MAX;
    assert(cupid_gb_step(a) && a->cpu.stopped && a->stop_ppu_vram_blocked);
    size_t length;
    uint8_t *state = encode(a, &length);
    *expected = *a;
    *wrong = *a;
    wrong->stop_ppu_vram_blocked = false;
    assert(cupid_gb_step(expected) && cupid_gb_step(wrong));
    assert(memcmp(wrong->object_attribute_memory, expected->object_attribute_memory,
                  sizeof(wrong->object_attribute_memory)) != 0);
    b->stop_ppu_vram_blocked = false;
    assert(cupid_gb_load_state(b, state, length) == CUPID_GB_STATE_OK);
    assert(b->stop_ppu_vram_blocked && cupid_gb_step(b));
    assert_encoded_equal(expected, b);
    cupid_gb_set_buttons(expected, 0xef);
    cupid_gb_set_buttons(b, 0xef);
    assert(cupid_gb_step(expected) && cupid_gb_step(b));
    assert(!b->cpu.stopped && !b->stop_ppu_vram_blocked);
    assert_encoded_equal(expected, b);
    free(state);
    cupid_gb_cleanup(a);
    cupid_gb_cleanup(b);
    free(expected);
    free(wrong);
    free(a);
    free(b);
}

static void test_object_fetch_stage_history(void) {
    CupidGb *a = calloc(1, sizeof(*a)), *b = calloc(1, sizeof(*b));
    CupidGb *before = malloc(sizeof(*before));
    assert(a && b && before);
    prepare(a, CUPID_GB_MODEL_CGB, false);
    prepare(b, CUPID_GB_MODEL_CGB, false);
    a->pixels.object_count = 1;
    a->pixels.object_index = 0;
    a->pixels.fetched_objects = 1;
    a->pixels.active = true;
    a->pixels.count = 8;
    a->pixels.delay = 7;
    a->pixels.phase = 6;
    const unsigned stages[][2] = {{CUPID_GB_OBJECT_IDLE, 0},
                                  {CUPID_GB_OBJECT_IDLE, 5},
                                  {CUPID_GB_OBJECT_WAIT_BACKGROUND, 0},
                                  {CUPID_GB_OBJECT_READ_WORD, 0},
                                  {CUPID_GB_OBJECT_LEGACY, 1},
                                  {CUPID_GB_OBJECT_LEGACY, 11}};
    for (unsigned entry = 0; entry < sizeof(stages) / sizeof(stages[0]); ++entry) {
        a->object_fetch_stage = (CupidGbObjectFetchStage)stages[entry][0];
        a->pixels.object_delay = (uint8_t)stages[entry][1];
        size_t length;
        uint8_t *state = encode(a, &length);
        assert(state[length - 1] == stages[entry][0]);
        assert(cupid_gb_load_state(b, state, length) == CUPID_GB_STATE_OK);
        assert(a->object_fetch_stage == b->object_fetch_stage);
        assert_encoded_equal(a, b);
        if (entry == 1 || entry == 2 || entry == 3) {
            const size_t offsets[] = {300464, 300464, 300461, 300511,
                                      300419, 300427, 300393, 300392};
            const uint8_t values[] = {1, 255, 0, 0, 0, 8, 160, 144};
            uint8_t *damaged = malloc(length);
            assert(damaged);
            for (unsigned field = 0; field < sizeof(values); ++field) {
                memcpy(damaged, state, length);
                damaged[offsets[field]] = values[field];
                update_integrity(damaged, length);
                *before = *b;
                rejected_unchanged(b, damaged, length, CUPID_GB_STATE_INVALID);
                assert(memcmp(before, b, sizeof(*b)) == 0);
            }
            if (entry != 2) {
                memcpy(damaged, state, length);
                damaged[300420] = 5;
                update_integrity(damaged, length);
                *before = *b;
                rejected_unchanged(b, damaged, length, CUPID_GB_STATE_INVALID);
                assert(memcmp(before, b, sizeof(*b)) == 0);
            }
            free(damaged);
        }
        state[length - 1] = 4;
        update_integrity(state, length);
        *before = *b;
        rejected_unchanged(b, state, length, CUPID_GB_STATE_INVALID);
        assert(memcmp(before, b, sizeof(*b)) == 0);
        if (entry == 0 || entry == 1 || entry == 5) {
            state[length - 1] = entry == 0   ? CUPID_GB_OBJECT_LEGACY
                                : entry == 1 ? CUPID_GB_OBJECT_WAIT_BACKGROUND
                                             : CUPID_GB_OBJECT_IDLE;
            update_integrity(state, length);
            *before = *b;
            rejected_unchanged(b, state, length, CUPID_GB_STATE_INVALID);
            assert(memcmp(before, b, sizeof(*b)) == 0);
        }
        free(state);
    }
    a->pixels.object_address = 0x1000;
    a->pixels.object_low = 0x12;
    a->pixels.object_high = 0x34;
    /* Legacy completion retains the original prefix domain, including an
     * inactive countdown and an unselected but in-range object array slot. */
    a->pixels.object_count = 0;
    a->pixels.object_index = 9;
    a->pixels.active = false;
    size_t length;
    uint8_t *state = encode(a, &length);
    const size_t old_length = length - 6;
    put_u32(state + 8, 8);
    put_u32(state + 12, (uint32_t)old_length);
    update_integrity(state, old_length);
    b->object_fetch_stage = CUPID_GB_OBJECT_READ_WORD;
    assert(cupid_gb_load_state(b, state, old_length) == CUPID_GB_STATE_OK);
    assert(b->object_fetch_stage == CUPID_GB_OBJECT_LEGACY && b->pixels.object_delay == 11 &&
           b->pixels.object_address == 0x1000 && b->pixels.object_low == 0x12 &&
           b->pixels.object_high == 0x34);
    size_t restored_length;
    uint8_t *restored = encode(b, &restored_length);
    assert(restored_length == length &&
           memcmp(restored + 128, state + 128, old_length - 128) == 0 &&
           restored[length - 1] == CUPID_GB_OBJECT_LEGACY);
    free(restored);
    free(state);
    a->pixels.initialized = true;
    a->pixels.line = 0;
    a->pixels.dot = 455;
    a->pixels.x = CUPID_GB_SCREEN_WIDTH;
    state = encode(a, &length);
    put_u32(state + 8, 8);
    put_u32(state + 12, (uint32_t)(length - 6));
    update_integrity(state, length - 6);
    assert(cupid_gb_load_state(b, state, length - 6) == CUPID_GB_STATE_OK);
    cupid_gb_tick_ppu_dots(b, 2);
    assert(b->object_fetch_stage == CUPID_GB_OBJECT_IDLE && !b->pixels.object_delay);
    restored = encode(b, &restored_length);
    assert(cupid_gb_load_state(a, restored, restored_length) == CUPID_GB_STATE_OK);
    assert_encoded_equal(a, b);
    free(restored);
    free(state);
    cupid_gb_cleanup(a);
    cupid_gb_cleanup(b);
    free(before);
    free(a);
    free(b);
}
