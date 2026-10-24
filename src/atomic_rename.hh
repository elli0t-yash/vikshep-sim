#pragma once

#include <filesystem>
#include <system_error>

// Renames `from` to `to` and fails if `to` exists, in one atomic step, where the operating
// system has such a call (renamex_np with RENAME_EXCL on macOS, renameat2 with
// RENAME_NOREPLACE on Linux). Elsewhere, or if the file system does not support it, it
// falls back to checking that `to` is absent and then calling the ordinary rename; that
// leaves a window of a few microseconds in which another writer could create `to`.
//
// On failure `ec` is set (std::errc::file_exists if `to` exists) and nothing was moved.
// Returns true if the atomic call was used, false if the fallback was used.
bool rename_no_replace(const std::filesystem::path& from, const std::filesystem::path& to,
                       std::error_code& ec);

// Which mechanism rename_no_replace() uses on this platform, for logs and tests:
// "renamex_np(RENAME_EXCL)", "renameat2(RENAME_NOREPLACE)" or "check-then-rename (fallback)".
const char* rename_no_replace_mechanism();
