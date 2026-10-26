// Trophy Manager - host test of the SHA-256 and of the trophy file editor. SPDX-License-Identifier: GPL-3.0-or-later
// Real console files are looked up in tests/fixtures (never committed: they carry account ids); without them
// only the self-contained checks run.
#include "../src/t2pd.hpp"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace trophy;

static std::vector<std::uint8_t> load(const char *name)
{
    std::string path = std::string("tests/fixtures/") + name;
    FILE *f = std::fopen(path.c_str(), "rb");
    std::vector<std::uint8_t> out;
    if (!f)
        return out;
    std::uint8_t buf[4096];
    std::size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0)
        out.insert(out.end(), buf, buf + n);
    std::fclose(f);
    return out;
}

static std::string hex(const std::array<std::uint8_t, 32> &h)
{
    static const char *d = "0123456789abcdef";
    std::string s;
    for (auto b : h)
    {
        s += d[b >> 4];
        s += d[b & 15];
    }
    return s;
}

static int earned_count(const std::vector<std::uint8_t> &stream)
{
    std::vector<t2pd::Trophy> t;
    assert(t2pd::parse(stream, t));
    int n = 0;
    for (auto &x : t)
        n += x.earned;
    return n;
}

int main()
{
    // SHA-256 known answers
    assert(hex(sha256(nullptr, 0)) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    const std::uint8_t abc[3] = {'a', 'b', 'c'};
    assert(hex(sha256(abc, 3)) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    std::vector<std::uint8_t> a(1000000, 'a');
    assert(hex(sha256(a.data(), a.size())) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    const char *two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"; // spans two blocks
    assert(hex(sha256(reinterpret_cast<const std::uint8_t *>(two), 56)) ==
           "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");

    // stream <-> file round trip on a synthetic file (header, one data block, one empty block)
    std::vector<std::uint8_t> s(t2pd::head_size + 2 * t2pd::chunk_size, 0);
    std::memcpy(s.data(), "T2PD", 4);
    for (std::size_t i = 0; i < 100; ++i)
        s[t2pd::head_size + i] = static_cast<std::uint8_t>(i * 7 + 1);
    auto file = t2pd::from_stream(s);
    assert(file.size() == t2pd::head_size + 2 * t2pd::stride && t2pd::verify(file.data(), file.size()));
    std::vector<std::uint8_t> back;
    assert(t2pd::to_stream(file.data(), file.size(), back) && back == s);
    file[t2pd::head_size + 3] ^= 1;
    assert(!t2pd::verify(file.data(), file.size()));
    assert(!t2pd::to_stream(file.data(), file.size() - 1, back));

    int checked = 0;
    // real files
    const char *names[] = {"gta_before.dat", "gta_after.dat", "onimusha_9.dat", "onimusha_all.dat"};
    for (const char *n : names)
    {
        auto d = load(n);
        if (d.empty())
            continue;
        std::vector<std::uint8_t> st;
        assert(t2pd::verify(d.data(), d.size()));
        assert(t2pd::to_stream(d.data(), d.size(), st));
        assert(t2pd::from_stream(st) == d); // rebuilding gives the very same file
        ++checked;
    }
    // the console's own before/after pair: unlocking trophy 1 must give the file the console wrote
    auto before = load("gta_before.dat"), after = load("gta_after.dat");
    if (!before.empty() && !after.empty())
    {
        std::vector<std::uint8_t> st;
        assert(t2pd::to_stream(before.data(), before.size(), st));
        assert(earned_count(st) == 0);
        assert(t2pd::unlock(st, 1, 0x00e31ebfe1f78200ull));
        assert(t2pd::from_stream(st) == after);
        assert(!t2pd::unlock(st, 1, 5)); // already earned
        // and locking it again leaves a valid file with nothing earned
        assert(t2pd::lock(st, 1) && earned_count(st) == 0);
        const auto again = t2pd::from_stream(st);
        assert(t2pd::verify(again.data(), again.size()));
        assert(!t2pd::lock(st, 1));
        ++checked;
    }
    // a game with nine earned: unlock one more, then every other, then lock one back
    auto oni = load("onimusha_9.dat");
    if (!oni.empty())
    {
        std::vector<std::uint8_t> st;
        assert(t2pd::to_stream(oni.data(), oni.size(), st));
        assert(earned_count(st) == 9);
        assert(t2pd::unlock(st, 5, t2pd::ticks_from_unix(1792000000)) && earned_count(st) == 10);
        std::vector<t2pd::Trophy> t;
        assert(t2pd::parse(st, t) && t.size() == 52);
        std::uint64_t when = t2pd::ticks_from_unix(1792000100);
        for (auto &x : t)
            if (!x.earned)
                assert(t2pd::unlock(st, x.id, when += 45000000));
        assert(earned_count(st) == 52);
        auto full = t2pd::from_stream(st);
        assert(t2pd::verify(full.data(), full.size()) && full.size() == oni.size());
        auto ref = load("onimusha_all.dat");
        if (!ref.empty())
        {
            std::vector<std::uint8_t> rs;
            assert(t2pd::to_stream(ref.data(), ref.size(), rs) && earned_count(rs) == 52);
        }
        assert(t2pd::lock(st, 0) && earned_count(st) == 51); // take the platinum back
        auto less = t2pd::from_stream(st);
        assert(t2pd::verify(less.data(), less.size()));
        ++checked;
    }
    std::printf("PASS: SHA-256, hash blocks, unlock/lock (%d real-file checks%s).\n", checked,
                checked ? "" : "; fixtures not found, only the self-contained ones ran");
    return 0;
}
