/**
 * @file main.c
 * @brief cupidgb entry point — argument parsing, ROM loading, and SDL2 launch.
 *
 * Startup sequence:
 *   1. Parse command-line arguments (`--model` and a ROM path).
 *   2. Attempt to auto-detect the target Game Boy model from the ROM
 *      filename; fall back to `DMG-ABC` if no hint is found.
 *   3. Initialize the emulator via @ref cupid_emulator_init.
 *   4. Load the ROM file and re-apply the model if the cartridge header
 *      indicates CGB or SGB support.
 *   5. Set up the battery-backed save path and load any existing save.
 *   6. Search for an external boot ROM and enter the boot sequence if found.
 *   7. Initialize and run the SDL2 front-end (@ref cupid_sdl_app_init /
 *      @ref cupid_sdl_app_run).
 *   8. Flush the battery-backed save and shut down cleanly.
 */
#include "cupid/common/log.h"
#include "cupid/core/emulator.h"
#include "cupid/core/system.h"
#include "cupid/gb/gb.h"
#include "cupid/platform/sdl_app.h"

#include <limits.h>
#include <unistd.h>
#include <string.h>

/**
 * @brief Returns whether a file-system path exists and is accessible.
 *
 * Uses `access(path, F_OK)` to test existence without requiring any
 * particular permission beyond a directory search.
 *
 * @param path The file-system path to test.
 *
 * @return `true` if @p path is non-NULL and `access` succeeds.
 */
static bool cupid_path_exists(const char *path)
{
    return path != 0 && access(path, F_OK) == 0;
}

/**
 * @brief Searches well-known locations for a boot ROM and loads the first one found.
 *
 * For DMG/MGB/SGB models, the following filenames are tried (in order):
 *   - `bootroms/dmg_boot.bin`, `bootroms/gb_boot.bin`
 *   - `dmg_boot.bin`, `gb_boot.bin`
 *
 * For the CGB model:
 *   - `bootroms/cgb_boot.bin`, `bootroms/gbc_boot.bin`
 *   - `cgb_boot.bin`, `gbc_boot.bin`
 *
 * If a file is found and loaded successfully via @ref cupid_gb_load_boot_rom_file,
 * the function returns immediately. If no file is found, an info message
 * is logged and the emulator will skip the original Nintendo boot sequence.
 *
 * @param gb Pointer to the Game Boy state.
 *
 * @note Does nothing if @p gb is NULL.
 */
static void cupid_try_load_boot_rom(CupidGb *gb)
{
    static const char *const dmg_paths[] = {
        "bootroms/dmg_boot.bin",
        "bootroms/gb_boot.bin",
        "dmg_boot.bin",
        "gb_boot.bin",
        0
    };
    static const char *const cgb_paths[] = {
        "bootroms/cgb_boot.bin",
        "bootroms/gbc_boot.bin",
        "cgb_boot.bin",
        "gbc_boot.bin",
        0
    };
    const char *const *paths;
    size_t i;

    if (gb == 0) {
        return;
    }

    paths = (gb->model == CUPID_GB_MODEL_CGB) ? cgb_paths : dmg_paths;

    for (i = 0u; paths[i] != 0; ++i) {
        if (cupid_path_exists(paths[i])) {
            if (cupid_gb_load_boot_rom_file(gb, paths[i])) {
                cupid_log_infof("Loaded external boot ROM: %s", paths[i]);
                return;
            }
        }
    }

    cupid_log_infof("No external %s boot ROM found; startup will skip the original Nintendo boot sequence.",
                    (gb->model == CUPID_GB_MODEL_CGB) ? "CGB" : "DMG");
}

/**
 * @brief Resolves a ROM path, appending a file extension if the bare path does not exist.
 *
 * If @p path exists as-is it is returned unchanged. Otherwise the
 * extensions `.gb`, `.gbc`, and `.sgb` are tried in sequence. If a
 * candidate path ends with `.`, the leading `.` of the extension is
 * skipped to avoid double-dots.
 *
 * When a match is found the resolved path is logged and a pointer to
 * @p resolved_path (the caller-supplied buffer) is returned. If no
 * candidate exists the original @p path pointer is returned so the
 * caller can still attempt to open it and report the error naturally.
 *
 * @param path               The raw ROM path from the command line.
 * @param resolved_path      Caller-supplied buffer to hold the resolved path.
 * @param resolved_path_size Size of @p resolved_path in bytes.
 *
 * @return Pointer to the best available path string (either @p path or
 *         @p resolved_path). Never NULL if @p path is non-NULL.
 */
