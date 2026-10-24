#pragma once

// A useful failure message for tests that compare the bytes of two files: the sizes, the offset
// of the first differing byte, how many bytes differ, and 32 bytes of hexadecimal context from each
// file around the first difference (16 bytes before it and 16 from it on).

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <sstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

// "" if the two byte strings are equal, otherwise a multi-line description of how they differ.
inline std::string describe_byte_difference(const std::string& actual, const std::string& expected,
                                            const std::string& actual_name = "actual",
                                            const std::string& expected_name = "expected")
{
  if (actual == expected) {
    return "";
  }
  const std::size_t common = std::min(actual.size(), expected.size());
  std::size_t first = common;  // the first differing offset (the shorter size if one is a prefix)
  std::size_t differing = 0;
  for (std::size_t i = 0; i < common; ++i) {
    if (actual[i] != expected[i]) {
      if (differing == 0) {
        first = i;
      }
      ++differing;
    }
  }

  auto hex = [&](const std::string& bytes) {
    const std::size_t from = first >= 16 ? first - 16 : 0;
    const std::size_t to = std::min(bytes.size(), first + 16);
    std::ostringstream out;
    char buffer[8];
    std::snprintf(buffer, sizeof buffer, "%zu", from);
    out << "offset " << from << ":";
    for (std::size_t i = from; i < to; ++i) {
      std::snprintf(buffer, sizeof buffer, " %02x%s", static_cast<unsigned>(static_cast<unsigned char>(bytes[i])),
                    i == first ? "<" : "");
      out << buffer;
    }
    return out.str();
  };

  std::ostringstream out;
  out << "bytes differ: " << actual_name << " is " << actual.size() << " bytes, " << expected_name << " is "
      << expected.size() << " bytes; first difference at offset " << first << "; " << differing
      << " of the first " << common << " bytes differ"
      << (actual.size() != expected.size() ? " (and the sizes differ)" : "") << "\n"
      << "  " << actual_name << " " << hex(actual) << "\n"
      << "  " << expected_name << " " << hex(expected) << "   ('<' marks the first differing byte)";
  return out.str();
}

#define CHECK_SAME_BYTES(actual, expected)                                                    \
  do {                                                                                        \
    const std::string vikshep_report = describe_byte_difference((actual), (expected), #actual, #expected); \
    INFO(vikshep_report);                                                                     \
    CHECK(vikshep_report.empty());                                                            \
  } while (0)
