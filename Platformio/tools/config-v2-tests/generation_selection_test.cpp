// Run: c++ -std=c++17 -Wall -Wextra -Werror -o /tmp/omote-gen-test \
//          Platformio/tools/config-v2-tests/generation_selection_test.cpp && /tmp/omote-gen-test
#include "../../lib/esp32HalImpl/wifiHandler/config_v2_generation_selection.hpp"
#include <cassert>
#include <iostream>
#include <set>
#include <string>

using namespace config_v2_generation;

int main() {
  const std::string a(64, 'a');
  const std::string b(64, 'b');
  const std::string c(64, 'c');
  const std::string d(64, 'd');

  assert(revisionIdValid(a));
  assert(!revisionIdValid("bad"));
  assert(!revisionIdValid(std::string(64, 'x')));

  Selection s{a, b, "", 0};
  const auto validAll = [](const std::string&) { return true; };
  assert(beginTrial(s, c));
  assert(!beginTrial(s, d));
  auto boot = chooseBoot(s, validAll);
  assert(boot.source == BootSource::Pending && boot.revision == c);
  assert(s.active == a && s.previous == b && s.trialBoots == 1);
  assert(!confirmUiHealthy(s, b));
  assert(confirmUiHealthy(s, c));
  assert(s.active == c && s.previous == a && s.pending.empty());

  assert(beginTrial(s, d));
  boot = chooseBoot(s, validAll);
  assert(boot.source == BootSource::Pending && s.trialBoots == 1);
  boot = chooseBoot(s, validAll);
  assert(boot.source == BootSource::Pending && s.trialBoots == 2);
  boot = chooseBoot(s, validAll);
  assert(boot.source == BootSource::Active && boot.revision == c);
  assert(s.active == c && s.previous == a && s.pending.empty());

  assert(beginTrial(s, d));
  boot = chooseBoot(s, [&](const std::string& r) { return r != d; });
  assert(boot.source == BootSource::Active && boot.revision == c);
  assert(s.pending.empty());

  boot = chooseBoot(s, [&](const std::string& r) { return r == a; });
  assert(boot.source == BootSource::Previous && boot.revision == a);
  assert(s.active == a && s.previous.empty());

  boot = chooseBoot(s, [](const std::string&) { return false; });
  assert(boot.source == BootSource::Recovery);
  assert(!rollback(s));

  Selection r{a, b, "", 0};
  assert(rollback(r));
  assert(r.active == b && r.previous == a);
  assert(beginTrial(r, c));
  assert(markLocalUiFailure(r, c));
  assert(r.pending.empty() && r.active == b && r.previous == a);

  // A confirmed revision failing its local UI health check falls back to
  // the previous confirmed revision, without involving Wi-Fi.
  assert(markLocalUiFailure(r, b));
  assert(r.active == a && r.previous.empty());
  assert(markLocalUiFailure(r, a));
  assert(r.active.empty());
  assert(chooseBoot(r, validAll).source == BootSource::Recovery);
  assert(!markLocalUiFailure(r, d));
  std::cout << "config v2 generation selection: PASS" << std::endl;
}
