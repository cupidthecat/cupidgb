#include "cheats_window.h"
#include "main_window_internal.h"

namespace {
QByteArray encodedCheats(const CupidGbCheats *cheats) {
    const size_t size = cupid_gb_cheats_encoded_size(cheats);
    if (!size || size > 8 * 1024 * 1024)
        return {};
    QByteArray bytes(qsizetype(size), char(0));
    if (cupid_gb_cheats_encode(cheats, reinterpret_cast<uint8_t *>(bytes.data()), size, nullptr) !=
        CUPID_GB_CHEAT_OK)
        return {};
    return bytes;
}
} // namespace

void MainWindow::refreshCheats(bool newSession) {
    if (emulator_)
        cupid_gb_set_cheats(&emulator_->gb, cupid_gb_cheats_enabled(cheats_.get()) &&
                                                    cupid_gb_cheats_count(cheats_.get())
                                                ? cheats_.get()
                                                : nullptr);
    if (enableCheatsAction_) {
        const QSignalBlocker block(enableCheatsAction_);
        enableCheatsAction_->setChecked(cupid_gb_cheats_enabled(cheats_.get()));
        for (auto *action : {enableCheatsAction_, cheatsAction_, cheatSearchAction_})
            action->setEnabled(bool(emulator_));
    }
    if (cheatsWindow_) {
        if (newSession)
            cheatsWindow_->sessionChanged();
        else
            cheatsWindow_->refresh();
        cheatsWindow_->setSaveWarning(cheatSaveWarning_);
    }
}

bool MainWindow::cheatDestinationProtected(const QString &path, const QString &rom,
                                           const QString &battery) const {
    const QFileInfo output(path);
    QString destination = output.canonicalFilePath();
    if (destination.isEmpty())
        destination = output.isSymLink() ? output.symLinkTarget() : output.absoluteFilePath();
#ifdef _WIN32
    constexpr auto pathCase = Qt::CaseInsensitive;
#else
    constexpr auto pathCase = Qt::CaseSensitive;
#endif
    QStringList protectedPaths{rom, battery};
    const QFileInfo save(battery);
    for (int slot = 0; slot < 10; ++slot) {
        protectedPaths.append(
            save.dir().filePath(save.completeBaseName() + QString(".s%1").arg(slot)));
        protectedPaths.append(
            save.dir().filePath(save.completeBaseName() + QString(".sn%1").arg(slot)));
    }
    for (const QString &protectedPath : protectedPaths) {
        const QFileInfo protectedInfo(protectedPath);
        const QString normalized = protectedInfo.canonicalFilePath();
        if (destination.compare(normalized.isEmpty() ? protectedInfo.absoluteFilePath()
                                                     : normalized,
                                pathCase) == 0)
            return true;
    }
    return false;
}

bool MainWindow::exportCheatFile(const QString &path) {
    if (!cheats_)
        return false;
    if (cheatDestinationProtected(path, romPath_, savePath_)) {
        reportError("The cheat export path points to a cartridge, battery, or state file.");
        return false;
    }
    const QByteArray bytes = encodedCheats(cheats_.get());
    QSaveFile file(path);
    if (bytes.isEmpty() || !file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() ||
        !file.commit()) {
        reportError("Could not save the cheat file: " + file.errorString());
        return false;
    }
    return true;
}

bool MainWindow::mutateCheats(const std::function<bool(CupidGbCheats *)> &operation) {
    if (!emulator_ || !cheats_)
        return false;
    QScopedValueRollback<bool> boundary(stateOperation_, true);
    const QByteArray before = encodedCheats(cheats_.get());
    CheatCollection candidate(cupid_gb_cheats_clone(cheats_.get()), cupid_gb_cheats_destroy);
    if (!candidate) {
        reportError("Could not prepare the cheat collection.");
        return false;
    }
    cupid_gb_cheats_set_enabled(candidate.get(), cupid_gb_cheats_enabled(cheats_.get()));
    if (!operation(candidate.get()))
        return false;
    const QByteArray updated = encodedCheats(candidate.get());
    if (updated.isEmpty()) {
        reportError("Could not encode the updated cheat collection.");
        return false;
    }
    // Persist real edits atomically. A read-only library can still use active cheats.
    const bool protectedFile = cheatDestinationProtected(cheatPath_, romPath_, savePath_);
    if (updated != before) {
        QSaveFile file(cheatPath_);
        if (protectedFile || !file.open(QIODevice::WriteOnly) ||
            file.write(updated) != updated.size() || !file.commit()) {
            cheatSaveWarning_ = "Cheats are active in memory. Automatic saving failed. "
                                "Use Export File to keep these edits.";
            qWarning("%s", qPrintable(cheatSaveWarning_));
            statusBar()->showMessage(cheatSaveWarning_);
        } else {
            cheatSaveWarning_.clear();
        }
    }
    cupid_gb_set_cheats(&emulator_->gb, nullptr);
    cheats_ = std::move(candidate);
    refreshCheats();
    return true;
}

bool MainWindow::loadCheatFile(const QString &path, bool replace) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 8 * 1024 * 1024) {
        reportError("Could not open the cheat file.");
        return false;
    }
    const QByteArray bytes = file.readAll();
    if (bytes.size() != file.size()) {
        reportError("Could not read the complete cheat file.");
        return false;
    }
    const bool result = mutateCheats([&](CupidGbCheats *collection) {
        return cupid_gb_cheats_decode(collection,
                                      reinterpret_cast<const uint8_t *>(bytes.constData()),
                                      size_t(bytes.size()), replace) == CUPID_GB_CHEAT_OK;
    });
    if (!result)
        reportError("The cheat file could not be imported.");
    return result;
}

bool MainWindow::writeSearchMemory(const std::function<bool(CupidGb &)> &operation) {
    if (!emulator_)
        return false;
    QScopedValueRollback<bool> boundary(stateOperation_, true);
    pauseDebugger();
    if (!operation(emulator_->gb))
        return false;
    ++timelineId_;
    resetDebuggerTimeline();
    resetRewind();
    rememberFrame();
    watchBattery();
    if (inspectorsWindow_)
        inspectorsWindow_->sessionChanged();
    return true;
}

void MainWindow::openCheats(bool search) {
    if (!emulator_)
        return;
    if (!cheatsWindow_) {
        cheatsWindow_ = new CheatsWindow(this);
        cheatsWindow_->setSession(
            [this] { return cheats_.get(); },
            [this] { return emulator_ ? &emulator_->gb : nullptr; },
            [this](const auto &operation) { return mutateCheats(operation); },
            [this](const auto &operation) { return writeSearchMemory(operation); });
        cheatsWindow_->importFileRequested = [this] {
            const bool wasPaused = paused_;
            setPaused(true);
            const QString path = QFileDialog::getOpenFileName(cheatsWindow_, "Import Cheats",
                                                              QFileInfo(cheatPath_).absolutePath(),
                                                              "Cheat files (*.cht);;All files (*)");
            if (!path.isEmpty())
                loadCheatFile(path);
            setPaused(wasPaused);
        };
        cheatsWindow_->exportFileRequested = [this] {
            const bool wasPaused = paused_;
            setPaused(true);
            const QString path = QFileDialog::getSaveFileName(cheatsWindow_, "Export Cheats",
                                                              cheatPath_, "Cheat files (*.cht)");
            if (!path.isEmpty())
                exportCheatFile(path);
            setPaused(wasPaused);
        };
    }
    cheatsWindow_->refresh();
    cheatsWindow_->selectSearch(search);
    cheatsWindow_->show();
    cheatsWindow_->raise();
    cheatsWindow_->activateWindow();
}
