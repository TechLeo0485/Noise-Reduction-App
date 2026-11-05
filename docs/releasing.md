# Publishing a release (and how the in-app update works)

VoiceStudio updates itself from the GitHub releases of
[TechLeo0485/Noise-Reduction-App](https://github.com/TechLeo0485/Noise-Reduction-App/releases)
(`kUpdateRepository` in `src/update/ReleaseInfo.h`).

Anyone can download the newest installer from
<https://github.com/TechLeo0485/Noise-Reduction-App/releases/latest>.

## Steps (GitHub builds it)

`.github/workflows/release.yml` builds, tests and packages VoiceStudio on GitHub's Windows machines and
publishes the release when a version tag is pushed:

1. Raise the version in `CMakeLists.txt` (`project(VoiceStudio VERSION 1.0.1 ...)`). The app only offers a release
   whose version is **higher** than the one it runs, compared number by number (0.10.0 > 0.9.0).
2. Write `release-notes/v1.0.1.md`: what changed, for users, in Markdown. The app shows exactly this text in
   "VoiceStudio X is available" and, after the update, in "VoiceStudio is updated to X".
3. Commit, push, then tag and push the tag:

   ```powershell
   git tag v1.0.1
   git push origin v1.0.1
   ```

4. GitHub → **Actions → Release** shows the build (about 20-30 minutes). When it is green, the release
   "VoiceStudio 1.0.1" exists with `VoiceStudio-1.0.1-setup.exe` attached, and installed copies offer it.

The workflow stops before building if the tag does not match the version in CMakeLists.txt, and nothing is
published if a unit test fails. **Actions → Release → Run workflow** makes a test installer without a release
(download it from the run's artifacts). The VoiceStudio Virtual Microphone driver is only included when its
signed package is committed in `installer/driver-package/` (from `build/driver-package/` after
`scripts\build-driver.ps1`); otherwise setup offers VB-CABLE.

## Steps (by hand, without GitHub Actions)

1. Raise the version in `CMakeLists.txt`, as above.
2. Build the installer: `scripts\package.ps1` (or `-SkipBuild` after a Release build). Result:
   `dist\VoiceStudio-<version>-setup.exe`.
3. On GitHub: **Releases → Draft a new release**
   * **Tag**: `v<version>`, e.g. `v1.0.0` (must match the version in CMakeLists.txt).
   * **Title**: anything, e.g. `VoiceStudio 1.0.0`.
   * **Description**: what changed, in Markdown. The app shows exactly this text in "VoiceStudio X is available"
     and, after the update, in "VoiceStudio is updated to X". Short bullet points read best.
   * **Attach** `VoiceStudio-<version>-setup.exe` (the name must have this form).
   * Leave **Set as a pre-release** off and click **Publish release**. Drafts and pre-releases are never offered.

   With the GitHub CLI instead:

   ```powershell
   gh release create v1.0.0 dist\VoiceStudio-1.0.0-setup.exe --title "VoiceStudio 1.0.0" --notes-file notes.md
   ```

Running copies see the release the next time they start (about 8 s after start), or at once with
**Setup → Updates → Check now**.

## What the app does

1. `GET https://api.github.com/repos/<repo>/releases/latest` (no account; GitHub allows 60 requests an hour per IP).
2. Newer than the running version, and not skipped by the user: the dialog shows the release description.
3. **Update now**: downloads the attached installer to `%TEMP%\VoiceStudio-update`, then checks its size and its
   SHA-256 against the `digest` GitHub computed when the file was uploaded. A mismatch deletes the file; a release
   without a digest is not offered. (GitHub adds the digest to every asset uploaded since mid-2025.)
4. Runs `VoiceStudio-<version>-setup.exe /SILENT /SUPPRESSMSGBOXES /NORESTART /RELAUNCH=1`. Windows asks for
   administrator rights; setup closes VoiceStudio (`--quit`, settings saved), installs with the previous choices,
   and starts VoiceStudio again as the user.
5. The new version notices that `lastRunVersion` in the settings is older and shows the release notes (saved
   before the update, or fetched for the tag `v<version>` if the user installed by hand).

To fix a bad release, publish a new one with a higher version rather than swapping the attached file: copies that
read the old digest while their download was running would reject the replaced file.