static const char *cupid_resolve_rom_path(const char *path, char *resolved_path, size_t resolved_path_size)
{
    static const char *const fallback_extensions[] = { ".gb", ".gbc", ".sgb" };
    size_t extension_index;
    size_t path_length;

    if (path == 0 || resolved_path == 0 || resolved_path_size == 0u) {
        return path;
    }

    if (cupid_path_exists(path)) {
        return path;
    }

    path_length = strlen(path);

    for (extension_index = 0u;
         extension_index < sizeof(fallback_extensions) / sizeof(fallback_extensions[0]);
         ++extension_index) {
        const char *extension = fallback_extensions[extension_index];

        if (path_length > 0u && path[path_length - 1u] == '.') {
            extension += 1;
        }

        if (path_length + strlen(extension) + 1u > resolved_path_size) {
            continue;
        }

        strcpy(resolved_path, path);
        strcat(resolved_path, extension);
        if (cupid_path_exists(resolved_path)) {
            cupid_log_infof("ROM path not found, using %s", resolved_path);
            return resolved_path;
        }
    }

    return path;
}

/**
 * @brief Heuristically detects the intended Game Boy model from the ROM filename.
 *
 * Inspects the filename component (everything after the last `/`) for
 * well-known substrings in this priority order:
 *   - `-dmg0` / `_dmg0`                               → @ref CUPID_GB_MODEL_DMG0
 *   - `-mgb` / `_mgb`                                 → @ref CUPID_GB_MODEL_MGB
 *   - `-cgb` / `_cgb` / `.gbc` / `(CGB` / `Game Boy Color` → @ref CUPID_GB_MODEL_CGB
 *   - `-sgb2` / `_sgb2` / `SGB2`                      → @ref CUPID_GB_MODEL_SGB2
 *   - `-sgb` / `_sgb` / `-S.gb` / `_S.gb` / `SGB Enhanced` / `(SGB` → @ref CUPID_GB_MODEL_SGB
 *   - `-dmgABC` / `_dmgABC`                            → @ref CUPID_GB_MODEL_DMG_ABC
 *
 * If no pattern matches, @p fallback is returned.
 *
 * @param path     The ROM file path (may include directory components).
 * @param fallback The model to return when no pattern is matched.
 *
 * @return The detected @ref CupidGbModel, or @p fallback.
 */
static CupidGbModel cupid_gb_detect_model_from_path(const char *path,
                                                    CupidGbModel fallback)
{
    const char *name;

    if (path == 0) {
        return fallback;
    }

    name = strrchr(path, '/');
    if (name != 0) {
        name += 1;
    } else {
        name = path;
    }

    if (strstr(name, "-dmg0") != 0 || strstr(name, "_dmg0") != 0) {
        return CUPID_GB_MODEL_DMG0;
    }

    if (strstr(name, "-mgb") != 0 || strstr(name, "_mgb") != 0) {
        return CUPID_GB_MODEL_MGB;
    }

    if (strstr(name, "-cgb") != 0 || strstr(name, "_cgb") != 0 ||
        strstr(name, ".gbc") != 0 || strstr(name, "(CGB") != 0 ||
        strstr(name, "Game Boy Color") != 0) {
        return CUPID_GB_MODEL_CGB;
    }

    if (strstr(name, "-sgb2") != 0 || strstr(name, "_sgb2") != 0 ||
        strstr(name, "SGB2") != 0) {
        return CUPID_GB_MODEL_SGB2;
    }

    if (strstr(name, "-sgb") != 0 || strstr(name, "_sgb") != 0 ||
        strstr(name, "-S.gb") != 0 || strstr(name, "_S.gb") != 0 ||
        strstr(name, "SGB Enhanced") != 0 || strstr(name, "(SGB") != 0) {
        return CUPID_GB_MODEL_SGB;
    }

    if (strstr(name, "-dmgABC") != 0 || strstr(name, "_dmgABC") != 0) {
        return CUPID_GB_MODEL_DMG_ABC;
    }

    return fallback;
}

