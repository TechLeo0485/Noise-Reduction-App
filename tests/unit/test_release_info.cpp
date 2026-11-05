#include "update/ReleaseInfo.h"

#include <gtest/gtest.h>

using vs::ReleaseInfo;

namespace {

// Shaped like GET /repos/{repo}/releases/latest (fields VoiceStudio does not read are left out).
QByteArray release(const char* tag, const char* assetName, const char* digest, bool prerelease = false) {
    return QStringLiteral(R"({
        "tag_name": "%1", "draft": false, "prerelease": %4,
        "html_url": "https://github.com/TechLeo0485/Noise-Reduction-App/releases/tag/%1",
        "body": "## What's new\r\n- Hotkeys\r\n",
        "assets": [
            { "name": "notes.txt", "size": 10, "browser_download_url": "https://example.invalid/notes.txt" },
            { "name": "%2", "size": 149934034, "digest": "%3",
              "browser_download_url": "https://github.com/TechLeo0485/Noise-Reduction-App/releases/download/%1/%2" }
        ]
    })")
        .arg(QLatin1String(tag), QLatin1String(assetName), QLatin1String(digest),
             prerelease ? QStringLiteral("true") : QStringLiteral("false"))
        .toUtf8();
}

constexpr const char* kDigest = "sha256:000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";

} // namespace

TEST(ReleaseInfo, ComparesVersions) {
    EXPECT_LT(vs::compareVersions("0.9.0", "0.10.0"), 0); // numeric, not text
    EXPECT_GT(vs::compareVersions("v1.0.0", "0.99.99"), 0);
    EXPECT_EQ(vs::compareVersions("v1.2", "1.2.0"), 0);
    EXPECT_EQ(vs::compareVersions("1.2.3-beta", "1.2.3"), 0);
    EXPECT_LT(vs::compareVersions("1.2.3", "1.2.4"), 0);
}

TEST(ReleaseInfo, ReadsTheInstallerAndItsChecksum) {
    ReleaseInfo r;
    QString error;
    ASSERT_TRUE(vs::parseRelease(release("v0.10.0", "VoiceStudio-0.10.0-setup.exe", kDigest), true, r, &error))
        << error.toStdString();
    EXPECT_EQ(r.version, "0.10.0");
    EXPECT_EQ(r.assetName, "VoiceStudio-0.10.0-setup.exe");
    EXPECT_TRUE(r.assetUrl.endsWith("/releases/download/v0.10.0/VoiceStudio-0.10.0-setup.exe"));
    EXPECT_EQ(r.assetSize, 149934034);
    ASSERT_EQ(r.sha256.size(), 32);
    EXPECT_EQ(static_cast<unsigned char>(r.sha256[31]), 0x1Fu);
    EXPECT_TRUE(r.notes.startsWith("## What's new"));
    EXPECT_TRUE(r.pageUrl.endsWith("/releases/tag/v0.10.0"));
}

TEST(ReleaseInfo, RejectsWhatCannotBeInstalledSafely) {
    ReleaseInfo r;
    QString error;
    EXPECT_FALSE(vs::parseRelease(release("v1.0.0", "VoiceStudio-1.0.0-setup.exe", ""), true, r, &error));
    EXPECT_FALSE(error.isEmpty()); // no checksum: never run
    EXPECT_FALSE(vs::parseRelease(release("v1.0.0", "VoiceStudio-portable.zip", kDigest), true, r));
    EXPECT_FALSE(vs::parseRelease(release("latest", "VoiceStudio-1.0.0-setup.exe", kDigest), true, r));
    EXPECT_FALSE(vs::parseRelease(release("v1.0.0", "VoiceStudio-1.0.0-setup.exe", kDigest, true), true, r));
    EXPECT_FALSE(vs::parseRelease("not json", true, r));
    // For "what's new" only the notes are needed.
    EXPECT_TRUE(vs::parseRelease(release("v1.0.0", "VoiceStudio-portable.zip", ""), false, r));
    EXPECT_EQ(r.version, "1.0.0");
}
