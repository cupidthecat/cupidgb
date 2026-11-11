#include "controller_dialog.h"
#include "controller_test_device.h"

#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>

using namespace cupid::input;
using namespace controller_test;

namespace {
template <typename T> T *control(ControllerDialog &dialog, const char *name) {
    auto *result = dialog.findChild<T *>(name);
    check(result != nullptr, name);
    return result;
}

void chooseDevice(ControllerDialog &dialog, SDL_JoystickID instance) {
    auto *devices = control<QComboBox>(dialog, "controllerDevice");
    const int index = devices->findData(instance);
    check(index >= 0, "dialog device available");
    devices->setCurrentIndex(index);
}

void chooseAxis(ControllerDialog &dialog, const char *name, MotionAxis::Kind kind, int axis = 0) {
    auto *combo = control<QComboBox>(dialog, name);
    for (int index = 0; index < combo->count(); ++index) {
        if (combo->itemData(index).toInt() == int(kind) &&
            combo->itemData(index, Qt::UserRole + 1).toInt() == axis) {
            combo->setCurrentIndex(index);
            return;
        }
    }
    check(false, "requested motion axis offered");
}

void expectAxis(ControllerDialog &dialog, const char *name, MotionAxis::Kind kind, int axis) {
    auto *combo = control<QComboBox>(dialog, name);
    check(combo->currentData().toInt() == int(kind) &&
              combo->currentData(Qt::UserRole + 1).toInt() == axis,
          "dialog shows saved motion axis");
}

void click(ControllerDialog &dialog, QDialogButtonBox::StandardButton button) {
    auto *buttons = control<QDialogButtonBox>(dialog, "controllerDialogButtons");
    buttons->button(button)->click();
}

void expectUnchanged(const ControllerManager &manager, const QByteArray &configuration) {
    check(manager.configuration().save() == configuration,
          "dialog leaves live configuration unchanged until caller applies acceptance");
}
} // namespace

