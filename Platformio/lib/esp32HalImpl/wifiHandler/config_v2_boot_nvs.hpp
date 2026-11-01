#pragma once

// Optional persistence adapter. Intentionally NOT called by normal startup:
// configuration activation must wait for complete immutable-manifest support.
#include "config_v2_boot_record.hpp"

namespace config_v2_boot_nvs {

struct Inspect {
  bool accessible = false;
  bool slot0Valid = false;
  bool slot1Valid = false;
  config_v2_boot_record::Chosen chosen;
};

// Read-only probe; no NVS key or namespace is created on blank devices.
Inspect inspect();

// Commit the next selector state into the *other* NVS slot. Retains the
// existing slot even if the new write is interrupted. Caller must guarantee
// exclusive access, ensure referenced manifest/object integrity and check
// the return value BEFORE activating anything.
bool store(const config_v2_boot_record::Chosen &expected,
           const config_v2_generation::Selection &next,
           config_v2_boot_record::Chosen &written);

} // namespace config_v2_boot_nvs
