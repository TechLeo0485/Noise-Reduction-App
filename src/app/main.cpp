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

namespace {
constexpr const wchar_t* kInstanceMutex = L"Local\\VoiceStudio.SingleInstance";
constexpr const wchar_t* kActivateEvent = L"Local\\VoiceStudio.Activate"; // "show your window" from a second launch
} // namespace

int main(int argc, char* argv[]) {
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
            exitCode = QApplication::exec();
        }
    }

    if (activateEvent != nullptr) {
        CloseHandle(activateEvent);
    }
    if (instanceMutex != nullptr) {
        ReleaseMutex(instanceMutex);
        CloseHandle(instanceMutex);
    }
    return exitCode;
}
