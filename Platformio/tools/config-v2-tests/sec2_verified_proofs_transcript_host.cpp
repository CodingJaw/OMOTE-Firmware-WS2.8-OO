// HOST-ONLY transcript cross-language test harness. No firmware runtime linkage.
// Uses the already-tested offline C++ P256/OpenSSL verifier for BOTH proofs.
// It verifies proof signatures before emitting canonical transcript digest.
// It cannot attest that a claimed session id came from a real Security2 channel.
#ifndef OMOTE_RECEIPT_HOST_OPENSSL
#error "Host-only source; compile with OMOTE_RECEIPT_HOST_OPENSSL"
#endif
#include "enrollment_receipt_verify_draft.hpp"
#include <openssl/sha.h>
#include <cerrno>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

using omote_receipt_offline::ishex;
using omote_receipt_offline::unhex;

static bool uint32_arg(const char* text, uint32_t& out) {
    if (!text || text[0]<'0' || text[0]>'9') return false;
    errno=0;
    char* end=nullptr;
    const unsigned long long n=std::strtoull(text,&end,10);
    if (errno!=0 || !end || *end!=0 || n==0 ||
        n>std::numeric_limits<uint32_t>::max()) return false;
    out=static_cast<uint32_t>(n);
    return true;
}
static void add32(std::vector<unsigned char>& b, uint32_t n) {
    b.push_back(static_cast<unsigned char>(n>>24));
    b.push_back(static_cast<unsigned char>(n>>16));
    b.push_back(static_cast<unsigned char>(n>>8));
    b.push_back(static_cast<unsigned char>(n));
}
static void append(std::vector<unsigned char>& b,
                   const std::vector<unsigned char>& v) {
    b.insert(b.end(),v.begin(),v.end());
}
static std::string as_string(const std::vector<unsigned char>& v) {
    return std::string(v.begin(),v.end());
}
static std::string hex_of(const std::vector<unsigned char>& value) {
    static const char* letters="0123456789abcdef";
    std::string out;
    out.reserve(value.size()*2);
    for (unsigned char b:value) {
        out+=letters[b>>4]; out+=letters[b&15];
    }
    return out;
}
static bool fixed(const std::string& h, size_t bytes,
                  std::vector<unsigned char>& out) {
    return ishex(h,bytes*2) && unhex(h,out);
}

int main(int argc, char** argv) {
    // version, session id, epoch, transaction hex, device id hex,
    // HA public SEC1, device public SEC1, HA nonce, device challenge,
    // HA DER signature, device DER signature
    if (argc!=12) return 2;
    uint32_t session=0,epoch=0;
    if (std::string(argv[1])!="1" ||
        !uint32_arg(argv[2],session) || !uint32_arg(argv[3],epoch)) return 3;
    std::vector<unsigned char> tx,dev,ha_pub,d_pub,ha_nonce,challenge,ha_sig,d_sig;
    const std::string hahex(argv[6]),dhex(argv[7]),hshex(argv[10]),dshex(argv[11]);
    if (!fixed(argv[4],16,tx) || !ishex(argv[5],std::string(argv[5]).size()) ||
        !unhex(argv[5],dev) || dev.size()<8 || dev.size()>32 ||
        !fixed(hahex,65,ha_pub) || ha_pub[0]!=4 ||
        !fixed(dhex,65,d_pub) || d_pub[0]!=4 ||
        !fixed(argv[8],32,ha_nonce) ||
        !fixed(argv[9],32,challenge) ||
        !unhex(hshex,ha_sig) || ha_sig.size()<8 || ha_sig.size()>80 ||
        !unhex(dshex,d_sig) || d_sig.size()<8 || d_sig.size()>80) return 4;

    std::vector<unsigned char> cookie;
    add32(cookie,session);
    const std::string ha_msg="OMOTE-SEC2-LAB-HA-PROOF-V1"+
        as_string(cookie)+as_string(ha_pub)+as_string(ha_nonce);
    const std::string dev_msg="OMOTE-SEC2-LAB-DEVICE-PROOF-V1"+
        as_string(cookie)+as_string(challenge)+as_string(d_pub);
    if (!omote_receipt_offline::verify_signature(ha_msg,hahex,hshex) ||
        !omote_receipt_offline::verify_signature(dev_msg,dhex,dshex)) return 5;

    const std::string domain="OMOTE-SEC2-VERIFIED-PROOFS-V1\n";
    std::vector<unsigned char> result(domain.begin(),domain.end());
    result.push_back(1);
    add32(result,session); add32(result,epoch);
    append(result,tx);
    result.push_back(static_cast<unsigned char>(dev.size()));
    append(result,dev); append(result,ha_pub); append(result,d_pub);
    append(result,ha_nonce); append(result,challenge);
    result.push_back(static_cast<unsigned char>(ha_sig.size()));
    append(result,ha_sig);
    result.push_back(static_cast<unsigned char>(d_sig.size()));
    append(result,d_sig);

    unsigned char digest[SHA256_DIGEST_LENGTH];
    if (!SHA256(result.data(),result.size(),digest)) return 6;
    std::vector<unsigned char> hashed(digest,digest+sizeof(digest));
    std::string ha_fp,d_fp;
    if (!omote_receipt_offline::public_fingerprint(hahex,ha_fp) ||
        !omote_receipt_offline::public_fingerprint(dhex,d_fp)) return 7;
    std::cout << hex_of(hashed) << "\n"
              << ha_fp << "\n" << d_fp << "\n" << hex_of(result) << "\n";
    return 0;
}
