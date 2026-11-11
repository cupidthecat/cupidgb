#include "controller_dialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

using namespace cupid::input;

ControllerDialog::ControllerDialog(ControllerManager &manager,
                                   const QList<QPair<QString, QString>> &shortcuts, QWidget *parent)
    : QDialog(parent), manager_(manager), draft_(manager.configuration()),
      players_(manager.playerOverrides()) {
    for (const auto &device : manager.backend().devices())
        if (!players_.contains(device.info.instance))
            players_[device.info.instance] = manager.profile(device.info).player;
    setObjectName("controllerSettings");
    setWindowTitle("CupidGB Controllers");
    resize(760, 700);
    auto *layout = new QVBoxLayout(this);
    auto *global = new QFormLayout;
    enabled_ = new QCheckBox("Enable controller input");
    enabled_->setObjectName("controllerEnabled");
    enabled_->setChecked(draft_.enabled);
    background_ = new QCheckBox("Allow gameplay input while CupidGB is in the background");
    background_->setObjectName("controllerBackground");
    background_->setChecked(draft_.background);
    rumble_ = new QCheckBox("Cartridge rumble on player 1 controllers");
    rumble_->setObjectName("controllerRumble");
    rumble_->setChecked(draft_.rumble);
    strength_ = new QSlider(Qt::Horizontal);
    strength_->setObjectName("controllerStrength");
    strength_->setRange(0, 100);
    strength_->setValue(draft_.rumbleStrength);
    preferred_ = new QComboBox;
    preferred_->setObjectName("controllerPreferred");
    global->addRow(enabled_);
    global->addRow(background_);
    global->addRow("Gameplay devices", preferred_);
    global->addRow(rumble_);
    global->addRow("Rumble strength", strength_);
    layout->addLayout(global);
    auto *form = new QFormLayout;
    devices_ = new QComboBox;
    devices_->setObjectName("controllerDevice");
    identity_ = new QLabel;
    identity_->setWordWrap(true);
    deviceEnabled_ = new QCheckBox("Use this device profile");
    deviceEnabled_->setObjectName("controllerProfileEnabled");
    player_ = new QComboBox;
    player_->setObjectName("controllerPlayer");
    for (int i = 1; i <= 4; ++i)
        player_->addItem(QString("Player %1").arg(i), i - 1);
    player_->setToolTip(
        "Players 2–4 require a Super Game Boy cartridge that requests multiplayer.");
    press_ = new QSpinBox;
    press_->setObjectName("controllerPressThreshold");
    press_->setRange(1, 32766);
    release_ = new QSpinBox;
    release_->setObjectName("controllerReleaseThreshold");
    release_->setRange(0, 32766);
    form->addRow("Configure device", devices_);
    form->addRow(identity_);
    form->addRow(deviceEnabled_);
    form->addRow("Player", player_);
    form->addRow("Axis press threshold", press_);
    form->addRow("Axis release threshold", release_);
    layout->addLayout(form);
    bindings_ = new QTableWidget;
    bindings_->setObjectName("controllerBindings");
    bindings_->setColumnCount(2);
    bindings_->setHorizontalHeaderLabels({"Action", "Binding"});
    bindings_->setSelectionBehavior(QAbstractItemView::SelectRows);
    bindings_->setSelectionMode(QAbstractItemView::SingleSelection);
    bindings_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    bindings_->horizontalHeader()->setStretchLastSection(true);
    actions_ = ControllerManager::gameplayActions();
    bindings_->setRowCount(actions_.size() + shortcuts.size());
    int row = 0;
    for (const auto &action : actions_)
        bindings_->setItem(row++, 0, new QTableWidgetItem(ControllerManager::actionName(action)));
    for (const auto &shortcut : shortcuts) {
        actions_.append("shortcut/" + shortcut.first);
        QString label = shortcut.second;
        label.remove('&');
        if (shortcut.first.startsWith("save/"))
            label.prepend("Save ");
        if (shortcut.first.startsWith("load/"))
            label.prepend("Load ");
        bindings_->setItem(row++, 0, new QTableWidgetItem(label));
    }
    layout->addWidget(bindings_, 1);
    auto *tools = new QHBoxLayout;
    const auto button = [&](const QString &label, const char *name) {
        auto *result = new QPushButton(label);
        result->setObjectName(name);
        tools->addWidget(result);
        return result;
    };
    auto *captureButton = button("Capture", "controllerCapture");
    auto *skip = button("Skip / cancel capture", "controllerSkip");
    auto *clear = button("Clear", "controllerClear");
    auto *defaults = button("Restore layout defaults", "controllerDefaults");
    auto *test = button("Test rumble", "controllerTestRumble");
    layout->addLayout(tools);
    preview_ = new QLabel;
    preview_->setObjectName("controllerPreview");
    preview_->setWordWrap(true);
    status_ = new QLabel;
    status_->setObjectName("controllerStatus");
    status_->setWordWrap(true);
    layout->addWidget(preview_);
    layout->addWidget(status_);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->setObjectName("controllerDialogButtons");
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        saveDevice();
        if (release_->value() > press_->value()) {
            status_->setText("The release threshold must not exceed the press threshold.");
            return;
        }
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(devices_, &QComboBox::currentIndexChanged, this, [this] { selectDevice(); });
    connect(captureButton, &QPushButton::clicked, this, [this] { beginCapture(); });
    connect(skip, &QPushButton::clicked, this, [this] {
        cancelCapture();
        if (bindings_->currentRow() + 1 < bindings_->rowCount())
            bindings_->selectRow(bindings_->currentRow() + 1);
    });
    connect(clear, &QPushButton::clicked, this, [this] {
        cancelCapture();
        const int row = bindings_->currentRow();
        if (!currentKey_.isEmpty() && row >= 0) {
            saveDevice();
            draft_.profiles[currentKey_].bindings[actions_[row]].clear();
            refreshBindings();
        }
    });
    connect(defaults, &QPushButton::clicked, this, [this] {
        DeviceState state;
        if (manager_.backend().state(current_, &state)) {
            cancelCapture();
            const int player = player_->currentIndex();
            draft_.profiles[currentKey_] = ControllerManager::defaults(state.info);
            draft_.profiles[currentKey_].player = player;
            press_->setValue(16384);
            release_->setValue(14336);
            deviceEnabled_->setChecked(true);
            refreshBindings();
        }
    });
    connect(test, &QPushButton::clicked, this, [this] {
        manager_.backend().stopRumble(current_);
        if (!manager_.backend().rumble(current_, uint16_t(65535 * strength_->value() / 100),
                                       uint16_t(65535 * strength_->value() / 100), 150, true))
            status_->setText(
                "Rumble is unavailable, disabled by focus, or unsupported by this device.");
        else
            status_->setText(
                "Sent a 150 ms rumble test. Physical feedback needs device verification.");
    });
    // Capture still receives physical events while gameplay delivery is blocked by the host.
    manager_.captureEvent = [this](const InputEvent &event) { capture(event); };
    manager_.devicesChanged = [this] { refreshDevices(); };
    previewTimer_.setInterval(100);
    connect(&previewTimer_, &QTimer::timeout, this, [this] { refreshPreview(); });
    previewTimer_.start();
    refreshDevices();
    if (bindings_->rowCount())
        bindings_->selectRow(0);
}

