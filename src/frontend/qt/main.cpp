#include "main_window.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("CupidGB");
    QCoreApplication::setApplicationName("CupidGB");
    QCoreApplication::setApplicationVersion("0.3.0");
    QGuiApplication::setDesktopFileName("io.github.cupidthecat.CupidGB");
    QCommandLineParser parser;
    parser.setApplicationDescription("CupidGB Game Boy and Game Boy Color emulator");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"smoke-test", "Run isolated frontend workflow checks and exit."});
    parser.addOption({"audio-check", "Require real audio during --smoke-test."});
    parser.addOption(
        {"boot-directory", "Use boot ROMs from this directory for this session.", "path"});
    parser.addOption(
        {"run-seconds", "Run the frontend for a bounded duration and exit.", "seconds"});
    parser.addOption({"report", "Write bounded frontend measurements as JSON.", "path"});
    parser.addOption(
        {"input-trace", "Apply frame-number and active-low hexadecimal input masks.", "path"});
    parser.addPositionalArgument("rom", "Optional .gb or .gbc cartridge path.");
    parser.process(app);
    const bool smoke = parser.isSet("smoke-test");
    const bool audioCheck = parser.isSet("audio-check");
    const bool bootOverride = parser.isSet("boot-directory");
    if ((audioCheck && !smoke) || (smoke && bootOverride))
        parser.showHelp(1);
    const bool bounded = parser.isSet("run-seconds");
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
    MainWindow window(smoke, bounded, audioCheck, bootDirectory);
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
