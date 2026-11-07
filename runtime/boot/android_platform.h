// Android only: the app's own folders and "All files access" (MANAGE_EXTERNAL_STORAGE), through SDL and the Java side
// (android/java/org/chacha/mh3u/ChaChaActivity.java). See docs/android.md.
#pragma once
#ifdef __ANDROID__

#include <filesystem>

namespace mh3u::boot {

// The data folder: /storage/emulated/0/Cha-Cha if that folder exists and Cha-Cha may write there (All files access:
// it outlives the app), else the app's external files folder, /storage/emulated/0/Android/data/<package>/files (no
// permission needed, a PC sees it over USB, removed when the app is uninstalled).
std::filesystem::path android_data_folder();
// Whether Cha-Cha may read files anywhere on the shared storage (Android 11+: All files access; 10: storage permission).
bool android_has_all_files_access();
// Opens the system screen that grants it (returns at once; the user comes back to Cha-Cha).
void android_request_all_files_access();

}  // namespace mh3u::boot

#endif