ControllerDialog::~ControllerDialog() {
    manager_.captureEvent = {};
    manager_.devicesChanged = {};
    manager_.stopRumble();
}

void ControllerDialog::saveDevice() {
    if (selecting_ || currentKey_.isEmpty())
        return;
    auto &profile = draft_.profiles[currentKey_];
    profile.enabled = deviceEnabled_->isChecked();
    profile.player = player_->currentData().toInt();
    profile.pressThreshold = press_->value();
    profile.releaseThreshold = std::min(release_->value(), press_->value());
    players_[current_] = profile.player;
}

void ControllerDialog::refreshDevices() {
    saveDevice();
    cancelCapture();
    if (preferredInitialized_)
        draft_.preferredDevice = preferred_->currentData(Qt::UserRole + 1).toString();
    selecting_ = true;
    const int previousPreferred = preferredInitialized_ ? preferred_->currentData().toInt() : -1;
    devices_->clear();
    preferred_->clear();
    preferred_->addItem("All enabled controllers", -1);
    for (const auto &device : manager_.backend().devices()) {
        const QString label = QString("%1 [%2]").arg(device.info.name).arg(device.info.instance);
        devices_->addItem(label, device.info.instance);
        preferred_->addItem(label, device.info.instance);
        preferred_->setItemData(preferred_->count() - 1, device.info.persistentKey,
                                Qt::UserRole + 1);
    }
    const int oldIndex = devices_->findData(current_);
    devices_->setCurrentIndex(oldIndex >= 0 ? oldIndex : devices_->count() ? 0 : -1);
    int preferredIndex = preferred_->findData(previousPreferred);
    if (preferredIndex < 0 || previousPreferred < 0) {
        preferredIndex = 0;
        for (int i = 1; i < preferred_->count(); ++i) {
            DeviceState state;
            if (manager_.backend().state(preferred_->itemData(i).toInt(), &state) &&
                state.info.persistentKey == draft_.preferredDevice) {
                preferredIndex = i;
                break;
            }
        }
    }
    preferred_->setCurrentIndex(preferredIndex);
    if (preferredIndex == 0 && !draft_.preferredDevice.isEmpty()) {
        preferred_->addItem("Preferred device (disconnected)", -2);
        preferred_->setItemData(preferred_->count() - 1, draft_.preferredDevice, Qt::UserRole + 1);
        preferred_->setCurrentIndex(preferred_->count() - 1);
    }
    preferredInitialized_ = true;
    selecting_ = false;
    currentKey_.clear();
    selectDevice();
    if (!devices_->count())
        status_->setText("No controllers connected. Connect a device to configure its layout.");
}

