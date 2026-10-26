// Trophy Manager - host sockets for the FTP test. SPDX-License-Identifier: GPL-3.0-or-later
#include "../src/net.hpp"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

namespace trophy::net
{
int connect_tcp(std::uint8_t a, std::uint8_t b, std::uint8_t c, std::uint8_t d, std::uint16_t port, int timeout_ms)
{
    const int s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0)
        return -1;
    timeval tv{timeout_ms / 1000, (timeout_ms % 1000) * 1000};
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl((static_cast<std::uint32_t>(a) << 24) | (static_cast<std::uint32_t>(b) << 16) |
                                 (static_cast<std::uint32_t>(c) << 8) | d);
    if (::connect(s, reinterpret_cast<sockaddr *>(&addr), sizeof addr) != 0)
    {
        ::close(s);
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
        const ssize_t n = ::send(sock, p + off, size - off, 0);
        if (n <= 0)
            return -1;
        off += static_cast<std::size_t>(n);
    }
    return 0;
}
int recv_some(int sock, void *buffer, std::size_t size)
{
    const ssize_t n = ::recv(sock, buffer, size, 0);
    return n < 0 ? -1 : static_cast<int>(n);
}
void close_socket(int sock)
{
    if (sock >= 0)
        ::close(sock);
}
} // namespace trophy::net
