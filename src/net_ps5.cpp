// Trophy Manager - sockets through sceNet. SPDX-License-Identifier: GPL-3.0-or-later
#include <initializer_list>
#include "net.hpp"

extern "C"
{
    int sceNetSocket(const char *, int, int, int);
    int sceNetConnect(int, const void *, std::uint32_t);
    int sceNetSend(int, const void *, std::size_t, int);
    int sceNetRecv(int, void *, std::size_t, int);
    int sceNetSetsockopt(int, int, int, const void *, std::uint32_t);
    int sceNetSocketClose(int);
}

namespace trophy::net
{
namespace
{
struct Address
{
    std::uint8_t length, family;
    std::uint16_t port; // network order
    std::uint32_t address;
    std::uint16_t virtual_port;
    std::uint8_t zero[6];
};
} // namespace

int connect_tcp(std::uint8_t a, std::uint8_t b, std::uint8_t c, std::uint8_t d, std::uint16_t port, int timeout_ms)
{
    const int s = sceNetSocket("trophy", 2, 1, 6);
    if (s < 0)
        return -1;
    const int timeout_us = timeout_ms * 1000;
    for (int option : {0x1105, 0x1106, 0x1109}) // send, receive and connect timeouts
        sceNetSetsockopt(s, 0xffff, option, &timeout_us, sizeof timeout_us);
    Address address{sizeof(Address),
                    2,
                    static_cast<std::uint16_t>((port << 8) | (port >> 8)),
                    static_cast<std::uint32_t>(a) | (static_cast<std::uint32_t>(b) << 8) |
                        (static_cast<std::uint32_t>(c) << 16) | (static_cast<std::uint32_t>(d) << 24),
                    0,
                    {}};
    if (sceNetConnect(s, &address, sizeof address) < 0)
    {
        sceNetSocketClose(s);
        return -1;
    }
    return s;
}

int send_all(int sock, const void *data, std::size_t size)
{
    const auto *p = static_cast<const std::uint8_t *>(data);
    std::size_t off = 0;
    while (off < size)
    {
        const int n = sceNetSend(sock, p + off, size - off, 0);
        if (n <= 0)
            return -1;
        off += static_cast<std::size_t>(n);
    }
    return 0;
}

int recv_some(int sock, void *buffer, std::size_t size)
{
    return sceNetRecv(sock, buffer, size, 0);
}

void close_socket(int sock)
{
    if (sock >= 0)
        sceNetSocketClose(sock);
}
} // namespace trophy::net
