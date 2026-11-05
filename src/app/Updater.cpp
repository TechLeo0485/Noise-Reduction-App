#include "app/Updater.h"

#include "app/EngineController.h"
#include "diagnostics/Logger.h"

#include <QDir>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>

namespace vs {

namespace {

constexpr int kStartDelayMs = 8000;      // the network may not be up yet right after signing in
constexpr int kApiTimeoutMs = 15000;
constexpr int kDownloadStallMs = 60000;  // no data for this long: give up

QString apiUrl(const QString& path) {
    return QStringLiteral("https://api.github.com/repos/%1/%2").arg(QLatin1String(kUpdateRepository), path);
}

QString releasePage(const QString& version) {
    return QStringLiteral("https://github.com/%1/releases/tag/v%2").arg(QLatin1String(kUpdateRepository), version);
}

// Downloads go to %TEMP%\VoiceStudio-update; whatever is left there from an earlier update is removed at start.
QString downloadDirectory() {
    return QDir::temp().filePath(QStringLiteral("VoiceStudio-update"));
}

} // namespace

Updater::Updater(EngineController& controller, QObject* parent) : QObject(parent), controller_(controller) {}

Updater::~Updater() {
    cancelDownload();
}

QString Updater::currentVersion() const {
    return QStringLiteral(VOICESTUDIO_VERSION);
}

bool Updater::checkAtStart() const {
    return controller_.settings().checkForUpdates;
}

void Updater::setCheckAtStart(bool enabled) {
    if (enabled != checkAtStart()) {
        controller_.changeSettings([enabled](AppSettings& s) { s.checkForUpdates = enabled; });
        emit changed();
    }
}

QNetworkReply* Updater::get(const QString& url, int timeoutMs) {
    QNetworkRequest request{QUrl(url)};
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("VoiceStudio/" VOICESTUDIO_VERSION));
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    request.setTransferTimeout(timeoutMs);
    return network_.get(request);
}

void Updater::setState(const QString& state, const QString& error) {
    state_ = state;
    error_ = error;
    emit changed();
}

void Updater::start() {
    QDir(downloadDirectory()).removeRecursively();

    const AppSettings& s = controller_.settings();
    const QString current = currentVersion();
    const QString last = QString::fromStdString(s.lastRunVersion);
    if (!last.isEmpty() && compareVersions(current, last) > 0) {
        whatsNewUrl_ = releasePage(current);
        if (QString::fromStdString(s.updateNotesVersion) == current && !s.updateNotes.empty()) {
            whatsNewNotes_ = QString::fromStdString(s.updateNotes);
            whatsNewVisible_ = true;
            emit changed();
        } else {
            fetchWhatsNew(current); // installed by hand: ask GitHub for this version's notes
        }
        logInfo("Updated from " + last.toStdString() + " to " + current.toStdString());
    }
    if (last != current || !s.updateNotes.empty()) {
        controller_.changeSettings([current](AppSettings& a) {
            a.lastRunVersion = current.toStdString();
            a.updateNotesVersion.clear();
            a.updateNotes.clear();
        });
    }

    if (checkAtStart()) {
        QTimer::singleShot(kStartDelayMs, this, [this] { check(false); });
    }
}

void Updater::fetchWhatsNew(const QString& version) {
    QNetworkReply* reply = get(apiUrl(QStringLiteral("releases/tags/v") + version), kApiTimeoutMs);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        ReleaseInfo info;
        if (reply->error() == QNetworkReply::NoError && parseRelease(reply->readAll(), false, info)) {
            whatsNewNotes_ = info.notes;
            if (!info.pageUrl.isEmpty()) {
                whatsNewUrl_ = info.pageUrl;
            }
        }
        // Without notes (offline, or a release without description) the dialog still says what was installed.
        whatsNewVisible_ = true;
        emit changed();
    });
}

void Updater::checkNow() {
    check(true);
}

void Updater::check(bool manual) {
    if (state_ == QLatin1String("checking") || state_ == QLatin1String("downloading") ||
        state_ == QLatin1String("installing")) {
        return;
    }
    setState(QStringLiteral("checking"));
    QNetworkReply* reply = get(apiUrl(QStringLiteral("releases/latest")), kApiTimeoutMs);
    connect(reply, &QNetworkReply::finished, this, [this, reply, manual] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 404) {
            logInfo("Update check: no release published yet");
            setState(QStringLiteral("upToDate"));
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            logInfo("Update check failed: " + reply->errorString().toStdString());
            if (manual) {
                setState(QStringLiteral("error"), QStringLiteral("Could not reach GitHub. Check the internet connection."));
            } else {
                setState(QStringLiteral("idle")); // quietly try again next start
            }
            return;
        }
        ReleaseInfo info;
        QString why;
        if (!parseRelease(reply->readAll(), true, info, &why)) {
            logWarning("Update check: the latest release cannot be used: " + why.toStdString());
            setState(manual ? QStringLiteral("error") : QStringLiteral("idle"), why);
            return;
        }
        if (compareVersions(info.version, currentVersion()) <= 0) {
            logInfo("Update check: up to date (latest release " + info.version.toStdString() + ")");
            setState(QStringLiteral("upToDate"));
            return;
        }
        latest_ = info;
        const bool skipped = !manual && info.version.toStdString() == controller_.settings().skippedVersion;
        logInfo("Update available: " + info.version.toStdString() + (skipped ? " (skipped by the user)" : ""));
        promptVisible_ = !skipped;
        setState(QStringLiteral("available"));
        if (!skipped && !manual) {
            emit updateFound(info.version);
        }
    });
}

