// Trophy Manager - see and edit the trophies of the games on a jailbroken PS5. GPL-3.0-or-later.
//
// Everything goes through the console's own Elf Arsenal on 127.0.0.1: HTTP (6969) for the state and the game list,
// FTP (2121) to read and write the trophy files. Nothing leaves the console and nothing is sent to PSN.
#include "demo_renderer.hpp"
#include "gl_main.hpp"

#include "cJSON.h"
#include "cover_decode.h"
#include "ftp.hpp"
#include "http.hpp"
#include "i18n.hpp"
#include "t2pd.hpp"
#include "ucp.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <pthread.h>
#include <strings.h>
#include <string>
#include <unistd.h>
#include <vector>

using namespace trophy;
using Canvas = estante::GlCanvas;
using ps5::demo::Color;

struct PadSample
{
    std::uint32_t buttons;
    std::uint8_t analog[6];
    std::uint8_t reserved0[66];
    std::int32_t connected;
    std::uint64_t timestamp;
    std::uint8_t reserved1[32];
};
static_assert(sizeof(PadSample) == 120);

extern "C"
{
    int sceUserServiceInitialize(const void *);
    int sceUserServiceGetInitialUser(int *);
    int sceSystemServiceParamGetInt(int, int *);
    int scePadInit();
    int scePadOpen(int, int, int, const void *);
    int scePadRead(int, PadSample *, int);
    std::uint64_t sceKernelGetProcessTime();
    int sceKernelDebugOutText(int, const char *);
}

