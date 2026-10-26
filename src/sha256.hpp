// Trophy Manager - SHA-256 (FIPS 180-4). SPDX-License-Identifier: GPL-3.0-or-later
// Small, dependency-free and header-only so the same code runs on the PS5 and in the host tests.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace trophy
{
class Sha256
{
  public:
    Sha256() noexcept
    {
        reset();
    }

    void reset() noexcept
    {
        state_ = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                  0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
        length_ = 0;
        used_ = 0;
    }

    void update(const std::uint8_t *data, std::size_t size) noexcept
    {
        length_ += size;
        while (size)
        {
            const std::size_t take = size < 64 - used_ ? size : 64 - used_;
            std::memcpy(block_ + used_, data, take);
            used_ += take;
            data += take;
            size -= take;
            if (used_ == 64)
            {
                compress(block_);
                used_ = 0;
            }
        }
    }

    std::array<std::uint8_t, 32> finish() noexcept
    {
        const std::uint64_t bits = length_ * 8;
        const std::uint8_t one = 0x80;
        update(&one, 1);
        const std::uint8_t zero = 0;
        while (used_ != 56)
            update(&zero, 1);
        std::uint8_t tail[8];
        for (int i = 0; i < 8; ++i)
            tail[i] = static_cast<std::uint8_t>(bits >> (56 - 8 * i));
        update(tail, 8);
        std::array<std::uint8_t, 32> out{};
        for (int i = 0; i < 8; ++i)
        {
            out[4 * i] = static_cast<std::uint8_t>(state_[i] >> 24);
            out[4 * i + 1] = static_cast<std::uint8_t>(state_[i] >> 16);
            out[4 * i + 2] = static_cast<std::uint8_t>(state_[i] >> 8);
            out[4 * i + 3] = static_cast<std::uint8_t>(state_[i]);
        }
        return out;
    }

  private:
    static constexpr std::uint32_t rotr(std::uint32_t x, int n) noexcept
    {
        return (x >> n) | (x << (32 - n));
    }

    void compress(const std::uint8_t *p) noexcept
    {
        static constexpr std::uint32_t k[64] = {
            0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u,
            0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu,
            0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu,
            0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
            0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
            0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
            0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u,
            0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
            0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u,
            0xc67178f2u};
        std::uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = (static_cast<std::uint32_t>(p[4 * i]) << 24) | (static_cast<std::uint32_t>(p[4 * i + 1]) << 16) |
                   (static_cast<std::uint32_t>(p[4 * i + 2]) << 8) | p[4 * i + 3];
        for (int i = 16; i < 64; ++i)
        {
            const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        std::uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3], e = state_[4], f = state_[5],
                      g = state_[6], h = state_[7];
        for (int i = 0; i < 64; ++i)
        {
            const std::uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const std::uint32_t ch = (e & f) ^ (~e & g);
            const std::uint32_t t1 = h + s1 + ch + k[i] + w[i];
            const std::uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t t2 = s0 + maj;
            h = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }
        state_[0] += a;
        state_[1] += b;
        state_[2] += c;
        state_[3] += d;
        state_[4] += e;
        state_[5] += f;
        state_[6] += g;
        state_[7] += h;
    }

    std::array<std::uint32_t, 8> state_{};
    std::uint8_t block_[64]{};
    std::size_t used_ = 0;
    std::uint64_t length_ = 0;
};

inline std::array<std::uint8_t, 32> sha256(const std::uint8_t *data, std::size_t size) noexcept
{
    Sha256 h;
    h.update(data, size);
    return h.finish();
}
} // namespace trophy
