// Trophy Manager - a plain HTTP GET to the console's own Elf Arsenal (port 6969). SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "net.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace trophy
{
// HTTP/1.0 so the answer is never chunked. True for a 2xx answer; `body` is what follows the headers.
inline bool http_get(const std::string &path, std::vector<std::uint8_t> &body, std::size_t max_bytes,
                     std::uint8_t a = 127, std::uint8_t b = 0, std::uint8_t c = 0, std::uint8_t d = 1,
                     std::uint16_t port = 6969)
{
    body.clear();
    const int s = net::connect_tcp(a, b, c, d, port, 6000);
    if (s < 0)
        return false;
    const std::string request = "GET " + path + " HTTP/1.0\r\nHost: 127.0.0.1:6969\r\nConnection: close\r\n\r\n";
    if (net::send_all(s, request.data(), request.size()) != 0)
    {
        net::close_socket(s);
        return false;
    }
    std::vector<std::uint8_t> all;
    std::uint8_t chunk[8192];
    for (;;)
    {
        const int n = net::recv_some(s, chunk, sizeof chunk);
        if (n == 0)
            break;
        if (n < 0 || all.size() + static_cast<std::size_t>(n) > max_bytes + 4096)
        {
            net::close_socket(s);
            return false;
        }
        all.insert(all.end(), chunk, chunk + n);
    }
    net::close_socket(s);
    int status = 0;
    if (all.size() < 12 || std::sscanf(reinterpret_cast<const char *>(all.data()), "HTTP/%*s %d", &status) != 1 ||
        status < 200 || status >= 300)
        return false;
    for (std::size_t i = 0; i + 4 <= all.size(); ++i)
        if (all[i] == '\r' && all[i + 1] == '\n' && all[i + 2] == '\r' && all[i + 3] == '\n')
        {
            body.assign(all.begin() + static_cast<std::ptrdiff_t>(i + 4), all.end());
            return body.size() <= max_bytes;
        }
    return false;
}
} // namespace trophy
