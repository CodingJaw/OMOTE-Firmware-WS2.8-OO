#if !defined(IS_SIMULATOR)
#include "config_v2_boot_nvs.hpp"
#include <Preferences.h>

namespace config_v2_boot_nvs {
namespace {

constexpr const char *kNamespace = "omotecfgv2";
constexpr const char *kKeys[2] = {"slot0", "slot1"};

bool read(Preferences &prefs, unsigned slot,
          config_v2_boot_record::Bytes &buffer) {
  if (prefs.getBytesLength(kKeys[slot]) != buffer.size())
    return false;
  return prefs.getBytes(kKeys[slot], buffer.data(), buffer.size()) ==
         buffer.size();
}

config_v2_boot_record::Chosen current(Preferences &prefs,
                                      bool &valid0, bool &valid1) {
  config_v2_boot_record::Bytes a{}, b{};
  const bool have0 = read(prefs, 0, a), have1 = read(prefs, 1, b);
  config_v2_boot_record::Snapshot checked;
  valid0 = have0 && config_v2_boot_record::decode(a, checked);
  valid1 = have1 && config_v2_boot_record::decode(b, checked);
  return config_v2_boot_record::choose(a, have0, b, have1);
}

} // namespace

Inspect inspect() {
  Inspect result;
  Preferences prefs;
  if (!prefs.begin(kNamespace, true))
    return result;
  result.accessible = true;
  result.chosen = current(prefs, result.slot0Valid, result.slot1Valid);
  prefs.end();
  return result;
}

bool store(const config_v2_boot_record::Chosen &expected,
           const config_v2_generation::Selection &next,
           config_v2_boot_record::Chosen &written) {
  Preferences prefs;
  if (!prefs.begin(kNamespace, false))
    return false;

  bool v0 = false, v1 = false;
  const auto actual = current(prefs, v0, v1);
  // Reject stale/concurrent updates and never overwrite the latest valid
  // selector. This is not a mutex; caller must serialize writers.
  if (expected.valid != actual.valid ||
      (expected.valid && (expected.slot != actual.slot ||
                          expected.snapshot.sequence != actual.snapshot.sequence))) {
    prefs.end();
    return false;
  }

  config_v2_boot_record::Bytes encoded{};
  unsigned nextSlot = 0;
  if (!config_v2_boot_record::makeNext(actual, next, encoded, nextSlot)) {
    prefs.end();
    return false;
  }

  const bool wrote = prefs.putBytes(kKeys[nextSlot], encoded.data(),
                                    encoded.size()) == encoded.size();
  bool validA = false, validB = false;
  const auto verified = current(prefs, validA, validB);
  const bool ok = wrote && verified.valid && verified.slot == nextSlot &&
                  verified.snapshot.sequence ==
                      (actual.valid ? actual.snapshot.sequence + 1 : 1) &&
                  config_v2_boot_record::validSelection(verified.snapshot.selection);
  if (ok)
    written = verified;
  prefs.end();
  return ok;
}

} // namespace config_v2_boot_nvs
#endif // !IS_SIMULATOR
