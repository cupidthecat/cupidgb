/**
 * @file    main_window_watchpoints.cpp
 * @brief   Qt memory watchpoint commands, listings, and access stop reports.
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://gnu.org>.
 */

#include "main_window_internal.h"

#include <limits>

namespace {
QString watchAddress(int bank, uint16_t address) {
    const QString prefix = bank < 0 ? "*" : "$" + QString::number(bank, 16);
    return QString("%1:$%2").arg(prefix).arg(address, 4, 16, QChar('0'));
}

QString watchAccess(uint8_t access) {
    if (access == CUPID_GB_DEBUG_ACCESS_READ)
        return "r";
    if (access == CUPID_GB_DEBUG_ACCESS_WRITE)
        return "w";
    return "rw";
}
} // namespace

QString MainWindow::debuggerWatchpointListing() const {
    QString text = "\nWatchpoints (unwatch by ID):\n";
    for (size_t index = 0; index < debugger_->wp_count; ++index) {
        const auto &point = debugger_->watchpoints[index];
        text += QString("%1  %2").arg(point.id).arg(watchAddress(point.bank, point.start));
        if (point.end != point.start)
            text += QString(" to $%1 inclusive").arg(point.end, 4, 16, QChar('0'));
        text += " [" + watchAccess(point.access) + "]";
        if (!point.enabled)
            text += " [disabled]";
        if (point.condition[0])
            text += " if " + QString::fromUtf8(point.condition);
        text += '\n';
    }
    if (!debugger_->wp_count)
        text += "None\n";
    return text;
}

QString MainWindow::debuggerWatchMessage(bool conditionError) const {
    const auto &hit = debugger_->last_watch_hit;
    const QString location = watchAddress(hit.bank, hit.address);
    if (conditionError)
        return QString("Watchpoint %1 condition evaluation failed at %2.")
            .arg(hit.id)
            .arg(location);
    if (hit.access == CUPID_GB_DEBUG_ACCESS_WRITE)
        return QString("Watchpoint %1: write %2 = $%3.")
            .arg(hit.id)
            .arg(location)
            .arg(hit.new_value, 2, 16, QChar('0'));
    return QString("Watchpoint %1: read %2.").arg(hit.id).arg(location);
}

bool MainWindow::debuggerWatchCommand(const QString &command) {
    const QStringList words = command.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    if (words.isEmpty())
        return false;
    const QString verb = words.front().toLower();
    const auto output = [this](const QString &text) { debuggerWindow_->appendOutput(text); };
    if (verb == "watchpoints" || verb == "list") {
        if (words.size() != 1) {
            output("Usage: watchpoints or list");
            return true;
        }
        output(QString("%1 breakpoints and %2 watchpoints configured.")
                   .arg(debugger_->bp_count)
                   .arg(debugger_->wp_count));
        refreshDebugger();
        return true;
    }
    if (verb == "unwatch") {
        if (words.size() == 1 ||
            (words.size() == 2 && words[1].compare("all", Qt::CaseInsensitive) == 0)) {
            cupid_gb_debugger_watchpoints_clear(debugger_.get());
        } else if (words.size() == 2) {
            bool valid = false;
            const auto id = words[1].toULongLong(&valid, 10);
            if (!valid || !id || id > std::numeric_limits<uint32_t>::max() ||
                !cupid_gb_debugger_watchpoint_remove(debugger_.get(), uint32_t(id))) {
                output("No watchpoint with that ID.");
                return true;
            }
        } else {
            output("Usage: unwatch [ID|all]");
            return true;
        }
        refreshDebugger();
        return true;
    }
    if (verb != "watch" && !verb.startsWith("watch/"))
        return false;

    uint8_t access = CUPID_GB_DEBUG_ACCESS_WRITE;
    if (verb.startsWith("watch/")) {
        access = 0;
        for (const QChar flag : verb.mid(6)) {
            if (flag == 'r')
                access |= CUPID_GB_DEBUG_ACCESS_READ;
            else if (flag == 'w')
                access |= CUPID_GB_DEBUG_ACCESS_WRITE;
            else {
                output("Watchpoint access must be r, w, or rw.");
                return true;
            }
        }
    }
    if (!access || words.size() < 2) {
        output("Usage: watch[/r|/w|/rw] address [to end [inclusive]] [if condition]");
        return true;
    }
    QString location = command.mid(words.front().size()).trimmed();
    QString condition;
    const auto conditional = QRegularExpression("\\s+if\\s+").match(location);
    if (conditional.hasMatch()) {
        condition = location.mid(conditional.capturedEnd()).trimmed();
        location = location.left(conditional.capturedStart()).trimmed();
        if (condition.isEmpty()) {
            output("A watchpoint condition cannot be empty.");
            return true;
        }
    }

    QString first = location;
    QString last;
    bool inclusive = false;
    const auto range = QRegularExpression("\\s+to\\s+").match(location);
    if (range.hasMatch()) {
        first = location.left(range.capturedStart()).trimmed();
        last = location.mid(range.capturedEnd()).trimmed();
        const auto suffix = QRegularExpression("\\s+inclusive$").match(last);
        if (suffix.hasMatch()) {
            inclusive = true;
            last = last.left(suffix.capturedStart()).trimmed();
        }
    }
    const auto evaluate = [this](const QString &text, CupidGbDebugValue *value) {
        const QByteArray expression = text.toUtf8();
        return cupid_gb_debugger_evaluate(debugger_.get(), &emulator_->gb, expression.constData(),
                                          value, nullptr);
    };
    CupidGbDebugValue startValue{};
    if (!evaluate(first, &startValue)) {
        output("Invalid watchpoint address or expression.");
        return true;
    }
    uint16_t end = startValue.value;
    if (range.hasMatch()) {
        CupidGbDebugValue endValue{};
        if (!evaluate(last, &endValue) || endValue.value <= startValue.value ||
            (startValue.bank >= 0 && endValue.bank >= 0 && startValue.bank != endValue.bank)) {
            output("Watchpoint range must increase without conflicting bank qualifiers.");
            return true;
        }
        end = uint16_t(endValue.value - (inclusive ? 0u : 1u));
    }
    const QByteArray expression = condition.toUtf8();
    if (!cupid_gb_debugger_watchpoint_add(debugger_.get(), &emulator_->gb, startValue.bank,
                                          startValue.value, end, access, expression.constData())) {
        output("Cannot add watchpoint: invalid condition or watchpoint limit reached.");
        return true;
    }
    const auto &point = debugger_->watchpoints[debugger_->wp_count - 1];
    output(QString("Watchpoint %1 added at %2 [%3].")
               .arg(point.id)
               .arg(location)
               .arg(watchAccess(access)));
    refreshDebugger();
    return true;
}
