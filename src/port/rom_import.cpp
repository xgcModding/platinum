#include "rom_import.h"
#include "port/rom_assets.h"
#include <SDL.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#else
#include <unistd.h>
#endif

extern "C" void SIM_ShowMissingResourceScreen(const char *) __attribute__((weak));

namespace {
namespace fs = std::filesystem;
std::mutex stateMutex;
std::string statusText;
int state;
bool restartPending;
SDL_Thread *worker;
std::atomic<bool> cancel{false};
struct Asset { fs::path path; uint32_t start, size; };
using File = std::unique_ptr<SDL_RWops, decltype(&SDL_RWclose)>;
void fail(const std::string &text) { throw std::runtime_error(text); }
uint16_t u16(const unsigned char *p) { return p[0] | (uint16_t(p[1]) << 8); }
uint32_t u32(const unsigned char *p) { return u16(p) | (uint32_t(u16(p + 2)) << 16); }
std::string lower(std::string s) {
    for (char &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
void progress(const std::string &text) {
    std::lock_guard<std::mutex> lock(stateMutex);
    if (state == 1) statusText = text;
}
void read(SDL_RWops *rom, uint64_t offset, void *data, size_t size) {
    if (SDL_RWseek(rom, static_cast<Sint64>(offset), RW_SEEK_SET) < 0 || SDL_RWread(rom, data, 1, size) != size)
        fail("The ROM is truncated or could not be read.");
}
void validName(const std::string &name) {
    if (name.empty() || name == "." || name == ".." || name.back() == '.' || name.back() == ' ')
        fail("The ROM contains an invalid asset name.");
    for (unsigned char c : name) if (c < 32 || c > 126 || std::strchr("<>:\"/\\|?*", c))
        fail("The ROM contains an unsafe asset name.");
    auto stem = lower(name.substr(0, name.find('.')));
    if (stem == "con" || stem == "prn" || stem == "aux" || stem == "nul"
        || (stem.size() == 4 && (stem.substr(0, 3) == "com" || stem.substr(0, 3) == "lpt") && stem[3] >= '1' && stem[3] <= '9'))
        fail("The ROM contains a reserved asset name.");
}
std::vector<Asset> manifest(SDL_RWops *rom) {
    auto length = SDL_RWsize(rom);
    if (length < 512 || length > 0x100000000LL) fail("This is not a supported Nintendo DS ROM.");
    std::array<unsigned char, 512> header{};
    read(rom, 0, header.data(), header.size());
    const bool spanish = std::memcmp(header.data() + 12, "CPUS", 4) == 0;
    if (std::memcmp(header.data(), "POKEMON PL", 10) || std::memcmp(header.data() + 12, "CPU", 3))
        fail("Select a Pokemon Platinum ROM.");
    uint32_t fntOffset = u32(header.data() + 0x40), fntSize = u32(header.data() + 0x44);
    uint32_t fatOffset = u32(header.data() + 0x48), fatSize = u32(header.data() + 0x4c);
    auto inRange = [length](uint32_t start, uint32_t size) { return start >= 512 && uint64_t(start) + size <= uint64_t(length); };
    if (!inRange(fntOffset, fntSize) || !inRange(fatOffset, fatSize) || fntSize < 8 || fntSize > 16 * 1024 * 1024
        || !fatSize || fatSize % 8 || fatSize / 8 > 65535) fail("The ROM has invalid filesystem tables.");
    std::vector<unsigned char> fnt(fntSize), fat(fatSize);
    read(rom, fntOffset, fnt.data(), fnt.size()); read(rom, fatOffset, fat.data(), fat.size());
    unsigned directories = u16(fnt.data() + 6);
    if (!directories || directories > 4096 || directories * 8 > fntSize) fail("The ROM has an invalid directory table.");
    std::vector<bool> visited(directories), used(fatSize / 8);
    std::set<std::string> paths;
    std::vector<Asset> assets;
    const std::set<std::string> roots = {"application","arc","battle","contest","data","debug","demo","dwc","fielddata",
        "frontier","graphic","itemtool","msgdata","particledata","pokeanime","poketool","resource","wazaeffect"};
    std::function<void(unsigned, const fs::path &, unsigned)> visit = [&](unsigned id, const fs::path &parent, unsigned depth) {
        if (id >= directories || visited[id] || depth > 64) fail("The ROM has cyclic or invalid directories.");
        visited[id] = true;
        size_t offset = u32(fnt.data() + id * 8);
        unsigned fileID = u16(fnt.data() + id * 8 + 4);
        if (offset < directories * 8 || offset >= fnt.size()) fail("The ROM has an invalid directory offset.");
        for (;;) {
            if (offset >= fnt.size()) fail("The ROM directory listing is truncated.");
            unsigned type = fnt[offset++], size = type & 127;
            if (!type) break;
            if (!size || offset + size > fnt.size()) fail("The ROM has an invalid filename.");
            std::string name(reinterpret_cast<char *>(fnt.data() + offset), size); offset += size;
            validName(name);
            fs::path path = parent / fs::u8path(name);
            auto key = lower(path.generic_u8string());
            if (key.size() > 1024 || !paths.insert(key).second) fail("The ROM has duplicate or oversized asset paths.");
            if (parent.empty() && !roots.count(lower(name))) fail("The ROM contains an unexpected resource directory.");
            if (type & 128) {
                if (offset + 2 > fnt.size()) fail("The ROM directory listing is truncated.");
                unsigned child = u16(fnt.data() + offset); offset += 2;
                if (child < 0xf000 || child - 0xf000 >= directories
                    || u16(fnt.data() + (child - 0xf000) * 8 + 6) != 0xf000 + id)
                    fail("The ROM has an invalid parent directory.");
                visit(child - 0xf000, path, depth + 1);
            } else {
                if (parent.empty() || fileID >= used.size() || used[fileID]) fail("The ROM has an invalid file identifier.");
                used[fileID] = true;
                uint32_t start = u32(fat.data() + fileID * 8), end = u32(fat.data() + fileID * 8 + 4);
                if (end < start || !inRange(start, end - start)) fail("An asset points outside the ROM.");
                assets.push_back({path, start, end - start}); ++fileID;
            }
        }
    };
    visit(0, {}, 0);
    if (std::find(visited.begin(), visited.end(), false) != visited.end() || !paths.count("graphic/pl_font.narc"))
        fail("The ROM is missing required Platinum resources.");
    auto ordered = assets;
    std::sort(ordered.begin(), ordered.end(), [](const Asset &a, const Asset &b) { return a.start < b.start; });
    uint64_t previousEnd = 0;
    for (const auto &asset : ordered) {
        if (asset.size && asset.start < previousEnd) fail("The ROM contains overlapping asset ranges.");
        previousEnd = std::max(previousEnd, uint64_t(asset.start) + asset.size);
    }
    // Localized graphics use different paths; keep the engine's resource names.
    {
        static const struct { const char *source; const char *destination; } localized[] = {
            {"b_plist/b_plist_gra.narc", "battle/graphic/b_plist_gra.narc"},
            {"b_plist/pl_b_plist_gra.narc", "battle/graphic/pl_b_plist_gra.narc"},
            {"bag/bag_gra.narc", "graphic/bag_gra.narc"},
            {"bag/pl_bag_gra.narc", "graphic/pl_bag_gra.narc"},
            {"batt_rec/batt_rec_gra.narc", "resource/eng/batt_rec/batt_rec_gra.narc"},
            {"battle_graphic/batt_obj.narc", "battle/graphic/batt_obj.narc"},
            {"battle_graphic/pl_batt_obj.narc", "battle/graphic/pl_batt_obj.narc"},
            {"box/box.narc", "graphic/box.narc"},
            {"contest_graphic/contest_bg.narc", "contest/graphic/contest_bg.narc"},
            {"contest_graphic/contest_obj.narc", "contest/graphic/contest_obj.narc"},
            {"dress_up_graphic/imageclip.narc", "graphic/imageclip.narc"},
            {"frontier_graphic/frontier_bg.narc", "resource/eng/frontier_graphic/frontier_bg.narc"},
            {"frontier_graphic/frontier_obj.narc", "resource/eng/frontier_graphic/frontier_obj.narc"},
            {"mysterycard/mystery.narc", "graphic/mystery.narc"},
            {"nameinput/namein.narc", "data/namein.narc"},
            {"nutmixer/nutmixer.narc", "graphic/nutmixer.narc"},
            {"opening_demo/op_demo.narc", "demo/title/op_demo.narc"},
            {"p_status/pl_pst_gra.narc", "graphic/pl_pst_gra.narc"},
            {"p_status/pst_gra.narc", "graphic/pst_gra.narc"},
            {"pms_aikotoba/pms_aikotoba.narc", "resource/eng/pms_aikotoba/pms_aikotoba.narc"},
            {"pokelist/pl_plist_gra.narc", "graphic/pl_plist_gra.narc"},
            {"pokelist/plist_gra.narc", "graphic/plist_gra.narc"},
            {"pokemon_trade/fld_trade.narc", "fielddata/pokemon_trade/fld_trade.narc"},
            {"poketch/poketch.narc", "graphic/poketch.narc"},
            {"scratch/scratch.narc", "resource/eng/scratch/scratch.narc"},
            {"slot/slot.narc", "data/slot.narc"},
            {"title/titledemo.narc", "demo/title/titledemo.narc"},
            {"touch_subwindow/touch_subwindow.narc", "graphic/touch_subwindow.narc"},
            {"trainer_case/trainer_case.narc", "graphic/trainer_case.narc"},
            {"vs_demo/vs_demo_gra.narc", "battle/graphic/vs_demo_gra.narc"},
            {"wifi_lobby_minigame/wlmngm_tool.narc", "resource/eng/wifi_lobby_minigame/wlmngm_tool.narc"},
            {"zukan/zukan.narc", "resource/eng/zukan/zukan.narc"},
        };
        std::set<std::string> destinations;
        for (auto &asset : assets) {
            auto resource = asset.path.generic_u8string();
            if (resource.compare(0, 9, "resource/") == 0) {
                auto languageEnd = resource.find('/', 9);
                if (languageEnd != std::string::npos) resource.erase(0, languageEnd + 1);
            }
            for (const auto &entry : localized) {
                if (resource == entry.source) {
                    asset.path = entry.destination;
                    break;
                }
            }
            if (!destinations.insert(lower(asset.path.generic_u8string())).second)
                fail("The ROM has duplicate localized asset paths.");
        }
    }
    // These two bitmaps live in executable sections of the original ROM.
    // Store only their locations and fingerprints in the port, never their pixels.
    uint32_t arm9Start = u32(header.data() + 0x20), arm9Size = u32(header.data() + 0x2c);
    if (arm9Size) {
        auto bitmap = [&](const char *name, uint32_t start, uint32_t size,
                          uint32_t hint, uint32_t bytes, uint64_t fingerprint,
                          uint32_t fingerprintOffset = 0, uint32_t fingerprintBytes = 0) {
            if (!inRange(start, size) || size > 4 * 1024 * 1024 || size < bytes)
                fail("The ROM has an invalid embedded graphics section.");
            std::vector<unsigned char> section(size);
            read(rom, start, section.data(), size);
            if (!fingerprintBytes) fingerprintBytes = bytes;
            auto matches = [&](uint32_t offset) {
                if (offset > size - bytes) return false;
                uint64_t hash = UINT64_C(14695981039346656037);
                for (uint32_t i = 0; i < fingerprintBytes; ++i)
                    hash = (hash ^ section[offset + fingerprintOffset + i]) * UINT64_C(1099511628211);
                return hash == fingerprint;
            };
            if (!matches(hint)) {
                hint = 0;
                while (hint <= size - bytes && !matches(hint)) hint += 4;
                if (hint > size - bytes) fail("The ROM's embedded graphics are not supported.");
            }
            assets.push_back({name, start + hint, bytes});
        };
        bitmap("port/arrow_cursor.4bpp", arm9Start, arm9Size,
               spanish ? 937196 : 937032, 64, UINT64_C(0x0e689a614e43e0c0));
        uint32_t overlayStart = u32(header.data() + 0x50), overlaySize = u32(header.data() + 0x54);
        if (!inRange(overlayStart, overlaySize) || overlaySize % 32 || overlaySize > 65536)
            fail("The ROM has an invalid overlay table.");
        bool found = false;
        for (uint32_t offset = 0; offset < overlaySize; offset += 32) {
            std::array<unsigned char, 32> entry{};
            read(rom, overlayStart + offset, entry.data(), entry.size());
            if (u32(entry.data()) != 16) continue;
            uint32_t id = u32(entry.data() + 24);
            if (id >= fatSize / 8) fail("The ROM has an invalid battle overlay.");
            uint32_t start = u32(fat.data() + id * 8), end = u32(fat.data() + id * 8 + 4);
            if (end < start) fail("The ROM has an invalid battle overlay.");
            bitmap("port/healthbox_parts.4bpp", start, end - start,
                   spanish ? 214644 : 214380, 2496,
                   // Gauge tiles are shared across languages; HP/PV labels are not.
                   UINT64_C(0x4d4efea57590f969), 832, 128);
            found = true;
            break;
        }
        if (!found) fail("The ROM is missing the battle graphics overlay.");
    }
    assets.push_back({"header.bin", 0, 512});
    return assets;
}
void safeDestination(const fs::path &root, const fs::path &relative) {
    auto path = root;
    for (const auto &part : relative) {
        path /= part;
        std::error_code error;
        auto status = fs::symlink_status(path, error);
        if (error && error != std::errc::no_such_file_or_directory) fail("Unable to inspect the destination folder.");
        if (fs::is_symlink(status)) fail("The destination contains a symbolic link. Use a regular resource folder.");
#ifdef _WIN32
        DWORD attributes = GetFileAttributesW(path.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
            fail("The destination contains a redirected folder.");
#endif
        if (fs::exists(status) && path != root / relative && !fs::is_directory(status)) fail("An asset folder is blocked by a file.");
    }
    auto target = root / relative;
    if (fs::exists(target) && !fs::is_regular_file(target)) fail("An asset destination is not a regular file.");
}
fs::path executableDirectory() {
#ifdef _WIN32
    std::vector<wchar_t> path(32768);
    DWORD length = GetModuleFileNameW(NULL, path.data(), static_cast<DWORD>(path.size()));
    if (!length || length >= path.size()) fail("Unable to locate the executable folder.");
    return fs::canonical(fs::path(path.data())).parent_path();
#elif defined(__APPLE__)
    uint32_t size = 0; _NSGetExecutablePath(nullptr, &size);
    std::vector<char> path(size);
    if (_NSGetExecutablePath(path.data(), &size)) fail("Unable to locate the executable folder.");
    fs::path directory = fs::canonical(fs::u8path(path.data())).parent_path();
    // Like an AppImage, the app contains code; imported assets, mods, saves
    // and settings belong beside the user-visible application, outside it.
    if (directory.filename() == "MacOS" && directory.parent_path().filename() == "Contents"
        && directory.parent_path().parent_path().extension() == ".app") {
        return directory.parent_path().parent_path().parent_path();
    }
    return directory;
#else
    // AppImage runs an internal executable from a read-only image. Resources
    // and configuration belong beside the original, user-visible AppImage.
    const char *appImage = std::getenv("APPIMAGE");
    if (appImage && *appImage) {
        fs::path image = fs::u8path(appImage);
        if (!image.is_absolute() || !fs::is_regular_file(image))
            fail("Unable to locate the AppImage folder.");
        return fs::canonical(image).parent_path();
    }
    return fs::canonical("/proc/self/exe").parent_path();
#endif
}
void extract(const char *filename, const fs::path &destination) {
    File rom(SDL_RWFromFile(filename, "rb"), SDL_RWclose);
    if (!rom) fail("Unable to read the selected ROM.");
    auto assets = manifest(rom.get()); // Validate every entry before creating output.
    fs::path root = fs::canonical(destination) / "assets";
    if (fs::is_symlink(fs::symlink_status(root)))
        fail("The assets folder must not be a symbolic link.");
#ifdef _WIN32
    DWORD attributes = GetFileAttributesW(root.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
        fail("The assets folder must not be redirected.");
#endif
    fs::create_directory(root);
    for (const auto &asset : assets) safeDestination(root, asset.path);
    fs::path staging;
    for (unsigned i = 0; i < 32; ++i) {
        auto candidate = root / (".rom-import-" + std::to_string(SDL_GetPerformanceCounter()) + "-" + std::to_string(i));
        if (fs::create_directory(candidate)) { staging = candidate; break; }
    }
    if (staging.empty()) fail("Unable to create a staging folder in the assets folder.");
    struct Installed { fs::path path; bool backedUp = false, installed = false; };
    std::vector<Installed> installed;
    bool keepBackup = false;
    try {
        std::array<unsigned char, 65536> buffer{};
        for (size_t i = 0; i < assets.size(); ++i) {
            if (cancel) fail("ROM import canceled.");
            const auto &asset = assets[i];
            auto output = staging / "new" / asset.path;
            fs::create_directories(output.parent_path());
            File file(SDL_RWFromFile(output.u8string().c_str(), "wb"), SDL_RWclose);
            if (!file) fail("Unable to write assets in the assets folder. Check folder permissions and free space.");
            uint32_t offset = 0;
            while (offset < asset.size) {
                if (cancel) fail("ROM import canceled.");
                size_t size = std::min<size_t>(buffer.size(), asset.size - offset);
                read(rom.get(), uint64_t(asset.start) + offset, buffer.data(), size);
                if (SDL_RWwrite(file.get(), buffer.data(), 1, size) != size) fail("Unable to write ROM assets. Check free disk space.");
                offset += static_cast<uint32_t>(size);
            }
            if (SDL_RWclose(file.release())) fail("Unable to finish writing a ROM asset.");
            progress("Extracting assets: " + std::to_string(i + 1) + " / " + std::to_string(assets.size()));
        }
        if (cancel) fail("ROM import canceled.");
        progress("Installing extracted assets...");
        installed.reserve(assets.size());
        for (const auto &asset : assets) {
            safeDestination(root, asset.path);
            installed.push_back({asset.path});
            auto &entry = installed.back();
            auto target = root / asset.path;
            fs::create_directories(target.parent_path());
            if (fs::exists(target)) {
                auto backup = staging / "backup" / asset.path;
                fs::create_directories(backup.parent_path()); fs::rename(target, backup); entry.backedUp = true;
            }
            fs::rename(staging / "new" / asset.path, target); entry.installed = true;
        }
    } catch (...) {
        for (auto it = installed.rbegin(); it != installed.rend(); ++it) {
            std::error_code error;
            if (it->installed) fs::remove(root / it->path, error);
            if (error) keepBackup = true;
            if (it->backedUp) { fs::rename(staging / "backup" / it->path, root / it->path, error); if (error) keepBackup = true; }
        }
        if (!keepBackup) { std::error_code error; fs::remove_all(staging, error); }
        else fail("Import failed. Original assets are preserved in " + staging.u8string());
        throw;
    }
    std::error_code error; fs::remove_all(staging, error);
}
int runImport(void *argument) {
    std::unique_ptr<std::string> filename(static_cast<std::string *>(argument));
    std::string text; int result;
    try { extract(filename->c_str(), executableDirectory()); text = "ROM imported. Starting the game..."; result = 2; }
    catch (const fs::filesystem_error &) { text = "Unable to write assets in the assets folder. Check folder permissions and free disk space."; result = 3; }
    catch (const std::exception &e) { text = e.what(); result = 3; }
    std::lock_guard<std::mutex> lock(stateMutex); statusText = text; state = result; restartPending = result == 2; return 0;
}
}
extern "C" int RomImport_GetStatus(char *text, size_t capacity) {
    std::lock_guard<std::mutex> lock(stateMutex); if (capacity) SDL_strlcpy(text, statusText.c_str(), capacity); return state;
}
extern "C" void RomImport_ReportError(const char *text) {
    std::lock_guard<std::mutex> lock(stateMutex); if (state != 1) { state = 3; statusText = text; }
}
extern "C" void RomImport_Start(const char *filename) {
    std::lock_guard<std::mutex> lock(stateMutex);
    if (state == 1 || !filename || !*filename) return;
    if (worker) { SDL_WaitThread(worker, nullptr); worker = nullptr; }
    cancel = false; restartPending = false; state = 1; statusText = "Validating ROM...";
    auto argument = std::make_unique<std::string>(filename);
    worker = SDL_CreateThread(runImport, "ROM import", argument.get());
    if (worker) argument.release(); else { state = 3; statusText = "Unable to start ROM extraction."; }
}
extern "C" void RomImport_Shutdown(void) {
    cancel = true;
    if (worker) { SDL_WaitThread(worker, nullptr); worker = nullptr; }
}
extern "C" int RomImport_ExtractTo(const char *filename, const char *directory, char *error, size_t capacity) {
    try { cancel = false; extract(filename, fs::u8path(directory)); if (capacity) error[0] = 0; return 1; }
    catch (const std::exception &e) { if (capacity) SDL_strlcpy(error, e.what(), capacity); return 0; }
}

extern "C" int RomImport_UseExecutableDirectory(void) {
    try {
        fs::current_path(executableDirectory());
        // DWC locates its four settings pages through the firmware header.
        // The old zero-filled placeholder made this address underflow.
        const fs::path settings = "firmware.bin";
        if (!fs::exists(settings)) {
            std::array<unsigned char, 1584> data{};
            data[0x20] = 0xc0; // 0xc0 * 8 - 0x400 = 0x200.
            FILE *file = std::fopen("firmware.bin", "wb");
            if (!file) return 0;
            bool ok = std::fwrite(data.data(), 1, data.size(), file) == data.size();
            ok = std::fclose(file) == 0 && ok;
            if (!ok) return 0;
        } else if (fs::file_size(settings) == 1584) {
            FILE *file = std::fopen("firmware.bin", "rb+");
            if (!file) return 0;
            unsigned char header[2]{};
            std::fseek(file, 0x20, SEEK_SET);
            bool legacy = std::fread(header, 1, 2, file) == 2 && !header[0] && !header[1];
            if (legacy) {
                // Preserve all settings bytes; repair only the missing pointer.
                std::fseek(file, 0x20, SEEK_SET);
                header[0] = 0xc0;
                if (std::fwrite(header, 1, 2, file) != 2) { std::fclose(file); return 0; }
            }
            if (std::fclose(file) != 0) return 0;
        }
        return 1;
    }
    catch (const std::exception &) { return 0; }
}

extern "C" FILE *RomImport_OpenAsset(const char *path) {
    // The working directory is the external executable/AppImage directory.
    try {
        std::string assetPath = std::string("assets/") + path;
        return std::fopen(assetPath.c_str(), "rb");
    } catch (...) { return nullptr; }
}

extern "C" int RomAssets_Read(const char *path, void *destination, size_t size) {
    FILE *file = RomImport_OpenAsset(path);
    bool success = false;
    if (file) {
        success = std::fread(destination, 1, size, file) == size;
        std::fclose(file);
    }
    // Embedded ROM graphics are as mandatory as filesystem assets. Never let
    // a missing or truncated bitmap become blank tiles in a running game.
    if (!success && SIM_ShowMissingResourceScreen) {
        SIM_ShowMissingResourceScreen(path);
    }
    return success;
}

extern "C" int RomImport_TakeRestartRequest(void) {
    std::lock_guard<std::mutex> lock(stateMutex);
    bool pending = restartPending;
    restartPending = false;
    return pending;
}
