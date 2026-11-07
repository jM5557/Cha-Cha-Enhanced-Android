// android_paths.h: picker URIs -> paths, and the dump candidates (a temporary folder standing in for the storage).
#include "android_paths.h"

#include <cstdio>
#include <fstream>

namespace stdfs = std::filesystem;
using namespace mh3u::boot;

static int failures = 0;
#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                 \
        }                                                               \
    } while (0)

int main() {
    const auto p = [](const char* uri) { return path_from_android_uri(uri).generic_string(); };
    CHECK(p("content://com.android.externalstorage.documents/document/primary%3AGames%2Fmh3u.wua") == "/storage/emulated/0/Games/mh3u.wua");
    CHECK(p("content://com.android.externalstorage.documents/document/1A2B-3C4D%3AWii%20U%2Fmh3u.wua") == "/storage/1A2B-3C4D/Wii U/mh3u.wua");
    CHECK(p("content://com.android.externalstorage.documents/tree/primary%3AGames%2FMH3U") == "/storage/emulated/0/Games/MH3U");
    CHECK(p("content://com.android.externalstorage.documents/tree/primary%3AGames/document/primary%3AGames%2Fx.wua") ==
          "/storage/emulated/0/Games/x.wua");
    CHECK(p("content://com.android.externalstorage.documents/document/home%3Amh3u.wua") == "/storage/emulated/0/Documents/mh3u.wua");
    CHECK(p("content://com.android.providers.downloads.documents/document/raw%3A%2Fstorage%2Femulated%2F0%2FDownload%2Fmh3u.wua") ==
          "/storage/emulated/0/Download/mh3u.wua");
    CHECK(p("content://com.android.providers.downloads.documents/document/msf%3A1234").empty());
    CHECK(p("content://com.google.android.apps.docs.storage/document/acc%3D1%3Bdoc%3D9").empty());
    CHECK(p("file:///sdcard/mh3u%20(US).wua") == "/sdcard/mh3u (US).wua");
    CHECK(p("/storage/emulated/0/x.wua") == "/storage/emulated/0/x.wua");
    CHECK(url_decode("a%2Fb%zz%4") == "a/b%zz%4");

    const stdfs::path root = stdfs::temp_directory_path() / "chacha_android_paths_test";
    stdfs::remove_all(root);
    const stdfs::path data = root / "data", shared = root / "shared";
    stdfs::create_directories(data / "game");
    std::ofstream(data / "game" / "mh3u.wua") << "x";
    stdfs::create_directories(shared / "Download");
    std::ofstream(shared / "Download" / "other.WUA") << "x";
    stdfs::create_directories(shared / "Games" / "MH3U" / "code");
    stdfs::create_directories(shared / "Games" / "MH3U" / "content");
    stdfs::create_directories(shared / "Music");
    std::ofstream(shared / "Music" / "ignored.wua") << "x";
    const auto c = android_game_candidates(data, shared);
    CHECK(c.size() == 3);
    if (c.size() == 3) {
        CHECK(c[0] == data / "game" / "mh3u.wua");  // the app's own folder first
        CHECK(c[1] == shared / "Download" / "other.WUA");
        CHECK(c[2] == shared / "Games" / "MH3U");
    }
    stdfs::remove_all(root);
    std::printf(failures ? "%d failure(s)\n" : "all checks passed\n", failures);
    return failures ? 1 : 0;
}
