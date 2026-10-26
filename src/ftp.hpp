// Trophy Manager - a small blocking FTP client for the console's own FTP server (Elf Arsenal, port 2121).
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Passive mode only, binary type. It accepts any 2xx as success because that server answers some commands with
// another 2xx than the standard one (a delete is "226 File deleted"). A read that stops early closes the control
// connection too, since the server would otherwise keep sending; the next call opens it again.
#pragma once

#include "net.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace trophy
{
struct FtpEntry
{
    std::string name;
    bool dir = false;
    std::uint64_t size = 0;
};

class Ftp
{
  public:
    Ftp(std::uint8_t a = 127, std::uint8_t b = 0, std::uint8_t c = 0, std::uint8_t d = 1,
        std::uint16_t port = 2121) noexcept
        : ip_{a, b, c, d}, port_(port)
    {
    }
    ~Ftp()
    {
        close();
    }
    Ftp(const Ftp &) = delete;
    Ftp &operator=(const Ftp &) = delete;

    bool open()
    {
        close();
        control_ = net::connect_tcp(ip_[0], ip_[1], ip_[2], ip_[3], port_, 8000);
        if (control_ < 0)
            return false;
        bool ok = reply() / 100 == 2;
        if (ok)
        {
            const int user = code_of("USER anonymous");
            ok = user / 100 == 2 || (user / 100 == 3 && code_of("PASS trophy") / 100 == 2);
        }
        if (ok)
            ok = code_of("TYPE I") / 100 == 2;
        if (!ok)
        {
            close();
            return false;
        }
        return true;
    }

    void close()
    {
        if (control_ >= 0)
            net::close_socket(control_);
        control_ = -1;
        buffer_.clear();
    }

    bool is_open() const noexcept
    {
        return control_ >= 0;
    }

    // Reads up to `limit` bytes (0 = whole file) starting at `offset`. `max_bytes` caps what is kept in memory.
    bool retr(const std::string &path, std::vector<std::uint8_t> &out, std::size_t max_bytes, std::uint64_t offset = 0,
              std::size_t limit = 0)
    {
        out.clear();
        if (!ensure())
            return false;
        const int data = passive();
        if (data < 0)
            return false;
        if (offset && code_of("REST " + std::to_string(offset)) / 100 != 3)
        {
            net::close_socket(data);
            return false;
        }
        if (code_of("RETR " + path) / 100 != 1)
        {
            net::close_socket(data);
            return false;
        }
        std::uint8_t chunk[8192];
        bool stopped_early = false, ok = true;
        for (;;)
        {
            const int n = net::recv_some(data, chunk, sizeof chunk);
            if (n == 0)
                break;
            if (n < 0)
            {
                ok = false;
                break;
            }
            std::size_t take = static_cast<std::size_t>(n);
            if (limit && out.size() + take > limit)
            {
                take = limit - out.size();
                stopped_early = true;
            }
            if (out.size() + take > max_bytes)
            {
                ok = false;
                stopped_early = true;
                break;
            }
            out.insert(out.end(), chunk, chunk + take);
            if (stopped_early || (limit && out.size() == limit))
            {
                stopped_early = true;
                break;
            }
        }
        net::close_socket(data);
        if (stopped_early || !ok)
        {
            close(); // the transfer was cut: do not wait for a reply that may never come
            return ok;
        }
        return reply() / 100 == 2;
    }

    bool stor(const std::string &path, const std::uint8_t *data, std::size_t size)
    {
        if (!ensure())
            return false;
        const int sock = passive();
        if (sock < 0)
            return false;
        if (code_of("STOR " + path) / 100 != 1)
        {
            net::close_socket(sock);
            return false;
        }
        const bool sent = net::send_all(sock, data, size) == 0;
        net::close_socket(sock);
        if (!sent)
        {
            close();
            return false;
        }
        return reply() / 100 == 2;
    }

    bool remove(const std::string &path)
    {
        return ensure() && code_of("DELE " + path) / 100 == 2;
    }

    bool rename(const std::string &from, const std::string &to)
    {
        return ensure() && code_of("RNFR " + from) / 100 == 3 && code_of("RNTO " + to) / 100 == 2;
    }

    // Creates a folder. A folder that already exists answers with an error too, so callers check by writing.
    bool make_directory(const std::string &path)
    {
        return ensure() && code_of("MKD " + path) / 100 == 2;
    }

    bool list(const std::string &dir, std::vector<FtpEntry> &out)
    {
        out.clear();
        if (!ensure())
            return false;
        const int data = passive();
        if (data < 0)
            return false;
        if (code_of("LIST " + dir) / 100 != 1)
        {
            net::close_socket(data);
            return false;
        }
        std::string text;
        std::uint8_t chunk[4096];
        for (;;)
        {
            const int n = net::recv_some(data, chunk, sizeof chunk);
            if (n <= 0)
                break;
            text.append(reinterpret_cast<char *>(chunk), static_cast<std::size_t>(n));
            if (text.size() > (4u << 20))
                break;
        }
        net::close_socket(data);
        if (reply() / 100 != 2)
            return false;
        std::size_t pos = 0;
        while (pos < text.size())
        {
            std::size_t end = text.find('\n', pos);
            if (end == std::string::npos)
                end = text.size();
            std::string line = text.substr(pos, end - pos);
            pos = end + 1;
            while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
                line.pop_back();
            FtpEntry e;
            if (parse_list_line(line, e) && e.name != "." && e.name != "..")
                out.push_back(e);
        }
        return true;
    }

  private:
    bool ensure()
    {
        return control_ >= 0 || open();
    }

    // "-rw-r--r-- 1 0 0 12736 Oct 26 13:39 name with spaces": the name is everything after eight fields.
    static bool parse_list_line(const std::string &line, FtpEntry &e)
    {
        if (line.size() < 10)
            return false;
        std::size_t pos = 0;
        std::string fields[8];
        for (int i = 0; i < 8; ++i)
        {
            while (pos < line.size() && line[pos] == ' ')
                ++pos;
            const std::size_t start = pos;
            while (pos < line.size() && line[pos] != ' ')
                ++pos;
            if (start == pos)
                return false;
            fields[i] = line.substr(start, pos - start);
        }
        while (pos < line.size() && line[pos] == ' ')
            ++pos;
        if (pos >= line.size())
            return false;
        e.dir = fields[0][0] == 'd';
        e.size = std::strtoull(fields[4].c_str(), nullptr, 10);
        e.name = line.substr(pos);
        return true;
    }

    bool read_line(std::string &line)
    {
        for (;;)
        {
            const std::size_t nl = buffer_.find('\n');
            if (nl != std::string::npos)
            {
                line = buffer_.substr(0, nl);
                buffer_.erase(0, nl + 1);
                while (!line.empty() && line.back() == '\r')
                    line.pop_back();
                return true;
            }
            char chunk[512];
            const int n = net::recv_some(control_, chunk, sizeof chunk);
            if (n <= 0)
                return false;
            buffer_.append(chunk, static_cast<std::size_t>(n));
        }
    }

    // One reply, possibly spread over several "NNN-" lines. Returns its code, or 0 on a connection error.
    int reply(std::string *text = nullptr)
    {
        std::string line;
        if (!read_line(line) || line.size() < 4)
        {
            close();
            return last_ = 0;
        }
        const std::string head = line.substr(0, 3);
        if (text)
            *text = line;
        if (line[3] == '-')
            for (;;)
            {
                if (!read_line(line))
                {
                    close();
                    return last_ = 0;
                }
                if (text)
                    *text += "\n" + line;
                if (line.compare(0, 3, head) == 0 && line.size() > 3 && line[3] == ' ')
                    break;
            }
        return last_ = std::atoi(head.c_str());
    }

    int code_of(const std::string &command, std::string *text = nullptr)
    {
        const std::string line = command + "\r\n";
        if (control_ < 0 || net::send_all(control_, line.data(), line.size()) != 0)
        {
            close();
            return last_ = 0;
        }
        return reply(text);
    }

    // PASV, then connect to the port it names on the same host as the control connection.
    int passive()
    {
        std::string text;
        if (code_of("PASV", &text) / 100 != 2)
            return -1;
        const std::size_t open = text.find('(');
        unsigned v[6] = {};
        if (open == std::string::npos ||
            std::sscanf(text.c_str() + open, "(%u,%u,%u,%u,%u,%u)", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6)
            return -1;
        return net::connect_tcp(ip_[0], ip_[1], ip_[2], ip_[3], static_cast<std::uint16_t>((v[4] << 8) | v[5]), 8000);
    }

    std::uint8_t ip_[4];
    std::uint16_t port_;
    int control_ = -1;
    int last_ = 0;
    std::string buffer_;
};
} // namespace trophy
