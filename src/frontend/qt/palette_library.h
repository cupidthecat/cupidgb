#pragma once

#include "display_files.h"

#include <QVector>

struct PaletteTheme {
    QString name;
    DisplayPaletteFile colors;
    bool manual = true;
    bool separateOff = true;
    double brightness = 0;
    double hue = 0;
    double strength = 0;
};

void resolvePaletteTheme(PaletteTheme &theme);
bool decodePaletteTheme(const QByteArray &bytes, PaletteTheme &theme);
QByteArray encodePaletteTheme(const PaletteTheme &theme);

class PaletteLibrary final {
  public:
    explicit PaletteLibrary(const DisplayPaletteFile &fallback = {});
    static PaletteLibrary decode(const QByteArray &bytes, const DisplayPaletteFile &fallback,
                                 bool *valid = nullptr);
    QByteArray encode() const;
    const QVector<PaletteTheme> &themes() const;
    PaletteTheme &current();
    const PaletteTheme &current() const;
    bool select(const QString &name);
    bool add(PaletteTheme theme, const QString &name = "Untitled Palette");
    bool renameCurrent(const QString &name);
    void removeCurrent();
    bool restoreDefaults(const QVector<PaletteTheme> &defaults);

  private:
    QString uniqueName(const QString &name, const QString &excluding = {}) const;
    void sort();
    QVector<PaletteTheme> themes_;
    QString selected_;
};
