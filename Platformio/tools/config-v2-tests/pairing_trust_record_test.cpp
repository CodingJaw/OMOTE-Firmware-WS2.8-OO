// Exact fingerprint encoding and re-pairing replacement format tests.
#include "../../lib/esp32HalImpl/wifiHandler/config_v2_pairing_trust_record.hpp"
#include <cassert>
#include <iostream>
#include <string>
using namespace config_v2_pairing_trust_record;
int main() {
  std::string encoded, fingerprint;
  const std::string first(64, 'a');
  const std::string replacement(64, 'f');
  assert(encode(first, encoded));
  assert(decode(encoded, fingerprint) && fingerprint == first);
  assert(!decode("", fingerprint) && fingerprint.empty());
  assert(!decode(encoded + "garbage", fingerprint));
  std::string corrupt = encoded;
  corrupt[0] = 'X';
  assert(!decode(corrupt, fingerprint));
  corrupt = encoded;
  corrupt[corrupt.size() - 1] = 'G';
  assert(!decode(corrupt, fingerprint));
  assert(!encode(std::string(63, '0'), encoded) && encoded.empty());
  assert(!encode(std::string(64, 'A'), encoded));
  assert(encode(replacement, encoded));
  assert(decode(encoded, fingerprint) && fingerprint == replacement);
  std::cout << "config v2 peer fingerprint record: PASS\n";
}
