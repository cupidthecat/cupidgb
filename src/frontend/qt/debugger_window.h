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
