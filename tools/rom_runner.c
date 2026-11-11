#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "cupid/gb/gb.h"
#include "cupid/gb/input.h"
#include "cupid/gbc/cgb.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#endif

static double host_seconds(void) {
#ifdef _WIN32
    LARGE_INTEGER counter, frequency;
    QueryPerformanceCounter(&counter);
    QueryPerformanceFrequency(&frequency);
    return (double)counter.QuadPart / (double)frequency.QuadPart;
#else
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (double)now.tv_sec + (double)now.tv_nsec / 1e9;
#endif
}

static uint32_t crc_byte(uint32_t crc, uint8_t byte) {
    crc ^= byte;
    for (unsigned bit = 0; bit < 8; ++bit) {
        crc = (crc >> 1) ^ ((crc & 1) ? UINT32_C(0xedb88320) : 0);
    }
    return crc;
}

static bool positive_integer(const char *text, uint64_t *value) {
    char *end = NULL;
    errno = 0;
    if (!*text || *text == '-') {
        return false;
    }
    *value = strtoull(text, &end, 10);
    return !errno && end && !*end && *value > 0 && *value <= UINT64_C(10000000000);
}

static int register_result(const CupidGb *gb) {
    /* The debug opcode is meaningful only when this protocol is explicitly selected. */
    if (gb->cpu.halted || gb->cpu.stopped || cupid_gb_read_u8(gb, gb->cpu.pc) != 0x40 ||
        (gb->cpu.ime && (gb->interrupt_enable & gb->interrupt_flags & 0x1f))) {
        return 0;
    }
    const uint8_t registers[] = {gb->cpu.b, gb->cpu.c, gb->cpu.d, gb->cpu.e, gb->cpu.h, gb->cpu.l};
    const uint8_t success[] = {3, 5, 8, 13, 21, 34};
    const uint8_t failure[] = {0x42, 0x42, 0x42, 0x42, 0x42, 0x42};
    if (!memcmp(registers, success, sizeof(success))) {
        return 1;
    }
    return !memcmp(registers, failure, sizeof(failure)) ? -1 : 0;
}

static int write_image(const CupidGb *gb, const char *path, uint32_t *crc) {
    bool sgb = cupid_gb_sgb_active(gb);
    unsigned width = sgb ? CUPID_SGB_SCREEN_WIDTH : CUPID_GB_SCREEN_WIDTH;
    unsigned height = sgb ? CUPID_SGB_SCREEN_HEIGHT : CUPID_GB_SCREEN_HEIGHT;
    uint32_t sgb_pixels[CUPID_SGB_SCREEN_WIDTH * CUPID_SGB_SCREEN_HEIGHT];
    if (sgb) {
        cupid_gb_sgb_render_argb(gb, sgb_pixels, sizeof(sgb_pixels) / sizeof(sgb_pixels[0]));
    }
    FILE *image = path ? fopen(path, "wb") : NULL;
    if (path && !image) {
        return -1;
    }
    if (image && fprintf(image, "P6\n%u %u\n255\n", width, height) < 0) {
        fclose(image);
        return -1;
    }
    *crc = UINT32_MAX;
    for (size_t i = 0; i < (size_t)width * height; ++i) {
        uint8_t rgb[3];
        if (sgb) {
            rgb[0] = (uint8_t)(sgb_pixels[i] >> 16);
            rgb[1] = (uint8_t)(sgb_pixels[i] >> 8);
            rgb[2] = (uint8_t)sgb_pixels[i];
        } else if (gb->cgb_mode || cupid_cgb_compat_active(gb)) {
            uint16_t color = gb->frame_buffer_color[i];
            for (unsigned c = 0; c < 3; ++c) {
                unsigned channel = (color >> (5 * c)) & 31;
                rgb[c] = (uint8_t)((channel << 3) | (channel >> 2));
            }
        } else {
            rgb[0] = rgb[1] = rgb[2] = (uint8_t)(255 - (gb->frame_buffer[i] & 3) * 85);
        }
        for (unsigned c = 0; c < 3; ++c) {
            *crc = crc_byte(*crc, rgb[c]);
        }
        if (image && fwrite(rgb, 1, sizeof(rgb), image) != sizeof(rgb)) {
            fclose(image);
            return -1;
        }
    }
    *crc ^= UINT32_MAX;
    return image ? fclose(image) : 0;
}

