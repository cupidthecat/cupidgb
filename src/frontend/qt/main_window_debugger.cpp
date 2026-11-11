#include "main_window_internal.h"

void MainWindow::ensureDebugger() {
    const bool created = !debugger_;
    if (!debugger_) {
        debugger_ = std::make_unique<CupidGbDebugger>();
        cupid_gb_debugger_init(debugger_.get());
    }
    if (!debuggerWindow_) {
        debuggerWindow_ = new DebuggerWindow(this);
        debuggerWindow_->setHandlers(
            [this](const QString &command) { debuggerCommand(command); },
            [this](const QString &path) { return loadDebuggerSymbols(path); },
            [this] {
                if (debuggerTimer_.isActive())
                    stopDebugger("Debugger window closed; execution paused.");
                if (paused_) {
                    resetDebuggerTimeline();
                } else {
                    debuggerHistory_.clear();
                    debuggerHistoryBytes_ = 0;
                    debuggerContext_.reset();
                }
            });
    }
    if (created && emulator_) {
        const QFileInfo rom(romPath_);
        const QString symbols = rom.dir().filePath(rom.completeBaseName() + ".sym");
        if (QFileInfo::exists(symbols))
            loadDebuggerSymbols(symbols);
    }
}

void MainWindow::openDebugger() {
    ensureDebugger();
    pauseDebugger();
    refreshDebugger();
    debuggerWindow_->show();
    debuggerWindow_->raise();
    debuggerWindow_->activateWindow();
}

void MainWindow::openInspectors() {
    pauseDebugger();
    if (!inspectorsWindow_) {
        inspectorsWindow_ = new InspectorsWindow(this);
        inspectorsWindow_->setSession(
            [this] { return emulator_ ? &emulator_->gb : nullptr; },
            [this](uint16_t address, uint8_t value) { return debuggerWrite(address, value); });
        inspectorsWindow_->setColorTable(cgbColorTable_.get());
    }
    inspectorsWindow_->refresh();
    inspectorsWindow_->show();
    inspectorsWindow_->raise();
    inspectorsWindow_->activateWindow();
}

void MainWindow::resetDebuggerTimeline(bool clearSession) {
    resetControllerRumble();
    debuggerTimer_.stop();
    debuggerHistory_.clear();
    debuggerHistoryBytes_ = 0;
    debuggerContext_.reset();
    debuggerCommandSteps_ = 0;
    if (debugger_) {
        if (clearSession) {
            cupid_gb_debugger_cleanup(debugger_.get());
            cupid_gb_debugger_init(debugger_.get());
        } else {
            cupid_gb_debugger_reset_execution(debugger_.get());
        }
    }
}

void MainWindow::pauseDebugger() {
    if (!debuggerInputPaused_)
        debuggerPlayerButtons_ = livePlayerButtons();
    debuggerInputPaused_ = true;
    debuggerTimer_.stop();
    const auto previousStop = debugger_ ? debugger_->last_stop : CUPID_GB_DEBUG_STOP_NONE;
    if (debugger_)
        cupid_gb_debugger_reset_execution(debugger_.get());
    if (!paused_) {
        // Host focus/key releases continue to clear physical bindings, while
        // paused tools inspect the guest input at the stopped boundary.
        setPaused(true);
    }
    // The pause transition also resets execution, so retain the stopped reason
    // after it completes. Errors must remain inspectable without skipping a hit.
    if (debugger_ && (previousStop == CUPID_GB_DEBUG_STOP_BREAKPOINT ||
                      previousStop == CUPID_GB_DEBUG_STOP_ERROR))
        debugger_->last_stop = previousStop;
}

