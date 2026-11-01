#include "../../lib/esp32HalImpl/wifiHandler/config_v2_sha256.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>

using namespace config_v2_sha256;

int main() {
  Hasher empty;
  assert(empty.hex() == "e3b0c44298fc1c149afbf4c8996fb924"
                        "27ae41e4649b934ca495991b7852b855");
  Hasher abc;
  assert(abc.update("abc"));
  assert(abc.hex() == "ba7816bf8f01cfea414140de5dae2223"
                      "b00361a396177a9cb410ff61f20015ad");
  assert(abc.hex() == abc.hex()); // final is non-mutating
  Hasher hello;
  for (char c : std::string("hello world")) {
    const auto byte = static_cast<uint8_t>(c);
    assert(hello.update(&byte, 1));
  }
  assert(hello.hex() == "b94d27b9934d3e08a52e52d7da7dabfa"
                        "c484efe37a5380ee9088f7ace2efcde9");
  Hasher aMillion;
  std::string chunk(1000, 'a');
  for (int i = 0; i < 1000; ++i)
    assert(aMillion.update(chunk));
  assert(aMillion.hex() == "cdc76e5c9914fb9281a1c7e284d73e67"
                           "f1809a48a497200e046d39ccc7112cd0");
  Hasher blocks;
  blocks.update(std::string(55, 'a'));
  assert(blocks.hex() == "9f4390f8d30c2dd92ec9f095b65e2b9a"
                         "e9b0a925a5258e241c9f1e910f734318");
  blocks.reset();
  assert(blocks.hex() == empty.hex());
  assert(!blocks.update(nullptr, 1));
  assert(blocks.hex() == empty.hex());
  std::cout << "config v2 SHA-256: PASS" << std::endl;
}
