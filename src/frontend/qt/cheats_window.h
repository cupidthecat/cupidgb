#pragma once

#include "cheat_search.h"

#include <QDialog>
#include <functional>

extern "C" {
#include "cupid/gb/cheats.h"
}

class QAbstractItemModel;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableView;
class QTableWidget;
class QTabWidget;

class CheatsWindow final : public QDialog {
  public:
    using Getter = std::function<const CupidGbCheats *()>;
    using StateGetter = std::function<const CupidGb *()>;
    using Mutator = std::function<bool(const std::function<bool(CupidGbCheats *)> &)>;
    using Writer = std::function<bool(const std::function<bool(CupidGb &)> &)>;

    explicit CheatsWindow(QWidget *parent = nullptr);
    void setSession(Getter getter, StateGetter state, Mutator mutate, Writer write);
    void sessionChanged();
    void refresh();
    void selectSearch(bool search);
    void setSaveWarning(const QString &warning);
    std::function<void()> importFileRequested;
    std::function<void()> exportFileRequested;

  private:
    void editCheat(bool existing);
    void importCode();
    void removeCheat();
    void filterSearch();
    void resetSearch();
    void addSearchCheat();
    void writeSearchValue();

    Getter getter_;
    StateGetter state_;
    Mutator mutate_;
    Writer write_;
    CheatSearch search_;
    QTabWidget *tabs_;
    QCheckBox *enabled_;
    QTableWidget *cheats_;
    QLabel *status_;
    QLabel *saveWarning_;
    uint64_t sessionGeneration_ = 0;
    QPushButton *edit_;
    QPushButton *remove_;
    QComboBox *type_;
    QComboBox *condition_;
    QLineEdit *operand_;
    QLabel *resultCount_;
    QTableView *results_;
    QAbstractItemModel *resultModel_;
    QPushButton *addResult_;
    QPushButton *writeResult_;
};