typedef struct InputEvent {
    uint64_t frame;
    uint8_t mask;
} InputEvent;

static bool load_input_trace(const char *path, InputEvent *events, size_t *count) {
    *count = 0;
    if (!path) {
        return true;
    }
    FILE *file = fopen(path, "r");
    if (!file) {
        return false;
    }
    char line[128];
    bool valid = true;
    while (fgets(line, sizeof(line), file)) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') {
            continue;
        }
        uint64_t frame;
        unsigned mask;
        char trailing;
        if (*count >= 4096 || sscanf(line, "%" SCNu64 " %x %c", &frame, &mask, &trailing) != 2 ||
            mask > 255 || frame > UINT64_C(10000000000) ||
            (*count && frame < events[*count - 1].frame)) {
            valid = false;
            break;
        }
        events[(*count)++] = (InputEvent){frame, (uint8_t)mask};
    }
    if (ferror(file)) {
        valid = false;
    }
    fclose(file);
    return valid;
}

typedef struct SerialCapture {
    char text[4096];
    size_t length;
    uint64_t bytes;
    bool output;
    bool output_error;
    bool changed;
    bool binary_protocol;
    uint8_t signature[6];
    unsigned signature_length;
    int signature_result;
} SerialCapture;

static void observe_serial(const CupidGb *gb, uint8_t transmitted, void *user_data) {
    (void)gb;
    SerialCapture *capture = user_data;
    ++capture->bytes;
    capture->changed = true;
    if (capture->binary_protocol && !capture->signature_result) {
        if (capture->signature_length == sizeof(capture->signature)) {
            memmove(capture->signature, capture->signature + 1, sizeof(capture->signature) - 1);
            --capture->signature_length;
        }
        capture->signature[capture->signature_length++] = transmitted;
        if (capture->signature_length == sizeof(capture->signature)) {
            static const uint8_t passed[] = {3, 5, 8, 13, 21, 34};
            static const uint8_t failed[] = {0x42, 0x42, 0x42, 0x42, 0x42, 0x42};
            if (!memcmp(capture->signature, passed, sizeof(passed))) {
                capture->signature_result = 1;
            } else if (!memcmp(capture->signature, failed, sizeof(failed))) {
                capture->signature_result = -1;
            }
        }
    }
    if (capture->length == sizeof(capture->text) - 1) {
        memmove(capture->text, capture->text + 1024, capture->length - 1024);
        capture->length -= 1024;
    }
    capture->text[capture->length++] = transmitted ? (char)transmitted : ' ';
    capture->text[capture->length] = '\0';
    if (capture->output && !capture->output_error && fputc(transmitted, stdout) == EOF) {
        capture->output_error = true;
    }
}

