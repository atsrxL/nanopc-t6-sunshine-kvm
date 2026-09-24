// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>

namespace rkmoon_sunshine {
// Never log passwords or headers. Each HTTP request authenticates anew.
class password_auth {
  std::array<unsigned char, 32> expected_ {};
  std::mutex mutex_;
  double tokens_ = 10;
  std::chrono::steady_clock::time_point last_ = std::chrono::steady_clock::now();
  static auto digest(std::string_view value) {
    std::array<unsigned char, 32> result {};
    unsigned int size = 0;
    if (EVP_Digest(value.data(), value.size(), result.data(), &size, EVP_sha256(), nullptr) != 1 || size != result.size())
      throw std::runtime_error("Password authentication initialization failed");
    return result;
  }
public:
  explicit password_auth(std::string_view password = "kvm") {
    std::string plain = "kvm:" + std::string(password);
    std::string encoded(4 * ((plain.size() + 2) / 3) + 1, char{});
    auto size = EVP_EncodeBlock(reinterpret_cast<unsigned char *>(encoded.data()),
                    reinterpret_cast<const unsigned char *>(plain.data()), static_cast<int>(plain.size()));
    encoded.resize(size);
    expected_ = digest("Basic " + encoded);
    OPENSSL_cleanse(plain.data(), plain.size());
    OPENSSL_cleanse(encoded.data(), encoded.size());
  }
  bool authorize(std::string_view header) {
    // Bounded global failure budget; no attacker-controlled per-IP allocation.
    std::lock_guard lock(mutex_);
    const auto now = std::chrono::steady_clock::now();
    tokens_ = std::min(10.0, tokens_ + std::chrono::duration<double>(now-last_).count());
    last_ = now;
    if (tokens_ < 1) return false;
    if (header.size() <= 1024) {
      auto actual = digest(header);
      if (CRYPTO_memcmp(actual.data(), expected_.data(), actual.size()) == 0) return true;
    }
    tokens_ -= 1;
    return false;
  }
};
}