void MainWindow::refreshDebugger() {
    if (inspectorsWindow_ && inspectorsWindow_->isVisible())
        inspectorsWindow_->refresh();
    if (!debuggerWindow_)
        return;
    if (!emulator_ || !debugger_) {
        debuggerWindow_->updateState({}, {}, {}, false, false, false);
        return;
    }
    const auto &gb = emulator_->gb;
    const auto &cpu = gb.cpu;
    const QString registers = QString::asprintf(
        "AF  %02X%02X\nBC  %02X%02X\nDE  %02X%02X\nHL  %02X%02X\n"
        "SP  %04X\nPC  %04X\nIME %s\nHALT %s  STOP %s\n"
        "Z %u  N %u  H %u  C %u\n\nBase T-cycles: %llu",
        cpu.a, cpu.f, cpu.b, cpu.c, cpu.d, cpu.e, cpu.h, cpu.l, cpu.sp, cpu.pc,
        cpu.ime ? "on" : "off", cpu.halted ? "yes" : "no", cpu.stopped ? "yes" : "no",
        !!(cpu.f & 0x80), !!(cpu.f & 0x40), !!(cpu.f & 0x20), !!(cpu.f & 0x10),
        static_cast<unsigned long long>(gb.elapsed_cycles));
    QString listing;
    uint16_t address = cpu.pc;
    for (unsigned line = 0; line < 16; ++line) {
        CupidGbDebugInstruction instruction{};
        cupid_gb_debug_disassemble(&gb, address, &instruction);
        const int bank = cupid_gb_debugger_bank(&gb, address);
        const char *symbol =
            cupid_gb_debugger_symbol_for_address(debugger_.get(), bank, address, true);
        if (symbol)
            listing += QString::fromUtf8(symbol) + ":\n";
        QString bytes;
        for (unsigned byte = 0; byte < instruction.length; ++byte)
            bytes += QString("%1 ").arg(instruction.bytes[byte], 2, 16, QChar('0')).toUpper();
        listing += QString("$%1:$%2  %3 %4\n")
                       .arg(bank, 2, 16, QChar('0'))
                       .arg(address, 4, 16, QChar('0'))
                       .arg(bytes, -9)
                       .arg(QString::fromUtf8(instruction.text));
        address = uint16_t(address + std::max<unsigned>(1, instruction.length));
    }
    QString breakpoints = "Breakpoints (delete by number):\n";
    for (size_t index = 0; index < debugger_->bp_count; ++index) {
        const auto &point = debugger_->breakpoints[index];
        breakpoints +=
            QString("%1  %2:$%3%4%5\n")
                .arg(index + 1)
                .arg(point.bank < 0 ? "*" : "$" + QString::number(point.bank, 16))
                .arg(point.address, 4, 16, QChar('0'))
                .arg(point.enabled ? "" : " [disabled]")
                .arg(point.condition[0] ? " if " + QString::fromUtf8(point.condition) : QString());
    }
    if (!debugger_->bp_count)
        breakpoints += "None\n";
    breakpoints += QString("\nSymbols: %1\nInstruction history: %2 / 128")
                       .arg(debugger_->symbol_count)
                       .arg(debuggerHistory_.size());
    debuggerWindow_->updateState(registers, listing, breakpoints, true,
                                 !paused_ || debuggerTimer_.isActive(), !debuggerHistory_.empty());
}

bool MainWindow::loadDebuggerSymbols(const QString &path) {
    ensureDebugger();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 4 * 1024 * 1024) {
        debuggerWindow_->appendOutput("Cannot read symbols: " + path + "\n" + file.errorString());
        return false;
    }
    const QByteArray text = file.readAll();
    size_t errorLine = 0;
    if (text.size() != file.size() ||
        !cupid_gb_debugger_load_symbols(debugger_.get(), text.constData(), size_t(text.size()),
                                        &errorLine)) {
        debuggerWindow_->appendOutput(
            QString("Symbols rejected at line %1; existing symbols are unchanged.").arg(errorLine));
        return false;
    }
    debuggerWindow_->appendOutput(QString("Loaded %1 symbols from %2.")
                                      .arg(debugger_->symbol_count)
                                      .arg(QFileInfo(path).fileName()));
    refreshDebugger();
    return true;
}

bool MainWindow::rememberDebuggerStep() {
    if (!emulator_)
        return false;
    if (!debuggerContext_) {
        debuggerContext_ = std::make_unique<CupidGbStateContext>();
        if (cupid_gb_state_context_init(debuggerContext_.get(), &emulator_->gb) !=
            CUPID_GB_STATE_OK) {
            debuggerContext_.reset();
            return false;
        }
    }
    const size_t size = cupid_gb_state_size(&emulator_->gb);
    if (!size || size > 16 * 1024 * 1024)
        return false;
    QByteArray state(qsizetype(size), Qt::Uninitialized);
    size_t written = 0;
    if (cupid_gb_save_state_with_context(&emulator_->gb, reinterpret_cast<uint8_t *>(state.data()),
                                         size, &written,
                                         debuggerContext_.get()) != CUPID_GB_STATE_OK)
        return false;
    state.resize(qsizetype(written));
    QByteArray compressed = qCompress(state, 1);
    if (compressed.isEmpty())
        return false;
    // Compression can retain space for an incompressible input. Release that
    // spare capacity before accounting for the retained snapshot allocation.
    compressed.squeeze();
    debuggerHistoryBytes_ += compressed.capacity() + qsizetype(sizeof(QByteArray));
    debuggerHistory_.push_back(std::move(compressed));
    while (debuggerHistory_.size() > 128 || debuggerHistoryBytes_ > 32 * 1024 * 1024) {
        debuggerHistoryBytes_ -=
            debuggerHistory_.front().capacity() + qsizetype(sizeof(QByteArray));
        debuggerHistory_.pop_front();
    }
    return true;
}

