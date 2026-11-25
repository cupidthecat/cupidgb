/**
 * @file    huc3_alarm_windows.cpp
 * @brief   Per-user Windows Task Scheduler registrations for one-shot HuC3 alarms.
 * @author  cupidthecat
 * @date    2026-11-24
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

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <sddl.h>
#include <taskschd.h>

#include <QByteArray>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QLibraryInfo>
#include <QScopeGuard>
#include <QString>
#include <vector>

namespace huc3Alarm {
namespace {
class ComApartment {
  public:
    ComApartment() : result_(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {
    }

    ~ComApartment() {
        if (SUCCEEDED(result_))
            CoUninitialize();
    }

    bool valid() const {
        // Qt or another caller may already have initialized this thread as MTA.
        return SUCCEEDED(result_) || result_ == RPC_E_CHANGED_MODE;
    }

    ComApartment(const ComApartment &) = delete;
    ComApartment &operator=(const ComApartment &) = delete;

  private:
    HRESULT result_;
};

template <typename T> class ComPointer {
  public:
    ComPointer() = default;

    ~ComPointer() {
        if (value_)
            value_->Release();
    }

    T *get() const {
        return value_;
    }

    T *operator->() const {
        return value_;
    }

    T **put() {
        return &value_;
    }

    ComPointer(const ComPointer &) = delete;
    ComPointer &operator=(const ComPointer &) = delete;

  private:
    T *value_ = nullptr;
};

class AutomationString {
  public:
    explicit AutomationString(const QString &text)
        : value_(SysAllocStringLen(reinterpret_cast<const OLECHAR *>(text.utf16()),
                                   UINT(text.size()))) {
    }

    ~AutomationString() {
        SysFreeString(value_);
    }

    BSTR get() const {
        return value_;
    }

    explicit operator bool() const {
        return value_ != nullptr;
    }

    AutomationString(const AutomationString &) = delete;
    AutomationString &operator=(const AutomationString &) = delete;

  private:
    BSTR value_;
};

bool validHex(const QByteArray &value, qsizetype length) {
    if (value.size() != length)
        return false;
    for (const char digit : value) {
        if (!((digit >= '0' && digit <= '9') || (digit >= 'a' && digit <= 'f')))
            return false;
    }
    return true;
}

QString currentUserSid() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return {};
    const auto close = qScopeGuard([&] { CloseHandle(token); });
    DWORD bytes = 0;
    if (GetTokenInformation(token, TokenUser, nullptr, 0, &bytes) ||
        GetLastError() != ERROR_INSUFFICIENT_BUFFER || bytes < sizeof(TOKEN_USER))
        return {};
    std::vector<unsigned char> storage(bytes);
    if (!GetTokenInformation(token, TokenUser, storage.data(), bytes, &bytes))
        return {};
    const auto *user = reinterpret_cast<const TOKEN_USER *>(storage.data());
    LPWSTR text = nullptr;
    if (!ConvertSidToStringSidW(user->User.Sid, &text))
        return {};
    const auto release = qScopeGuard([&] { LocalFree(text); });
    return QString::fromWCharArray(text);
}

QString taskName(const QString &sid, const QByteArray &cartridgeId) {
    // Separate users without exposing account names or depending on their spelling.
    const auto userId = QCryptographicHash::hash(sid.toUtf8(), QCryptographicHash::Sha256).toHex();
    return QStringLiteral("CupidGB.HuC3.") + QString::fromLatin1(userId) + QLatin1Char('.') +
           QString::fromLatin1(cartridgeId);
}

bool hasDeployedRuntime(const QString &executable) {
    // Scheduler launches do not inherit a developer shell's DLL search path.
    // Require the exact Qt Core runtime already loaded by this executable.
    const auto library = QLibraryInfo::isDebugBuild() ? QStringLiteral("Qt6Cored.dll")
                                                      : QStringLiteral("Qt6Core.dll");
    const auto beside = QFileInfo(QFileInfo(executable).absoluteDir().filePath(library));
    const auto deployed = beside.canonicalFilePath();
    if (!beside.isFile() || deployed.isEmpty())
        return false;
    const auto module = GetModuleHandleW(reinterpret_cast<LPCWSTR>(library.utf16()));
    if (!module)
        return false;
    std::vector<wchar_t> buffer(32768);
    const DWORD length = GetModuleFileNameW(module, buffer.data(), DWORD(buffer.size()));
    if (!length || length >= buffer.size())
        return false;
    const auto loaded =
        QFileInfo(QString::fromWCharArray(buffer.data(), int(length))).canonicalFilePath();
    return !loaded.isEmpty() && deployed.compare(loaded, Qt::CaseInsensitive) == 0;
}

bool connectScheduler(ComPointer<ITaskService> &service, ComPointer<ITaskFolder> &folder) {
    if (FAILED(CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER,
                                IID_ITaskService, reinterpret_cast<void **>(service.put()))))
        return false;
    const VARIANT empty{};
    if (FAILED(service->Connect(empty, empty, empty, empty)))
        return false;
    const AutomationString root(QStringLiteral("\\"));
    return root && SUCCEEDED(service->GetFolder(root.get(), folder.put()));
}
} // namespace

bool armWindowsAlarm(const QByteArray &cartridgeId, const QByteArray &generation,
                     qint64 dueUtcSeconds) {
    if (!validHex(cartridgeId, 64) || !validHex(generation, 32) || dueUtcSeconds <= 0)
        return false;
    const auto due = QDateTime::fromSecsSinceEpoch(dueUtcSeconds).toUTC();
    const auto expires = due.addSecs(24 * 60 * 60);
    if (!due.isValid() || !expires.isValid() || due.date().year() < 1970 ||
        expires.date().year() > 9999)
        return false;
    const auto executable = QCoreApplication::applicationFilePath();
    const auto sid = currentUserSid();
    if (executable.isEmpty() || sid.isEmpty() || !hasDeployedRuntime(executable))
        return false;

    ComApartment apartment;
    if (!apartment.valid())
        return false;
    ComPointer<ITaskService> service;
    ComPointer<ITaskFolder> folder;
    if (!connectScheduler(service, folder))
        return false;
    ComPointer<ITaskDefinition> task;
    if (FAILED(service->NewTask(0, task.put())))
        return false;

    const AutomationString owner(sid);
    const AutomationString name(taskName(sid, cartridgeId));
    const AutomationString path(QDir::toNativeSeparators(executable));
    const AutomationString directory(
        QDir::toNativeSeparators(QFileInfo(executable).absolutePath()));
    const AutomationString arguments(QStringLiteral("--deliver-huc3-alarm ") +
                                     QString::fromLatin1(cartridgeId) + QLatin1Char(' ') +
                                     QString::fromLatin1(generation));
    const AutomationString start(due.toString(QStringLiteral("yyyy-MM-dd'T'HH:mm:ss'Z'")));
    const AutomationString end(expires.toString(QStringLiteral("yyyy-MM-dd'T'HH:mm:ss'Z'")));
    const AutomationString cleanup(QStringLiteral("PT1H"));
    if (!owner || !name || !path || !directory || !arguments || !start || !end || !cleanup)
        return false;

    ComPointer<IPrincipal> principal;
    if (FAILED(task->get_Principal(principal.put())) ||
        FAILED(principal->put_UserId(owner.get())) ||
        FAILED(principal->put_LogonType(TASK_LOGON_INTERACTIVE_TOKEN)) ||
        FAILED(principal->put_RunLevel(TASK_RUNLEVEL_LUA)))
        return false;
    ComPointer<ITaskSettings> settings;
    if (FAILED(task->get_Settings(settings.put())) || FAILED(settings->put_Enabled(VARIANT_TRUE)) ||
        FAILED(settings->put_AllowDemandStart(VARIANT_TRUE)) ||
        FAILED(settings->put_StartWhenAvailable(VARIANT_TRUE)) ||
        FAILED(settings->put_DisallowStartIfOnBatteries(VARIANT_FALSE)) ||
        FAILED(settings->put_StopIfGoingOnBatteries(VARIANT_FALSE)) ||
        FAILED(settings->put_RunOnlyIfIdle(VARIANT_FALSE)) ||
        FAILED(settings->put_RunOnlyIfNetworkAvailable(VARIANT_FALSE)) ||
        FAILED(settings->put_MultipleInstances(TASK_INSTANCES_PARALLEL)) ||
        FAILED(settings->put_DeleteExpiredTaskAfter(cleanup.get())))
        return false;

    ComPointer<ITriggerCollection> triggers;
    ComPointer<ITrigger> trigger;
    if (FAILED(task->get_Triggers(triggers.put())) ||
        FAILED(triggers->Create(TASK_TRIGGER_TIME, trigger.put())) ||
        FAILED(trigger->put_StartBoundary(start.get())) ||
        FAILED(trigger->put_EndBoundary(end.get())) || FAILED(trigger->put_Enabled(VARIANT_TRUE)))
        return false;

    ComPointer<IActionCollection> actions;
    ComPointer<IAction> action;
    ComPointer<IExecAction> execution;
    if (FAILED(task->get_Actions(actions.put())) ||
        FAILED(actions->Create(TASK_ACTION_EXEC, action.put())) ||
        FAILED(
            action->QueryInterface(IID_IExecAction, reinterpret_cast<void **>(execution.put()))) ||
        FAILED(execution->put_Path(path.get())) ||
        FAILED(execution->put_Arguments(arguments.get())) ||
        FAILED(execution->put_WorkingDirectory(directory.get())))
        return false;

    const VARIANT empty{};
    VARIANT user{};
    user.vt = VT_BSTR;
    user.bstrVal = owner.get(); // Borrowed for this synchronous registration call.
    ComPointer<IRegisteredTask> registered;
    // Updating the definition leaves an already-running delivery process alive.
    return SUCCEEDED(folder->RegisterTaskDefinition(name.get(), task.get(), TASK_CREATE_OR_UPDATE,
                                                    user, empty, TASK_LOGON_INTERACTIVE_TOKEN,
                                                    empty, registered.put()));
}

void disarmWindowsAlarm(const QByteArray &cartridgeId) {
    if (!validHex(cartridgeId, 64))
        return;
    const auto sid = currentUserSid();
    if (sid.isEmpty())
        return;
    ComApartment apartment;
    if (!apartment.valid())
        return;
    ComPointer<ITaskService> service;
    ComPointer<ITaskFolder> folder;
    const AutomationString name(taskName(sid, cartridgeId));
    if (!name || !connectScheduler(service, folder))
        return;
    // Delete only this user's exact cartridge definition. A running helper must
    // observe record cancellation itself; it is never stopped by this backend.
    folder->DeleteTask(name.get(), 0);
}
} // namespace huc3Alarm
