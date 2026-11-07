// WAVESHARE ISOLATED P-256 RECEIPT CRYPTO DIAGNOSTIC IMAGE.
// Opt-in ONLY: waveshare28v2_omote_sec2_receipt_device_lab.
// Replaces the running OMOTE image WHEN UPLOADED; do not upload without
// an explicit backup/recovery plan. Does not start WiFi, Security2, web
// services, LittleFS, NVS, OTA or eFuse operations. USB serial only.
// Public signed synthetic vectors; no private key or HA candidate identity.
#include <Arduino.h>
#include "enrollment_receipt_verify_draft.hpp"
using namespace omote_receipt_offline;

void setup() {
  Serial.begin(115200);
  const uint32_t since=millis();
  while (!Serial && millis()-since < 1800) delay(10);
  Serial.println("[RECEIPT-LAB] isolated firmware mbedTLS offline vector verification");
  bool all_ok=true;
  {
    Body b;
    b.version="omote-enrollment-receipt-v1-draft";
    b.tx_id="11111111111111111111111111111111";
    b.device_id="a1a1a1a1a1a1a1a1";
    b.epoch=2;
    b.ha_key_sha256="da622c1aeac3f0c9306c60ae1dd977b7c52e48ec238a929a2da4b7e25b4a4913";
    b.device_key_sha256="03ca36c1dcb5d04db2b9a1ce9a6183ec5e4d92dafe1b7f97d52eb049baf6482a";
    b.session_transcript_sha256="abababababababababababababababababababababababababababababababab";
    b.nonce="00000000000000000000000000000001";
    b.signer="ha";
    b.phase="prepare";
    Expected expected{b.tx_id,b.device_id,b.epoch,b.ha_key_sha256,
                      b.device_key_sha256,b.session_transcript_sha256,b.signer};
    const std::string public_key="04f6b026dcd8b70bc0560f7a607db138a58cfc0e6d3c9b695335ebe5dd409a11bef5c8e972388e68b8081a1312a6a7b77d698698b273c08127dfde91ff5a44d685";
    const std::string signature="3045022100c1852f303e3fb36fa3174aa5135f39b326f2328e17ec258e03816e38bac2641f02204e917ee48fd572f6dcb4e0d73cf85e0c3504990bcd04d5af2b97bee92ff57e8f";
    const bool valid_sig=verify(b,expected,public_key,signature);
    Body tampered=b;
    tampered.phase=(b.phase=="prepare" ? "commit_intent" : "prepare");
    const bool tamper_refused=!verify(tampered,expected,public_key,signature);
    Expected wrong_session=expected;
    wrong_session.session_transcript_sha256=std::string(64,'f');
    const bool session_refused=!verify(b,wrong_session,public_key,signature);
    Serial.printf("[RECEIPT-LAB] ha valid=%d tamper_refused=%d session_refused=%d\n",
                  valid_sig,tamper_refused,session_refused);
    all_ok=all_ok && valid_sig && tamper_refused && session_refused;
  }
  {
    Body b;
    b.version="omote-enrollment-receipt-v1-draft";
    b.tx_id="11111111111111111111111111111111";
    b.device_id="a1a1a1a1a1a1a1a1";
    b.epoch=2;
    b.ha_key_sha256="da622c1aeac3f0c9306c60ae1dd977b7c52e48ec238a929a2da4b7e25b4a4913";
    b.device_key_sha256="03ca36c1dcb5d04db2b9a1ce9a6183ec5e4d92dafe1b7f97d52eb049baf6482a";
    b.session_transcript_sha256="abababababababababababababababababababababababababababababababab";
    b.nonce="00000000000000000000000000000002";
    b.signer="device";
    b.phase="commit_intent";
    Expected expected{b.tx_id,b.device_id,b.epoch,b.ha_key_sha256,
                      b.device_key_sha256,b.session_transcript_sha256,b.signer};
    const std::string public_key="04fa92c3456c2614849abe3467da04aa7dac97bfaea62792a7ab151ea8a310ec323e5ad17ae974a84ca3460f431c326a7807b318c2dd2604e9c828d490a3f50852";
    const std::string signature="3046022100c0ef3ed30c45e8c13660d901c4486ca419f24efac812f9f8f050c6103298f720022100fd502074054f436f0507432e057fe3aa6cedff2fb962828d6b05359cdc39c4d8";
    const bool valid_sig=verify(b,expected,public_key,signature);
    Body tampered=b;
    tampered.phase=(b.phase=="prepare" ? "commit_intent" : "prepare");
    const bool tamper_refused=!verify(tampered,expected,public_key,signature);
    Expected wrong_session=expected;
    wrong_session.session_transcript_sha256=std::string(64,'f');
    const bool session_refused=!verify(b,wrong_session,public_key,signature);
    Serial.printf("[RECEIPT-LAB] device valid=%d tamper_refused=%d session_refused=%d\n",
                  valid_sig,tamper_refused,session_refused);
    all_ok=all_ok && valid_sig && tamper_refused && session_refused;
  }
  Serial.printf("[RECEIPT-LAB] FINAL %s; paired=0 deployment=0 ota=0 efuse=0\n",
                all_ok ? "PASS" : "FAIL");
}
void loop() { delay(1000); }
