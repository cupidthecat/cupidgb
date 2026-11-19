/**
 * @file    test_debugger_window.cpp
 * @brief   Qt tests for debugger commands, history, and controls.
 * @author  cupidthecat
 * @date    2026-11-16
 *
 * Copyright (C) 2026 cupidthecat
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://gnu.org>.
 */

#include "debugger_window.h"

#include <QApplication>
#include <QComboBox>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStringList>
#include <cassert>

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    DebuggerWindow window;
    QStringList commands;
    QString symbolPath;
    int closed = 0;
    window.setHandlers([&](const QString &command) { commands.append(command); },
                       [&](const QString &path) {
                           symbolPath = path;
                           return path.endsWith(".sym");
                       },
                       [&] { ++closed; });
    auto *input = window.findChild<QLineEdit *>("debuggerCommand");
    auto *step = window.findChild<QPushButton *>("debugger/step");
    auto *backstep = window.findChild<QPushButton *>("debugger/backstep");
    auto *registers = window.findChild<QPlainTextEdit *>("debuggerRegisters");
    assert(input && step && backstep && registers);
    assert(!step->isEnabled());
    window.updateState("PC 0150", "00:0150 NOP", "None", true, false, true);
    assert(step->isEnabled() && backstep->isEnabled());
    assert(registers->toPlainText() == "PC 0150");
    step->click();
    assert(commands == QStringList{"step"});
    input->setText("  next  ");
    QMetaObject::invokeMethod(input, "returnPressed", Qt::DirectConnection);
    assert(commands.back() == "next" && input->text().isEmpty());
    input->setText("unfinished");
    QKeyEvent previous(QEvent::KeyPress, Qt::Key_Up, Qt::NoModifier);
    QKeyEvent next(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier);
    QApplication::sendEvent(input, &previous);
    assert(input->text() == "next");
    QApplication::sendEvent(input, &previous);
    assert(input->text() == "step");
    QApplication::sendEvent(input, &next);
    QApplication::sendEvent(input, &next);
    assert(input->text() == "unfinished");
    assert(window.loadSymbols(QString::fromUtf8("symbols-\xc3\xa9.sym")));
    assert(symbolPath == QString::fromUtf8("symbols-\xc3\xa9.sym"));
    assert(!window.loadSymbols("invalid.txt"));
    window.updateState({}, {}, {}, true, true, false);
    assert(!step->isEnabled() && !backstep->isEnabled());
    assert(window.findChild<QLabel *>("debuggerState")->text().startsWith("Running"));
    window.submitCommand("   ");
    assert(commands.size() == 2);
    auto *watchAddress = window.findChild<QLineEdit *>("debuggerWatchpointAddress");
    auto *watchMode = window.findChild<QComboBox *>("debuggerWatchpointMode");
    auto *addWatch = window.findChild<QPushButton *>("debuggerAddWatchpoint");
    auto *clearWatches = window.findChild<QPushButton *>("debuggerClearWatchpoints");
    assert(watchAddress && watchMode && addWatch && clearWatches);
    watchAddress->setText("$c000 to $c010 if new == $42");
    assert(watchMode->currentData().toString() == "w");
    for (const auto &mode : QStringList{"w", "r", "rw"}) {
        watchMode->setCurrentIndex(watchMode->findData(mode));
        addWatch->click();
        assert(commands.back() == "watch/" + mode + " $c000 to $c010 if new == $42");
    }
    clearWatches->click();
    assert(commands.back() == "unwatch all");
    const qsizetype handledCommands = commands.size();
    for (int line = 0; line < 2100; ++line)
        window.appendOutput(QString::number(line));
    assert(window.findChild<QPlainTextEdit *>("debuggerOutput")->blockCount() <= 2000);
    assert(window.outputText().endsWith("2099"));
    window.show();
    window.close();
    assert(closed == 1);
    window.show();
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(&window, &escape);
    assert(!window.isVisible() && closed == 2);
    window.show();
    window.reject();
    assert(!window.isVisible() && closed == 3);
    window.show();
    window.done(QDialog::Accepted);
    assert(!window.isVisible() && closed == 4);
    window.setHandlers({}, {}, {});
    window.submitCommand("help");
    assert(commands.size() == handledCommands);
    return 0;
}
