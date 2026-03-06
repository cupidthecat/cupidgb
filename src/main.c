#include "cupid/common/log.h"
#include "cupid/core/emulator.h"
#include "cupid/core/system.h"
#include "cupid/platform/sdl_app.h"

int main(void)
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
    cupid_log_info("Next step: implement cartridge loading and the memory bus.");

    if (!cupid_sdl_app_init(&app, &config, &emulator)) {
        cupid_emulator_shutdown(&emulator);
        return 1;
    }

    cupid_sdl_app_run(&app);
    cupid_sdl_app_shutdown(&app);

    cupid_emulator_shutdown(&emulator);
    return 0;
}
