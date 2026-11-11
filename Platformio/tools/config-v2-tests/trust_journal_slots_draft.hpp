// OMOTE trust journal: OFFLINE HOST-TESTABLE STORAGE CORE ONLY.
//
// This file is deliberately under tools/, never included in a firmware build.
// The backend models two NVS blob keys with durable write+readback; there is
// NO nvs_set_blob or live call site. Checksum detects accidental corruption,
// NOT malicious tampering/backup rollback; this module never grants trust.
#pragma once
#include <algorithm>
#include <cstdint>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace omote_trust_lab {
enum class ReadResult { Missing, Present, Error };
enum class Result { Empty, Ready, Corrupt, IoFailure, Invalid, Replayed, WriteFailure };
enum class Phase : unsigned { Prepared=1, CommitIntent=2, PeerConfirmed=3, Aborted=4, Revoked=5 };

struct Record {
    uint64_t epoch = 0;
    Phase phase = Phase::Prepared;
    std::string txid;         // 32 lowercase hex bytes, NO secrets
    std::string device_id;    // 16..64 lowercase hex, even length
    std::string ha_sha256;    // 64 lowercase hex
    std::string device_sha256;// 64 lowercase hex
};

class Backend {
public:
    virtual ~Backend() = default;
    virtual ReadResult read(unsigned slot, std::string& bytes) const = 0;
    // Return true ONLY after the slot write and corresponding durability
    // barrier have completed. Actual NVS adapter is NOT implemented.
    virtual bool write(unsigned slot, const std::string& bytes) = 0;
};

