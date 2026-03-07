#include "cupid/common/log.h"
#include "cupid/core/emulator.h"
#include "cupid/core/system.h"
#include "cupid/gb/gb.h"
#include "cupid/platform/sdl_app.h"

#include <string.h>

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

    if (strstr(name, "-dmgABC") != 0 || strstr(name, "_dmgABC") != 0 ||
        strstr(name, "-mgb") != 0 || strstr(name, "_mgb") != 0) {
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
            } else {
                cupid_log_errorf("Unknown Game Boy model '%s'. Use dmgabc or dmg0.", argv[arg_index]);
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