void MainWindow::drainDebuggerAudio() {
    if (!emulator_)
        return;
    int16_t samples[CUPID_GB_APU_BUF_FRAMES * 2];
    const auto frames = cupid_gb_apu_drain(&emulator_->gb, samples, CUPID_GB_APU_BUF_FRAMES);
    generatedAudioFrames_ += frames;
    if (recorder_ && recorder_->active() && !recorder_->append(samples, frames)) {
        const QString error = recorder_->errorString();
        stopRecording();
        reportError("Audio recording stopped: " + error);
    }
}

void MainWindow::stopDebugger(const QString &message) {
    pauseDebugger();
    drainDebuggerAudio();
    if (emulator_) {
        present();
        watchBattery();
    }
    if (debuggerWindow_)
        debuggerWindow_->appendOutput(message);
    if (debuggerWindow_ && !debuggerWindow_->isVisible()) {
        debuggerWindow_->show();
        debuggerWindow_->raise();
    }
    statusBar()->showMessage(message, 5000);
    refreshDebugger();
}

void MainWindow::debuggerSlice() {
    if (!emulator_ || !debugger_ || sessionId_ != debuggerSessionId_) {
        resetDebuggerTimeline();
        refreshDebugger();
        return;
    }
    if (stateOperation_ || menuDepth_)
        return;
    QScopedValueRollback<bool> operation(stateOperation_, true);
    QElapsedTimer slice;
    slice.start();
    for (unsigned count = 0; count < 128 && slice.elapsed() < 4; ++count) {
        if (!rememberDebuggerStep()) {
            stopDebugger("Debugger stopped: instruction history could not be captured.");
            return;
        }
        const uint64_t previousSteps = debugger_->steps;
        const auto result = cupid_gb_debugger_step(debugger_.get(), &emulator_->gb);
        const bool conditionError =
            result == CUPID_GB_DEBUG_STOP_ERROR && debugger_->steps == previousSteps;
        if (emulator_->gb.sgb.player_count != controllerPlayerCount_)
            refreshControllerPlayerCount();
        if (result == CUPID_GB_DEBUG_STOP_BREAKPOINT || result == CUPID_GB_DEBUG_STOP_NO_ROM ||
            conditionError) {
            // These stops precede execution. Keep a failed hardware-step
            // attempt, but do not offer a fictitious instruction Backstep.
            debuggerHistoryBytes_ -=
                debuggerHistory_.back().capacity() + qsizetype(sizeof(QByteArray));
            debuggerHistory_.pop_back();
        }
        ++debuggerCommandSteps_;
        drainDebuggerAudio();
        updatePrinter();
        if (result != CUPID_GB_DEBUG_STOP_NONE) {
            stopDebugger(conditionError ? "Breakpoint condition evaluation failed."
                                        : QString::fromUtf8(cupid_gb_debugger_stop_name(result)));
            return;
        }
        if (debuggerCommandSteps_ >= 1000000) {
            stopDebugger("Debugger instruction budget exhausted before the target was reached.");
            return;
        }
    }
    present();
    refreshDebugger();
}

