// Run from repository root:
// c++ -std=c++17 -Wall -Wextra -Werror -o /tmp/omote-boot-record \
//   Platformio/tools/config-v2-tests/boot_record_test.cpp && /tmp/omote-boot-record
#include "../../lib/esp32HalImpl/wifiHandler/config_v2_boot_record.hpp"
#include <cassert>
#include <iostream>
#include <limits>
#include <string>

using namespace config_v2_boot_record;

int main() {
  const std::string a(64, 'a'), b(64, 'b'), c(64, 'c'), d(64, 'd');
  config_v2_generation::Selection selection{a, b, "", 0};
  Snapshot s{selection, 1};
  Bytes slot0{}, slot1{}, corrupt{};
  assert(encode(s, slot0));
  Snapshot decoded;
  assert(decode(slot0, decoded));
  assert(decoded.sequence == 1);
  assert(decoded.selection.active == a);
  assert(decoded.selection.previous == b);
  assert(decoded.selection.pending.empty());
  assert(!decoded.selection.trialBoots);
  assert(!decode(Bytes{}, decoded));

  auto chosen = choose(slot0, true, slot1, false);
  assert(chosen.valid && chosen.slot == 0 && chosen.snapshot.sequence == 1);

  // Simulate an interrupted write and bit corruption: keep last good slot.
  corrupt = slot0;
  corrupt[32] ^= 0x44;
  chosen = choose(slot0, true, corrupt, true);
  assert(chosen.valid && chosen.slot == 0);
  corrupt = Bytes{};
  corrupt[0] = 'O';
  chosen = choose(slot0, true, corrupt, true);
  assert(chosen.valid && chosen.slot == 0);

  // Alternate slots; after a fully committed update choose the newer one.
  unsigned nextSlot = 9;
  assert(makeNext(chosen, selection, slot1, nextSlot) && nextSlot == 1);
  chosen = choose(slot0, true, slot1, true);
  assert(chosen.valid && chosen.slot == 1 && chosen.snapshot.sequence == 2);
  assert(makeNext(chosen, selection, slot0, nextSlot) && nextSlot == 0);
  chosen = choose(slot0, true, slot1, true);
  assert(chosen.valid && chosen.slot == 0 && chosen.snapshot.sequence == 3);

  // Trial boots survive encode/decode and never confuse active/previous.
  selection.pending = c;
  selection.trialBoots = 1;
  assert(makeNext(chosen, selection, slot1, nextSlot) && nextSlot == 1);
  chosen = choose(slot0, true, slot1, true);
  assert(chosen.valid && chosen.snapshot.selection.pending == c &&
         chosen.snapshot.selection.trialBoots == 1);
  assert(chosen.snapshot.selection.active == a);
  assert(chosen.snapshot.selection.previous == b);

  // A corrupt newer slot must never eclipse the older valid one.
  corrupt = slot1;
  corrupt[212] ^= 1;
  auto fallback = choose(slot0, true, corrupt, true);
  assert(fallback.valid && fallback.slot == 0 && fallback.snapshot.sequence == 3);

  // Reject invalid digests, duplicate revisions, impossible trial metadata.
  selection.pending = a;
  assert(!encode({selection, 5}, corrupt));
  selection.pending = d;
  selection.trialBoots = 3;
  assert(!encode({selection, 5}, corrupt));
  selection.trialBoots = 0;
  selection.pending.clear();
  assert(encode({selection, 5}, corrupt));
  corrupt[4] = 2; // Unsupported record format
  assert(!decode(corrupt, decoded));
  assert(encode({selection, 5}, corrupt));
  corrupt[5] = 1; // Reserved field
  assert(!decode(corrupt, decoded));

  // Max sequence cannot wrap and accidentally supersede the good record.
  Chosen exhausted;
  exhausted.valid = true;
  exhausted.slot = 0;
  exhausted.snapshot = {selection, std::numeric_limits<uint64_t>::max()};
  assert(!makeNext(exhausted, selection, corrupt, nextSlot));

  // A blank NVS namespace can bootstrap a first confirmed generation.
  auto empty = choose(Bytes{}, false, Bytes{}, false);
  assert(!empty.valid);
  assert(makeNext(empty, selection, corrupt, nextSlot));
  assert(nextSlot == 0);
  assert(decode(corrupt, decoded) && decoded.sequence == 1);

  std::cout << "config v2 NVS boot record: PASS" << std::endl;
}