int main(int argc, char **argv) {
    const char *rom = NULL, *boot = NULL, *image_path = NULL, *audio_path = NULL;
    const char *protocol = "none", *model_name = "dmg", *trace_path = NULL;
    uint64_t frames = 600, max_steps = 0;
    bool serial_output = false;
    CupidGbModel model = CUPID_GB_MODEL_DMG_ABC;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--help")) {
            puts(
                "CupidGB ROM runner: [--model dmg|mgb|cgb|sgb|sgb2] [--frames N] "
                "[--max-steps N] [--protocol none|serial|register|fibonacci|memory] [--boot FILE] "
                "[--image FILE.ppm] [--audio FILE.pcm] [--input-trace FILE] [--serial-output] ROM");
            return 0;
        }
        if (!strcmp(argv[i], "--serial-output")) {
            serial_output = true;
            continue;
        }
        if (argv[i][0] == '-') {
            if (i + 1 >= argc) {
                fprintf(stderr, "Missing option value.\n");
                return 2;
            }
            const char *option = argv[i++];
            const char *value = argv[i];
            if (!strcmp(option, "--frames") || !strcmp(option, "--max-steps")) {
                if (!positive_integer(value, !strcmp(option, "--frames") ? &frames : &max_steps)) {
                    fprintf(stderr, "Invalid numeric limit.\n");
                    return 2;
                }
            } else if (!strcmp(option, "--model")) {
                model_name = value;
            } else if (!strcmp(option, "--protocol")) {
                protocol = value;
            } else if (!strcmp(option, "--boot")) {
                boot = value;
            } else if (!strcmp(option, "--image")) {
                image_path = value;
            } else if (!strcmp(option, "--input-trace")) {
                trace_path = value;
            } else if (!strcmp(option, "--audio")) {
                audio_path = value;
            } else {
                fprintf(stderr, "Unknown option: %s\n", option);
                return 2;
            }
        } else if (rom) {
            fprintf(stderr, "Only one ROM may be supplied.\n");
            return 2;
        } else {
            rom = argv[i];
        }
    }
    const char *models[] = {"dmg", "mgb", "cgb", "sgb", "sgb2"};
    const CupidGbModel model_ids[] = {CUPID_GB_MODEL_DMG_ABC, CUPID_GB_MODEL_MGB,
                                      CUPID_GB_MODEL_CGB, CUPID_GB_MODEL_SGB, CUPID_GB_MODEL_SGB2};
    bool known_model = false;
    for (size_t i = 0; i < sizeof(model_ids) / sizeof(model_ids[0]); ++i) {
        if (!strcmp(model_name, models[i])) {
            model = model_ids[i];
            known_model = true;
        }
    }
    if (!rom || !known_model ||
        (strcmp(protocol, "none") && strcmp(protocol, "serial") && strcmp(protocol, "register") &&
         strcmp(protocol, "fibonacci") && strcmp(protocol, "memory"))) {
        fprintf(stderr, "A ROM, valid model and valid protocol are required. Use --help.\n");
        return 2;
    }
    const bool protocol_fibonacci = !strcmp(protocol, "fibonacci");
    const bool protocol_register = !strcmp(protocol, "register") || protocol_fibonacci;
    const bool protocol_memory = !strcmp(protocol, "memory");
    const bool protocol_serial = !strcmp(protocol, "serial");
    if (!max_steps) {
        max_steps = frames * UINT64_C(100000);
    }
    InputEvent events[4096];
    size_t event_count, event_index = 0;
    if (!load_input_trace(trace_path, events, &event_count)) {
        fprintf(stderr,
                "Invalid input trace. Expected ordered frame and hexadecimal active-low mask.\n");
        return 2;
    }
    CupidGb *gb = calloc(1, sizeof(*gb));
    if (!gb) {
        return 2;
    }
    cupid_gb_set_model(gb, model);
    if (!cupid_gb_load_rom_file(gb, rom) || (boot && !cupid_gb_load_boot_rom_file(gb, boot))) {
        cupid_gb_cleanup(gb);
        free(gb);
        return 2;
    }
    if (boot) {
        cupid_gb_enter_boot_rom(gb);
    }
    FILE *audio = audio_path ? fopen(audio_path, "wb") : NULL;
    if (audio_path && !audio) {
        cupid_gb_cleanup(gb);
        free(gb);
        return 2;
    }
    uint64_t steps = 0, presentation_events = 0, audio_frames = 0;
    uint32_t audio_crc = UINT32_MAX, image_crc = 0;
    SerialCapture serial = {.output = serial_output, .binary_protocol = protocol_fibonacci};
    cupid_gb_set_serial_output_callback(gb, observe_serial, &serial);
    bool memory_running = false;
    int memory_code = -1;
    const char *status = "bounded";
    int result = 0;
    double start = host_seconds();
    while (gb->elapsed_cycles < frames * UINT64_C(70224) && steps < max_steps) {
        while (event_index < event_count &&
               gb->elapsed_cycles >= events[event_index].frame * UINT64_C(70224)) {
            cupid_gb_set_buttons(gb, events[event_index++].mask);
        }
        if (protocol_register) {
            int detected = register_result(gb);
            if (detected) {
                status = detected > 0 ? "pass" : "fail";
                break;
            }
        }
        if (!cupid_gb_step(gb)) {
            status = "core_error";
            result = 2;
            break;
        }
        ++steps;
        bool test_finished = false;
        if (protocol_memory && cupid_gb_read_u8(gb, 0xa001) == 0xde &&
            cupid_gb_read_u8(gb, 0xa002) == 0xb0 && cupid_gb_read_u8(gb, 0xa003) == 0x61) {
            memory_code = cupid_gb_read_u8(gb, 0xa000);
            if (memory_code == 0x80) {
                memory_running = true;
            } else if (memory_code == 0x81) {
                status = "reset_required";
                test_finished = true;
            } else if (memory_running && memory_code < 0x80) {
                /* Observe running first: initialization writes the signature before status. */
                status = memory_code == 0 ? "pass" : "fail";
                test_finished = true;
            }
        }
        if (serial.output_error) {
            status = "output_error";
            result = 2;
            break;
        }
        if (protocol_serial && serial.changed) {
            serial.changed = false;
            if (strstr(serial.text, "Failed") || strstr(serial.text, "FAILED")) {
                status = "fail";
                test_finished = true;
            } else if (strstr(serial.text, "Passed") || strstr(serial.text, "PASSED")) {
                status = "pass";
                test_finished = true;
            }
        }
        if (protocol_fibonacci && serial.signature_result) {
            status = serial.signature_result > 0 ? "pass" : "fail";
            test_finished = true;
        }
        if (gb->frame_ready) {
            ++presentation_events;
            gb->frame_ready = false;
        }
        /* Drain per instruction, including with the LCD off, to avoid audio overflow. */
        int16_t samples[128];
        uint32_t count = cupid_gb_apu_drain(gb, samples, 64);
        audio_frames += count;
        for (uint32_t i = 0; i < count * 2; ++i) {
            uint16_t sample = (uint16_t)samples[i];
            uint8_t bytes[] = {(uint8_t)sample, (uint8_t)(sample >> 8)};
            audio_crc = crc_byte(crc_byte(audio_crc, bytes[0]), bytes[1]);
            if (audio && fwrite(bytes, 1, 2, audio) != 2) {
                status = "output_error";
                result = 2;
                break;
            }
        }
        if (result || test_finished) {
            break;
        }
    }
    double elapsed = host_seconds() - start;
    const uint64_t vblank_frames = gb->vblank_events;
    // Serial output is a host diagnostic. Flush once at the report boundary.
    if (serial_output && fflush(stdout) == EOF) {
        result = 2;
        status = "output_error";
    }
    if (audio && fclose(audio)) {
        result = 2;
        status = "output_error";
    }
    if (write_image(gb, image_path, &image_crc)) {
        result = 2;
        status = "output_error";
    }
    if (!strcmp(status, "bounded") && steps == max_steps) {
        status = "step_limit";
    }
    char memory_text[4097] = {0};
    if (memory_code >= 0) {
        static const char hex[] = "0123456789abcdef";
        for (unsigned i = 0; i < 2048; ++i) {
            uint8_t byte = cupid_gb_read_u8(gb, (uint16_t)(0xa004 + i));
            if (!byte) {
                break;
            }
            memory_text[i * 2] = hex[byte >> 4];
            memory_text[i * 2 + 1] = hex[byte & 15];
        }
    }
    printf("\nCUPIDGB_RESULT {\"status\":\"%s\",\"model\":\"%s\",\"protocol\":\"%s\","
           "\"steps\":%" PRIu64 ",\"base_cycles\":%" PRIu64 ",\"base_clock_hz\":%" PRIu32 ","
           "\"vblank_frames\":%" PRIu64 ","
           "\"presentation_events\":%" PRIu64 ","
           "\"audio_frames\":%" PRIu64 ",\"serial_bytes\":%" PRIu64 ","
           "\"audio_crc32\":\"%08" PRIx32 "\","
           "\"image_crc32\":\"%08" PRIx32 "\",\"host_seconds\":%.9f,"
           "\"memory_result_code\":%d,\"memory_text_hex\":\"%s\"}\n",
           status, model_name, protocol, steps, gb->elapsed_cycles, cupid_gb_base_clock_hz(gb),
           vblank_frames, presentation_events, audio_frames, serial.bytes, audio_crc ^ UINT32_MAX,
           image_crc, elapsed, memory_code, memory_text);
    if (!strcmp(status, "fail")) {
        result = 1;
    } else if (strcmp(protocol, "none") && strcmp(status, "pass") && !result) {
        result = 3;
    }
    cupid_gb_cleanup(gb);
    free(gb);
    return result;
}
