#include "controller_dialog.h"
#include "controller_test_device.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPixmap>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTableWidget>
#include <QTemporaryDir>

using namespace cupid::input;
using namespace controller_test;

static void checkMotionWorkflow() {
    for (int value = -32768; value <= 32767; ++value) {
        const uint16_t latch = uint16_t(0x81d0 + 112.0 * (-value / 32768.0));
        check(ControllerManager::motionCounts(int16_t(value)) == int(latch) - 0x81d0,
              "every signed axis value preserves final hardware-latch quantization");
    }
    check(ControllerManager::motionCounts(1) == -1 && ControllerManager::motionCounts(-1) == 0 &&
              ControllerManager::motionCounts(32767) == -112 &&
              ControllerManager::motionCounts(-32768) == 112,
          "fractional tilt floors negative offsets and preserves asymmetric endpoints");
    DeviceInfo limited;
    auto limitedProfile = ControllerManager::defaults(limited);
    check(limitedProfile.motionX == MotionAxis{} && limitedProfile.motionY == MotionAxis{} &&
              ControllerManager::motionAxesValid(limitedProfile, limited),
          "button-only device defaults leave unavailable axes unassigned");
    limited.axes = 1;
    limitedProfile = ControllerManager::defaults(limited);
    check(limitedProfile.motionX == MotionAxis{MotionAxis::Raw, 0} &&
              limitedProfile.motionY == MotionAxis{},
          "one-axis raw controller keeps only its available default");
    limited.mapped = true;
    limited.controllerAxisMask = 1u << SDL_CONTROLLER_AXIS_LEFTY;
    limitedProfile = ControllerManager::defaults(limited);
    check(limitedProfile.motionX == MotionAxis{} &&
              limitedProfile.motionY ==
                  MotionAxis{MotionAxis::Controller, SDL_CONTROLLER_AXIS_LEFTY},
          "mapped defaults honor available normalized-axis capabilities");

    ControllerManager manager;
    QString error;
    check(manager.backend().start(&error), "start motion workflow backend");
    manager.setFocused(true);
    Device mapped(0x70), raw(0x71, false);
    check(waitFor([&] {
              DeviceState state;
              return manager.backend().state(mapped.id, &state) &&
                     manager.backend().state(raw.id, &state);
          }),
          "motion virtual devices connected");
    DeviceState mappedState, rawState;
    check(manager.backend().state(mapped.id, &mappedState) &&
              manager.backend().state(raw.id, &rawState),
          "motion device identities");
    auto configuration = manager.configuration();
    for (const auto &device : manager.backend().devices()) {
        auto profile = ControllerManager::defaults(device.info);
        profile.enabled = device.info.instance == mapped.id || device.info.instance == raw.id;
        configuration.profiles[device.info.persistentKey] = profile;
    }
    const auto mappedKey = mappedState.info.persistentKey;
    const auto rawKey = rawState.info.persistentKey;
    configuration.profiles[mappedKey].player = 2;
    configuration.profiles[rawKey].player = 3;
    manager.configure(configuration);
    const auto mappedProfile = manager.profile(mappedState.info);
    const auto rawProfile = manager.profile(rawState.info);
    check(mappedProfile.motionX == MotionAxis{MotionAxis::Controller, SDL_CONTROLLER_AXIS_LEFTX} &&
              mappedProfile.motionY ==
                  MotionAxis{MotionAxis::Controller, SDL_CONTROLLER_AXIS_LEFTY} &&
              rawProfile.motionX == MotionAxis{MotionAxis::Raw, 0} &&
              rawProfile.motionY == MotionAxis{MotionAxis::Raw, 1},
          "motion defaults choose mapped left stick or raw axes zero and one");

    auto legacy = QJsonDocument::fromJson(configuration.save()).object();
    auto legacyProfiles = legacy.value("profiles").toObject();
    for (auto it = legacyProfiles.begin(); it != legacyProfiles.end(); ++it) {
        auto object = it.value().toObject();
        object.remove("motionX");
        object.remove("motionY");
        it.value() = object;
    }
    legacy["profiles"] = legacyProfiles;
    bool valid = false;
    const auto migrated = ControllerConfiguration::load(QJsonDocument(legacy).toJson(), &valid);
    check(valid && !migrated.profiles[mappedKey].motionX && !migrated.profiles[rawKey].motionY,
          "legacy profile preserves absent motion selections until device resolution");
    manager.configure(migrated);
    check(manager.profile(mappedState.info) == mappedProfile &&
              manager.profile(rawState.info) == rawProfile,
          "legacy migration resolves device type without changing buttons or players");
    manager.configure(configuration);
    const QByteArray saved = configuration.save();
    check(ControllerConfiguration::load(saved, &valid).save() == saved && valid,
          "explicit motion axes survive persistence");

    mapped.axis(0, 30000);
    manager.backend().pollNow();
    check(!(manager.output().buttons[2] & 1) && !manager.output().motionAvailable,
          "ordinary cartridges keep axis direction bindings");
    manager.setMotionEnabled(true);
    check(manager.motionEnabled() && manager.output().motionAvailable &&
              manager.output().motionX == -103 && manager.output().motionY == 0 &&
              manager.output().buttons[2] == 0xff,
          "motion mode replaces selected axis directions for any single-player assignment");
    raw.axis(0, -32768);
    manager.backend().pollNow();
    check(manager.output().motionX == -103,
          "lowest eligible instance owns motion regardless of later device events");
    mapped.button(0, true);
    mapped.hat(SDL_HAT_LEFT);
    manager.backend().pollNow();
    check(!(manager.output().buttons[2] & 0x10) && !(manager.output().buttons[2] & 2) &&
              (manager.output().buttons[2] & 1),
          "digital button and hat directions remain available beside tilt");
    mapped.button(0, false);
    mapped.hat(SDL_HAT_CENTERED);
    mapped.axis(0, 1);
    mapped.axis(1, -1);
    manager.backend().pollNow();
    check(manager.output().motionX == -1 && manager.output().motionY == 0,
          "motion has no gameplay axis deadzone");
    unsigned changes = 0;
    manager.outputChanged = [&](const ControllerOutput &) { ++changes; };
    mapped.axis(0, 2);
    manager.backend().pollNow();
    manager.backend().pollNow();
    check(changes == 0, "unchanged quantized tilt does not emit repeated output callbacks");

    manager.setFocused(false);
    check(!manager.output().motionAvailable && manager.output().motionX == 0 &&
              manager.output().motionY == 0,
          "focus loss releases motion to unavailable neutral");
    configuration.background = true;
    manager.configure(configuration);
    check(manager.output().motionAvailable && manager.output().motionX == -1,
          "background opt-in resynchronizes held tilt");
    manager.setDeliveryEnabled(false);
    check(!manager.output().motionAvailable && manager.output().motionX == 0,
          "delivery gate overrides background motion");
    manager.setDeliveryEnabled(true);
    check(manager.output().motionAvailable, "delivery resume restores physical tilt");
    manager.setFocused(true);
    configuration.enabled = false;
    manager.configure(configuration);
    check(!manager.output().motionAvailable, "global controller disable releases motion");
    configuration.enabled = true;
    configuration.profiles[mappedKey].enabled = false;
    configuration.profiles[rawKey].enabled = false;
    manager.configure(configuration);
    check(!manager.output().motionAvailable, "disabled profiles cannot drive motion");
    configuration.profiles[rawKey].enabled = true;
    manager.configure(configuration);
    check(manager.output().motionAvailable && manager.output().motionX == 112,
          "player four raw controller can own the single cartridge sensor");
    configuration.profiles[mappedKey].enabled = true;
    configuration.preferredDevice = rawKey;
    manager.configure(configuration);
    check(manager.output().motionX == 112, "preferred device controls the motion source");
    configuration.preferredDevice = mappedKey;
    manager.configure(configuration);
    check(manager.output().motionX == -1, "preferred device changes resynchronize motion");

    auto invalid = configuration;
    invalid.profiles[mappedKey].motionY = invalid.profiles[mappedKey].motionX;
    manager.configure(invalid);
    check(manager.configuration().save() == configuration.save(),
          "duplicate motion selection rejects configuration transactionally");
    ControllerConfiguration::load(invalid.save(), &valid);
    check(!valid, "duplicate persisted motion selectors are rejected");
    invalid = configuration;
    invalid.profiles[mappedKey].motionY = MotionAxis{MotionAxis::Raw, 0};
    check(!ControllerManager::motionAxesValid(invalid.profiles[mappedKey], mappedState.info),
          "mapped and raw selectors cannot alias the same physical axis");
    manager.configure(invalid);
    check(manager.configuration().save() == configuration.save(),
          "physical axis alias rejection leaves active configuration untouched");
    invalid = configuration;
    invalid.profiles[mappedKey].motionX =
        MotionAxis{MotionAxis::Controller, SDL_CONTROLLER_AXIS_MAX};
    ControllerConfiguration::load(invalid.save(), &valid);
    check(!valid, "invalid normalized motion axis index rejected");
    invalid = configuration;
    invalid.profiles[mappedKey].motionX = MotionAxis{MotionAxis::Raw, 200};
    check(!ControllerManager::motionAxesValid(invalid.profiles[mappedKey], mappedState.info),
          "saved unavailable raw axis is invalid for the connected device");
    manager.configure(invalid);
    check(manager.configuration().save() == configuration.save(),
          "unavailable motion selection rejects configuration without changing live input");

    configuration.profiles[mappedKey].motionX = MotionAxis{};
    configuration.profiles[mappedKey].motionY = MotionAxis{};
    manager.configure(configuration);
    check(!manager.output().motionAvailable && manager.output().motionX == 0 &&
              manager.output().motionY == 0,
          "unassigning both axes releases motion");
    configuration.profiles[mappedKey].motionY =
        MotionAxis{MotionAxis::Controller, SDL_CONTROLLER_AXIS_LEFTY};
    manager.configure(configuration);
    mapped.axis(1, -16384);
    manager.backend().pollNow();
    check(manager.output().motionAvailable && manager.output().motionX == 0 &&
              manager.output().motionY == 56,
          "one unassigned axis remains neutral while its peer drives tilt");
    configuration.profiles[mappedKey].motionX = MotionAxis{MotionAxis::Raw, 2};
    configuration.profiles[mappedKey].motionY =
        MotionAxis{MotionAxis::Controller, SDL_CONTROLLER_AXIS_RIGHTY};
    configuration.profiles[mappedKey].bindings["right"] = {{Binding::Axis, 2, 1}};
    configuration.profiles[mappedKey].bindings["left"] = {
        {Binding::ControllerAxis, SDL_CONTROLLER_AXIS_RIGHTX, -1}};
    manager.configure(configuration);
    mapped.axis(2, 32767);
    mapped.axis(3, -32768);
    manager.backend().pollNow();
    check(manager.output().motionX == -112 && manager.output().motionY == 112 &&
              (manager.output().buttons[2] & 3) == 3,
          "custom raw and mapped motion axes suppress their directional aliases");
    manager.setMotionEnabled(false);
    check(!manager.output().motionAvailable && manager.output().motionX == 0 &&
              !(manager.output().buttons[2] & 1),
          "leaving motion mode releases tilt and restores axis directions");
    manager.setMotionEnabled(true);
    {
        Device aliased(0x72);
        check(waitFor([&] {
                  DeviceState state;
                  return manager.backend().state(aliased.id, &state);
              }),
              "mapped alias test device connected");
        char guid[33]{}, mapping[256]{};
        SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(aliased.handle), guid, sizeof(guid));
        SDL_snprintf(mapping, sizeof(mapping), "%s,CupidGB aliased axes,leftx:a0,lefty:a0,", guid);
        check(SDL_GameControllerAddMapping(mapping) >= 0, "install shared physical axis mapping");
        manager.backend().pollNow();
        DeviceState state;
        check(manager.backend().state(aliased.id, &state), "read remapped axis device");
        ControllerProfile aliasProfile;
        aliasProfile.motionX = MotionAxis{MotionAxis::Controller, SDL_CONTROLLER_AXIS_LEFTX};
        aliasProfile.motionY = MotionAxis{MotionAxis::Controller, SDL_CONTROLLER_AXIS_LEFTY};
        check(!ControllerManager::motionAxesValid(aliasProfile, state.info),
              "different normalized selectors cannot share the same physical SDL axis");
    }
    mapped.detach();
    check(waitFor([&] { return !manager.output().motionAvailable; }) &&
              manager.output().motionX == 0 && manager.output().motionY == 0,
          "preferred device disconnect releases motion instead of retaining stale tilt");
    manager.outputChanged = {};
    manager.backend().stop();
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    try {
        Session provider;
        QStringList shortcuts;
        ControllerManager manager;
        manager.shortcutPressed = [&](const QString &id) { shortcuts.append(id); };
        QString error;
        check(manager.backend().start(&error), "start real SDL/Qt backend");
        manager.setFocused(true);
        const int baseline = SDL_NumJoysticks();
        Device first(1), second(2), raw(3, false);
        check(waitFor([&] { return manager.backend().devices().size() == size_t(baseline + 3); }),
              "device hotplug through Qt timer");
        DeviceState a, b, c;
        check(manager.backend().state(first.id, &a) && manager.backend().state(second.id, &b) &&
                  manager.backend().state(raw.id, &c),
              "device snapshots");
        auto configuration = manager.configuration();
        for (const auto &device : manager.backend().devices())
            if (device.info.instance != first.id && device.info.instance != second.id &&
                device.info.instance != raw.id) {
                auto profile = ControllerManager::defaults(device.info);
                profile.enabled = false;
                configuration.profiles[device.info.persistentKey] = profile;
            }
        configuration.profiles[a.info.persistentKey] = ControllerManager::defaults(a.info);
        configuration.profiles[b.info.persistentKey] = ControllerManager::defaults(b.info);
        configuration.profiles[c.info.persistentKey] = ControllerManager::defaults(c.info);
        configuration.profiles[b.info.persistentKey].player = 1;
        configuration.profiles[a.info.persistentKey].bindings["shortcut/mute"] = {
            {Binding::ControllerButton, SDL_CONTROLLER_BUTTON_X, 1}};
        manager.configure(configuration);
        first.button(0, true);
        second.button(1, true);
        raw.hat(SDL_HAT_RIGHTUP);
        check(waitFor([&] {
                  return manager.output().buttons[0] == 0xeau &&
                         manager.output().buttons[1] == 0xdfu;
              }),
              "multiple players and diagonal raw hat");
        first.button(2, true);
        SDL_JoystickUpdate();
        first.button(2, false);
        SDL_JoystickUpdate();
        check(waitFor([&] { return shortcuts == QStringList{"mute"}; }),
              "short controller shortcut edge survives polling");
        first.axis(0, 16384);
        raw.hat(SDL_HAT_CENTERED);
        check(waitFor([&] { return manager.output().buttons[0] == 0xef; }),
              "axis strict threshold");
        first.axis(0, 16385);
        check(waitFor([&] { return !(manager.output().buttons[0] & 1); }), "axis press boundary");
        first.axis(0, 14336);
        manager.backend().pollNow();
        check(!(manager.output().buttons[0] & 1), "axis hysteresis holds at release boundary");
        first.axis(0, 14335);
        check(waitFor([&] { return manager.output().buttons[0] & 1; }), "axis release boundary");
        manager.setFocused(false);
        check(manager.output().buttons == ControllerOutput{}.buttons, "default focus release");
        manager.setFocused(true);
        check(manager.output().buttons[0] == 0xef && manager.output().buttons[1] == 0xdf,
              "focus resynchronizes held gameplay");
        configuration.background = true;
        manager.configure(configuration);
        manager.setFocused(false);
        check(manager.output().buttons[0] == 0xef, "background input opt-in");
        manager.setDeliveryEnabled(false);
        check(manager.output().buttons == ControllerOutput{}.buttons,
              "capture gate overrides background");
        first.button(2, true);
        SDL_JoystickUpdate();
        manager.setDeliveryEnabled(true);
        check(shortcuts.size() == 1, "old blocked press does not trigger shortcut");
        manager.setFocused(true);
        configuration.preferredDevice = a.info.persistentKey;
        manager.configure(configuration);
        manager.setPreferredInstance(first.id);
        check(manager.output().buttons[1] == 0xff, "explicit device selection");
        configuration.preferredDevice.clear();
        manager.configure(configuration);

        const QByteArray persisted = configuration.save();
        bool valid = false;
        const auto loaded = ControllerConfiguration::load(persisted, &valid);
        check(valid && loaded.save() == persisted, "persistent configuration roundtrip");
        ControllerConfiguration::load("{\"version\":99}", &valid);
        check(!valid, "invalid configuration uses defaults");
        auto invalid = QJsonDocument::fromJson(persisted).object();
        auto invalidProfiles = invalid.value("profiles").toObject();
        auto invalidProfile = invalidProfiles.value(a.info.persistentKey).toObject();
        auto invalidBindings = invalidProfile.value("bindings").toObject();
        invalidBindings["a"] =
            QJsonArray{QJsonObject{{"kind", -1}, {"control", 0}, {"direction", 1}}};
        invalidProfile["bindings"] = invalidBindings;
        invalidProfiles[a.info.persistentKey] = invalidProfile;
        invalid["profiles"] = invalidProfiles;
        ControllerConfiguration::load(QJsonDocument(invalid).toJson(), &valid);
        check(!valid, "invalid binding enum rejected before conversion");
        InputState centered;
        centered.axes = {0};
        ControllerProfile zeroRelease;
        zeroRelease.releaseThreshold = 0;
        check(!ControllerManager::bindingDown({Binding::Axis, 0, 1}, centered, zeroRelease, true),
              "zero release threshold still releases a centered axis");
        QTemporaryDir directory;
        check(directory.isValid(), "temporary settings directory");
        QSettings settings(directory.filePath("controllers.ini"), QSettings::IniFormat);
        settings.setValue("controllers/configuration", persisted);
        settings.sync();
        QSettings restored(directory.filePath("controllers.ini"), QSettings::IniFormat);
        check(ControllerConfiguration::load(
                  restored.value("controllers/configuration").toByteArray(), &valid)
                          .save() == persisted &&
                  valid,
              "configuration survives settings file reload");

        manager.setDeliveryEnabled(false);
        {
            auto selectedConfiguration = configuration;
            selectedConfiguration.preferredDevice = a.info.persistentKey;
            manager.configure(selectedConfiguration);
            manager.setPreferredInstance(first.id);
            auto otherPreference = selectedConfiguration;
            otherPreference.preferredDevice = b.info.persistentKey;
            manager.configure(otherPreference);
            manager.setDeliveryEnabled(true);
            check(manager.output().buttons[0] == 0xff && manager.output().buttons[1] == 0xdf,
                  "new preferred key clears old connection override");
            manager.setDeliveryEnabled(false);
            {
                ControllerDialog savedPreference(manager, {});
                check(savedPreference.findChild<QComboBox *>("controllerPreferred")
                              ->currentData()
                              .toInt() == second.id,
                      "initial preferred selection follows saved identity, not connection zero");
                savedPreference.reject();
            }
            manager.configure(selectedConfiguration);
            ControllerDialog dialog(manager, {});
            auto *preferred = dialog.findChild<QComboBox *>("controllerPreferred");
            preferred->setCurrentIndex(0);
            Device extra(0x91, false);
            check(waitFor([&] { return preferred->findData(extra.id) >= 0; }),
                  "preference list refreshes on hotplug");
            check(preferred->currentIndex() == 0 &&
                      dialog.configuration().preferredDevice.isEmpty(),
                  "hotplug preserves explicit all-controller choice");
            preferred->setCurrentIndex(preferred->findData(extra.id));
            DeviceState extraState;
            check(manager.backend().state(extra.id, &extraState), "preferred extra identity");
            extra.detach();
            check(waitFor([&] { return preferred->currentData().toInt() == -2; }) &&
                      dialog.configuration().preferredDevice == extraState.info.persistentKey,
                  "disconnect preserves unsaved preferred identity");
            dialog.reject();
        }
        manager.configure(configuration);
        {
            ControllerDialog dialog(manager, {{"pause", "Pause"},
                                              {"mute", "Mute"},
                                              {"save/0", "Slot 10"},
                                              {"load/0", "Slot 10"}});
            dialog.show();
            manager.setFocused(true);
            auto *devices = dialog.findChild<QComboBox *>("controllerDevice");
            auto *table = dialog.findChild<QTableWidget *>("controllerBindings");
            auto *capture = dialog.findChild<QPushButton *>("controllerCapture");
            auto *press = dialog.findChild<QSpinBox *>("controllerPressThreshold");
            check(devices && table && capture && press, "native controller dialog controls");
            check(table->item(16, 0)->text() == "Save Slot 10" &&
                      table->item(17, 0)->text() == "Load Slot 10",
                  "save and load bindings have distinct command labels");
            check(dialog.findChild<QComboBox *>("controllerPreferred")->currentData().toInt() == -1,
                  "default gameplay selection keeps all controllers enabled");
            devices->setCurrentIndex(devices->findData(first.id));
            const QString captureDirectory = qEnvironmentVariable("CUPIDGB_UI_CAPTURE_DIR");
            if (!captureDirectory.isEmpty()) {
                check(QDir().mkpath(captureDirectory), "controller capture directory");
                QApplication::processEvents();
                check(dialog.grab().save(QDir(captureDirectory).filePath("controllers.png")),
                      "native controller dialog image");
            }
            table->selectRow(4); // A
            capture->click();
            check(dialog.property("controllerCaptureActive").toBool(), "capture begins");
            second.button(3, true);
            manager.backend().pollNow();
            check(dialog.property("controllerCaptureActive").toBool(),
                  "wrong device cannot capture");
            first.button(3, true);
            check(waitFor([&] { return !dialog.property("controllerCaptureActive").toBool(); }),
                  "mapped button capture");
            check(dialog.configuration().profiles[a.info.persistentKey].bindings["a"] ==
                      QList<Binding>{{Binding::ControllerButton, SDL_CONTROLLER_BUTTON_Y, 1}},
                  "captured normalized binding is staged");
            press->setValue(19000);
            dialog.reject();
        }
        check(manager.configuration().save() == persisted,
              "cancel leaves controller settings unchanged");
        {
            ControllerDialog dialog(manager, {});
            dialog.show();
            manager.setFocused(true);
            auto *devices = dialog.findChild<QComboBox *>("controllerDevice");
            devices->setCurrentIndex(devices->findData(first.id));
            dialog.findChild<QSpinBox *>("controllerPressThreshold")->setValue(20000);
            dialog.findChild<QComboBox *>("controllerPlayer")->setCurrentIndex(2);
            dialog.findChild<QDialogButtonBox *>("controllerDialogButtons")
                ->button(QDialogButtonBox::Ok)
                ->click();
            check(dialog.result() == QDialog::Accepted, "controller settings OK");
            manager.configure(dialog.configuration(), dialog.playerOverrides());
            check(manager.profile(a.info).player == 2 &&
                      manager.profile(a.info).pressThreshold == 20000,
                  "accepted player and dead zone apply");
        }
        manager.setDeliveryEnabled(true);
        manager.setFocused(true);
        check(manager.output().buttons[2] == 0xef && manager.output().buttons[0] == 0xff,
              "accepted player routes held input");
        manager.setDeliveryEnabled(false);
        {
            ControllerDialog dialog(manager, {});
            dialog.show();
            manager.setFocused(true);
            auto *devices = dialog.findChild<QComboBox *>("controllerDevice");
            devices->setCurrentIndex(devices->findData(first.id));
            dialog.findChild<QTableWidget *>("controllerBindings")->selectRow(4);
            dialog.findChild<QPushButton *>("controllerCapture")->click();
            first.detach();
            check(waitFor([&] { return devices->findData(first.id) < 0; }),
                  "dialog updates on detach");
            check(!dialog.property("controllerCaptureActive").toBool(), "detach cancels capture");
            dialog.reject();
        }
        manager.setDeliveryEnabled(true);
        check(manager.output().buttons[2] == 0xff, "held detach releases assigned player");
        manager.backend().stop();
        second.detach();
        raw.detach();
        check(SDL_NumJoysticks() == baseline, "all virtual devices removed");
        checkMotionWorkflow();
        std::puts("CupidGB controller workflow checks passed (virtual devices only).");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "Controller workflow failure: %s\n", error.what());
        return 1;
    }
}