bool MainWindow::debuggerBackstep() {
    if (!emulator_ || debuggerHistory_.empty()) {
        if (debuggerWindow_)
            debuggerWindow_->appendOutput(
                "No instruction history. Only explicit debugger stepping is recorded.");
        return false;
    }
    pauseDebugger();
    const QByteArray state = qUncompress(debuggerHistory_.back());
    if (state.isEmpty() || cupid_gb_load_state_with_context(
                               &emulator_->gb, reinterpret_cast<const uint8_t *>(state.constData()),
                               size_t(state.size()), debuggerContext_.get()) != CUPID_GB_STATE_OK) {
        stopDebugger("Debugger history could not be restored.");
        return false;
    }
    ++timelineId_;
    resetPrinter();
    resetControllerRumble();
    controllerPlayerCount_ =
        cupid_gb_sgb_active(&emulator_->gb) ? emulator_->gb.sgb.player_count : 1;
    debuggerHistoryBytes_ -= debuggerHistory_.back().capacity() + qsizetype(sizeof(QByteArray));
    debuggerHistory_.pop_back();
    if (inspectorsWindow_)
        inspectorsWindow_->sessionChanged();
    batteryDirty_ = true;
    saveTimer_.start();
    clearAudio();
    resetRewind();
    rememberFrame();
    previousDisplayFrame_ = {};
    currentDisplayFrame_ = {};
    present();
    refreshDebugger();
    return true;
}

bool MainWindow::debuggerWrite(uint16_t address, uint8_t value) {
    if (!emulator_)
        return false;
    pauseDebugger();
    if (!cupid_gb_debug_poke(&emulator_->gb, address, value))
        return false;
    ++timelineId_;
    resetDebuggerTimeline();
    resetRewind();
    rememberFrame();
    watchBattery();
    previousDisplayFrame_ = {};
    currentDisplayFrame_ = {};
    present();
    refreshDebugger();
    return true;
}

