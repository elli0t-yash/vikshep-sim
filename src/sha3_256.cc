#include "sha3_256.hh"

#include <fstream>
#include <stdexcept>
#include <vector>

#include "errors.hh"

std::string to_hex(const std::uint8_t* bytes, std::size_t size)
{
  static const char digits[] = "0123456789abcdef";
  std::string out;
  out.reserve(size * 2);
  for (std::size_t i = 0; i < size; ++i) {
    out += digits[bytes[i] >> 4];
    out += digits[bytes[i] & 0x0F];
  }
  return out;
}

std::string oid_of(const std::array<std::uint8_t, 32>& digest)
{
  return to_hex(digest.data(), 14);
}

std::string oid_of_bytes(const void* data, std::size_t size)
{
  Sha3_256 hash;
  hash.update(data, size);
  return oid_of(hash.finish());
}

std::string oid_of_bytes(const std::string& bytes)
{
  return oid_of_bytes(bytes.data(), bytes.size());
}

std::array<std::uint8_t, 32> sha3_256_of_file(const std::string& path)
{
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    throw WriteError("cannot read the file back to verify its hash");
  }
  Sha3_256 hash;
  std::vector<char> buffer(1 << 20);
  while (in) {
    in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    hash.update(buffer.data(), static_cast<std::size_t>(in.gcount()));
  }
  if (in.bad()) {
    throw WriteError("error while reading the file back to verify its hash");
  }
  return hash.finish();
}
