#include <doctest/doctest.h>

#include "key_transform.h"

#include <string>

TEST_CASE("key_transform.h / normalization is stable and short") {
  const std::uint64_t first = KeyTransform::normalize("customer:123456789");
  const std::uint64_t second = KeyTransform::normalize("customer:123456789");

  CHECK(first == second);
}

TEST_CASE("key_transform.cpp / shuffling changes the normalized storage key") {
  KeyTransform no_shuffle(KeyTransformOptions{false});
  KeyTransform shuffle(KeyTransformOptions{true});

  const std::string normalized_key = no_shuffle.storageKey("customer:123456789");
  const std::string shuffled_key = shuffle.storageKey("customer:123456789");

  CHECK(normalized_key[0] == 'h');
  CHECK(shuffled_key[0] == 's');
  CHECK(normalized_key != shuffled_key);
  CHECK(normalized_key.size() == 9);
  CHECK(shuffled_key.size() == 9);
}

TEST_CASE("key_transform.cpp / shuffling is deterministic") {
  KeyTransform shuffle(KeyTransformOptions{true});

  CHECK(shuffle.storageKey("alpha") == shuffle.storageKey("alpha"));
  CHECK(shuffle.storageKey("alpha") != shuffle.storageKey("bravo"));
}