void MainWindow::debuggerCommand(const QString &input) {
    ensureDebugger();
    const QString command = input.trimmed();
    const QStringList words = command.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    if (words.isEmpty())
        return;
    const QString verb = words.front().toLower();
    const auto output = [this](const QString &text) { debuggerWindow_->appendOutput(text); };
    if (verb == "help" || verb == "?") {
        output("break [bank:address|symbol] [if expression]   delete NUMBER|all   breakpoints\n"
               "continue (c)   step (s)   next (n)   finish (f)   backstep (bs)\n"
               "registers (r)   print (p) expression   disassemble [address]\n"
               "x address [decimal count]\n"
               "write address byte   set register value   symbols path\n"
               "Numbers are decimal; use $ or 0x for hexadecimal. Symbols are supported.\n"
               "Backstep retains up to 128 explicit debugger steps within 32 MiB.\n"
               "Continuous playback does not record instruction history.\n"
               "Expressions are pure 16-bit values; [address] reads a byte and {address} a word.\n"
               "Precedence: comparisons; + - | ||; * / % & ^ &&; shifts; bank:address.\n"
               "Assignment and unary operators are not supported.");
        return;
    }
    if (verb == "symbols") {
        QString path = command.mid(words.front().size()).trimmed();
        if (path.size() >= 2 && path.startsWith('"') && path.endsWith('"'))
            path = path.mid(1, path.size() - 2);
        if (path.isEmpty()) {
            output("Usage: symbols path");
            return;
        }
        loadDebuggerSymbols(path);
        return;
    }
    if (!emulator_) {
        output("Open a cartridge before using debugger execution or memory commands.");
        return;
    }
    const auto number = [](QString text, unsigned maximum, unsigned *value) {
        int base = 10;
        if (text.startsWith('$')) {
            text.remove(0, 1);
            base = 16;
        } else if (text.startsWith("0x", Qt::CaseInsensitive)) {
            text.remove(0, 2);
            base = 16;
        }
        bool valid = false;
        *value = text.toUInt(&valid, base);
        return valid && *value <= maximum;
    };
    const auto address = [this, &number](const QString &text, int *bank, uint16_t *value) {
        const auto parts = text.split(':');
        unsigned raw = 0;
        *bank = -1;
        if (parts.size() == 2) {
            unsigned rawBank = 0;
            if (parts[0] != "*" && !number(parts[0], 0xffffu, &rawBank))
                return false;
            if (!number(parts[1], 0xffffu, &raw))
                return false;
            *bank = parts[0] == "*" ? -1 : int(rawBank);
            *value = uint16_t(raw);
            return true;
        }
        if (parts.size() != 1)
            return false;
        uint16_t symbolBank = 0;
        const QByteArray name = text.toUtf8();
        if (cupid_gb_debugger_lookup_symbol(debugger_.get(), name.constData(), &symbolBank,
                                            value)) {
            *bank = symbolBank;
            return true;
        }
        if (number(text, 0xffffu, &raw)) {
            *value = uint16_t(raw);
            return true;
        }
        return false;
    };
    if ((verb == "print" || verb == "p") && words.size() >= 2) {
        const QByteArray expression = command.mid(words.front().size()).trimmed().toUtf8();
        CupidGbDebugValue value;
        size_t error = 0;
        if (!cupid_gb_debugger_evaluate(debugger_.get(), &emulator_->gb, expression.constData(),
                                        &value, &error)) {
            output(QString("Invalid expression near byte %1.").arg(error + 1));
            return;
        }
        const QString prefix =
            value.bank < 0 ? QString() : "$" + QString::number(value.bank, 16) + ":";
        output(
            QString("%1$%2 (%3)").arg(prefix).arg(value.value, 4, 16, QChar('0')).arg(value.value));
        return;
    }
    if ((verb == "break" || verb == "b") && words.size() >= 2) {
        QString locationText = command.mid(words.front().size()).trimmed();
        QString condition;
        const auto separator = QRegularExpression("\\s+if\\s+").match(locationText);
        if (separator.hasMatch()) {
            condition = locationText.mid(separator.capturedEnd()).trimmed();
            locationText = locationText.left(separator.capturedStart()).trimmed();
        }
        int bank;
        uint16_t location;
        bool valid = address(locationText, &bank, &location);
        if (!valid) {
            const QByteArray expression = locationText.toUtf8();
            CupidGbDebugValue value;
            valid = cupid_gb_debugger_evaluate(debugger_.get(), &emulator_->gb,
                                               expression.constData(), &value, nullptr);
            if (valid) {
                bank = value.bank;
                location = value.value;
            }
        }
        const QByteArray expression = condition.toUtf8();
        if (!valid ||
            !cupid_gb_debugger_breakpoint_add_condition(debugger_.get(), &emulator_->gb, bank,
                                                        location, expression.constData())) {
            output("Cannot add breakpoint: invalid address/condition or breakpoint limit reached.");
            return;
        }
        output(QString("Breakpoint added at %1%2.")
                   .arg(locationText)
                   .arg(condition.isEmpty() ? QString() : " if " + condition));
        refreshDebugger();
        return;
    }
    if (verb == "delete" && words.size() == 2) {
        if (words[1].compare("all", Qt::CaseInsensitive) == 0) {
            cupid_gb_debugger_breakpoints_clear(debugger_.get());
        } else {
            bool valid = false;
            const auto index = words[1].toULongLong(&valid, 10);
            if (!valid || !index ||
                !cupid_gb_debugger_breakpoint_remove(debugger_.get(), size_t(index - 1))) {
                output("No breakpoint with that number.");
                return;
            }
        }
        refreshDebugger();
        return;
    }
    if (verb == "breakpoints" && words.size() == 1) {
        output(QString("%1 breakpoints configured.").arg(debugger_->bp_count));
        refreshDebugger();
        return;
    }
    if ((verb == "continue" || verb == "c") && words.size() == 1) {
        setPaused(false);
        output("Continuing; instruction history cleared.");
        refreshDebugger();
        return;
    }
    if ((verb == "break" || verb == "b") && words.size() == 1) {
        stopDebugger("Break requested.");
        return;
    }
    if ((verb == "backstep" || verb == "bs") && words.size() == 1) {
        if (debuggerBackstep())
            output("Restored the preceding instruction boundary.");
        return;
    }
    CupidGbDebugMode mode = CUPID_GB_DEBUG_PAUSED;
    if (verb == "step" || verb == "s")
        mode = CUPID_GB_DEBUG_STEP;
    else if (verb == "next" || verb == "n")
        mode = CUPID_GB_DEBUG_NEXT;
    else if (verb == "finish" || verb == "f")
        mode = CUPID_GB_DEBUG_FINISH;
    if (mode != CUPID_GB_DEBUG_PAUSED && words.size() == 1) {
        pauseDebugger();
        debuggerCommandSteps_ = 0;
        debuggerSessionId_ = sessionId_;
        cupid_gb_debugger_begin(debugger_.get(), &emulator_->gb, mode);
        debuggerTimer_.start();
        refreshDebugger();
        return;
    }
    if ((verb == "registers" || verb == "r") && words.size() == 1) {
        pauseDebugger();
        refreshDebugger();
        output(debuggerWindow_->findChild<QPlainTextEdit *>("debuggerRegisters")->toPlainText());
        return;
    }
    if (verb == "set" && words.size() == 3) {
        unsigned value;
        if (!number(words[2], 0xffffu, &value)) {
            output("Register value must fit in sixteen bits.");
            return;
        }
        const QString name = words[1].toUpper();
        if (!QStringList{"AF", "BC", "DE", "HL", "SP", "PC", "A", "F", "B", "C", "D", "E", "H", "L",
                         "IME"}
                 .contains(name) ||
            (name.size() == 1 && value > 255) || (name == "IME" && value > 1)) {
            output("Unknown register or value outside its range.");
            return;
        }
        pauseDebugger();
        auto &cpu = emulator_->gb.cpu;
        if (name == "AF") {
            cpu.a = uint8_t(value >> 8);
            cpu.f = uint8_t(value & 0xf0u);
        } else if (name == "BC") {
            cpu.b = uint8_t(value >> 8);
            cpu.c = uint8_t(value);
        } else if (name == "DE") {
            cpu.d = uint8_t(value >> 8);
            cpu.e = uint8_t(value);
        } else if (name == "HL") {
            cpu.h = uint8_t(value >> 8);
            cpu.l = uint8_t(value);
        } else if (name == "PC")
            cpu.pc = uint16_t(value);
        else if (name == "SP")
            cpu.sp = uint16_t(value);
        else if (name == "IME") {
            cpu.ime = value != 0;
            cpu.ime_delay = 0;
        } else if (name == "A")
            cpu.a = uint8_t(value);
        else if (name == "F")
            cpu.f = uint8_t(value & 0xf0u);
        else if (name == "B")
            cpu.b = uint8_t(value);
        else if (name == "C")
            cpu.c = uint8_t(value);
        else if (name == "D")
            cpu.d = uint8_t(value);
        else if (name == "E")
            cpu.e = uint8_t(value);
        else if (name == "H")
            cpu.h = uint8_t(value);
        else if (name == "L")
            cpu.l = uint8_t(value);
        ++timelineId_;
        resetDebuggerTimeline();
        if (inspectorsWindow_)
            inspectorsWindow_->sessionChanged();
        resetRewind();
        rememberFrame();
        refreshDebugger();
        return;
    }
    int bank = -1;
    uint16_t location = emulator_->gb.cpu.pc;
    if (verb == "x" || verb == "memory" || verb == "write" || verb == "disassemble" ||
        verb == "dis") {
        const bool disassemble = verb == "disassemble" || verb == "dis";
        if (words.size() > (disassemble ? 2 : 3) || (!disassemble && words.size() < 2) ||
            (words.size() >= 2 && !address(words[1], &bank, &location))) {
            output("Invalid memory address or command arguments.");
            return;
        }
        if (bank >= 0 && bank != cupid_gb_debugger_bank(&emulator_->gb, location)) {
            output("That bank is not currently mapped at the requested address.");
            return;
        }
        if (verb == "write") {
            unsigned value;
            if (words.size() != 3 || !number(words[2], 255, &value) ||
                !debuggerWrite(location, uint8_t(value))) {
                output("Write rejected. ROM and unusable memory cannot be edited.");
                return;
            }
            output("Memory edited; instruction and rewind history cleared.");
            return;
        }
        unsigned count = 64;
        if (!disassemble && words.size() == 3) {
            bool valid = false;
            count = words[2].toUInt(&valid, 10);
            if (!valid || !count || count > 256) {
                output("Memory count must be between 1 and 256 decimal bytes.");
                return;
            }
        }
        pauseDebugger();
        QString result;
        if (disassemble) {
            for (unsigned line = 0; line < 16; ++line) {
                CupidGbDebugInstruction instruction{};
                cupid_gb_debug_disassemble(&emulator_->gb, location, &instruction);
                result += QString("$%1: %2\n")
                              .arg(location, 4, 16, QChar('0'))
                              .arg(QString::fromUtf8(instruction.text));
                location = uint16_t(location + std::max<unsigned>(1, instruction.length));
            }
        } else {
            for (unsigned byte = 0; byte < count; ++byte) {
                if (byte % 16 == 0)
                    result += QString("$%1: ").arg(uint16_t(location + byte), 4, 16, QChar('0'));
                result += QString("%1 ").arg(
                    cupid_gb_debug_peek(&emulator_->gb, uint16_t(location + byte)), 2, 16,
                    QChar('0'));
                if (byte % 16 == 15 || byte + 1 == count)
                    result += '\n';
            }
        }
        output(result);
        refreshDebugger();
        return;
    }
    output("Unknown command or arguments. Type help for supported commands.");
}
