#include <doctest/doctest.h>

#include "record.h"


TEST_CASE("record.h / Record default initialization") {
  Record r;

  CHECK(r.key.empty());
  CHECK(r.val.empty());
  CHECK(r.tombstone == false);
}

TEST_CASE("record.h / Record assignment works correctly") {
  Record r;
  r.key = "name";
  r.val = "Alice";
  r.tombstone = false;

  CHECK(r.key == "name");
  CHECK(r.val == "Alice");
  CHECK(r.tombstone == false);
}

TEST_CASE("record.h / Record tombstone flag works (logical delete)") {
  Record r;
  r.key = "user1";
  r.val = "data";
  r.tombstone = true;

  CHECK(r.tombstone == true);
}

TEST_CASE("record.h / Record copy constructor") {
  Record r1;
  r1.key = "k1";
  r1.val = "v1";
  r1.tombstone = true;

  Record r2 = r1; // 拷贝

  CHECK(r2.key == "k1");
  CHECK(r2.val == "v1");
  CHECK(r2.tombstone == true);
}

TEST_CASE("record.h / Record assignment operator") {
  Record r1;
  r1.key = "k2";
  r1.val = "v2";
  r1.tombstone = false;

  Record r2;
  r2 = r1;

  CHECK(r2.key == "k2");
  CHECK(r2.val == "v2");
  CHECK(r2.tombstone == false);
}

TEST_CASE("record.h / Record overwrite values") {
  Record r;
  r.key = "k";
  r.val = "v1";

  r.val = "v2";  // 覆盖

  CHECK(r.val == "v2");
}
