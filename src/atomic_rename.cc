#include "atomic_rename.hh"

#include <cerrno>

#if defined(__APPLE__)
#include <stdio.h>  // renamex_np, RENAME_EXCL
#elif defined(__linux__)
#include <fcntl.h>
#include <sys/syscall.h>
#include <unistd.h>
#ifndef RENAME_NOREPLACE
#define RENAME_NOREPLACE (1 << 0)
#endif
#endif

namespace fs = std::filesystem;

bool rename_no_replace(const fs::path& from, const fs::path& to, std::error_code& ec)
{
  ec.clear();

  int rc = -1;
  bool tried = false;
#if defined(__APPLE__) && defined(RENAME_EXCL)
  tried = true;
  rc = ::renamex_np(from.c_str(), to.c_str(), RENAME_EXCL);
#elif defined(__linux__) && defined(SYS_renameat2)
  // glibc before 2.28 has no wrapper, so the system call is made directly.
  tried = true;
  rc = static_cast<int>(::syscall(SYS_renameat2, AT_FDCWD, from.c_str(), AT_FDCWD, to.c_str(),
                                  RENAME_NOREPLACE));
#endif
  if (tried) {
    if (rc == 0) {
      return true;
    }
    const int error = errno;
    if (error == EEXIST || error == ENOTEMPTY) {
      ec = std::make_error_code(std::errc::file_exists);
      return true;
    }
    if (error != ENOSYS && error != EINVAL && error != ENOTSUP) {
      ec = std::error_code(error, std::generic_category());
      return true;
    }
    // The call or the file system does not support it: fall through to the fallback.
  }

  if (fs::symlink_status(to, ec).type() != fs::file_type::not_found) {
    ec = std::make_error_code(std::errc::file_exists);
    return false;
  }
  ec.clear();
  fs::rename(from, to, ec);
  return false;
}

const char* rename_no_replace_mechanism()
{
#if defined(__APPLE__) && defined(RENAME_EXCL)
  return "renamex_np(RENAME_EXCL)";
#elif defined(__linux__) && defined(SYS_renameat2)
  return "renameat2(RENAME_NOREPLACE)";
#else
  return "check-then-rename (fallback)";
#endif
}
