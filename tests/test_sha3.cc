#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "sha3_256.hh"
#include "cli_helpers.hh"

namespace {

std::string hex_of(const std::string& data)
{
  Sha3_256 hash;
  hash.update(data.data(), data.size());
  const auto digest = hash.finish();
  return to_hex(digest.data(), digest.size());
}

}  // namespace

// Official SHA3-256 vectors (FIPS 202 / NIST examples); the same values come out of
// Python's hashlib.sha3_256.
TEST_CASE("SHA3-256 of the empty string", "[sha3]")
{
  CHECK(hex_of("") == "a7ffc6f8bf1ed76651c14756a061d662f580ff4de43b49fa82d80a4b80f8434a");
}

TEST_CASE("SHA3-256 of abc", "[sha3]")
{
  CHECK(hex_of("abc") == "3a985da74fe225b2045c172d6bd390bd855f086e3e9d525b46bfe24511431532");
}

TEST_CASE("SHA3-256 of the 448-bit NIST message", "[sha3]")
{
  CHECK(hex_of("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
        "41c0dba2a9d6240849100376a8235e2c82e1b9998a999e21db32dd97496d3376");
}

TEST_CASE("SHA3-256 of one million times a", "[sha3]")
{
  CHECK(hex_of(std::string(1000000, 'a')) ==
        "5c8875ae474a3634ba4fd55ec85bffd661f32aca75c6d699d0cdcb6c115891c1");
}

TEST_CASE("updating in pieces gives the same hash as in one go", "[sha3]")
{
  std::string data(100000, '\0');
  for (std::size_t i = 0; i < data.size(); ++i) {
    data[i] = static_cast<char>(i * 7 + i / 251);
  }
  Sha3_256 pieces;
  std::size_t pos = 0;
  for (std::size_t step : {1, 135, 136, 137, 1000, 4096}) {  // 136 is the SHA3-256 block size
    for (int i = 0; i < 5 && pos < data.size(); ++i) {
      const std::size_t n = std::min(step, data.size() - pos);
      pieces.update(data.data() + pos, n);
      pos += n;
    }
  }
  pieces.update(data.data() + pos, data.size() - pos);
  const auto digest = pieces.finish();
  CHECK(to_hex(digest.data(), digest.size()) == hex_of(data));
}

TEST_CASE("the OID is the first 14 bytes as 28 lowercase hex characters", "[sha3]")
{
  const std::string oid = oid_of_bytes("abc");
  CHECK(oid == "3a985da74fe225b2045c172d6bd3");
  CHECK(oid.size() == 28);
  CHECK(oid_of_bytes(std::string("abc")) == oid);
  for (char c : oid) {
    CHECK(((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')));
  }
}

TEST_CASE("the hash of a file equals the hash of its bytes", "[sha3]")
{
  const std::string data(3 * (1 << 20) + 17, 'x');  // more than one read buffer
  const cli::TempFile file(data, ".bin");
  const auto digest = sha3_256_of_file(file.path());
  CHECK(to_hex(digest.data(), digest.size()) == hex_of(data));
  CHECK_THROWS(sha3_256_of_file(file.path() + ".missing"));
}
