#include "android_paths.h"

#include <algorithm>
#include <cctype>

namespace mh3u::boot {
namespace stdfs = std::filesystem;

std::string url_decode(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    const auto hex = [](char c) {
        return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
    };
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size() && hex(s[i + 1]) >= 0 && hex(s[i + 2]) >= 0) {
            out += static_cast<char>(hex(s[i + 1]) * 16 + hex(s[i + 2]));
            i += 2;
        } else {
            out += s[i];
        }
    }
    return out;
}

namespace {

stdfs::path u8path(const std::string& s) { return stdfs::path(std::u8string(s.begin(), s.end())); }

// "primary:Games/mh3u.wua", "1234-ABCD:mh3u.wua", "home:x" (Documents), "raw:/storage/..."
stdfs::path from_document_id(const std::string& id) {
    if (id.rfind("raw:", 0) == 0) return u8path(id.substr(4));
    if (!id.empty() && id[0] == '/') return u8path(id);
    const size_t colon = id.find(':');
    if (colon == std::string::npos) return {};
    const std::string volume = id.substr(0, colon), rel = id.substr(colon + 1);
    std::string root;
    if (volume == "primary") root = "/storage/emulated/0";
    else if (volume == "home") root = "/storage/emulated/0/Documents";
    else if (volume.size() == 9 && volume[4] == '-' &&
             std::all_of(volume.begin(), volume.end(), [](char c) { return c == '-' || std::isxdigit(static_cast<unsigned char>(c)); }))
        root = "/storage/" + volume;  // an SD card or USB drive, by its volume id
    else
        return {};
    return rel.empty() ? u8path(root) : u8path(root + "/" + rel);
}

}  // namespace

stdfs::path path_from_android_uri(const std::string& uri) {
    if (uri.rfind("file://", 0) == 0) return u8path(url_decode(uri.substr(7)));
    if (uri.rfind("content://", 0) != 0) return u8path(uri);
    const std::string rest = uri.substr(10);  // authority/kind/id[/document/id]
    const size_t slash = rest.find('/');
    if (slash == std::string::npos) return {};
    const std::string authority = rest.substr(0, slash);
    if (authority != "com.android.externalstorage.documents" && authority != "com.android.providers.downloads.documents")
        return {};
    // the last id wins: /tree/<tree id>/document/<document id> names the document
    std::string id;
    for (const char* kind : {"/document/", "/tree/"}) {
        const size_t at = rest.rfind(kind);
        if (at != std::string::npos) {
            id = rest.substr(at + std::char_traits<char>::length(kind));
            break;
        }
    }
    if (id.empty()) return {};
    if (const size_t q = id.find_first_of("?#"); q != std::string::npos) id.resize(q);
    return from_document_id(url_decode(id));
}

std::vector<stdfs::path> android_game_candidates(const stdfs::path& data, const stdfs::path& shared) {
    std::vector<stdfs::path> out;
    std::error_code ec;
    const auto is_game_folder = [&](const stdfs::path& d) {
        return stdfs::is_directory(d / "code", ec) && stdfs::is_directory(d / "content", ec);
    };
    const auto scan = [&](const stdfs::path& dir, int depth) {
        std::vector<stdfs::path> found;
        for (auto it = stdfs::recursive_directory_iterator(dir, stdfs::directory_options::skip_permission_denied, ec);
             !ec && it != stdfs::recursive_directory_iterator(); it.increment(ec)) {
            if (it.depth() >= depth) it.disable_recursion_pending();
            std::string ext = it->path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
            if (ext == ".wua" && it->is_regular_file(ec)) found.push_back(it->path());
            else if (it->is_directory(ec) && is_game_folder(it->path())) {
                found.push_back(it->path());
                it.disable_recursion_pending();
            }
        }
        ec.clear();
        std::sort(found.begin(), found.end());
        out.insert(out.end(), found.begin(), found.end());
    };
    if (is_game_folder(data / "game")) out.push_back(data / "game");
    scan(data / "game", 2);
    for (const char* sub : {"Cha-Cha", "Download", "Documents", "Games", "ROMs", "Roms", "roms", "Emulation"})
        if (stdfs::is_directory(shared / sub, ec)) scan(shared / sub, 3);
    return out;
}

}  // namespace mh3u::boot
