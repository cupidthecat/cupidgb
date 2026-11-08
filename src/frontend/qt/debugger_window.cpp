#include "debugger_window.h"

#include <QFileDialog>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QSplitter>
#include <QStringList>
#include <QTextDocument>
#include <QVBoxLayout>
#include <utility>

namespace {
class CommandEdit final : public QLineEdit {
  public:
    explicit CommandEdit(QWidget *parent) : QLineEdit(parent) {
    }

    void remember(const QString &command) {
        if (commands_.isEmpty() || commands_.back() != command)
            commands_.append(command);
        while (commands_.size() > 100)
            commands_.removeFirst();
        position_ = commands_.size();
        pending_.clear();
    }

  protected:
    void keyPressEvent(QKeyEvent *event) override {
        if (event->key() != Qt::Key_Up && event->key() != Qt::Key_Down) {
            QLineEdit::keyPressEvent(event);
            return;
        }
        if (position_ == commands_.size())
            pending_ = text();
        if (event->key() == Qt::Key_Up && position_ > 0)
            --position_;
        if (event->key() == Qt::Key_Down && position_ < commands_.size())
            ++position_;
        setText(position_ == commands_.size() ? pending_ : commands_[position_]);
        event->accept();
    }

  private:
    QStringList commands_;
    qsizetype position_ = 0;
    QString pending_;
};

QPlainTextEdit *readout(const QString &name, QWidget *parent) {
    auto *widget = new QPlainTextEdit(parent);
    widget->setObjectName(name);
    widget->setReadOnly(true);
    widget->setLineWrapMode(QPlainTextEdit::NoWrap);
    widget->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    return widget;
}
} // namespace

DebuggerWindow::DebuggerWindow(QWidget *parent) : QDialog(parent, Qt::Window) {
    setWindowTitle("CupidGB Debugger");
    setObjectName("debuggerWindow");
    auto *layout = new QVBoxLayout(this);
    auto *controls = new QHBoxLayout;
    const auto button = [this, controls](const QString &label, const QString &command) {
        auto *result = new QPushButton(label, this);
        result->setObjectName("debugger/" + command);
        controls->addWidget(result);
        connect(result, &QPushButton::clicked, this, [this, command] { submitCommand(command); });
        return result;
    };
    button("Break", "break");
    continue_ = button("Continue", "continue");
    step_ = button("Step", "step");
    next_ = button("Next", "next");
    finish_ = button("Finish", "finish");
    backstep_ = button("Backstep", "backstep");
    auto *symbols = new QPushButton("Load Symbols…", this);
    symbols->setObjectName("debugger/loadSymbols");
    controls->addWidget(symbols);
    connect(symbols, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, "Load Debugger Symbols", {},
                                                          "Symbol files (*.sym);;All files (*)");
        if (!path.isEmpty())
            loadSymbols(path);
    });
    layout->addLayout(controls);
    state_ = new QLabel("Open a cartridge to begin.", this);
    state_->setObjectName("debuggerState");
    layout->addWidget(state_);
    auto *horizontal = new QSplitter(Qt::Horizontal, this);
    registers_ = readout("debuggerRegisters", horizontal);
    disassembly_ = readout("debuggerDisassembly", horizontal);
    breakpoints_ = readout("debuggerBreakpoints", horizontal);
    registers_->setMinimumWidth(180);
    disassembly_->setMinimumWidth(320);
    horizontal->addWidget(registers_);
    horizontal->addWidget(disassembly_);
    horizontal->addWidget(breakpoints_);
    horizontal->setStretchFactor(1, 2);
    layout->addWidget(horizontal, 1);
    auto *breakControls = new QHBoxLayout;
    auto *breakAddress = new QLineEdit(this);
    breakAddress->setObjectName("debuggerBreakpointAddress");
    breakAddress->setPlaceholderText("Address or bank:address ($hex, 0xhex, decimal, or symbol)");
    auto *add = new QPushButton("Add Breakpoint", this);
    connect(add, &QPushButton::clicked, this,
            [this, breakAddress] { submitCommand("break " + breakAddress->text()); });
    auto *clear = new QPushButton("Clear Breakpoints", this);
    connect(clear, &QPushButton::clicked, this, [this] { submitCommand("delete all"); });
    breakControls->addWidget(breakAddress, 1);
    breakControls->addWidget(add);
    breakControls->addWidget(clear);
    layout->addLayout(breakControls);
    output_ = readout("debuggerOutput", this);
    output_->document()->setMaximumBlockCount(2000);
    output_->setMaximumHeight(220);
    layout->addWidget(output_);
    input_ = new CommandEdit(this);
    input_->setObjectName("debuggerCommand");
    input_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    input_->setPlaceholderText("Debugger command (help lists commands)");
    layout->addWidget(input_);
    connect(input_, &QLineEdit::returnPressed, this, [this] {
        const QString command = input_->text();
        input_->clear();
        submitCommand(command);
    });
    resize(940, 620);
    updateState({}, {}, {}, false, false, false);
}

void DebuggerWindow::setHandlers(std::function<void(const QString &)> command,
                                 std::function<bool(const QString &)> symbols,
                                 std::function<void()> closed) {
    commandHandler_ = std::move(command);
    symbolsHandler_ = std::move(symbols);
    closedHandler_ = std::move(closed);
}

void DebuggerWindow::updateState(const QString &registers, const QString &disassembly,
                                 const QString &breakpoints, bool loaded, bool running,
                                 bool canBackstep) {
    registers_->setPlainText(registers);
    disassembly_->setPlainText(disassembly);
    breakpoints_->setPlainText(breakpoints);
    state_->setText(!loaded   ? "No cartridge loaded."
                    : running ? "Running. Break stops at the next instruction boundary."
                              : "Paused at an instruction boundary.");
    continue_->setEnabled(loaded && !running);
    step_->setEnabled(loaded && !running);
    next_->setEnabled(loaded && !running);
    finish_->setEnabled(loaded && !running);
    backstep_->setEnabled(loaded && !running && canBackstep);
}

void DebuggerWindow::appendOutput(const QString &text) {
    output_->appendPlainText(text);
}

void DebuggerWindow::submitCommand(const QString &text) {
    const QString command = text.trimmed();
    if (command.isEmpty())
        return;
    static_cast<CommandEdit *>(input_)->remember(command);
    appendOutput("> " + command);
    if (commandHandler_)
        commandHandler_(command);
}

bool DebuggerWindow::loadSymbols(const QString &path) {
    return symbolsHandler_ && symbolsHandler_(path);
}

QString DebuggerWindow::outputText() const {
    return output_->toPlainText();
}

void DebuggerWindow::done(int result) {
    if (closing_)
        return;
    QScopedValueRollback<bool> closing(closing_, true);
    // Dialog rejection (including Escape) can bypass closeEvent. Cancel host
    // execution before the window hides, for every dialog completion path.
    if (closedHandler_)
        closedHandler_();
    QDialog::done(result);
}
