// ISOLATED portable OMOTE enrollment receipt verifier (NOT firmware-linked).
// C++17 host uses OpenSSL (-DOMOTE_RECEIPT_HOST_OPENSSL=1); an ESP-IDF
// mbedTLS path is included but needs a board build and physical review.
// Input public keys MUST be independently authenticated; this does not
// grant pairing, NVS writes, OTA or device configuration authorization.
#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>
#if defined(OMOTE_RECEIPT_HOST_OPENSSL)
#include <openssl/evp.h>
#include <openssl/x509.h>
#else
#include "mbedtls/md.h"
#include "mbedtls/pk.h"
#endif

namespace omote_receipt_offline {
struct Body {
    std::string version, tx_id, device_id;
    uint32_t epoch = 0;
    std::string ha_key_sha256, device_key_sha256;
    std::string session_transcript_sha256, nonce, signer, phase;
};
struct Expected {
    std::string tx_id, device_id;
    uint32_t epoch = 0;
    std::string ha_key_sha256, device_key_sha256;
    std::string session_transcript_sha256, signer;
};
inline bool ishex(const std::string& x, size_t n) {
    if (x.size()!=n) return false;
    return std::all_of(x.begin(), x.end(), [](char c) {
        return (c>='0' && c<='9') || (c>='a' && c<='f');
    });
}
inline int nibble(char x) {
    if (x>='0' && x<='9') return x-'0';
    if (x>='a' && x<='f') return x-'a'+10;
    return -1;
}
inline bool unhex(const std::string& hex, std::vector<unsigned char>& out) {
    if ((hex.size() & 1U) || hex.size()>1024) return false;
    out.clear(); out.reserve(hex.size()/2);
    for (size_t i=0; i<hex.size(); i+=2) {
        int a=nibble(hex[i]), b=nibble(hex[i+1]);
        if (a<0 || b<0) return false;
        out.push_back(static_cast<unsigned char>((a<<4)|b));
    }
    return true;
}
inline bool valid(const Body& b) {
    return b.version=="omote-enrollment-receipt-v1-draft" &&
        ishex(b.tx_id,32) && ishex(b.nonce,32) && b.epoch>0 &&
        b.device_id.size()>=16 && b.device_id.size()<=64 &&
        (b.device_id.size()%2)==0 &&
        ishex(b.device_id,b.device_id.size()) &&
        ishex(b.ha_key_sha256,64) && ishex(b.device_key_sha256,64) &&
        ishex(b.session_transcript_sha256,64) &&
        (b.signer=="ha" || b.signer=="device") &&
        (b.phase=="prepare" || b.phase=="commit_intent" ||
         b.phase=="peer_confirmed");
}
inline std::string canonical(const Body& b) {
    if (!valid(b)) return {};
    // Keys sorted exactly as the Python canonical JSON serializer. Every
    // string is an allowlisted ASCII token/hex, so no escaping ambiguity.
    return std::string("OMOTE-ENROLLMENT-RECEIPT-V1\n") +
      "{\"device_id\":\"" + b.device_id +
      "\",\"device_key_sha256\":\"" + b.device_key_sha256 +
      "\",\"epoch\":" + std::to_string(b.epoch) +
      ",\"ha_key_sha256\":\"" + b.ha_key_sha256 +
      "\",\"nonce\":\"" + b.nonce +
      "\",\"phase\":\"" + b.phase +
      "\",\"session_transcript_sha256\":\"" + b.session_transcript_sha256 +
      "\",\"signer\":\"" + b.signer +
      "\",\"tx_id\":\"" + b.tx_id +
      "\",\"version\":\"" + b.version + "\"}";
}
inline bool verify_signature(const std::string& payload,
                             const std::string& sec1hex,
                             const std::string& signaturehex) {
    if (!ishex(sec1hex,130) || sec1hex.substr(0,2)!="04" ||
        signaturehex.size()<16 || signaturehex.size()>160) return false;
    std::vector<unsigned char> sec1, sig;
    if (!unhex(sec1hex,sec1) || !unhex(signaturehex,sig)) return false;
    // X.509 SubjectPublicKeyInfo for secp256r1 uncompressed SEC1 point.
    static const unsigned char prefix[] = {
        0x30,0x59,0x30,0x13,0x06,0x07,0x2a,0x86,0x48,0xce,0x3d,0x02,
        0x01,0x06,0x08,0x2a,0x86,0x48,0xce,0x3d,0x03,0x01,0x07,0x03,
        0x42,0x00
    };
    std::vector<unsigned char> spki(prefix,prefix+sizeof(prefix));
    spki.insert(spki.end(),sec1.begin(),sec1.end());
#if defined(OMOTE_RECEIPT_HOST_OPENSSL)
    const unsigned char* p=spki.data();
    EVP_PKEY* key=d2i_PUBKEY(nullptr,&p,static_cast<long>(spki.size()));
    if (!key || p!=spki.data()+spki.size()) {
        EVP_PKEY_free(key); return false;
    }
    EVP_MD_CTX* ctx=EVP_MD_CTX_new();
    bool ok=ctx && EVP_DigestVerifyInit(ctx,nullptr,EVP_sha256(),nullptr,key)==1 &&
        EVP_DigestVerifyUpdate(ctx,payload.data(),payload.size())==1 &&
        EVP_DigestVerifyFinal(ctx,sig.data(),sig.size())==1;
    EVP_MD_CTX_free(ctx); EVP_PKEY_free(key);
    return ok;
#else
    mbedtls_pk_context key; mbedtls_pk_init(&key);
    if (mbedtls_pk_parse_public_key(&key,spki.data(),spki.size())!=0) {
        mbedtls_pk_free(&key); return false;
    }
    unsigned char digest[32];
    const mbedtls_md_info_t* md=mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    bool ok=md && mbedtls_md(md,reinterpret_cast<const unsigned char*>(payload.data()),
                              payload.size(),digest)==0 &&
       mbedtls_pk_verify(&key,MBEDTLS_MD_SHA256,digest,sizeof(digest),
                         sig.data(),sig.size())==0;
    mbedtls_pk_free(&key);
    return ok;
#endif
}
inline bool verify(const Body& b, const Expected& e,
                   const std::string& verified_peer_sec1hex,
                   const std::string& signature_der_hex) {
    if (!valid(b) || b.tx_id!=e.tx_id || b.device_id!=e.device_id ||
        b.epoch!=e.epoch || b.ha_key_sha256!=e.ha_key_sha256 ||
        b.device_key_sha256!=e.device_key_sha256 ||
        b.session_transcript_sha256!=e.session_transcript_sha256 ||
        b.signer!=e.signer) return false;
    // Signature verification alone does not prove hardware identity.
    // Peer key fingerprint must be independently pinned in production.
    // This isolated test takes that fingerprint as a separately validated
    // context input and deliberately does not claim trust.
    return verify_signature(canonical(b),verified_peer_sec1hex,signature_der_hex);
}
inline bool production_pairing_enabled() { return false; }
inline bool device_write_enabled() { return false; }
} // namespace omote_receipt_offline
