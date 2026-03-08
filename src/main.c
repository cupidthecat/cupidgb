#include "cupid/common/log.h"
#include "cupid/core/emulator.h"
#include "cupid/core/system.h"
#include "cupid/gb/gb.h"
#include "cupid/platform/sdl_app.h"

#include <limits.h>
#include <unistd.h>
#include <string.h>

static bool cupid_path_exists(const char *path)
{
    return path != 0 && access(path, F_OK) == 0;
}

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
    CupidSdlAppConfig config = {
        .title = "cupidgb",
        .width = 640,
        .height = 576,
    };
    char resolved_rom_path[PATH_MAX];
    const char *rom_path = 0;
    int arg_index;

    cupid_emulator_init(&emulator, CUPID_SYSTEM_GB);

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
                cupid_emulator_shutdown(&emulator);
                return 1;
            }
            model_explicit = true;
        } else if (rom_path == 0) {
            rom_path = argv[arg_index];
        } else {
            cupid_log_errorf("Unexpected argument '%s'.", argv[arg_index]);
            cupid_emulator_shutdown(&emulator);
            return 1;
        }
    }

    rom_path = cupid_resolve_rom_path(rom_path, resolved_rom_path, sizeof(resolved_rom_path));

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
    } else {
        cupid_log_info("No ROM supplied. Pass a .gb file path to load a cartridge.");
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
