#pragma once

#include "palette_library.h"

#include <QDialog>
#include <functional>

class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

class PaletteEditor final : public QDialog {
  public:
    using Changed = std::function<void(const PaletteLibrary &)>;
    PaletteEditor(PaletteLibrary library, QVector<PaletteTheme> defaults, Changed changed,
                  QWidget *parent = nullptr);
    const PaletteLibrary &library() const;

  private:
    void refresh(bool refreshList = false);
    void changed();
    void chooseColor(size_t shade);
    PaletteLibrary library_;
    QVector<PaletteTheme> defaults_;
    Changed changed_;
    bool refreshing_ = false;
    QListWidget *list_;
    QLineEdit *name_;
    QCheckBox *manual_;
    QCheckBox *separateOff_;
    std::array<QPushButton *, 5> colors_{};
    std::array<QDoubleSpinBox *, 3> controls_{};
    QLabel *preview_;
};