namespace
{
constexpr Color rgb(unsigned r, unsigned g, unsigned b)
{
    return static_cast<Color>(0xff000000u | (b << 16) | (g << 8) | r);
}
const Color bg = rgb(8, 9, 12), panel = rgb(26, 28, 33), accent = rgb(0, 112, 209), muted = rgb(152, 156, 166),
            good = rgb(88, 196, 120), bad = rgb(232, 96, 96), white = Color::white;

constexpr std::uint32_t R1 = 0x800, UP = 0x10, RIGHT = 0x20, DOWN = 0x40, LEFT = 0x80, L2 = 0x100, R2 = 0x200, TRIANGLE = 0x1000,
                        CIRCLE = 0x2000, CROSS = 0x4000, SQUARE = 0x8000, OPTIONS = 0x8, L1 = 0x400;

constexpr int max_games = 160, max_trophies = 128, icon_edge = 96, icon_count = 40;
constexpr const char *backup_root = "/data/trophy-manager";

void log(const char *text)
{
    char line[240];
    std::snprintf(line, sizeof line, "TROPHY: %s\n", text);
    sceKernelDebugOutText(0, line);
}
const char *tr(const char *text)
{
    return i18n::tr(text);
}

Color grade_color(int grade)
{
    return grade == 1 ? rgb(166, 214, 255) : grade == 2 ? rgb(255, 196, 36) : grade == 3 ? rgb(196, 202, 212)
                                                                                            : rgb(205, 127, 62);
}
const char *grade_name(int grade)
{
    return grade == 1 ? "Platinum" : grade == 2 ? "Gold" : grade == 3 ? "Silver" : "Bronze";
}

// ---------------------------------------------------------------------------------------------------------------
// State shared between the screen and the worker thread. Everything below `mu` is touched under it.
// ---------------------------------------------------------------------------------------------------------------
struct GameInfo
{
    char id[10]{}, name[96]{}, npwr[16]{};
    int total = 0, earned = 0, total_by[5]{}, earned_by[5]{};
    bool ok = false;
};
struct TrophyInfo
{
    int id = 0, grade = 0;
    bool earned = false;
    std::uint64_t when = 0, icon_offset = 0, icon_size = 0;
    char name[112]{}, detail[220]{};
};
struct IconSlot
{
    std::uint32_t pixels[icon_edge * icon_edge];
    char key[64]{};
    std::uint64_t age = 0;
    int state = 0; // 0 free, 1 loading, 2 ready, 3 failed
    bool dirty = false;
};
enum class Op
{
    None,
    Unlock,
    Lock,
    UnlockAll,
    LockAll,
    UnlockSel,
    LockSel,
    Restore
};
enum class Job
{
    LoadList,
    OpenGame,
    Edit,
    Icon
};
struct Request
{
    Job job = Job::LoadList;
    int game = -1, trophy = -1, slot = -1;
    Op op = Op::None;
    char path[200]{};
    std::uint64_t offset = 0, size = 0;
    std::uint8_t pick[16]{}; // the ticked trophies of a selection edit, one bit per trophy id
};

pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
struct Guard
{
    Guard()
    {
        pthread_mutex_lock(&mu);
    }
    ~Guard()
    {
        pthread_mutex_unlock(&mu);
    }
};

GameInfo games[max_games];
int game_count = 0, visible[max_games], visible_count = 0;
enum { list_idle, list_loading, list_done, list_failed };
int list_state = list_idle;
bool psn_blocked = false, psn_known = false;
char user_id[16]{};
char toast[220]{};
std::uint64_t toast_until = 0;
int open_game = -1, open_state = 0; // 0 none, 1 loading, 2 ready, 3 failed
TrophyInfo trophies[max_trophies];
int trophy_count = 0;
char ucp_path[200]{};
IconSlot icons[icon_count];
std::uint64_t icon_clock = 0;
bool icon_busy = false, edit_busy = false;
std::vector<Request> queue;

// The unlock pop-up: a few can wait their turn; the one on screen starts counting when it is first drawn.
struct Popup
{
    char title[96]{}, name[112]{}, key[64]{}, path[200]{};
    int grade = 0;
    std::uint64_t offset = 0, size = 0, start = 0;
};
constexpr int popup_max = 4;
Popup popups[popup_max];
int popup_count = 0;

// Screen-only state (the screen thread alone).
int screen = 0; // 0 games, 1 trophies
int cursor = 0, scroll = 0, tcursor = 0, tscroll = 0;
int modal = 0; // 0 none, 1 confirm, 2 menu
Op pending_op = Op::None;
int pending_trophy = -1, menu_cursor = 0;
bool picked[max_trophies]; // ticked rows of the open game
constexpr int menu_entries = 7;

int picked_count()
{
    int n = 0;
    Guard g;
    for (int i = 0; i < trophy_count; ++i)
        n += picked[i] ? 1 : 0;
    return n;
}
void clear_picks()
{
    std::memset(picked, 0, sizeof picked);
}

void set_toast(const char *text)
{
    Guard g;
    std::snprintf(toast, sizeof toast, "%s", tr(text));
    toast_until = sceKernelGetProcessTime() + 6000000;
    log(toast);
}
void post(const Request &r)
{
    Guard g;
    queue.push_back(r);
}

// ---------------------------------------------------------------------------------------------------------------
// Worker
// ---------------------------------------------------------------------------------------------------------------
std::string trophy_file(const GameInfo &g)
{
    return std::string("/user/home/") + user_id + "/trophy2/nobackup/data/" + g.npwr + "/TRPTITLE.DAT";
}

bool find_npwr(const std::vector<std::uint8_t> &d, char *out)
{
    for (std::size_t i = 0; i + 12 <= d.size(); ++i)
        if (!std::memcmp(&d[i], "NPWR", 4) && d[i + 9] == '_' && d[i + 10] == '0' && d[i + 11] == '0')
        {
            bool digits = true;
            for (std::size_t k = 4; k < 9; ++k)
                digits = digits && d[i + k] >= '0' && d[i + k] <= '9';
            if (digits)
            {
                std::memcpy(out, &d[i], 12);
                out[12] = 0;
                return true;
            }
        }
    return false;
}

void summarize(GameInfo &g, const std::vector<t2pd::Trophy> &t)
{
    g.total = g.earned = 0;
    std::memset(g.total_by, 0, sizeof g.total_by);
    std::memset(g.earned_by, 0, sizeof g.earned_by);
    for (const t2pd::Trophy &x : t)
    {
        const int grade = x.grade >= 1 && x.grade <= 4 ? x.grade : 4;
        g.total++;
        g.total_by[grade]++;
        if (x.earned)
        {
            g.earned++;
            g.earned_by[grade]++;
        }
    }
}

bool name_less(int a, int b)
{
    return strcasecmp(games[a].name, games[b].name) < 0;
}
void rebuild_visible() // under mu
{
    visible_count = 0;
    for (int i = 0; i < game_count; ++i)
        if (games[i].ok)
            visible[visible_count++] = i;
    std::sort(visible, visible + visible_count, name_less);
}

bool http_text(const char *path, std::string &out)
{
    std::vector<std::uint8_t> body;
    if (!http_get(path, body, 1 << 20))
        return false;
    out.assign(body.begin(), body.end());
    return true;
}

// The two things editing depends on: the PSN block is on, and no game is running.
bool console_ready_for_edit(const char **why)
{
    std::string state;
    if (!http_text("/api/state", state))
    {
        *why = "Could not reach Elf Arsenal. Start it and press Refresh.";
        return false;
    }
    const bool blocked = state.find("\"nanodnsRunning\":true") != std::string::npos;
    // This app is a title too, so it shows up as running itself: only another title counts.
    bool running = state.find("\"gameRunning\":true") != std::string::npos;
    if (running && state.find("\"runningTitleId\":\"PPSA99842\"") != std::string::npos)
        running = false;
    {
        Guard g;
        psn_blocked = blocked;
        psn_known = true;
    }
    if (!blocked)
    {
        *why = "Editing needs the PSN block to be on.";
        return false;
    }
    if (running)
    {
        *why = "Close the running game first.";
        return false;
    }
    return true;
}

void job_load_list(Ftp &ftp)
{
    {
        Guard g;
        list_state = list_loading;
        game_count = visible_count = 0;
    }
    std::string text;
    if (!http_text("/api/state", text))
    {
        Guard g;
        list_state = list_failed;
        return;
    }
    {
        Guard g;
        psn_blocked = text.find("\"nanodnsRunning\":true") != std::string::npos;
        psn_known = true;
    }
    if (http_text("/api/np/info", text))
        if (cJSON *root = cJSON_Parse(text.c_str()))
        {
            const cJSON *id = cJSON_GetObjectItemCaseSensitive(root, "userIdHex");
            if (cJSON_IsString(id) && id->valuestring)
            {
                const char *p = id->valuestring;
                if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))
                    p += 2;
                Guard g;
                std::snprintf(user_id, sizeof user_id, "%s", p);
                for (char *q = user_id; *q; ++q)
                    *q = static_cast<char>(std::tolower(static_cast<unsigned char>(*q)));
            }
            cJSON_Delete(root);
        }
    if (!user_id[0] || !http_text("/appdb", text))
    {
        Guard g;
        list_state = list_failed;
        return;
    }
    cJSON *root = cJSON_Parse(text.c_str());
    const cJSON *titles = root ? cJSON_GetObjectItemCaseSensitive(root, "titles") : nullptr;
    {
        Guard g;
        const cJSON *t = nullptr;
        cJSON_ArrayForEach(t, titles)
        {
            const cJSON *id = cJSON_GetObjectItemCaseSensitive(t, "titleId");
            const cJSON *name = cJSON_GetObjectItemCaseSensitive(t, "titleName");
            if (game_count >= max_games || !cJSON_IsString(id) || !id->valuestring ||
                std::strncmp(id->valuestring, "PPSA", 4) != 0 || std::strlen(id->valuestring) >= sizeof games[0].id)
                continue;
            GameInfo &x = games[game_count++];
            x = GameInfo();
            std::snprintf(x.id, sizeof x.id, "%s", id->valuestring);
            std::snprintf(x.name, sizeof x.name, "%s",
                          cJSON_IsString(name) && name->valuestring ? name->valuestring : id->valuestring);
        }
    }
    if (root)
        cJSON_Delete(root);
    int total;
    {
        Guard g;
        total = game_count;
    }
    std::vector<std::uint8_t> data;
    for (int i = 0; i < total; ++i)
    {
        char id[10], npwr[16];
        {
            Guard g;
            std::snprintf(id, sizeof id, "%s", games[i].id);
        }
        const std::string bind = std::string("/system_data/priv/appmeta/") + id + "/trophy2/npbind.dat";
        if (!ftp.retr(bind, data, 1 << 16) || !find_npwr(data, npwr))
            continue;
        GameInfo probe;
        std::snprintf(probe.npwr, sizeof probe.npwr, "%s", npwr);
        if (!ftp.retr(trophy_file(probe), data, 1 << 20))
            continue;
        std::vector<std::uint8_t> stream;
        std::vector<t2pd::Trophy> list;
        if (!t2pd::to_stream(data.data(), data.size(), stream) || !t2pd::parse(stream, list))
            continue;
        Guard g;
        std::snprintf(games[i].npwr, sizeof games[i].npwr, "%s", npwr);
        summarize(games[i], list);
        games[i].ok = true;
        rebuild_visible();
    }
    Guard g;
    list_state = visible_count ? list_done : list_failed;
}

