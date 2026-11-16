/**
 * @file    debugger_window.h
 * @brief   Qt debugger console state, command handlers, and control interface.
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

#pragma once

#include <QDialog>
#include <functional>

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;

class DebuggerWindow final : public QDialog {
  public:
    explicit DebuggerWindow(QWidget *parent = nullptr);
    void setHandlers(std::function<void(const QString &)> command,
                     std::function<bool(const QString &)> symbols, std::function<void()> closed);
    void updateState(const QString &registers, const QString &disassembly,
                     const QString &breakpoints, bool loaded, bool running, bool canBackstep);
    void appendOutput(const QString &text);
    void submitCommand(const QString &text);
    bool loadSymbols(const QString &path);
    QString outputText() const;

    void done(int result) override;

  private:
    std::function<void(const QString &)> commandHandler_;
    std::function<bool(const QString &)> symbolsHandler_;
    std::function<void()> closedHandler_;
    QPlainTextEdit *registers_ = nullptr;
    QPlainTextEdit *disassembly_ = nullptr;
    QPlainTextEdit *breakpoints_ = nullptr;
    QPlainTextEdit *output_ = nullptr;
    QLineEdit *input_ = nullptr;
    QLabel *state_ = nullptr;
    QPushButton *continue_ = nullptr;
    QPushButton *step_ = nullptr;
    QPushButton *next_ = nullptr;
    QPushButton *finish_ = nullptr;
    QPushButton *backstep_ = nullptr;
    bool closing_ = false;
};
