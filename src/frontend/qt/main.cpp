#include "main_window.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QTemporaryDir>

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("CupidGB");
    QCoreApplication::setApplicationName("CupidGB");
    QCoreApplication::setApplicationVersion("0.2.0");
    QCommandLineParser parser;
    parser.setApplicationDescription("CupidGB Game Boy and Game Boy Color emulator");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"smoke-test", "Run isolated frontend workflow checks and exit."});
    parser.addOption(
        {"run-seconds", "Run the frontend for a bounded duration and exit.", "seconds"});
    parser.addOption({"report", "Write bounded frontend measurements as JSON.", "path"});
    parser.addOption(
        {"input-trace", "Apply frame-number and active-low hexadecimal input masks.", "path"});
    parser.addPositionalArgument("rom", "Optional .gb or .gbc cartridge path.");
    parser.process(app);
    const bool smoke = parser.isSet("smoke-test");
    const bool bounded = parser.isSet("run-seconds");
    QTemporaryDir temporary;
    if (smoke || bounded) {
        if (!temporary.isValid())
            return 1;
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
    }
    MainWindow window(smoke, bounded);
    window.show();
    if (smoke)
        return window.smokeTest() ? 0 : 1;
    if (parser.positionalArguments().size() > 1)
        parser.showHelp(1);
    if (!parser.positionalArguments().isEmpty() &&
        !window.openRom(parser.positionalArguments().first()))
        return 1;
    if (bounded) {
        bool valid = false;
        const double seconds = parser.value("run-seconds").toDouble(&valid);
        if (!valid || seconds <= 0 || seconds > 86400 || parser.value("report").isEmpty() ||
            parser.positionalArguments().isEmpty())
            parser.showHelp(1);
        if (parser.isSet("input-trace") && !window.loadInputTrace(parser.value("input-trace")))
            return 1;
        window.beginBoundedRun(seconds, parser.value("report"));
    }
    return app.exec();
}