std::vector<std::string> catalog_languages()
{
    switch (i18n::language)
    {
    case 1:
        return {"pt-BR", "pt-PT", "en-US", "en-GB"};
    case 2:
        return {"es-419", "es-ES", "en-US", "en-GB"};
    default:
        return {"en-US", "en-GB"};
    }
}

void job_open_game(Ftp &ftp, int index)
{
    GameInfo g;
    {
        Guard guard;
        if (index < 0 || index >= game_count)
            return;
        g = games[index];
        open_game = index;
        open_state = 1;
        trophy_count = 0;
    }
    std::vector<std::uint8_t> data, stream;
    std::vector<t2pd::Trophy> list;
    if (!ftp.retr(trophy_file(g), data, 1 << 20) || !t2pd::to_stream(data.data(), data.size(), stream) ||
        !t2pd::parse(stream, list))
    {
        Guard guard;
        open_state = 3;
        return;
    }
    std::sort(list.begin(), list.end(), [](const t2pd::Trophy &a, const t2pd::Trophy &b) { return a.id < b.id; });
    static TrophyInfo loaded[max_trophies];
    int n = 0;
    for (const t2pd::Trophy &x : list)
        if (n < max_trophies)
        {
            TrophyInfo &t = loaded[n++];
            t = TrophyInfo();
            t.id = x.id;
            t.grade = x.grade >= 1 && x.grade <= 4 ? x.grade : 4;
            t.earned = x.earned;
            t.when = x.when;
            std::snprintf(t.name, sizeof t.name, "%s %d", tr("Trophy"), x.id);
        }
    // names, details and pictures come from the game's catalog; without it the list still works
    const std::string ucp_file = std::string("/user/trophy2/nobackup/conf/") + g.npwr + "/TROPHY.UCP";
    std::vector<std::uint8_t> head, index_bytes, json;
    std::vector<ucp::Entry> entries;
    if (ftp.retr(ucp_file, head, 1 << 16, 0, 0x20))
    {
        const std::size_t need = ucp::index_bytes(head.data(), head.size());
        if (need && ftp.retr(ucp_file, index_bytes, 1 << 20, 0, need) &&
            ucp::parse_index(index_bytes.data(), index_bytes.size(), entries))
        {
            for (int i = 0; i < n; ++i)
            {
                char icon[24];
                std::snprintf(icon, sizeof icon, "trop%04d.png", loaded[i].id);
                if (const ucp::Entry *e = ucp::find(entries, icon))
                {
                    loaded[i].icon_offset = e->offset;
                    loaded[i].icon_size = e->size;
                }
            }
            const ucp::Entry *meta = ucp::find_meta(entries, catalog_languages());
            if (meta && meta->size < (1u << 20) && ftp.retr(ucp_file, json, 1 << 20, meta->offset, meta->size))
            {
                json.push_back(0);
                if (cJSON *root = cJSON_Parse(reinterpret_cast<const char *>(json.data())))
                {
                    const cJSON *md = cJSON_GetObjectItemCaseSensitive(root, "metadata");
                    const cJSON *list_json = md ? cJSON_GetObjectItemCaseSensitive(md, "trophyMetadata") : nullptr;
                    const cJSON *item = nullptr;
                    cJSON_ArrayForEach(item, list_json)
                    {
                        const cJSON *id = cJSON_GetObjectItemCaseSensitive(item, "id");
                        const cJSON *name = cJSON_GetObjectItemCaseSensitive(item, "name");
                        const cJSON *detail = cJSON_GetObjectItemCaseSensitive(item, "detail");
                        if (!cJSON_IsString(id) || !id->valuestring)
                            continue;
                        const int want = std::atoi(id->valuestring);
                        for (int i = 0; i < n; ++i)
                            if (loaded[i].id == want)
                            {
                                if (cJSON_IsString(name) && name->valuestring)
                                    std::snprintf(loaded[i].name, sizeof loaded[i].name, "%s", name->valuestring);
                                if (cJSON_IsString(detail) && detail->valuestring)
                                    std::snprintf(loaded[i].detail, sizeof loaded[i].detail, "%s",
                                                  detail->valuestring);
                            }
                    }
                    cJSON_Delete(root);
                }
            }
        }
    }
    Guard guard;
    std::memcpy(trophies, loaded, sizeof(TrophyInfo) * static_cast<std::size_t>(n));
    trophy_count = n;
    std::snprintf(ucp_path, sizeof ucp_path, "%s", ucp_file.c_str());
    open_state = 2;
}

void job_icon(Ftp &ftp, const Request &r)
{
    std::vector<std::uint8_t> data;
    bool ok = ftp.retr(r.path, data, 4 << 20, r.offset, static_cast<std::size_t>(r.size));
    Guard g;
    IconSlot &s = icons[r.slot];
    if (ok && decode_cover(data.data(), data.size(), s.pixels, icon_edge))
    {
        s.state = 2;
        s.dirty = true;
    }
    else
        s.state = 3;
    icon_busy = false;
}

// Pop-ups for what an edit just earned: one for a single trophy, or a count and then the platinum for many.
void add_popup(const char *title, const char *name, int grade, const char *key, const char *path,
               std::uint64_t offset, std::uint64_t size) // under mu
{
    if (popup_count >= popup_max)
        return;
    Popup &p = popups[popup_count++];
    p = Popup();
    std::snprintf(p.title, sizeof p.title, "%s", title);
    std::snprintf(p.name, sizeof p.name, "%s", name);
    std::snprintf(p.key, sizeof p.key, "%s", key);
    std::snprintf(p.path, sizeof p.path, "%s", path);
    p.grade = grade;
    p.offset = offset;
    p.size = size;
}