int main(int argc, char **argv) {
    QApplication application(argc, argv);
    try {
        Session provider;
        ControllerManager manager;
        QString error;
        check(manager.backend().start(&error), "start dialog controller backend");
        manager.setFocused(true);
        const int baseline = SDL_NumJoysticks();
        Device mapped(0x41), raw(0x42, false);
        check(waitFor([&] { return manager.backend().devices().size() == size_t(baseline + 2); }),
              "dialog test devices connected");
        DeviceState mappedState, rawState;
        check(manager.backend().state(mapped.id, &mappedState) &&
                  manager.backend().state(raw.id, &rawState),
              "dialog test device identities");
        const QString mappedKey = mappedState.info.persistentKey;
        const QString rawKey = rawState.info.persistentKey;
        auto initial = manager.configuration();
        for (const auto &device : manager.backend().devices()) {
            auto profile = ControllerManager::defaults(device.info);
            profile.enabled = device.info.instance == mapped.id || device.info.instance == raw.id;
            initial.profiles[device.info.persistentKey] = profile;
        }
        // Version-one layouts did not store motion selections. Opening one
        // must display the device defaults without changing the live settings.
        for (const QString &key : {mappedKey, rawKey}) {
            initial.profiles[key].motionX.reset();
            initial.profiles[key].motionY.reset();
        }
        manager.configure(initial);
        const QByteArray legacy = manager.configuration().save();
        {
            ControllerDialog dialog(manager, {});
            dialog.show();
            chooseDevice(dialog, mapped.id);
            expectAxis(dialog, "controllerMotionX", MotionAxis::Controller,
                       SDL_CONTROLLER_AXIS_LEFTX);
            expectAxis(dialog, "controllerMotionY", MotionAxis::Controller,
                       SDL_CONTROLLER_AXIS_LEFTY);
            chooseDevice(dialog, raw.id);
            expectAxis(dialog, "controllerMotionX", MotionAxis::Raw, 0);
            expectAxis(dialog, "controllerMotionY", MotionAxis::Raw, 1);
            chooseAxis(dialog, "controllerMotionX", MotionAxis::Raw, 4);
            chooseAxis(dialog, "controllerMotionY", MotionAxis::Raw, 5);
            click(dialog, QDialogButtonBox::Cancel);
            check(dialog.result() == QDialog::Rejected, "cancel motion configuration");
        }
        expectUnchanged(manager, legacy);

        ControllerConfiguration accepted;
        {
            ControllerDialog dialog(manager, {});
            dialog.show();
            chooseDevice(dialog, raw.id);
            chooseAxis(dialog, "controllerMotionX", MotionAxis::Raw, 4);
            chooseAxis(dialog, "controllerMotionY", MotionAxis::Raw, 5);
            chooseDevice(dialog, mapped.id);
            chooseAxis(dialog, "controllerMotionX", MotionAxis::Controller,
                       SDL_CONTROLLER_AXIS_RIGHTX);
            chooseAxis(dialog, "controllerMotionY", MotionAxis::Controller,
                       SDL_CONTROLLER_AXIS_RIGHTY);
            click(dialog, QDialogButtonBox::Ok);
            check(dialog.result() == QDialog::Accepted, "accept distinct motion axes");
            accepted = dialog.configuration();
            for (const QString &key : {mappedKey, rawKey}) {
                const auto before = initial.profiles.value(key);
                const auto after = accepted.profiles.value(key);
                check(before.bindings == after.bindings &&
                          before.pressThreshold == after.pressThreshold &&
                          before.releaseThreshold == after.releaseThreshold,
                      "motion changes preserve gameplay bindings and hysteresis");
            }
        }
        expectUnchanged(manager, legacy);
        QTemporaryDir directory;
        check(directory.isValid(), "motion settings directory");
        const QString settingsPath = directory.filePath("controllers.ini");
        {
            QSettings settings(settingsPath, QSettings::IniFormat);
            settings.setValue("controllers/configuration", accepted.save());
            settings.sync();
            check(settings.status() == QSettings::NoError, "persist motion configuration");
        }
        QSettings settings(settingsPath, QSettings::IniFormat);
        bool valid = false;
        const auto restored = ControllerConfiguration::load(
            settings.value("controllers/configuration").toByteArray(), &valid);
        check(valid, "load persisted motion configuration");
        manager.configure(restored);
        const QByteArray persisted = manager.configuration().save();
        {
            ControllerDialog dialog(manager, {});
            dialog.show();
            chooseDevice(dialog, raw.id);
            expectAxis(dialog, "controllerMotionX", MotionAxis::Raw, 4);
            expectAxis(dialog, "controllerMotionY", MotionAxis::Raw, 5);
            chooseDevice(dialog, mapped.id);
            expectAxis(dialog, "controllerMotionX", MotionAxis::Controller,
                       SDL_CONTROLLER_AXIS_RIGHTX);
            expectAxis(dialog, "controllerMotionY", MotionAxis::Controller,
                       SDL_CONTROLLER_AXIS_RIGHTY);
            click(dialog, QDialogButtonBox::Cancel);
        }
        expectUnchanged(manager, persisted);
        {
            ControllerDialog dialog(manager, {});
            dialog.show();
            chooseDevice(dialog, raw.id);
            control<QPushButton>(dialog, "controllerDefaults")->click();
            expectAxis(dialog, "controllerMotionX", MotionAxis::Raw, 0);
            expectAxis(dialog, "controllerMotionY", MotionAxis::Raw, 1);
            chooseDevice(dialog, mapped.id);
            control<QPushButton>(dialog, "controllerDefaults")->click();
            expectAxis(dialog, "controllerMotionX", MotionAxis::Controller,
                       SDL_CONTROLLER_AXIS_LEFTX);
            expectAxis(dialog, "controllerMotionY", MotionAxis::Controller,
                       SDL_CONTROLLER_AXIS_LEFTY);
            click(dialog, QDialogButtonBox::Cancel);
        }
        expectUnchanged(manager, persisted);

        {
            ControllerDialog dialog(manager, {});
            dialog.show();
            chooseDevice(dialog, raw.id);
            chooseAxis(dialog, "controllerMotionX", MotionAxis::Raw, 2);
            chooseAxis(dialog, "controllerMotionY", MotionAxis::Raw, 2);
            chooseDevice(dialog, mapped.id);
            click(dialog, QDialogButtonBox::Ok);
            check(dialog.isVisible() && dialog.result() != QDialog::Accepted,
                  "duplicate axes on another draft profile prevent acceptance");
            check(control<QLabel>(dialog, "controllerStatus")->text().contains(rawState.info.name),
                  "invalid motion profile identifies its device");
            expectUnchanged(manager, persisted);
            chooseDevice(dialog, raw.id);
            chooseAxis(dialog, "controllerMotionY", MotionAxis::Raw, 3);
            click(dialog, QDialogButtonBox::Ok);
            check(dialog.result() == QDialog::Accepted, "corrected motion profile accepts");
        }
        expectUnchanged(manager, persisted);
        {
            ControllerDialog dialog(manager, {});
            dialog.show();
            chooseDevice(dialog, raw.id);
            auto *combo = control<QComboBox>(dialog, "controllerMotionX");
            combo->addItem("Saved axis no longer present", int(MotionAxis::Raw));
            combo->setItemData(combo->count() - 1, 200, Qt::UserRole + 1);
            combo->setCurrentIndex(combo->count() - 1);
            click(dialog, QDialogButtonBox::Ok);
            check(dialog.isVisible() && dialog.result() != QDialog::Accepted,
                  "unavailable motion source prevents acceptance");
            click(dialog, QDialogButtonBox::Cancel);
        }
        expectUnchanged(manager, persisted);
        {
            ControllerDialog dialog(manager, {});
            dialog.show();
            chooseDevice(dialog, mapped.id);
            chooseAxis(dialog, "controllerMotionX", MotionAxis::Controller,
                       SDL_CONTROLLER_AXIS_LEFTX);
            chooseAxis(dialog, "controllerMotionY", MotionAxis::Raw, 0);
            click(dialog, QDialogButtonBox::Ok);
            check(dialog.isVisible() && dialog.result() != QDialog::Accepted,
                  "raw and mapped aliases of the same physical axis are rejected");
            chooseAxis(dialog, "controllerMotionY", MotionAxis::Unassigned);
            click(dialog, QDialogButtonBox::Ok);
            check(dialog.result() == QDialog::Accepted, "an unassigned motion direction accepts");
            const auto profile = dialog.configuration().profiles.value(mappedKey);
            check(profile.motionY && profile.motionY->kind == MotionAxis::Unassigned,
                  "unassigned selection remains explicit in the accepted profile");
        }
        expectUnchanged(manager, persisted);
        {
            ControllerDialog dialog(manager, {});
            dialog.show();
            chooseDevice(dialog, raw.id);
            chooseAxis(dialog, "controllerMotionX", MotionAxis::Raw, 2);
            chooseAxis(dialog, "controllerMotionY", MotionAxis::Raw, 2);
            raw.detach();
            auto *devices = control<QComboBox>(dialog, "controllerDevice");
            check(waitFor([&] { return devices->findData(raw.id) < 0; }),
                  "motion dialog notices device removal");
            click(dialog, QDialogButtonBox::Ok);
            check(dialog.isVisible() && dialog.result() != QDialog::Accepted,
                  "disconnect cannot conceal an invalid draft motion profile");
            click(dialog, QDialogButtonBox::Cancel);
        }
        expectUnchanged(manager, persisted);
        manager.backend().stop();
        mapped.detach();
        check(SDL_NumJoysticks() == baseline, "motion dialog virtual devices removed");
        std::puts("CupidGB motion-axis dialog checks passed (virtual devices only).");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "Motion-axis dialog failure: %s\n", error.what());
        return 1;
    }
}
