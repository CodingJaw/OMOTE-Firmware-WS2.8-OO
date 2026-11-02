// Compile-only feasibility probe for Espressif Security2 on the Waveshare.
// This file never starts a transport, creates a listener or accepts a PIN.
// Remove/reassess after the actual patched ESP-IDF version and HA client
// interoperation have been proven by separate tests.
#if defined(OMOTE_WAVESHARE28V2) && defined(OMOTE_CONFIG_V2_PREVIEW)
#include "sdkconfig.h"

#if !defined(CONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_VERSION_2) || \
    !CONFIG_ESP_PROTOCOMM_SUPPORT_SECURITY_VERSION_2
#error "Config v2 pairing candidate requires ESP-IDF protocomm_security2"
#endif

#include "protocomm.h"
#include "protocomm_security2.h"

#include <type_traits>
#include <utility>

// Header/API compatibility check only; deliberately no protocomm calls.
static_assert(std::is_pointer<decltype(std::declval<protocomm_security2_params_t>().salt)>::value,
              "Unexpected ESP-IDF Security2 salt API");
static_assert(std::is_pointer<decltype(std::declval<protocomm_security2_params_t>().verifier)>::value,
              "Unexpected ESP-IDF Security2 verifier API");
#endif
