#include "debugger_window.h"

#include <QApplication>
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
    assert(commands.size() == 2);
    return 0;
}