inline bool hex(const std::string& s, size_t min, size_t max) {
    if (s.size() < min || s.size() > max || (s.size() & 1U)) return false;
    return std::all_of(s.begin(), s.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}
inline bool valid(const Record& r) {
    return r.epoch != 0 && static_cast<unsigned>(r.phase) >= 1 &&
           static_cast<unsigned>(r.phase) <= 5 &&
           hex(r.txid, 32, 32) && hex(r.device_id, 16, 64) &&
           hex(r.ha_sha256, 64, 64) && hex(r.device_sha256, 64, 64);
}
inline uint32_t crc32(const std::string& s) {
    uint32_t crc = 0xFFFFFFFFU;
    for (unsigned char c: s) {
        crc ^= c;
        for (unsigned i = 0; i < 8; ++i) {
            const uint32_t mask = 0U - (crc & 1U);
            crc = (crc >> 1U) ^ (0xEDB88320U & mask);
        }
    }
    return ~crc;
}
inline std::string raw(uint64_t seq, const Record& r) {
    return "OMJ1|" + std::to_string(seq) + "|" + std::to_string(r.epoch) +
           "|" + std::to_string(static_cast<unsigned>(r.phase)) + "|" +
           r.txid + "|" + r.device_id + "|" + r.ha_sha256 + "|" + r.device_sha256;
}
inline std::string encode(uint64_t seq, const Record& r) {
    const std::string contents = raw(seq, r);
    return contents + "|" + std::to_string(crc32(contents));
}
inline bool decimal(const std::string& text, uint64_t& result) {
    if (text.empty() || text.size() > 20 || (text.size() > 1 && text[0] == '0'))
        return false;
    uint64_t n = 0;
    for (char c : text) {
        if (c < '0' || c > '9') return false;
        uint64_t digit = static_cast<unsigned>(c - '0');
        if (n > (std::numeric_limits<uint64_t>::max() - digit) / 10U)
            return false;
        n = n * 10U + digit;
    }
    result = n;
    return true;
}
inline bool decode(const std::string& wire, uint64_t& seq, Record& r) {
    if (wire.size() > 512 || wire.size() < 185) return false;
    std::vector<std::string> bits;
    size_t start = 0;
    for (size_t pos = 0; pos <= wire.size(); ++pos) {
        if (pos == wire.size() || wire[pos] == '|') {
            bits.push_back(wire.substr(start, pos - start));
            start = pos + 1;
        }
    }
    if (bits.size() != 9 || bits[0] != "OMJ1") return false;
    uint64_t epoch=0, phase=0, checksum=0;
    if (!decimal(bits[1], seq) || seq == 0 ||
        !decimal(bits[2], epoch) || !decimal(bits[3], phase) ||
        !decimal(bits[8], checksum) || checksum > UINT32_MAX ||
        phase < 1 || phase > 5)
        return false;
    Record found{epoch, static_cast<Phase>(phase), bits[4], bits[5], bits[6], bits[7]};
    if (!valid(found)) return false;
    const size_t last = wire.rfind('|');
    if (last == std::string::npos ||
        crc32(wire.substr(0, last)) != static_cast<uint32_t>(checksum))
        return false;
    r = found;
    return wire == encode(seq, r);  // refuse alternate/ambiguous serialization
}

class Journal {
public:
    explicit Journal(Backend& store) : store_(store) {}
    Result load() {
        loaded_ = false;
        ready_ = false;
        seq_ = 0;
        current_ = Record{};
        std::string raw0, raw1;
        ReadResult s0 = store_.read(0, raw0), s1 = store_.read(1, raw1);
        if (s0 == ReadResult::Error || s1 == ReadResult::Error)
            return Result::IoFailure;
        if (s0 == ReadResult::Missing && s1 == ReadResult::Missing) {
            loaded_ = true;
            return Result::Empty;
        }
        uint64_t n0 = 0, n1 = 0;
        Record r0, r1;
        if ((s0 == ReadResult::Present && !decode(raw0, n0, r0)) ||
            (s1 == ReadResult::Present && !decode(raw1, n1, r1)))
            return Result::Corrupt;  // never silently roll back a bad newest slot
        if (s0 == ReadResult::Present && (n0 % 2U != 0U)) return Result::Corrupt;
        if (s1 == ReadResult::Present && (n1 % 2U != 1U)) return Result::Corrupt;
        // A single surviving slot with sequence >1 is incomplete. Sequence 1
        // cannot distinguish an initial write from deletion of sequence 2;
        // external anti-rollback evidence is required to detect that case.
        if (s0 != s1 && ((s0 == ReadResult::Present ? n0 : n1) != 1U))
            return Result::Corrupt;
        if (s0 == ReadResult::Present && s1 == ReadResult::Present) {
            uint64_t high = std::max(n0, n1), low = std::min(n0, n1);
            if (low == 0 || high - low != 1) return Result::Corrupt;
        }
        if (s0 == ReadResult::Present && (s1 != ReadResult::Present || n0 > n1)) {
            seq_ = n0; current_ = r0;
        } else {
            seq_ = n1; current_ = r1;
        }
        ready_ = true;
        loaded_ = true;
        return Result::Ready;
    }
    Result save(const Record& next) {
        // Require an explicit successful load: never overwrite a journal
        // before assessing its existing slots or after a corrupt read.
        if (!loaded_) return Result::Invalid;
        // No security grant. Only public diagnostics may be saved.
        if (!valid(next)) return Result::Invalid;
        if (seq_ == std::numeric_limits<uint64_t>::max()) return Result::Invalid;
        if (ready_) {
            if (next.epoch < current_.epoch) return Result::Replayed;
            if (next.epoch == current_.epoch) {
                if (next.txid != current_.txid ||
                    next.device_id != current_.device_id ||
                    next.ha_sha256 != current_.ha_sha256 ||
                    next.device_sha256 != current_.device_sha256)
                    return Result::Replayed;
                const Phase prev = current_.phase, dest = next.phase;
                const bool allowed = dest == prev ||
                    (prev == Phase::Prepared && (dest == Phase::CommitIntent || dest == Phase::Aborted || dest == Phase::Revoked)) ||
                    (prev == Phase::CommitIntent && (dest == Phase::PeerConfirmed || dest == Phase::Aborted || dest == Phase::Revoked)) ||
                    (prev == Phase::PeerConfirmed && (dest == Phase::Aborted || dest == Phase::Revoked));
                if (!allowed)
                    return Result::Replayed;
            } else if (next.epoch != current_.epoch + 1 ||
                       (current_.phase != Phase::Aborted && current_.phase != Phase::Revoked) ||
                       next.txid == current_.txid || next.phase != Phase::Prepared)
                return Result::Replayed;
        } else if (seq_ != 0) {
            return Result::Invalid;
        } else if (next.phase != Phase::Prepared) {
            return Result::Invalid;
        }
        const uint64_t nextseq = seq_ + 1;
        const unsigned slot = static_cast<unsigned>(nextseq % 2U);
        const std::string wire = encode(nextseq, next);
        // A failed write or readback may still have changed durable storage.
        // Require an explicit reload before any subsequent write.
        if (!store_.write(slot, wire)) {
            loaded_ = false; ready_ = false;
            return Result::WriteFailure;
        }
        std::string verify;
        if (store_.read(slot, verify) != ReadResult::Present || verify != wire) {
            loaded_ = false; ready_ = false;
            return Result::WriteFailure;
        }
        return load();  // fails closed on any corrupt slot after write
    }
    bool trusted() const { return false; }
    bool deployment_enabled() const { return false; }
    bool production_reconnect_enabled() const { return false; }
    bool has_record() const { return ready_; }
    uint64_t sequence() const { return seq_; }
    const Record& record() const { return current_; }
private:
    Backend& store_;
    uint64_t seq_ = 0;
    bool ready_ = false;
    bool loaded_ = false;
    Record current_;
};
} // namespace omote_trust_lab
