#pragma once

#include <filesystem>

namespace vs {

// Installs an unhandled-exception filter that writes a small minidump (thread stacks and
// module list only - MiniDumpNormal, no full process memory, so no audio buffers) to
// `directory`, then lets Windows Error Reporting continue.
void installCrashHandler(const std::filesystem::path& directory);

} // namespace vs
