#include "app/CrashHandler.h"
#include "app/EngineController.h"
#include "app/GlobalHotkeys.h"
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
#include <dwmapi.h>

#include <cstring>

namespace {
constexpr const wchar_t* kInstanceMutex = L"Local\\VoiceStudio.SingleInstance";
constexpr const wchar_t* kActivateEvent = L"Local\\VoiceStudio.Activate"; // "show your window" from a second launch
constexpr const wchar_t* kQuitEvent = L"Local\\VoiceStudio.Quit";         // "quit" from VoiceStudio.exe --quit (setup)

// Title bar in the sidebar colour with light text and caption buttons, instead of the default white bar (Windows 11;
// Windows 10 applies only the dark mode and ignores the colour attributes). Colours from ui/themes/Theme.qml.
void styleTitleBar(QQuickWindow& window) {
    const auto hwnd = reinterpret_cast<HWND>(window.winId());
    const BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    const COLORREF caption = RGB(0x0A, 0x0C, 0x11); // Theme.sidebar
    const COLORREF text = RGB(0xEC, 0xEE, 0xF4);    // Theme.text
    const COLORREF border = RGB(0x23, 0x28, 0x36);  // Theme.border
    DwmSetWindowAttribute(hwnd, DWMWA_CAPTION_COLOR, &caption, sizeof(caption));
    DwmSetWindowAttribute(hwnd, DWMWA_TEXT_COLOR, &text, sizeof(text));
    DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &border, sizeof(border));
}

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
    // The .ico holds 16..256 px; the 1024 px PNG keeps the logo sharp on high-DPI screens (scripts/make-icons.ps1).
    QIcon appIcon(QStringLiteral(":/icons/voicestudio.ico"));
    appIcon.addFile(QStringLiteral(":/icons/voicestudio.png"));
    QApplication::setWindowIcon(appIcon);
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    // --minimized: started with Windows; stay in the tray until the user opens the window.
    const bool startMinimized = QApplication::arguments().contains(QStringLiteral("--minimized"));

    int exitCode = 0;
    {
        vs::EngineController controller;
        vs::GlobalHotkeys hotkeys(controller);
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &controller);
        engine.rootContext()->setContextProperty(QStringLiteral("hotkeys"), &hotkeys);
        QObject::connect(
            &engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(-1); },
            Qt::QueuedConnection);
        engine.loadFromModule("VoiceStudio", "Main");

        auto* window = engine.rootObjects().isEmpty() ? nullptr : qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        if (window == nullptr) {
            exitCode = -1;
        } else {
            styleTitleBar(*window);
            vs::TrayIcon tray(controller, *window);
            controller.setTrayAvailable(tray.available());
            // The "show" shortcut toggles: pressed while the window is in front, it hides it again (to the tray,
            // or minimised without one); otherwise it brings the window to the front.
            hotkeys.attach(*window, [&tray, window] {
                const bool inFront = window->isVisible() && window->isActive() &&
                                     !(window->windowState() & Qt::WindowMinimized);
                if (!inFront) {
                    tray.showWindow();
                } else if (tray.available()) {
                    window->hide();
                } else {
                    window->showMinimized();
                }
            });
            const auto showHints = [&] {
                tray.setShortcutHints(hotkeys.text(QStringLiteral("showWindow")), hotkeys.text(QStringLiteral("mute")));
            };
            QObject::connect(&hotkeys, &vs::GlobalHotkeys::changed, &tray, showHints);
            showHints();
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
