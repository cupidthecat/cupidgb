#include "cheats_window.h"
#include "main_window_internal.h"

#include <QTableView>
#include <QTableWidget>

bool MainWindow::cheatSmokeTest(const QString &path, const QString &directory) {
    QFile original(path);
    if (!original.open(QIODevice::ReadOnly))
        return smokeFailure(__FILE__, __LINE__);
    const QString rom = QDir(directory).filePath("cheat-workflow.gb");
    QFile fixture(rom);
    QByteArray data = original.readAll();
    if (data.size() < 0x150)
        return smokeFailure(__FILE__, __LINE__);
    data[0x143] = char(0x80);
    uint8_t checksum = 0;
    for (int i = 0x134; i <= 0x14c; ++i)
        checksum = uint8_t(checksum - uint8_t(data[i]) - 1);
    data[0x14d] = char(checksum);
    if (!fixture.open(QIODevice::WriteOnly) || fixture.write(data) != data.size())
        return smokeFailure(__FILE__, __LINE__);
    fixture.close();
    MainWindow session(true, true, false, QStringLiteral(""), CUPID_GB_MODEL_CGB);
    if (!session.openRom(rom) || !session.cheats_ || session.emulator_->gb.cheats)
        return smokeFailure(__FILE__, __LINE__);
    session.setPaused(true);
    auto &gb = session.emulator_->gb;
    gb.work_ram[0] = 0x17;
    const QByteArray saved = session.captureState();
    if (!session.mutateCheats([](CupidGbCheats *collection) {
            return cupid_gb_cheats_import(collection, "014200C0", "Test code", true) ==
                   CUPID_GB_CHEAT_OK;
        }) ||
        cupid_gb_read_u8(&gb, 0xc000) != 0x17)
        return smokeFailure(__FILE__, __LINE__);
    session.enableCheatsAction_->trigger();
    if (cupid_gb_read_u8(&gb, 0xc000) != 0x42 || gb.work_ram[0] != 0x17 ||
        session.captureState() != saved || !session.restoreState(saved, true) ||
        cupid_gb_read_u8(&gb, 0xc000) != 0x42)
        return smokeFailure(__FILE__, __LINE__);
    session.openCheats();
    auto *table = session.cheatsWindow_->findChild<QTableWidget *>("cheatsTable");
    auto *edit = session.cheatsWindow_->findChild<QPushButton *>("editCheat");
    if (!table || !edit || table->rowCount() != 1)
        return smokeFailure(__FILE__, __LINE__);
    table->selectRow(0);
    table->item(0, 0)->setCheckState(Qt::Unchecked);
    if (cupid_gb_read_u8(&gb, 0xc000) != 0x17)
        return smokeFailure(__FILE__, __LINE__);
    bool edited = false;
    QTimer::singleShot(0, &session, [&] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dialog)
            return;
        auto *value = dialog->findChild<QLineEdit *>("cheatValue");
        auto *buttons = dialog->findChild<QDialogButtonBox *>();
        if (!value || !buttons) {
            dialog->reject();
            return;
        }
        value->setText("6A");
        edited = true;
        buttons->button(QDialogButtonBox::Ok)->click();
    });
    edit->click();
    if (!edited || cupid_gb_cheats_get(session.cheats_.get(), 0)->value != 0x6a)
        return smokeFailure(__FILE__, __LINE__);
    table->item(0, 0)->setCheckState(Qt::Checked);
    if (cupid_gb_read_u8(&gb, 0xc000) != 0x6a)
        return smokeFailure(__FILE__, __LINE__);
    const QString exported = QDir(directory).filePath("exported.cht");
    if (!session.exportCheatFile(exported) || !session.loadCheatFile(exported) ||
        cupid_gb_cheats_count(session.cheats_.get()) != 2 ||
        !session.loadCheatFile(exported, true) || cupid_gb_cheats_count(session.cheats_.get()) != 1)
        return smokeFailure(__FILE__, __LINE__);
    const auto *collection = session.cheats_.get();
    QFile invalid(QDir(directory).filePath("invalid.cht"));
    if (!invalid.open(QIODevice::WriteOnly) || invalid.write("invalid") != 7)
        return smokeFailure(__FILE__, __LINE__);
    invalid.close();
    if (session.loadCheatFile(invalid.fileName()) || session.cheats_.get() != collection ||
        session.exportCheatFile(rom))
        return smokeFailure(__FILE__, __LINE__);
    session.failed_ = false;
    // A failed automatic write keeps the user's rules usable for manual export.
    const QString backup = session.cheatPath_ + ".backup";
    if (!QFile::rename(session.cheatPath_, backup) || !QDir().mkdir(session.cheatPath_))
        return smokeFailure(__FILE__, __LINE__);
    session.enableCheatsAction_->trigger();
    if (cupid_gb_cheats_enabled(session.cheats_.get()) || session.failed_)
        return smokeFailure(__FILE__, __LINE__);
    session.enableCheatsAction_->trigger();
    if (!session.mutateCheats([](CupidGbCheats *cheats) {
            CupidGbCheat rule = *cupid_gb_cheats_get(cheats, 0);
            rule.value = 0x7b;
            return cupid_gb_cheats_update(cheats, 0, &rule) == CUPID_GB_CHEAT_OK;
        }) ||
        session.cheatSaveWarning_.isEmpty() || cupid_gb_read_u8(&gb, 0xc000) != 0x7b ||
        !session.restart() || cupid_gb_read_u8(&session.emulator_->gb, 0xc000) != 0x7b)
        return smokeFailure(__FILE__, __LINE__);
    session.setPaused(true);
    if (!QDir().rmdir(session.cheatPath_) || !QFile::rename(backup, session.cheatPath_))
        return smokeFailure(__FILE__, __LINE__);
    session.openCheats(true);
    auto &searchGb = session.emulator_->gb;
    std::memset(searchGb.work_ram, 0, sizeof(searchGb.work_ram));
    searchGb.work_ram[0x5009] = 0x77;
    auto *condition = session.cheatsWindow_->findChild<QComboBox *>("cheatSearchCondition");
    auto *operand = session.cheatsWindow_->findChild<QLineEdit *>("cheatSearchOperand");
    auto *filter = session.cheatsWindow_->findChild<QPushButton *>("filterCheatSearch");
    auto *results = session.cheatsWindow_->findChild<QTableView *>("cheatSearchResults");
    auto *add = session.cheatsWindow_->findChild<QPushButton *>("addSearchCheat");
    if (!condition || !operand || !filter || !results || !add)
        return smokeFailure(__FILE__, __LINE__);
    condition->setCurrentIndex(1);
    operand->setText("77");
    filter->click();
    if (results->model()->rowCount() != 1)
        return smokeFailure(__FILE__, __LINE__);
    results->selectRow(0);
    add->click();
    const auto *found = cupid_gb_cheats_get(session.cheats_.get(), 1);
    if (!found || found->address != 0xd009 || found->bank != 5 || found->value != 0x77)
        return smokeFailure(__FILE__, __LINE__);
    searchGb.work_ram[0x5009] = 0;
    cupid_gb_write_u8(&searchGb, 0xff70, 5);
    if (cupid_gb_read_u8(&searchGb, 0xd009) != 0x77 || !session.closeRom() ||
        !session.openRom(rom) || cupid_gb_cheats_count(session.cheats_.get()) != 2)
        return smokeFailure(__FILE__, __LINE__);
    session.setPaused(true);
    if (!session.saveSlot(0))
        return smokeFailure(__FILE__, __LINE__);
    QFile stateFile(session.slotPath(0));
    if (!stateFile.open(QIODevice::ReadOnly))
        return smokeFailure(__FILE__, __LINE__);
    const QByteArray protectedState = stateFile.readAll();
    stateFile.close();
    const QString automaticPath = session.cheatPath_;
    session.cheatPath_ = stateFile.fileName();
    if (!session.mutateCheats([](CupidGbCheats *collection) {
            CupidGbCheat record = *cupid_gb_cheats_get(collection, 0);
            record.value ^= 1;
            return cupid_gb_cheats_update(collection, 0, &record) == CUPID_GB_CHEAT_OK;
        }) ||
        session.cheatSaveWarning_.isEmpty() || !stateFile.open(QIODevice::ReadOnly) ||
        stateFile.readAll() != protectedState)
        return smokeFailure(__FILE__, __LINE__);
    session.cheatPath_ = automaticPath;
    return !session.failed_;
}
