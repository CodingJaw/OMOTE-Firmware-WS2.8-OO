// Host-only CLI for signed receipt vector verification. Does not read NVS,
// USB, HA state, firmware config or private keys. Build with -lcrypto.
#define OMOTE_RECEIPT_HOST_OPENSSL 1
#include "enrollment_receipt_verify_draft.hpp"
#include <cstdlib>
#include <iostream>
using namespace omote_receipt_offline;
int main(int argc,char** argv) {
    // args: sec1 signature txid deviceid epoch hafp devfp session nonce
    //       signer phase version [negative-context]
    if (argc!=13 && argc!=14) return 3;
    Body b;
    b.tx_id=argv[3]; b.device_id=argv[4];
    try {
        size_t consumed=0;
        const unsigned long epoch=std::stoul(argv[5],&consumed);
        if (consumed!=std::string(argv[5]).size() || epoch>UINT32_MAX) return 3;
        b.epoch=static_cast<uint32_t>(epoch);
    } catch (...) { return 3; }
    b.ha_key_sha256=argv[6]; b.device_key_sha256=argv[7];
    b.session_transcript_sha256=argv[8]; b.nonce=argv[9];
    b.signer=argv[10]; b.phase=argv[11]; b.version=argv[12];
    Expected e{b.tx_id,b.device_id,b.epoch,b.ha_key_sha256,
               b.device_key_sha256,b.session_transcript_sha256,b.signer};
    if (argc==14 && std::string(argv[13])=="bad-session")
        e.session_transcript_sha256=std::string(64,'e');
    if (argc==14 && std::string(argv[13])=="bad-peer")
        e.ha_key_sha256=std::string(64,'e');
    bool ok=verify(b,e,argv[1],argv[2]);
    const auto message=canonical(b);
    static const char hexchars[]="0123456789abcdef";
    for (unsigned char c : message) {
        std::cout<<hexchars[c>>4]<<hexchars[c&15];
    }
    std::cout<<"\n";
    return ok ? 0 : 2;
}
