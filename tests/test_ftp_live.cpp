// Trophy Manager - the FTP client against a real console. SPDX-License-Identifier: GPL-3.0-or-later
// Run with TM_FTP_HOST=<a.b.c.d> and TM_USER=<your user id in hex>, and TM_NPWR=<a game's NPWR id> (default NPWR49248_00). It reads trophy files and writes/renames/deletes one scratch file in
// /data/tmp; nothing else on the console is touched.
#include "../src/ftp.hpp"
#include "../src/http.hpp"
#include "../src/ucp.hpp"
#include "../src/t2pd.hpp"

#include <cassert>
#include <cstdio>
#include <cstdlib>

using namespace trophy;

int main()
{
    const char *host = std::getenv("TM_FTP_HOST");
    if (!host)
    {
        std::puts("SKIP: TM_FTP_HOST not set");
        return 0;
    }
    unsigned a, b, c, d;
    assert(std::sscanf(host, "%u.%u.%u.%u", &a, &b, &c, &d) == 4);
    Ftp ftp(static_cast<std::uint8_t>(a), static_cast<std::uint8_t>(b), static_cast<std::uint8_t>(c),
            static_cast<std::uint8_t>(d), 2121);
    assert(ftp.open());

    std::vector<FtpEntry> list;
    assert(ftp.list("/user/trophy2/nobackup/conf", list) && !list.empty());
    std::printf("catalogs on the console: %zu (first: %s)\n", list.size(), list[0].name.c_str());

    // a whole trophy file, and its hashes
    std::vector<std::uint8_t> file;
    const char *user = std::getenv("TM_USER");
    assert(user && "set TM_USER to the console user id in hex");
    assert(ftp.retr(std::string("/user/home/") + user + "/trophy2/nobackup/data/NPWR49248_00/TRPTITLE.DAT", file, 1 << 20));
    std::printf("TRPTITLE.DAT: %zu bytes, hashes %s\n", file.size(), t2pd::verify(file.data(), file.size()) ? "ok" : "BAD");
    assert(t2pd::verify(file.data(), file.size()));

    // a partial read of a 28 MB file: only its first 4 KB, then the connection is usable again
    std::vector<std::uint8_t> part;
    assert(ftp.retr("/user/trophy2/nobackup/conf/NPWR49248_00/TROPHY.UCP", part, 1 << 20, 0, 4096));
    assert(part.size() == 4096);
    std::vector<std::uint8_t> mid;
    assert(ftp.retr("/user/trophy2/nobackup/conf/NPWR49248_00/TROPHY.UCP", mid, 1 << 20, 4096, 1024));
    assert(mid.size() == 1024);
    std::printf("partial reads: ok (first bytes %02x%02x%02x%02x)\n", part[0], part[1], part[2], part[3]);


    // the catalog of a game: index, a language file and one picture, all by partial reads
    const std::string ucp_path = "/user/trophy2/nobackup/conf/NPWR49248_00/TROPHY.UCP";
    std::vector<std::uint8_t> head;
    assert(ftp.retr(ucp_path, head, 1 << 20, 0, 0x20));
    const std::size_t need = ucp::index_bytes(head.data(), head.size());
    assert(need > 0);
    std::vector<std::uint8_t> idx;
    assert(ftp.retr(ucp_path, idx, 1 << 20, 0, need));
    std::vector<ucp::Entry> entries;
    assert(ucp::parse_index(idx.data(), idx.size(), entries));
    std::printf("catalog index: %zu files\n", entries.size());
    const ucp::Entry *meta = ucp::find_meta(entries, {"pt-BR", "en-US"});
    assert(meta && meta->name == "tropmeta_pt-BR.json");
    std::vector<std::uint8_t> json;
    assert(ftp.retr(ucp_path, json, 1 << 20, meta->offset, meta->size) && json.size() == meta->size);
    const std::string js(json.begin(), json.end());
    assert(js.find("Inigual") != std::string::npos);
    const ucp::Entry *icon = ucp::find(entries, "trop0005.png");
    assert(icon && icon->size > 1000);
    std::vector<std::uint8_t> png;
    assert(ftp.retr(ucp_path, png, 4 << 20, icon->offset, icon->size) && png.size() == icon->size);
    assert(png[1] == 'P' && png[2] == 'N' && png[3] == 'G');
    std::printf("catalog: pt-BR metadata %zu bytes, trophy 5 icon %zu bytes\n", json.size(), png.size());

    // the HTTP side
    std::vector<std::uint8_t> body;
    assert(http_get("/api/state", body, 1 << 16, static_cast<std::uint8_t>(a), static_cast<std::uint8_t>(b),
                    static_cast<std::uint8_t>(c), static_cast<std::uint8_t>(d)));
    const std::string state(body.begin(), body.end());
    assert(state.find("nanodnsRunning") != std::string::npos);
    assert(http_get("/appdb", body, 1 << 20, static_cast<std::uint8_t>(a), static_cast<std::uint8_t>(b),
                    static_cast<std::uint8_t>(c), static_cast<std::uint8_t>(d)));
    assert(!http_get("/nao-existe", body, 1 << 16, static_cast<std::uint8_t>(a), static_cast<std::uint8_t>(b),
                     static_cast<std::uint8_t>(c), static_cast<std::uint8_t>(d)));
    std::printf("http: state %zu bytes ok\n", state.size());

    // write, read back, rename, delete
    const std::string tmp = "/data/tmp/tm_ftp_test.bin", tmp2 = "/data/tmp/tm_ftp_test2.bin";
    std::vector<std::uint8_t> payload(5000);
    for (std::size_t i = 0; i < payload.size(); ++i)
        payload[i] = static_cast<std::uint8_t>(i * 31 + 7);
    assert(ftp.stor(tmp, payload.data(), payload.size()));
    std::vector<std::uint8_t> back;
    assert(ftp.retr(tmp, back, 1 << 20) && back == payload);
    assert(ftp.rename(tmp, tmp2));
    assert(ftp.retr(tmp2, back, 1 << 20) && back == payload);
    assert(ftp.remove(tmp2));
    assert(!ftp.retr(tmp2, back, 1 << 20));
    assert(ftp.open() && ftp.list("/data/tmp", list));
    for (auto &e : list)
        assert(e.name != "tm_ftp_test.bin" && e.name != "tm_ftp_test2.bin");
    std::puts("PASS: FTP, HTTP and catalog readers against the console.");
    return 0;
}
