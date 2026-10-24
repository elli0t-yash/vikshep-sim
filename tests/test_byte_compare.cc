#include <string>

#include <catch2/catch_test_macros.hpp>

#include "byte_compare.hh"

TEST_CASE("equal byte strings give an empty report", "[bytes]")
{
  CHECK(describe_byte_difference("abc", "abc").empty());
  CHECK(describe_byte_difference("", "").empty());
}

TEST_CASE("the report gives sizes, first offset, count and hex context", "[bytes]")
{
  std::string a(100, '\0');
  std::string b = a;
  b[40] = 0x7f;
  b[41] = 0x01;
  b[90] = 0x02;
  const std::string report = describe_byte_difference(a, b, "left", "right");
  INFO(report);
  CHECK(report.find("left is 100 bytes") != std::string::npos);
  CHECK(report.find("right is 100 bytes") != std::string::npos);
  CHECK(report.find("first difference at offset 40") != std::string::npos);
  CHECK(report.find("3 of the first 100 bytes differ") != std::string::npos);
  CHECK(report.find("offset 24:") != std::string::npos);  // 16 bytes of context before the first difference
  CHECK(report.find(" 00<") != std::string::npos);  // the first difference in the left file ...
  CHECK(report.find(" 7f<") != std::string::npos);  // ... and in the right one
}

TEST_CASE("a size difference alone is reported", "[bytes]")
{
  const std::string report = describe_byte_difference("abcd", "abcdef");
  CHECK(report.find("first difference at offset 4") != std::string::npos);
  CHECK(report.find("0 of the first 4 bytes differ") != std::string::npos);
  CHECK(report.find("sizes differ") != std::string::npos);
}
