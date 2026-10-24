#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

extern "C" {
#include "sha3.h"  // vendored tiny_sha3
}

// SHA3-256 (FIPS 202), incremental.
class Sha3_256 {
 public:
  Sha3_256() { sha3_init(&ctx_, 32); }

  void update(const void* data, std::size_t size) { sha3_update(&ctx_, data, size); }

  // Finishes the hash. The object must not be used afterwards.
  std::array<std::uint8_t, 32> finish()
  {
    std::array<std::uint8_t, 32> digest;
    sha3_final(digest.data(), &ctx_);
    return digest;
  }

 private:
  sha3_ctx_t ctx_;
};

// The digest as lowercase hexadecimal.
std::string to_hex(const std::uint8_t* bytes, std::size_t size);

// The OID of a digest (docs/HITS.md, section 6): the first 14 bytes of the
// SHA3-256 digest as 28 lowercase hexadecimal characters. Never changes.
std::string oid_of(const std::array<std::uint8_t, 32>& digest);

// The OID of a byte string.
std::string oid_of_bytes(const void* data, std::size_t size);
std::string oid_of_bytes(const std::string& bytes);

// SHA3-256 of a file, streamed. Throws std::runtime_error if it cannot be read.
std::array<std::uint8_t, 32> sha3_256_of_file(const std::string& path);
