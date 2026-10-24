#include "app/CrashHandler.h"
#include "app/EngineController.h"
#include "diagnostics/Logger.h"

#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

int main(int argc, char* argv[]) {
    vs::installCrashHandler(vs::appDataDirectory() / L"crashdumps");

    QGuiApplication app(argc, argv);
    QGuiApplication::setOrganizationName(QStringLiteral("VoiceStudio"));
    QGuiApplication::setApplicationName(QStringLiteral("VoiceStudio"));
    QGuiApplication::setApplicationVersion(QStringLiteral(VOICESTUDIO_VERSION));
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    // Single instance: two engines fighting over the same endpoints helps nobody.
    HANDLE instanceMutex = CreateMutexW(nullptr, TRUE, L"Local\\VoiceStudio.SingleInstance");
    if (instanceMutex != nullptr && GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, L"VoiceStudio is already running.", L"VoiceStudio", MB_OK | MB_ICONINFORMATION);
        CloseHandle(instanceMutex);
        return 0;
    }

    int exitCode = 0;
    {
        vs::EngineController controller;
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("backend"), &controller);
        QObject::connect(
            &engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(-1); },
            Qt::QueuedConnection);
        engine.loadFromModule("VoiceStudio", "Main");
        exitCode = QGuiApplication::exec();
    }

    if (instanceMutex != nullptr) {
        ReleaseMutex(instanceMutex);
        CloseHandle(instanceMutex);
    }
    return exitCode;
}
