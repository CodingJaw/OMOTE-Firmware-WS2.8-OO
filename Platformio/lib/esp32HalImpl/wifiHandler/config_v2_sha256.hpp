#pragma once

// Small streaming SHA-256 implementation for identical host/ESP32 digest
// calculations. Does not allocate heap, block the UI, or require Arduino.
// Firmware callers must feed bounded chunks and yield between those chunks.
// This is an integrity primitive, NOT transport authentication.
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace config_v2_sha256 {

using Digest = std::array<uint8_t, 32>;

class Hasher {
 public:
  Hasher() { reset(); }

  void reset() {
    state_ = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
              0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
    buffered_ = 0;
    total_ = 0;
    buffer_.fill(0);
  }

  bool update(const uint8_t *data, std::size_t count) {
    if (count && !data)
      return false;
    if (count > UINT64_MAX / 8u - total_)
      return false; // SHA-256 input bit length must fit uint64_t.
    total_ += count;
    for (std::size_t i = 0; i < count; ++i) {
      buffer_[buffered_++] = data[i];
      if (buffered_ == buffer_.size()) {
        compress(buffer_.data());
        buffered_ = 0;
      }
    }
    return true;
  }

  bool update(const std::string &data) {
    return update(reinterpret_cast<const uint8_t *>(data.data()), data.size());
  }

  Digest final() const {
    Hasher copy = *this;
    const uint64_t bits = total_ * 8u;
    copy.buffer_[copy.buffered_++] = 0x80;
    if (copy.buffered_ > 56) {
      while (copy.buffered_ < 64)
        copy.buffer_[copy.buffered_++] = 0;
      copy.compress(copy.buffer_.data());
      copy.buffered_ = 0;
    }
    while (copy.buffered_ < 56)
      copy.buffer_[copy.buffered_++] = 0;
    for (unsigned i = 0; i < 8; ++i)
      copy.buffer_[56 + i] = static_cast<uint8_t>(bits >> (56 - 8 * i));
    copy.compress(copy.buffer_.data());
    Digest digest{};
    for (unsigned i = 0; i < 8; ++i)
      for (unsigned j = 0; j < 4; ++j)
        digest[i * 4 + j] = static_cast<uint8_t>(copy.state_[i] >> (24 - 8 * j));
    return digest;
  }

  std::string hex() const {
    constexpr char digits[] = "0123456789abcdef";
    std::string result(64, '0');
    const auto bytes = final();
    for (std::size_t i = 0; i < bytes.size(); ++i) {
      result[2 * i] = digits[bytes[i] >> 4];
      result[2 * i + 1] = digits[bytes[i] & 15];
    }
    return result;
  }

 private:
  static uint32_t rotateRight(uint32_t a, unsigned b) {
    return (a >> b) | (a << (32 - b));
  }

  void compress(const uint8_t *block) {
    static constexpr uint32_t k[64] = {
      0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
      0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
      0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
      0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
      0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
      0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
      0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
      0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
      0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
      0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
      0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
      0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
      0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
      0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
      0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
      0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
    };
    uint32_t w[64]{};
    for (unsigned i = 0; i < 16; ++i)
      w[i] = (static_cast<uint32_t>(block[4 * i]) << 24) |
             (static_cast<uint32_t>(block[4 * i + 1]) << 16) |
             (static_cast<uint32_t>(block[4 * i + 2]) << 8) |
              static_cast<uint32_t>(block[4 * i + 3]);
    for (unsigned i = 16; i < 64; ++i) {
      const uint32_t s0 = rotateRight(w[i - 15], 7) ^
                          rotateRight(w[i - 15], 18) ^ (w[i - 15] >> 3);
      const uint32_t s1 = rotateRight(w[i - 2], 17) ^
                          rotateRight(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
    uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];
    for (unsigned i = 0; i < 64; ++i) {
      const uint32_t s1 = rotateRight(e, 6) ^ rotateRight(e, 11) ^
                          rotateRight(e, 25);
      const uint32_t ch = (e & f) ^ (~e & g);
      const uint32_t temp1 = h + s1 + ch + k[i] + w[i];
      const uint32_t s0 = rotateRight(a, 2) ^ rotateRight(a, 13) ^
                          rotateRight(a, 22);
      const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
      const uint32_t temp2 = s0 + maj;
      h = g; g = f; f = e; e = d + temp1;
      d = c; c = b; b = a; a = temp1 + temp2;
    }
    state_[0] += a; state_[1] += b; state_[2] += c; state_[3] += d;
    state_[4] += e; state_[5] += f; state_[6] += g; state_[7] += h;
  }

  std::array<uint32_t, 8> state_{};
  std::array<uint8_t, 64> buffer_{};
  std::size_t buffered_ = 0;
  uint64_t total_ = 0;
};

} // namespace config_v2_sha256
