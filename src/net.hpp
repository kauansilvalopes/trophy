// Trophy Manager - the few socket calls the FTP client needs. SPDX-License-Identifier: GPL-3.0-or-later
// The PS5 build implements them with sceNet (src/net_ps5.cpp); the host test uses BSD sockets (tests/net_host.cpp),
// so the same FTP code is exercised against a real console from the PC.
#pragma once

#include <cstddef>
#include <cstdint>

namespace trophy::net
{
// Blocking TCP connection to an IPv4 address. Returns a socket id, or -1. Every later call on it times out
// after `timeout_ms` instead of waiting forever.
int connect_tcp(std::uint8_t a, std::uint8_t b, std::uint8_t c, std::uint8_t d, std::uint16_t port,
                int timeout_ms);
// 0 when everything was sent, -1 on error.
int send_all(int sock, const void *data, std::size_t size);
// Bytes received (> 0), 0 when the peer closed, -1 on error or timeout.
int recv_some(int sock, void *buffer, std::size_t size);
void close_socket(int sock);
} // namespace trophy::net
