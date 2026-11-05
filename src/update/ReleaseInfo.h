#pragma once

#include <QByteArray>
#include <QString>

namespace vs {

// Updates come from the project's GitHub releases: a release tagged "v1.2.3" (not a draft or pre-release) with
// the installer "VoiceStudio-1.2.3-setup.exe" attached. Its description (Markdown) is shown as "what's new".
inline constexpr const char* kUpdateRepository = "TechLeo0485/Noise-Reduction-App";

struct ReleaseInfo {
    QString version;    // "1.2.3" (tag without the leading v)
    QString notes;      // release description, Markdown
    QString pageUrl;    // the release on github.com
    QString assetName;  // VoiceStudio-<version>-setup.exe
    QString assetUrl;   // download URL of the installer
    qint64 assetSize = 0;
    QByteArray sha256;  // 32 bytes, from the asset's "digest" (GitHub computes it on upload)
};

// Compares "1.2.3" style versions (a leading "v" and missing parts allowed: "v1.2" == "1.2.0").
// Returns <0, 0 or >0. Unparsable parts count as 0.
[[nodiscard]] int compareVersions(const QString& a, const QString& b);

// Parses one release of the GitHub REST API (GET /repos/{repo}/releases/latest or /releases/tags/{tag}).
// `needInstaller`: the release must carry the installer with a SHA-256 digest, or it is rejected (an installer
// that cannot be verified is never run). False with `error` set if the release cannot be used.
bool parseRelease(const QByteArray& json, bool needInstaller, ReleaseInfo& out, QString* error = nullptr);

} // namespace vs
