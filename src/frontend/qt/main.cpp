/**
 * @file    main.cpp
 * @brief   Qt application startup, command-line parsing, and desktop entry point.
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

#include "main_window.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QTemporaryDir>

#include <cmath>

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("CupidGB");
    QCoreApplication::setApplicationName("CupidGB");
    QCoreApplication::setApplicationVersion(QString::fromLatin1(CUPIDGB_VERSION));
    QGuiApplication::setDesktopFileName("io.github.cupidthecat.CupidGB");
    QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/cupidgb.png")));
    QCommandLineParser parser;
    parser.setApplicationDescription("CupidGB Game Boy and Game Boy Color emulator");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"smoke-test", "Run isolated frontend workflow checks and exit."});
    parser.addOption({"smoke-workflow",
                      "Select smoke checks: all, desktop, storage, sgb, link, or gbs.", "group"});
    parser.addOption({"audio-check", "Require real audio during --smoke-test."});
    parser.addOption(
        {"boot-directory", "Use boot ROMs from this directory for this session.", "path"});
    parser.addOption(
        {"power-on-seed",
         "Use auto, an unsigned decimal seed, or a 0x hexadecimal seed for this session.", "seed"});
    parser.addOption({"model",
                      "Use hardware for this session: auto, dmg, dmgabc, dmg0, mgb, "
                      "cgb, cgb-0, cgb-a, cgb-b, cgb-c, cgb-d, cgb-e, sgb, sgbpal, sgb2.",
                      "profile"});
    parser.addOption(
        {"camera-source", "Use camera input for this session: system or generated.", "source"});
    parser.addOption(
        {"run-seconds", "Run the frontend for a bounded duration and exit.", "seconds"});
    parser.addOption({"report", "Write bounded frontend measurements as JSON.", "path"});
    parser.addOption(
        {"input-trace", "Apply frame-number and active-low hexadecimal input masks.", "path"});
    parser.addPositionalArgument("file",
                                 "Optional cartridge, GBS music file, or compressed archive.");
    parser.process(app);
    const bool smoke = parser.isSet("smoke-test");
    const bool audioCheck = parser.isSet("audio-check");
    MainWindow::SmokeWorkflow smokeWorkflow = MainWindow::SmokeWorkflow::All;
    if (parser.isSet("smoke-workflow")) {
        const QString group = parser.value("smoke-workflow");
        if (group == "desktop")
            smokeWorkflow = MainWindow::SmokeWorkflow::Desktop;
        else if (group == "storage")
            smokeWorkflow = MainWindow::SmokeWorkflow::Storage;
        else if (group == "sgb")
            smokeWorkflow = MainWindow::SmokeWorkflow::Sgb;
        else if (group == "link")
            smokeWorkflow = MainWindow::SmokeWorkflow::Link;
        else if (group == "gbs")
            smokeWorkflow = MainWindow::SmokeWorkflow::Gbs;
        else if (group != "all")
            parser.showHelp(1);
        if (!smoke || (audioCheck && smokeWorkflow == MainWindow::SmokeWorkflow::Storage))
            parser.showHelp(1);
    }
    const bool bootOverride = parser.isSet("boot-directory");
    const bool seedOverride = parser.isSet("power-on-seed");
    std::optional<uint64_t> powerOnSeedOverride;
    for (const QString &argument : parser.values("power-on-seed")) {
        if (argument == "auto") {
            powerOnSeedOverride.reset();
            continue;
        }
        const bool hexadecimal = argument.startsWith("0x", Qt::CaseInsensitive);
        const QString digits = argument.mid(hexadecimal ? 2 : 0);
        bool validDigits = !digits.isEmpty();
        for (const QChar digit : digits) {
            const bool decimal = digit >= '0' && digit <= '9';
            const bool hexLetter = (digit >= 'a' && digit <= 'f') || (digit >= 'A' && digit <= 'F');
            validDigits = validDigits && (decimal || (hexadecimal && hexLetter));
        }
        bool converted = false;
        const quint64 seed = digits.toULongLong(&converted, hexadecimal ? 16 : 10);
        if (!validDigits || !converted) {
            qCritical("Invalid power-on seed. Use auto, unsigned decimal, or 0x hexadecimal "
                      "within the 64-bit range.");
            return 1;
        }
        powerOnSeedOverride = uint64_t(seed);
    }
    std::optional<int> modelOverride;
    if (parser.isSet("model")) {
        const QString name = parser.value("model");
        if (name == "auto") {
            modelOverride = -1;
        } else {
            for (int model = CUPID_GB_MODEL_DMG_ABC; model <= CUPID_GB_MODEL_CGB_D; ++model) {
                if (!cupid_gb_model_is_valid(CupidGbModel(model)))
                    continue;
                if (name == QString::fromLatin1(cupid_gb_model_name(CupidGbModel(model))) ||
                    (name == "cgb-e" && model == CUPID_GB_MODEL_CGB) ||
                    (name == "dmg" && model == CUPID_GB_MODEL_DMG_ABC)) {
                    modelOverride = model;
                    break;
                }
            }
        }
        if (!modelOverride) {
            qCritical("Invalid hardware profile. Use --model "
                      "auto|dmg|dmgabc|dmg0|mgb|cgb|cgb-0|cgb-a|cgb-b|cgb-c|cgb-d|cgb-e|sgb|sgbpal|"
                      "sgb2.");
            return 1;
        }
    }
    std::optional<MainWindow::CameraSource> cameraSourceOverride;
    for (const QString &source : parser.values("camera-source")) {
        if (source == "system")
            cameraSourceOverride = MainWindow::CameraSource::System;
        else if (source == "generated")
            cameraSourceOverride = MainWindow::CameraSource::Generated;
        else {
            qCritical("Invalid camera input. Use --camera-source system|generated.");
            return 1;
        }
    }
    if ((audioCheck && !smoke) ||
        (smoke && (bootOverride || modelOverride || seedOverride || cameraSourceOverride)))
        parser.showHelp(1);
    const bool bounded = parser.isSet("run-seconds");
    double seconds = 0;
    if (bounded && !smoke) {
        bool valid = false;
        seconds = parser.value("run-seconds").toDouble(&valid);
        if (!valid || !std::isfinite(seconds) || seconds <= 0 || seconds > 86400 ||
            parser.value("report").isEmpty() || parser.positionalArguments().isEmpty())
            parser.showHelp(1);
    }
    QTemporaryDir temporary;
    if (smoke || bounded) {
        if (!temporary.isValid())
            return 1;
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
    }
    QString bootDirectory;
    if (bootOverride) {
        bootDirectory = QDir(parser.value("boot-directory")).absolutePath();
        if (!QFileInfo(bootDirectory).isDir()) {
            qCritical("Boot ROM directory does not exist: %s", qPrintable(bootDirectory));
            return 1;
        }
    }
    MainWindow window(smoke, bounded, audioCheck, bootDirectory, modelOverride, powerOnSeedOverride,
                      cameraSourceOverride);
    window.show();
    if (smoke)
        return window.smokeTest(smokeWorkflow) ? 0 : 1;
    if (parser.positionalArguments().size() > 1)
        parser.showHelp(1);
    if (!parser.positionalArguments().isEmpty() &&
        !window.openRom(parser.positionalArguments().first()))
        return 1;
    if (bounded) {
        if (parser.isSet("input-trace") && !window.loadInputTrace(parser.value("input-trace")))
            return 1;
        window.beginBoundedRun(seconds, parser.value("report"));
    }
    return app.exec();
}
