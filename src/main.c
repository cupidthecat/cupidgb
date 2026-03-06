#include "cupid/common/log.h"
#include "cupid/core/emulator.h"
#include "cupid/core/system.h"
#include "cupid/gb/gb.h"
#include "cupid/platform/sdl_app.h"

int main(int argc, char **argv)
{
    CupidEmulator emulator = {0};
    CupidSdlApp app = {0};
    CupidSdlAppConfig config = {
        .title = "cupidgb",
        .width = 640,
        .height = 576,
    };

    cupid_emulator_init(&emulator, CUPID_SYSTEM_GB);

    cupid_log_infof("cupidgb boot stub ready for %s development.",
                    cupid_system_name(emulator.target_system));
    cupid_log_info("Terminal logging is active.");

    if (argc > 1) {
        if (!cupid_emulator_load_rom_file(&emulator, argv[1])) {
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
        cupid_gb_set_save_path(&emulator.gb, argv[1]);
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
