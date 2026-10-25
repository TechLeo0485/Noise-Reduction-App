#include "app/CrashHandler.h"
#include "app/EngineController.h"
#include "app/TrayIcon.h"
#include "diagnostics/Logger.h"

#include <QApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QWinEventNotifier>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstring>

namespace {
constexpr const wchar_t* kInstanceMutex = L"Local\\VoiceStudio.SingleInstance";
constexpr const wchar_t* kActivateEvent = L"Local\\VoiceStudio.Activate"; // "show your window" from a second launch
constexpr const wchar_t* kQuitEvent = L"Local\\VoiceStudio.Quit";         // "quit" from VoiceStudio.exe --quit (setup)

// VoiceStudio.exe --quit: asks the running VoiceStudio to quit (settings are saved as on "Quit" in the tray menu)
// and waits up to 15 s for it to end. Used by setup and uninstall, which cannot replace or delete files in use.
// Exit code 0: not running (any more); 1: still running.
int quitRunningInstance() {
    HANDLE running = OpenMutexW(SYNCHRONIZE, FALSE, kInstanceMutex);
    if (running == nullptr) {
        return 0;
    }
    if (HANDLE quit = OpenEventW(EVENT_MODIFY_STATE, FALSE, kQuitEvent); quit != nullptr) {
        SetEvent(quit);
        CloseHandle(quit);
    }
    // The instance holds the mutex until it exits (released at the end of main, or abandoned if it is killed).
    const DWORD wait = WaitForSingleObject(running, 15000);
    if (wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED) {
        ReleaseMutex(running);
    }
    CloseHandle(running);
    return wait == WAIT_TIMEOUT ? 1 : 0;
}
} // namespace

int main(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--quit") == 0) {
            return quitRunningInstance();
        }
    }
    vs::installCrashHandler(vs::appDataDirectory() / L"crashdumps");

    // Single instance: two engines fighting over the same endpoints helps nobody. A second launch (start
    // menu, desktop shortcut) brings the running VoiceStudio - possibly hidden in the tray - to the front.
    HANDLE instanceMutex = CreateMutexW(nullptr, TRUE, kInstanceMutex);
    if (instanceMutex != nullptr && GetLastError() == ERROR_ALREADY_EXISTS) {
        HANDLE activate = OpenEventW(EVENT_MODIFY_STATE, FALSE, kActivateEvent);
        if (activate != nullptr) {
            AllowSetForegroundWindow(ASFW_ANY);
            SetEvent(activate);
            CloseHandle(activate);
        } else {
            MessageBoxW(nullptr, L"VoiceStudio is already running.", L"VoiceStudio", MB_OK | MB_ICONINFORMATION);
        }
        CloseHandle(instanceMutex);
        return 0;
    }
    HANDLE activateEvent = CreateEventW(nullptr, FALSE, FALSE, kActivateEvent);
    HANDLE quitEvent = CreateEventW(nullptr, FALSE, FALSE, kQuitEvent);

    // QApplication rather than QGuiApplication: the notification-area icon uses Qt Widgets.
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("VoiceStudio"));
    QApplication::setApplicationName(QStringLiteral("VoiceStudio"));
    QApplication::setApplicationVersion(QStringLiteral(VOICESTUDIO_VERSION));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/voicestudio.ico")));
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    // --minimized: started with Windows; stay in the tray until the user opens the window.
    const bool startMinimized = QApplication::arguments().contains(QStringLiteral("--minimized"));

    int exitCode = 0;
    {
        vs::EngineController controller;
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &controller);
        QObject::connect(
            &engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(-1); },
            Qt::QueuedConnection);
        engine.loadFromModule("VoiceStudio", "Main");

        auto* window = engine.rootObjects().isEmpty() ? nullptr : qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        if (window == nullptr) {
            exitCode = -1;
        } else {
            vs::TrayIcon tray(controller, *window);
            controller.setTrayAvailable(tray.available());
            // With a tray icon, closing the window only hides it; "Quit" in the tray menu ends the app.
            QApplication::setQuitOnLastWindowClosed(!tray.available());
            if (QApplication::arguments().contains(QStringLiteral("--guide"))) { // Start menu "User Guide"
                QMetaObject::invokeMethod(window, "showPage", Q_ARG(QVariant, QStringLiteral("help")));
            }
            if (!startMinimized || !tray.available()) {
                tray.showWindow();
            }
            QWinEventNotifier activation(activateEvent);
            QObject::connect(&activation, &QWinEventNotifier::activated, &tray, [&tray] { tray.showWindow(); });
            QWinEventNotifier quitRequest(quitEvent);
            QObject::connect(&quitRequest, &QWinEventNotifier::activated, &app, [] { QCoreApplication::quit(); });
            exitCode = QApplication::exec();
        }
    }

    if (activateEvent != nullptr) {
        CloseHandle(activateEvent);
    }
    if (quitEvent != nullptr) {
        CloseHandle(quitEvent);
    }
    if (instanceMutex != nullptr) {
        ReleaseMutex(instanceMutex);
        CloseHandle(instanceMutex);
    }
    return exitCode;
}
