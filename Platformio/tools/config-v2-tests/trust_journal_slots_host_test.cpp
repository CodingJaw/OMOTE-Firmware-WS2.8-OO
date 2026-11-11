// Host-only test for disconnected OMOTE dual-slot journal.
// Compiled by test_trust_journal_slots_host.py, never linked to firmware.
#include "trust_journal_slots_draft.hpp"
#include <array>
#include <cassert>
#include <string>
using namespace omote_trust_lab;

struct FakeNvs final : Backend {
    std::array<bool,2> present{{false,false}};
    std::array<std::string,2> values{};
    bool failwrite = false;
    bool shortwrite = false;
    bool failread = false;
    bool fail_after_write = false;
    ReadResult read(unsigned slot, std::string& out) const override {
        if (failread) return ReadResult::Error;
        if (!present[slot]) return ReadResult::Missing;
        out = values[slot];
        return ReadResult::Present;
    }
    bool write(unsigned slot, const std::string& text) override {
        if (failwrite) return false;
        present[slot] = true;
        values[slot] = shortwrite ? text.substr(0, text.size()/2U) : text;
        return !fail_after_write;
    }
};
Record example(unsigned epoch=1, Phase phase=Phase::Prepared, char tx='1') {
    return Record{epoch,phase,std::string(32,tx),std::string(16,'a'),
                  std::string(64,'b'),std::string(64,'c')};
}
int main() {
    FakeNvs nvs;
    Journal journal(nvs);
    assert(!journal.trusted() && !journal.deployment_enabled());
    assert(journal.save(example()) == Result::Invalid); // must load first
    assert(journal.load() == Result::Empty);
    assert(journal.save(example()) == Result::Ready);
    assert(nvs.present[1] && !nvs.present[0]);
    assert(journal.sequence() == 1);
    assert(journal.save(example(1, Phase::PeerConfirmed)) == Result::Replayed);
    Journal reboot(nvs);
    assert(reboot.load() == Result::Ready);
    assert(!reboot.trusted() && !reboot.production_reconnect_enabled());
    assert(reboot.save(example(1, Phase::CommitIntent)) == Result::Ready);
    assert(nvs.present[0] && nvs.present[1]);
    assert(reboot.sequence() == 2);
    FakeNvs missing_slot = nvs;
    missing_slot.present[1] = false;
    Journal missing(missing_slot);
    assert(missing.load() == Result::Corrupt);
    assert(reboot.save(example(1, Phase::Prepared)) == Result::Replayed);
    assert(reboot.save(example(1, Phase::CommitIntent, '2')) == Result::Replayed);
    assert(reboot.save(example(0)) == Result::Invalid);
    assert(reboot.save(example(2)) == Result::Replayed); // cannot skip abort
    assert(reboot.save(example(1, Phase::Aborted)) == Result::Ready);
    assert(reboot.save(example(2, Phase::Prepared,'2')) == Result::Ready);
    assert(!reboot.trusted());
    Journal restored(nvs);
    assert(restored.load() == Result::Ready);
    assert(restored.record().epoch == 2);

    FakeNvs bad = nvs;
    bad.values[0][10] = bad.values[0][10] == 'a' ? 'b' : 'a';
    Journal damaged(bad);
    assert(damaged.load() == Result::Corrupt);
    assert(damaged.save(example(3)) == Result::Invalid);

    FakeNvs torn;
    Journal powercut(torn);
    assert(powercut.load() == Result::Empty);
    torn.shortwrite = true;
    assert(powercut.save(example()) == Result::WriteFailure);
    torn.shortwrite = false;
    assert(powercut.save(example()) == Result::Invalid);
    assert(powercut.load() == Result::Corrupt); // no silent fallback
    assert(powercut.save(example(2)) == Result::Invalid);

    FakeNvs ioerror;
    Journal io(ioerror);
    ioerror.failread = true;
    assert(io.load() == Result::IoFailure);
    assert(io.save(example()) == Result::Invalid);
    ioerror.failread = false;
    assert(io.load() == Result::Empty);
    ioerror.failwrite = true;
    assert(io.save(example()) == Result::WriteFailure);
    ioerror.failwrite = false;
    assert(io.save(example()) == Result::Invalid);

    FakeNvs late;
    Journal interrupted(late);
    assert(interrupted.load() == Result::Empty);
    late.fail_after_write = true;
    assert(interrupted.save(example()) == Result::WriteFailure);
    late.fail_after_write = false;
    assert(interrupted.save(example(1, Phase::Prepared, '2')) == Result::Invalid);
    assert(interrupted.load() == Result::Ready);
    assert(interrupted.record().txid == std::string(32, '1'));

    uint64_t seq=0;
    Record parsed;
    assert(!decode("invalid",seq,parsed));
    assert(valid(example()));
    assert(!valid(example(0)));
    assert(!valid(Record{1,Phase::Prepared,"X",std::string(16,'a'),
                         std::string(64,'b'),std::string(64,'c')}));
    return 0;
}