void ControllerDialog::selectDevice() {
    if (selecting_)
        return;
    saveDevice();
    cancelCapture();
    current_ = devices_->currentData().toInt();
    DeviceState state;
    if (!manager_.backend().state(current_, &state)) {
        current_ = -1;
        currentKey_.clear();
        identity_->setText("No device selected.");
        bindings_->setEnabled(false);
        return;
    }
    bindings_->setEnabled(true);
    currentKey_ = state.info.persistentKey;
    if (!draft_.profiles.contains(currentKey_))
        draft_.profiles[currentKey_] = manager_.profile(state.info);
    const auto &profile = draft_.profiles[currentKey_];
    selecting_ = true;
    deviceEnabled_->setChecked(profile.enabled);
    player_->setCurrentIndex(players_.value(current_, profile.player));
    press_->setValue(profile.pressThreshold);
    release_->setValue(profile.releaseThreshold);
    selecting_ = false;
    identity_->setText(
        QString("Axes: %1; buttons: %2; hats: %3. %4\n%5")
            .arg(state.info.axes)
            .arg(state.info.buttons)
            .arg(state.info.hats)
            .arg(state.info.mapped ? "Standard controller mapping available."
                                   : "Raw joystick; verify the layout before playing.")
            .arg(state.info.identity == DeviceInfo::Identity::DeviceClass
                     ? "Identical devices share a saved layout; player overrides last for this "
                       "connection."
                     : "Layout and default player are saved for this device identity."));
    refreshBindings();
    refreshPreview();
}

void ControllerDialog::refreshBindings() {
    for (int row = 0; row < actions_.size(); ++row) {
        QStringList names;
        for (const auto &binding : draft_.profiles.value(currentKey_).bindings.value(actions_[row]))
            names.append(ControllerManager::bindingName(binding));
        bindings_->setItem(
            row, 1, new QTableWidgetItem(names.isEmpty() ? "Unassigned" : names.join(" or ")));
    }
}

void ControllerDialog::beginCapture() {
    if (current_ < 0 || bindings_->currentRow() < 0)
        return;
    saveDevice();
    captureAction_ = actions_[bindings_->currentRow()];
    setProperty("controllerCaptureActive", true);
    status_->setText("Press a button, move one axis beyond the press threshold, or move a hat in "
                     "one direction. Skip cancels capture.");
}

void ControllerDialog::cancelCapture() {
    captureAction_.clear();
    setProperty("controllerCaptureActive", false);
}

void ControllerDialog::capture(const InputEvent &event) {
    if (captureAction_.isEmpty() || event.instance != current_)
        return;
    DeviceState state;
    if (!manager_.backend().state(current_, &state))
        return;
    // Prefer normalized controls when available; raw joystick fallback remains configurable.
    if (state.info.mapped &&
        (event.kind == InputEvent::Kind::Button || event.kind == InputEvent::Kind::Axis))
        return;
    Binding binding;
    if (!ControllerManager::bindingFromEvent(event, press_->value(), &binding))
        return;
    draft_.profiles[currentKey_].bindings[captureAction_] = {binding};
    setProperty("controllerCaptureConsumed", true);
    QTimer::singleShot(0, this, [this] { setProperty("controllerCaptureConsumed", false); });
    status_->setText("Captured " + ControllerManager::bindingName(binding) +
                     ". Release the control before capturing another action.");
    cancelCapture();
    refreshBindings();
    if (bindings_->currentRow() + 1 < bindings_->rowCount())
        bindings_->selectRow(bindings_->currentRow() + 1);
}

void ControllerDialog::refreshPreview() {
    DeviceState state;
    if (!manager_.backend().state(current_, &state)) {
        preview_->clear();
        return;
    }
    QStringList axes, buttons, hats;
    for (size_t i = 0; i < state.physical.axes.size(); ++i)
        axes << QString("%1:%2").arg(i).arg(state.physical.axes[i]);
    for (size_t i = 0; i < state.physical.buttons.size(); ++i)
        if (state.physical.buttons[i])
            buttons << QString::number(i);
    for (size_t i = 0; i < state.physical.hats.size(); ++i)
        hats << QString("%1:%2").arg(i).arg(state.physical.hats[i], 0, 16);
    preview_->setText("Axes " + axes.join(", ") + "\nHeld buttons " + buttons.join(", ") +
                      " | Hats " + hats.join(", "));
}

ControllerConfiguration ControllerDialog::configuration() {
    saveDevice();
    draft_.enabled = enabled_->isChecked();
    draft_.background = background_->isChecked();
    draft_.rumble = rumble_->isChecked();
    draft_.rumbleStrength = strength_->value();
    draft_.preferredDevice = preferred_->currentData(Qt::UserRole + 1).toString();
    return draft_;
}

QMap<SDL_JoystickID, int> ControllerDialog::playerOverrides() {
    saveDevice();
    return players_;
}

SDL_JoystickID ControllerDialog::preferredInstance() const {
    return preferred_->currentData().toInt();
}