void Updater::showPrompt() {
    if (state_ == QLatin1String("available") || state_ == QLatin1String("downloading")) {
        promptVisible_ = true;
        emit changed();
    }
}

void Updater::later() {
    promptVisible_ = false;
    emit changed();
}

void Updater::skipVersion() {
    const std::string version = latest_.version.toStdString();
    controller_.changeSettings([version](AppSettings& s) { s.skippedVersion = version; });
    promptVisible_ = false;
    emit changed();
}

void Updater::closeWhatsNew() {
    whatsNewVisible_ = false;
    emit changed();
}

void Updater::install() {
    if (state_ != QLatin1String("available") || latest_.assetUrl.isEmpty()) {
        return;
    }
    QDir().mkpath(downloadDirectory());
    file_.setFileName(QDir(downloadDirectory()).filePath(latest_.assetName + QStringLiteral(".part")));
    if (!file_.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setState(QStringLiteral("available"), QStringLiteral("Cannot save the download: ") + file_.errorString());
        return;
    }
    hash_.reset();
    progress_ = 0.0;
    setState(QStringLiteral("downloading"));
    logInfo("Downloading " + latest_.assetUrl.toStdString());

    download_ = get(latest_.assetUrl, kDownloadStallMs);
    connect(download_, &QNetworkReply::readyRead, this, [this] {
        const QByteArray chunk = download_->readAll();
        hash_.addData(chunk);
        file_.write(chunk);
    });
    connect(download_, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
        const qint64 size = total > 0 ? total : latest_.assetSize;
        progress_ = size > 0 ? static_cast<double>(received) / static_cast<double>(size) : 0.0;
        emit changed();
    });
    connect(download_, &QNetworkReply::finished, this, &Updater::downloadFinished);
}

void Updater::cancelDownload() {
    if (download_) {
        download_->disconnect(this);
        download_->abort();
        download_->deleteLater();
        download_.clear();
        file_.close();
        file_.remove();
        if (state_ == QLatin1String("downloading")) {
            setState(QStringLiteral("available"));
        }
    }
}

void Updater::downloadFinished() {
    QNetworkReply* reply = download_;
    download_.clear();
    reply->deleteLater();
    if (reply->error() == QNetworkReply::NoError) {
        const QByteArray rest = reply->readAll();
        hash_.addData(rest);
        file_.write(rest);
    }
    const qint64 size = file_.size();
    file_.close();
    if (reply->error() != QNetworkReply::NoError) {
        file_.remove();
        logWarning("Update download failed: " + reply->errorString().toStdString());
        setState(QStringLiteral("available"), QStringLiteral("The download failed. Try again later."));
        return;
    }
    // Only the exact file GitHub has on record is run.
    if ((latest_.assetSize > 0 && size != latest_.assetSize) || hash_.result() != latest_.sha256) {
        file_.remove();
        logError("Update download does not match its SHA-256 from GitHub; deleted");
        setState(QStringLiteral("available"), QStringLiteral("The download was damaged and was deleted. Try again."));
        return;
    }
    const QString path = QDir(downloadDirectory()).filePath(latest_.assetName);
    QFile::remove(path);
    if (!file_.rename(path)) {
        setState(QStringLiteral("available"), QStringLiteral("Cannot prepare the installer: ") + file_.errorString());
        return;
    }
    runInstaller(path);
}

void Updater::runInstaller(const QString& path) {
    // Kept for the first start of the new version ("what's new"), which may have no internet.
    const std::string version = latest_.version.toStdString();
    const std::string notes = latest_.notes.toStdString();
    controller_.changeSettings([&](AppSettings& s) {
        s.updateNotesVersion = version;
        s.updateNotes = notes;
    });

    // /SILENT shows only setup's progress; setup closes VoiceStudio itself ("VoiceStudio.exe --quit") and, with
    // /RELAUNCH=1, starts it again when done. The "runas" prompt comes from setup's own manifest.
    const std::wstring file = QDir::toNativeSeparators(path).toStdWString();
    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOASYNC;
    info.lpVerb = L"open";
    info.lpFile = file.c_str();
    info.lpParameters = L"/SILENT /SUPPRESSMSGBOXES /NORESTART /RELAUNCH=1";
    info.nShow = SW_SHOWNORMAL;
    if (ShellExecuteExW(&info)) {
        logInfo("Installer started: " + path.toStdString());
        promptVisible_ = true;
        setState(QStringLiteral("installing"));
        return;
    }
    const DWORD code = GetLastError();
    controller_.changeSettings([](AppSettings& s) {
        s.updateNotesVersion.clear();
        s.updateNotes.clear();
    });
    if (code == ERROR_CANCELLED) {
        setState(QStringLiteral("available"),
                 QStringLiteral("The update needs administrator permission. Click Update now and choose Yes."));
    } else {
        logError("Starting the installer failed (error " + std::to_string(code) + ")");
        setState(QStringLiteral("available"), QStringLiteral("The installer could not be started."));
    }
}

} // namespace vs
