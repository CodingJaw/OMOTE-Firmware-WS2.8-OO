#if defined(OMOTE_RECEIPT_MBEDTLS_COMPILE_PROBE)
// Receipt cryptography compile-and-link probe for Waveshare ESP32-S3.
//
// Included ONLY in [env:waveshare28v2_omote_sec2_receipt_compile].
// No setup()/loop(), network listener, UI registration, NVS writes or eFuses.
// This deliberately uncalled function makes the real framework's mbedTLS
// API and the offline receipt verifier compile in the actual IDF toolchain.
// It is NOT a live Security2 transcript proof or a trusted enrollment.
#include "enrollment_receipt_verify_draft.hpp"

extern "C" bool omote_receipt_mbedtls_compile_probe() {
    using namespace omote_receipt_offline;
    Body b;
    b.version = "omote-enrollment-receipt-v1-draft";
    b.tx_id = std::string(32,'1');
    b.device_id = std::string(16,'a');
    b.epoch = 1;
    b.ha_key_sha256 = std::string(64,'b');
    b.device_key_sha256 = std::string(64,'c');
    b.session_transcript_sha256 = std::string(64,'d');
    b.nonce = std::string(32,'e');
    b.signer = "ha";
    b.phase = "prepare";
    Expected e{b.tx_id,b.device_id,b.epoch,b.ha_key_sha256,
               b.device_key_sha256,b.session_transcript_sha256,b.signer};
    // Invalid key and signature MUST fail. Return false if the verifier
    // ever starts accepting a missing/corrupt public identity.
    return !verify(b,e,std::string(130,'0'),std::string(140,'0'))
           && !production_pairing_enabled()
           && !device_write_enabled();
}

#endif // OMOTE_RECEIPT_MBEDTLS_COMPILE_PROBE