int main(int argc, char **argv)
{
    CupidEmulator emulator = {0};
    CupidSdlApp app = {0};
    CupidGbModel model = CUPID_GB_MODEL_DMG_ABC;
    bool model_explicit = false;
    CupidSystem target_system = CUPID_SYSTEM_GB;
    CupidSdlAppConfig config = {
        .title = "cupidgb",
        .width = 640,
        .height = 576,
    };
    char resolved_rom_path[PATH_MAX];
    const char *rom_path = 0;
    int arg_index;

    for (arg_index = 1; arg_index < argc; ++arg_index) {
        if (strcmp(argv[arg_index], "--model") == 0 && arg_index + 1 < argc) {
            ++arg_index;
            if (strcmp(argv[arg_index], "dmg0") == 0) {
                model = CUPID_GB_MODEL_DMG0;
            } else if (strcmp(argv[arg_index], "dmgabc") == 0) {
                model = CUPID_GB_MODEL_DMG_ABC;
            } else if (strcmp(argv[arg_index], "mgb") == 0) {
                model = CUPID_GB_MODEL_MGB;
            } else if (strcmp(argv[arg_index], "cgb") == 0) {
                model = CUPID_GB_MODEL_CGB;
            } else if (strcmp(argv[arg_index], "sgb") == 0) {
                model = CUPID_GB_MODEL_SGB;
            } else if (strcmp(argv[arg_index], "sgb2") == 0) {
                model = CUPID_GB_MODEL_SGB2;
            } else {
                cupid_log_errorf("Unknown Game Boy model '%s'. Use dmgabc, dmg0, mgb, cgb, sgb or sgb2.", argv[arg_index]);
                return 1;
            }
            model_explicit = true;
        } else if (rom_path == 0) {
            rom_path = argv[arg_index];
        } else {
            cupid_log_errorf("Unexpected argument '%s'.", argv[arg_index]);
            return 1;
        }
    }

    rom_path = cupid_resolve_rom_path(rom_path, resolved_rom_path, sizeof(resolved_rom_path));

    cupid_emulator_init(&emulator, target_system);

    {
        // Game Boy / Game Boy Color path
        if (!model_explicit) {
            model = cupid_gb_detect_model_from_path(rom_path, model);
        }

        cupid_gb_set_model(&emulator.gb, model);

        cupid_log_infof("cupidgb boot stub ready for %s development.",
                        cupid_system_name(emulator.target_system));
        cupid_log_info("Terminal logging is active.");
        cupid_log_infof("Game Boy boot profile: %s", cupid_gb_model_name(model));

        if (rom_path != 0) {
            if (!cupid_emulator_load_rom_file(&emulator, rom_path)) {
                cupid_emulator_shutdown(&emulator);
                return 1;
            }

            if (!model_explicit && model == CUPID_GB_MODEL_DMG_ABC) {
                if ((emulator.gb.header.cgb_flag & 0x80u) != 0u) {
                    model = CUPID_GB_MODEL_CGB;
                } else if (emulator.gb.header.sgb_flag == 0x03u) {
                    model = CUPID_GB_MODEL_SGB;
                }
            }

            if (model != emulator.gb.model) {
                cupid_gb_set_model(&emulator.gb, model);
                if (!cupid_emulator_load_rom_file(&emulator, rom_path)) {
                    cupid_emulator_shutdown(&emulator);
                    return 1;
                }
                cupid_log_infof("Game Boy boot profile: %s", cupid_gb_model_name(model));
            }

            cupid_log_infof("title: %s",
                            emulator.gb.header.title[0] != '\0' ? emulator.gb.header.title : "<untitled>");
            cupid_log_infof("cart type: %s",
                            cupid_gb_cartridge_type_name(emulator.gb.header.cartridge_type));
            cupid_log_infof("rom banks: %zu", emulator.gb.rom_bank_count);
            cupid_log_infof("ram banks: %zu", emulator.gb.ram_bank_count);
            cupid_log_infof("mbc: %s", cupid_gb_mbc_name(emulator.gb.header.mbc_type));

            /* Set up battery-backed save path and load existing save */
            cupid_gb_set_save_path(&emulator.gb, rom_path);
            cupid_gb_load_save(&emulator.gb);

            /* Try to load an external boot ROM for authentic startup logo + chime */
            cupid_try_load_boot_rom(&emulator.gb);
            if (emulator.gb.boot_rom_size > 0u) {
                cupid_gb_enter_boot_rom(&emulator.gb);
            }
        } else {
            cupid_log_info("No ROM supplied. Pass a .gb file path to load a cartridge.");
        }
    }

    if (!cupid_sdl_app_init(&app, &config, &emulator)) {
        cupid_emulator_shutdown(&emulator);
        return 1;
    }

    cupid_sdl_app_run(&app);
    cupid_sdl_app_shutdown(&app);

    /* Save battery-backed RAM before exiting */
    cupid_gb_save(&emulator.gb);

    cupid_emulator_shutdown(&emulator);
    return 0;
}
