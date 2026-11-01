#include "../../lib/esp32HalImpl/wifiHandler/config_v2_stage_policy.hpp"
#include <cassert>
#include <iostream>
#include <string>
using namespace config_v2_stage_policy;

int main() {
  assert(transactionIdValid(std::string(32, 'a')));
  assert(!transactionIdValid(std::string(31, 'a')));
  assert(!transactionIdValid(std::string(32, 'g')));
  assert(!transactionIdValid("../something"));
  assert(classifyChunk(0, 0, 1024, 4096) == ChunkDecision::Append);
  assert(classifyChunk(1024, 1024, 1024, 4096) == ChunkDecision::Append);
  assert(classifyChunk(1024, 0, 1024, 4096) == ChunkDecision::RetryCompare);
  assert(classifyChunk(1024, 512, 1024, 4096) == ChunkDecision::Reject);
  assert(classifyChunk(1024, 1025, 1, 4096) == ChunkDecision::Reject);
  assert(classifyChunk(0, 0, 1025, 4096) == ChunkDecision::Reject);
  assert(classifyChunk(0, 0, 0, 4096) == ChunkDecision::Reject);
  assert(classifyChunk(0, 0, 64, 10) == ChunkDecision::Reject);
  assert(classifyChunk(0, 0, 64, kMaxStagedFileBytes + 1) == ChunkDecision::Reject);
  assert(!hasCapacity(1024, 512, 512));
  assert(hasCapacity(1024 * 1024, 128 * 1024, 256 * 1024));
  assert(!hasCapacity(1024 * 1024, 1024 * 1024 + 1, 1));
  assert(!hasCapacity(1024 * 1024, 800 * 1024, 100 * 1024));
  std::cout << "config v2 staging policy: PASS" << std::endl;
}
