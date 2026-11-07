// Android: files the system's document picker returns as content:// URIs, mapped back to file paths, and the folders
// where Cha-Cha looks for a dump by itself. Plain string logic, so it builds and is tested everywhere
// (android_paths_test); the calls into Java are in android_platform.cpp (Android only).
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace mh3u::boot {

// A path for a document picked through the Storage Access Framework, readable with "All files access":
//   content://com.android.externalstorage.documents/document/primary%3AGames%2Fmh3u.wua -> /storage/emulated/0/Games/mh3u.wua
//   .../document/1234-ABCD%3Amh3u.wua (an SD card)                                         -> /storage/1234-ABCD/mh3u.wua
//   .../tree/primary%3AGames%2FMH3U (a folder)                                             -> /storage/emulated/0/Games/MH3U
//   content://com.android.providers.downloads.documents/document/raw%3A%2Fstorage%2F...    -> /storage/...
//   file:///path -> /path; a plain path stays as it is
// Empty for a URI with no file behind it that can be named (e.g. Downloads' "msf:123" or a cloud provider).
std::filesystem::path path_from_android_uri(const std::string& uri);

std::string url_decode(const std::string& s);

// Where a dump is looked for when none is configured: <data>/game first (no permission needed), then common folders
// of the shared storage (with All files access). Each .wua found, or a folder holding code/ content/ meta/.
std::vector<std::filesystem::path> android_game_candidates(const std::filesystem::path& data_folder,
                                                           const std::filesystem::path& shared_storage = "/storage/emulated/0");

}  // namespace mh3u::boot