void queue_popups(const GameInfo &g, int game, const std::vector<int> &newly) // under mu
{
    if (newly.empty() || open_game != game)
        return;
    const TrophyInfo *plat = nullptr;
    char key[64], path[200], title[96];
    for (int id : newly)
        for (int i = 0; i < trophy_count; ++i)
            if (trophies[i].id == id && trophies[i].grade == 1)
                plat = &trophies[i];
    if (newly.size() > 1)
    {
        std::snprintf(key, sizeof key, "G:%s", g.id);
        std::snprintf(path, sizeof path, "/user/appmeta/%s/icon0.png", g.id);
        std::snprintf(title, sizeof title, "%d %s", static_cast<int>(newly.size()), tr("trophies unlocked"));
        add_popup(title, g.name, 0, key, path, 0, 0);
    }
    for (int id : newly)
        for (int i = 0; i < trophy_count; ++i)
        {
            const TrophyInfo &t = trophies[i];
            if (t.id != id || (newly.size() > 1 && &t != plat))
                continue;
            std::snprintf(key, sizeof key, "T:%s:%d", g.npwr, t.id);
            add_popup(t.grade == 1 ? tr("Platinum trophy unlocked!") : tr("Trophy unlocked"), t.name, t.grade, key,
                      ucp_path, t.icon_offset, t.icon_size);
        }
}

// Reads the file, applies the operation on the data, keeps a copy, writes it back and checks it.
void job_edit(Ftp &ftp, const Request &r)
{
    const char *why = nullptr;
    GameInfo g;
    int idx = r.game;
    {
        Guard guard;
        if (idx < 0 || idx >= game_count)
        {
            edit_busy = false;
            return;
        }
        g = games[idx];
    }
    auto fail = [&](const char *text)
    {
        set_toast(text);
        Guard guard;
        edit_busy = false;
    };
    if (!console_ready_for_edit(&why))
        return fail(why);
    const std::time_t now = std::time(nullptr);
    if (now < 1700000000)
        return fail("The console clock is not set.");
    const std::string target = trophy_file(g);
    std::vector<std::uint8_t> file;
    if (!ftp.retr(target, file, 1 << 20))
        return fail("Could not read the trophy file.");
    if (!t2pd::verify(file.data(), file.size()))
        return fail("The trophy file is not valid.");
    const std::string dir = std::string(backup_root) + "/" + g.npwr;
    std::vector<std::uint8_t> result;
    std::vector<std::uint8_t> stream;
    std::vector<int> newly; // the trophies this edit earned, for the pop-ups
    if (r.op == Op::Restore)
    {
        if (!ftp.retr(dir + "/original.dat", result, 1 << 20) || !t2pd::verify(result.data(), result.size()))
            return fail("There is no original copy for this game yet.");
    }
    else
    {
        std::vector<t2pd::Trophy> list;
        if (!t2pd::to_stream(file.data(), file.size(), stream) || !t2pd::parse(stream, list))
            return fail("The trophy file is not valid.");
        std::sort(list.begin(), list.end(), [](const t2pd::Trophy &a, const t2pd::Trophy &b) { return a.id < b.id; });
        bool changed = false;
        std::uint64_t when = t2pd::ticks_from_unix(static_cast<std::int64_t>(now));
        const bool whole = r.op == Op::UnlockAll || r.op == Op::LockAll;
        auto chosen = [&](int id) { return whole || (id >= 0 && id < 128 && ((r.pick[id >> 3] >> (id & 7)) & 1)); };
        if (r.op == Op::Unlock)
        {
            changed = t2pd::unlock(stream, r.trophy, when);
            if (changed)
                newly.push_back(r.trophy);
        }
        else if (r.op == Op::Lock)
            changed = t2pd::lock(stream, r.trophy);
        else if (r.op == Op::UnlockAll || r.op == Op::UnlockSel)
        {
            std::vector<int> order;
            for (const t2pd::Trophy &x : list)
                if (!x.earned && x.grade != 1 && chosen(x.id))
                    order.push_back(x.id);
            for (const t2pd::Trophy &x : list)
                if (!x.earned && x.grade == 1 && chosen(x.id))
                    order.push_back(x.id); // the platinum last
            std::uint64_t t = when - static_cast<std::uint64_t>(order.size()) * 45000000ull;
            for (int id : order)
            {
                t += 45000000ull;
                if (t2pd::unlock(stream, id, t))
                {
                    changed = true;
                    newly.push_back(id);
                }
            }
        }
        else if (r.op == Op::LockAll || r.op == Op::LockSel)
        {
            for (const t2pd::Trophy &x : list)
                if (x.earned && chosen(x.id))
                    changed = t2pd::lock(stream, x.id) || changed;
        }
        if (!changed)
            return fail("Nothing to change.");
        result = t2pd::from_stream(stream);
        if (!t2pd::verify(result.data(), result.size()) || result.size() != file.size())
            return fail("The trophy file is not valid.");
    }
    // A copy first: the very first one is kept for good, and the last few before each edit.
    ftp.make_directory(backup_root);
    ftp.make_directory(dir);
    std::vector<std::uint8_t> existing;
    if (!ftp.retr(dir + "/original.dat", existing, 1 << 20) || existing.empty())
        if (!ftp.stor(dir + "/original.dat", file.data(), file.size()))
            return fail("Could not save. Your copy is safe in /data/trophy-manager.");
    char stamp[40];
    std::snprintf(stamp, sizeof stamp, "/before-%010lld.dat", static_cast<long long>(now));
    if (!ftp.stor(dir + stamp, file.data(), file.size()))
        return fail("Could not save. Your copy is safe in /data/trophy-manager.");
    {
        std::vector<FtpEntry> backups;
        if (ftp.list(dir, backups))
        {
            std::vector<std::string> names;
            for (const FtpEntry &e : backups)
                if (!e.dir && e.name.compare(0, 7, "before-") == 0)
                    names.push_back(e.name);
            std::sort(names.begin(), names.end());
            while (names.size() > 8)
            {
                ftp.remove(dir + "/" + names.front());
                names.erase(names.begin());
            }
        }
    }
    // Write beside the file, check it, then swap.
    const std::string staging = target + ".tm-new";
    std::vector<std::uint8_t> check;
    if (!ftp.stor(staging, result.data(), result.size()) || !ftp.retr(staging, check, 1 << 20) || check != result)
    {
        ftp.remove(staging);
        return fail("Could not save. Your copy is safe in /data/trophy-manager.");
    }
    ftp.remove(target);
    if (!ftp.rename(staging, target))
    {
        // The swap failed after the old file was removed: put the new one in place directly.
        if (!ftp.stor(target, result.data(), result.size()))
            return fail("Could not save. Your copy is safe in /data/trophy-manager.");
        ftp.remove(staging);
    }
    if (!ftp.retr(target, check, 1 << 20) || check != result)
        return fail("Could not save. Your copy is safe in /data/trophy-manager.");
    // Update what the screens show from the file that is now on the console.
    std::vector<std::uint8_t> now_stream;
    std::vector<t2pd::Trophy> after;
    if (t2pd::to_stream(result.data(), result.size(), now_stream) && t2pd::parse(now_stream, after))
    {
        Guard guard;
        summarize(games[idx], after);
        if (open_game == idx)
            for (int i = 0; i < trophy_count; ++i)
                for (const t2pd::Trophy &x : after)
                    if (x.id == trophies[i].id)
                    {
                        trophies[i].earned = x.earned;
                        trophies[i].when = x.when;
                    }
    }
    set_toast("Saved. Open the PS5 trophy list to check.");
    Guard guard;
    queue_popups(g, idx, newly);
    edit_busy = false;
}

void *worker(void *)
{
    static Ftp ftp; // 127.0.0.1:2121
    for (;;)
    {
        Request r;
        bool have = false;
        {
            Guard g;
            if (!queue.empty())
            {
                r = queue.front();
                queue.erase(queue.begin());
                have = true;
            }
        }
        if (!have)
        {
            usleep(20000);
            continue;
        }
        switch (r.job)
        {
        case Job::LoadList:
            job_load_list(ftp);
            break;
        case Job::OpenGame:
            job_open_game(ftp, r.game);
            break;
        case Job::Edit:
            job_edit(ftp, r);
            break;
        case Job::Icon:
            job_icon(ftp, r);
            break;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------------------------------------------
// Pictures: a small cache of decoded icons, one loading at a time.
// ---------------------------------------------------------------------------------------------------------------
// Returns the slot of a ready icon, or -1 (and starts loading it when nothing else is loading).
int icon_for(const char *key, const char *path, std::uint64_t offset, std::uint64_t size)
{
    Guard g;
    int free_slot = -1;
    for (int i = 0; i < icon_count; ++i)
    {
        if (icons[i].state != 0 && !std::strcmp(icons[i].key, key))
        {
            icons[i].age = ++icon_clock;
            return icons[i].state == 2 ? i : -1;
        }
        if (icons[i].state != 1 && (free_slot < 0 || icons[i].age < icons[free_slot].age))
            free_slot = i;
    }
    if (icon_busy || free_slot < 0)
        return -1;
    IconSlot &s = icons[free_slot];
    std::snprintf(s.key, sizeof s.key, "%s", key);
    s.state = 1;
    s.age = ++icon_clock;
    icon_busy = true;
    Request r;
    r.job = Job::Icon;
    r.slot = free_slot;
    std::snprintf(r.path, sizeof r.path, "%s", path);
    r.offset = offset;
    r.size = size;
    queue.push_back(r);
    return -1;
}

// ---------------------------------------------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------------------------------------------
std::uint32_t stick_directions(std::uint8_t x, std::uint8_t y)
{
    const int dx = static_cast<int>(x) - 128, dy = static_cast<int>(y) - 128;
    const int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
    if (ax < 56 && ay < 56)
        return 0;
    if (ax >= ay)
        return dx < 0 ? LEFT : RIGHT;
    return dy < 0 ? UP : DOWN;
}

std::uint32_t buttons()
{
    static int pad = -1;
    static std::uint32_t held = 0;
    static std::uint64_t retry_pad = 0, repeat_at = 0;
    const std::uint64_t now = sceKernelGetProcessTime();
    if (pad < 0 && now >= retry_pad)
    {
        retry_pad = now + 3000000;
        int user = 0;
        sceUserServiceInitialize(nullptr);
        if (sceUserServiceGetInitialUser(&user) == 0 && scePadInit() >= 0)
            pad = scePadOpen(user, 0, 0, nullptr);
        if (pad >= 0)
            log("controller open");
    }
    if (pad < 0)
        return 0;
    std::array<PadSample, 64> samples{};
    int n = scePadRead(pad, samples.data(), 64);
    if (n < 0)
    {
        pad = -1;
        held = 0;
        return 0;
    }
    if (n > 64)
        n = 64;
    std::uint32_t pressed = 0;
    for (int i = 0; i < n; i++)
    {
        const std::uint32_t current = samples[i].buttons | stick_directions(samples[i].analog[0], samples[i].analog[1]);
        pressed |= current & ~held;
        held = current;
    }
    if (pressed & (UP | DOWN | LEFT | RIGHT))
        repeat_at = now + 450000;
    else if ((held & (UP | DOWN | LEFT | RIGHT)) && now > repeat_at)
    {
        pressed |= held & (UP | DOWN | LEFT | RIGHT);
        repeat_at = now + 110000;
    }
    return pressed;
}

constexpr int rows_games = 8, rows_trophies = 10;

void clamp_list(int count, int &cur, int &top, int rows)
{
    if (count <= 0)
    {
        cur = top = 0;
        return;
    }
    cur = std::max(0, std::min(cur, count - 1));
    if (cur < top)
        top = cur;
    if (cur >= top + rows)
        top = cur - rows + 1;
    top = std::max(0, std::min(top, std::max(0, count - rows)));
}

void start_edit(Op op, int trophy)
{
    Request r;
    r.job = Job::Edit;
    r.op = op;
    r.trophy = trophy;
    {
        Guard g;
        if (edit_busy)
            return;
        edit_busy = true;
        r.game = open_game;
        for (int i = 0; i < trophy_count; ++i)
            if (picked[i] && trophies[i].id >= 0 && trophies[i].id < 128)
                r.pick[trophies[i].id >> 3] =
                    static_cast<std::uint8_t>(r.pick[trophies[i].id >> 3] | (1 << (trophies[i].id & 7)));
    }
    clear_picks();
    post(r);
}

void open_selected_game()
{
    int idx = -1;
    {
        Guard g;
        if (cursor >= 0 && cursor < visible_count)
            idx = visible[cursor];
    }
    if (idx < 0)
        return;
    tcursor = tscroll = 0;
    clear_picks();
    screen = 1;
    Request r;
    r.job = Job::OpenGame;
    r.game = idx;
    post(r);
}

void refresh_list()
{
    {
        Guard g;
        if (list_state == list_loading)
            return;
        list_state = list_loading;
        open_state = 0;
        open_game = -1;
    }
    Request r;
    r.job = Job::LoadList;
    post(r);
}

void input(std::uint32_t b)
{
    bool busy;
    int vcount, tcount, state;
    {
        Guard g;
        busy = edit_busy;
        vcount = visible_count;
        tcount = trophy_count;
        state = open_state;
    }
    if (b & L1)
        i18n::language = (i18n::language + 1) % 3;
    if (busy)
        return;
    if (modal == 1) // confirm
    {
        if (b & CIRCLE)
            modal = 0;
        else if (b & CROSS)
        {
            modal = 0;
            start_edit(pending_op, pending_trophy);
        }
        return;
    }
    if (modal == 2) // menu
    {
        if (b & CIRCLE)
            modal = 0;
        if (b & UP)
            menu_cursor = (menu_cursor + menu_entries - 1) % menu_entries;
        if (b & DOWN)
            menu_cursor = (menu_cursor + 1) % menu_entries;
        if (b & CROSS)
        {
            pending_trophy = -1;
            if (menu_cursor == 2 || menu_cursor == 3) // select all / clear the selection
            {
                Guard g;
                for (int i = 0; i < trophy_count; ++i)
                    picked[i] = menu_cursor == 2;
                modal = 0;
            }
            else if ((menu_cursor == 0 || menu_cursor == 1) && picked_count() == 0)
            {
                modal = 0;
                set_toast("No trophies selected.");
            }
            else
            {
                static const Op ops[menu_entries] = {Op::UnlockSel, Op::LockSel, Op::None, Op::None,
                                                     Op::UnlockAll, Op::LockAll, Op::Restore};
                pending_op = ops[menu_cursor];
                modal = 1;
            }
        }
        return;
    }
    if (screen == 0)
    {
        if (b & UP)
            cursor--;
        if (b & DOWN)
            cursor++;
        if (b & L2)
            cursor -= rows_games;
        if (b & R2)
            cursor += rows_games;
        clamp_list(vcount, cursor, scroll, rows_games);
        if ((b & CROSS) && vcount)
            open_selected_game();
        if (b & SQUARE)
            refresh_list();
        return;
    }
    if (b & CIRCLE)
    {
        clear_picks();
        screen = 0;
        return;
    }
    if (state != 2)
        return;
    if (b & UP)
        tcursor--;
    if (b & DOWN)
        tcursor++;
    if (b & L2)
        tcursor -= rows_trophies;
    if (b & R2)
        tcursor += rows_trophies;
    clamp_list(tcount, tcursor, tscroll, rows_trophies);
    if ((b & SQUARE) && tcount)
        picked[tcursor] = !picked[tcursor];
    if (b & R1) // select all, or clear when everything is already ticked
    {
        const bool all = picked_count() == tcount;
        Guard g;
        for (int i = 0; i < tcount; ++i)
            picked[i] = !all;
    }
    if ((b & CROSS) && tcount)
    {
        if (picked_count() > 0)
        {
            menu_cursor = 0;
            modal = 2;
        }
        else
        {
            Guard g;
            pending_trophy = trophies[tcursor].id;
            pending_op = trophies[tcursor].earned ? Op::Lock : Op::Unlock;
            modal = 1;
        }
    }
    if (b & (TRIANGLE | OPTIONS))
    {
        menu_cursor = 0;
        modal = 2;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------------------------------------------
void text(Canvas &c, unsigned x, unsigned y, const char *s, unsigned size, Color color, unsigned width = 1700)
{
    c.label(x, y, tr(s), size, color, width);
}
void raw_text(Canvas &c, unsigned x, unsigned y, const char *s, unsigned size, Color color, unsigned width = 1700)
{
    c.label(x, y, s, size, color, width);
}

void format_when(std::uint64_t ticks, char *out, std::size_t cap)
{
    // The console's C library has no usable localtime, so the date is worked out here (UTC).
    const std::int64_t seconds = t2pd::unix_from_ticks(ticks);
    if (seconds < 0)
    {
        std::snprintf(out, cap, "-");
        return;
    }
    const std::int64_t days = seconds / 86400, rest = seconds % 86400;
    const std::int64_t z = days + 719468, era = z / 146097, doe = z - era * 146097;
    const std::int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const std::int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
    const int day = static_cast<int>(doy - (153 * mp + 2) / 5 + 1), month = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
    const int year = static_cast<int>(yoe + era * 400 + (month <= 2 ? 1 : 0));
    std::snprintf(out, cap, "%04d-%02d-%02d %02d:%02d UTC", year, month, day, static_cast<int>(rest / 3600),
                  static_cast<int>(rest % 3600 / 60));
}

void draw_progress(Canvas &c, unsigned x, unsigned y, unsigned w, unsigned h, int part, int whole)
{
    c.rounded_rectangle(x, y, w, h, h / 2, rgb(48, 52, 60));
    if (whole > 0 && part > 0)
    {
        const unsigned fill = std::max<unsigned>(h, static_cast<unsigned>(static_cast<std::uint64_t>(w) * part / whole));
        c.rounded_rectangle(x, y, std::min(fill, w), h, h / 2, part == whole ? rgb(255, 196, 36) : accent);
    }
}

void draw_icon(Canvas &c, int slot, unsigned x, unsigned y, unsigned size)
{
    if (slot < 0)
    {
        c.rounded_rectangle(x, y, size, size, 12, panel);
        return;
    }
    IconSlot &s = icons[slot];
    if (s.dirty)
    {
        c.invalidate(s.pixels);
        s.dirty = false;
    }
    c.rounded_picture(x, y, size, size, 12, icon_edge, s.pixels);
}

void draw_header(Canvas &c)
{
    text(c, 100, 38, "Trophy Manager", 40, white, 700);
    char who[40];
    std::snprintf(who, sizeof who, "%s", user_id);
    bool known, blocked;
    {
        Guard g;
        known = psn_known;
        blocked = psn_blocked;
    }
    const Color chip = !known ? muted : blocked ? good : bad;
    const char *label = !known ? "Checking the console..." : blocked ? "PSN blocked" : "PSN reachable: editing is off";
    const unsigned w = 560;
    c.rounded_rectangle(1820 - w, 38, w, 46, 23, panel);
    c.circle(1820 - w + 28, 61, 8, chip);
    text(c, 1820 - w + 52, 45, label, 24, chip, w - 70);
}

void draw_games(Canvas &c)
{
    Guard g;
    text(c, 100, 120, "Your games", 44, white, 900);
    if (visible_count == 0)
    {
        const char *msg = list_state == list_failed ? "Could not reach Elf Arsenal. Start it and press Refresh."
                          : list_state == list_done ? "No games with trophies were found."
                                                    : "Loading your games...";
        text(c, 100, 280, msg, 32, list_state == list_failed ? bad : muted, 1500);
    }
    clamp_list(visible_count, cursor, scroll, rows_games);
    for (int row = 0; row < rows_games && scroll + row < visible_count; ++row)
    {
        const int pos = scroll + row;
        const GameInfo &gm = games[visible[pos]];
        const unsigned y = 210 + static_cast<unsigned>(row) * 100;
        const bool focus = pos == cursor;
        if (focus)
            c.rounded_rectangle(90, y - 8, 1740, 92, 16, panel);
        char key[64], path[80];
        std::snprintf(key, sizeof key, "G:%s", gm.id);
        std::snprintf(path, sizeof path, "/user/appmeta/%s/icon0.png", gm.id);
        // icon_for locks the mutex itself: step out of our own lock around the call
        pthread_mutex_unlock(&mu);
        const int slot = icon_for(key, path, 0, 0);
        pthread_mutex_lock(&mu);
        draw_icon(c, slot, 110, y, 76);
        raw_text(c, 214, y + 2, gm.name, 32, white, 700);
        raw_text(c, 214, y + 44, gm.id, 22, muted, 300);
        draw_progress(c, 940, y + 14, 420, 12, gm.earned, gm.total);
        char counts[40];
        std::snprintf(counts, sizeof counts, "%d / %d", gm.earned, gm.total);
        raw_text(c, 1384, y - 2, counts, 30, white, 200);
        char pct[16];
        std::snprintf(pct, sizeof pct, "%d%%", gm.total ? gm.earned * 100 / gm.total : 0);
        raw_text(c, 1384, y + 40, pct, 22, muted, 120);
        for (int grade = 1; grade <= 4; ++grade)
        {
            const unsigned px = 940 + static_cast<unsigned>(grade - 1) * 105;
            c.circle(px + 8, y + 56, 7, grade_color(grade));
            char n[24];
            std::snprintf(n, sizeof n, "%d/%d", gm.earned_by[grade], gm.total_by[grade]);
            raw_text(c, px + 24, y + 44, n, 20, muted, 80);
        }
    }
}

void draw_trophies(Canvas &c)
{
    Guard g;
    if (open_game < 0 || open_game >= game_count)
        return;
    const GameInfo &gm = games[open_game];
    raw_text(c, 100, 112, gm.name, 40, white, 1000);
    char line[120];
    int ticked = 0;
    for (int i = 0; i < trophy_count; ++i)
        ticked += picked[i] ? 1 : 0;
    if (ticked)
        std::snprintf(line, sizeof line, "%d / %d %s  ·  %d%%  ·  %d %s", gm.earned, gm.total, tr("earned"),
                      gm.total ? gm.earned * 100 / gm.total : 0, ticked, tr("selected"));
    else
        std::snprintf(line, sizeof line, "%d / %d %s  ·  %d%%", gm.earned, gm.total, tr("earned"),
                      gm.total ? gm.earned * 100 / gm.total : 0);
    raw_text(c, 100, 162, line, 24, muted, 600);
    draw_progress(c, 720, 126, 520, 12, gm.earned, gm.total);
    if (open_state != 2)
    {
        text(c, 100, 300, open_state == 3 ? "Could not read the trophy file." : "Loading trophies...", 32,
             open_state == 3 ? bad : muted, 1500);
        return;
    }
    clamp_list(trophy_count, tcursor, tscroll, rows_trophies);
    for (int row = 0; row < rows_trophies && tscroll + row < trophy_count; ++row)
    {
        const int pos = tscroll + row;
        const TrophyInfo &t = trophies[pos];
        const unsigned y = 210 + static_cast<unsigned>(row) * 80;
        if (pos == tcursor)
            c.rounded_rectangle(90, y - 6, 1740, 74, 14, panel);
        c.rounded_rectangle(96, y + 6, 6, 50, 3, grade_color(t.grade));
        char key[64];
        std::snprintf(key, sizeof key, "T:%s:%d", gm.npwr, t.id);
        pthread_mutex_unlock(&mu);
        const int slot = t.icon_size ? icon_for(key, ucp_path, t.icon_offset, t.icon_size) : -1;
        pthread_mutex_lock(&mu);
        c.rounded_rectangle(114, y + 8, 38, 38, 9, picked[pos] ? accent : rgb(92, 96, 106));
        if (picked[pos])
            c.rounded_rectangle(124, y + 18, 18, 18, 4, white);
        else
            c.rounded_rectangle(118, y + 12, 30, 30, 7, bg);
        draw_icon(c, slot, 170, y - 2, 62);
        const Color dim = t.earned ? white : rgb(168, 172, 182);
        raw_text(c, 256, y - 4, t.name, 30, dim, 1130);
        if (t.detail[0])
            raw_text(c, 256, y + 30, t.detail, 22, muted, 1130);
        raw_text(c, 1400, y + 30, tr(grade_name(t.grade)), 22, grade_color(t.grade), 200);
        if (t.earned)
        {
            char when[40];
            format_when(t.when, when, sizeof when);
            raw_text(c, 1400, y - 4, when, 24, good, 420);
        }
        else
            text(c, 1400, y - 4, "Locked", 26, muted, 300);
    }
}

const char *confirm_question(Op op)
{
    switch (op)
    {
    case Op::Unlock:
        return "Unlock this trophy?";
    case Op::Lock:
        return "Lock this trophy again?";
    case Op::UnlockAll:
        return "Unlock every trophy of this game, the platinum last?";
    case Op::LockAll:
        return "Lock every trophy of this game?";
    case Op::UnlockSel:
        return "Unlock the selected trophies?";
    case Op::LockSel:
        return "Lock the selected trophies?";
    default:
        return "Put back the first copy saved before any edit?";
    }
}

void draw_modal(Canvas &c)
{
    bool busy;
    {
        Guard g;
        busy = edit_busy;
    }
    if (!modal && !busy)
        return;
    c.dim();
    if (modal == 2)
        c.rounded_rectangle(440, 220, 1040, 600, 24, panel);
    else
        c.rounded_rectangle(440, 320, 1040, 400, 24, panel);
    if (busy)
    {
        text(c, 500, 480, "Working...", 44, white, 900);
        return;
    }
    if (modal == 1)
    {
        text(c, 500, 370, confirm_question(pending_op), 34, white, 920);
        if (pending_op == Op::UnlockSel || pending_op == Op::LockSel)
        {
            char count[48];
            std::snprintf(count, sizeof count, "%d %s", picked_count(), tr("selected"));
            raw_text(c, 500, 450, count, 30, accent, 920);
        }
        if (pending_trophy >= 0)
        {
            Guard g;
            for (int i = 0; i < trophy_count; ++i)
                if (trophies[i].id == pending_trophy)
                    raw_text(c, 500, 450, trophies[i].name, 30, accent, 920);
        }
        text(c, 500, 540, "A copy of the file is saved first.", 24, muted, 920);
        text(c, 500, 580, "Edits stay on this console and never reach PSN.", 24, muted, 920);
        text(c, 500, 650, "X  Confirm        O  Cancel", 28, white, 920);
        return;
    }
    static const char *const entries[menu_entries] = {"Unlock selected", "Lock selected", "Select all",
                                                      "Clear selection", "Unlock all",      "Lock all",
                                                      "Restore original"};
    text(c, 500, 250, "Menu", 36, white, 900);
    for (int i = 0; i < menu_entries; ++i)
    {
        const unsigned y = 310 + static_cast<unsigned>(i) * 62;
        if (i == menu_cursor)
            c.rounded_rectangle(480, y - 8, 960, 54, 14, accent);
        text(c, 510, y, entries[i], 30, white, 880);
    }
}

void draw_footer(Canvas &c)
{
    const char *hint = screen == 0 ? "X  Open     Square  Refresh     L2 / R2  Page     L1  Language"
                                   : "X  Unlock / Lock     Square  Select     R1  All     Triangle  Menu     O  Back     L1  Language";
    text(c, 100, 1010, hint, 24, muted, 1500);
    Guard g;
    if (toast[0] && sceKernelGetProcessTime() < toast_until)
    {
        const unsigned w = std::min<unsigned>(1500, c.measure(toast, 28) + 60);
        c.rounded_rectangle(960 - w / 2, 930, w, 54, 27, panel);
        raw_text(c, 960 - w / 2 + 30, 940, toast, 28, white, w - 40);
    }
}

// The unlock pop-up: rises from the bottom edge, shines once, holds, then sinks again. Platinum lasts longer and
// pulses its border.
void draw_popup(Canvas &c)
{
    Popup p;
    {
        Guard g;
        const std::uint64_t now = sceKernelGetProcessTime();
        while (popup_count && popups[0].start &&
               now >= popups[0].start + (popups[0].grade == 1 ? 6500000u : 4500000u))
        {
            for (int i = 1; i < popup_count; ++i)
                popups[i - 1] = popups[i];
            popup_count--;
        }
        if (!popup_count)
            return;
        if (!popups[0].start)
            popups[0].start = now;
        p = popups[0];
    }
    const std::uint64_t now = sceKernelGetProcessTime();
    const float life = p.grade == 1 ? 6.5f : 4.5f;
    const float t = static_cast<float>(now - p.start) / 1000000.0f;
    float shown = 1.0f; // 0 hidden, 1 in place
    if (t < 0.5f)
        shown = 1.0f - (1.0f - t / 0.5f) * (1.0f - t / 0.5f) * (1.0f - t / 0.5f);
    else if (t > life - 0.45f)
    {
        const float u = std::max(0.0f, (life - t) / 0.45f);
        shown = u * u;
    }
    const unsigned w = 860, h = 116, x = (1920 - w) / 2;
    const unsigned y = 1080 - static_cast<unsigned>(static_cast<float>(1080 - 800) * shown);
    const Color tone = p.grade ? grade_color(p.grade) : accent;
    Color edge = tone;
    if (p.grade == 1)
        edge = (static_cast<int>(t * 4.0f) & 1) ? rgb(255, 255, 255) : tone;
    c.rounded_rectangle(x - 5, y - 5, w + 10, h + 10, 34, edge);
    c.rounded_rectangle(x, y, w, h, 30, rgb(18, 20, 26));
    const int slot = icon_for(p.key, p.path, p.offset, p.size);
    draw_icon(c, slot, x + 18, y + 14, 88);
    raw_text(c, x + 128, y + 14, p.title, 26, tone, w - 160);
    raw_text(c, x + 128, y + 50, p.name, 34, white, w - 160);
    if (t > 0.5f && t < 1.5f)
    {
        const float u = (t - 0.5f) / 1.0f;
        c.blend(x + 20 + static_cast<unsigned>(u * static_cast<float>(w - 160)), y + 12, 70, h - 24, white, 36);
    }
}

void draw(Canvas &c) noexcept
{
    input(buttons());
    c.clear(bg);
    draw_header(c);
    if (screen == 0)
        draw_games(c);
    else
        draw_trophies(c);
    draw_footer(c);
    draw_modal(c);
    draw_popup(c);
}

// PS5 system language: Brazilian (17) and European (7) Portuguese, Spanish (3) and Latin American Spanish (20).
void detect_language()
{
    int value = 1;
    if (sceSystemServiceParamGetInt(1, &value) == 0)
        i18n::language = (value == 17 || value == 7) ? 1 : (value == 3 || value == 20) ? 2 : 0;
}
} // namespace

int main()
{
    detect_language();
    pthread_t thread;
    if (pthread_create(&thread, nullptr, worker, nullptr) == 0)
        pthread_detach(thread);
    refresh_list();
    estante::run_gl(draw);
}
