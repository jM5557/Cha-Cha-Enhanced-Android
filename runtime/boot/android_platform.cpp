#ifdef __ANDROID__
#include "android_platform.h"

#include <SDL3/SDL.h>
#include <jni.h>
#include <unistd.h>

namespace mh3u::boot {
namespace {

// activity.<name>() returning boolean (Z) or void (V)
bool call_activity(const char* name, const char* sig, bool* result) {
    auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
    if (!env || !activity) return false;
    bool ok = false;
    jclass cls = env->GetObjectClass(activity);
    if (jmethodID m = env->GetMethodID(cls, name, sig)) {
        if (result) *result = env->CallBooleanMethod(activity, m) != JNI_FALSE;
        else env->CallVoidMethod(activity, m);
        ok = !env->ExceptionCheck();
    }
    if (env->ExceptionCheck()) env->ExceptionClear();  // e.g. an activity without the method
    env->DeleteLocalRef(cls);
    env->DeleteLocalRef(activity);
    return ok;
}

}  // namespace

std::filesystem::path android_data_folder() {
    // a Cha-Cha folder on the shared storage, when there is one Cha-Cha may write to (All files access): it survives
    // uninstalling, and any file manager reaches it. Otherwise the app's own folder (deleted with the app).
    const std::filesystem::path shared = "/storage/emulated/0/Cha-Cha";
    std::error_code ec;
    if (std::filesystem::is_directory(shared, ec) && access(shared.c_str(), W_OK) == 0) return shared;
    const char* p = SDL_GetAndroidExternalStoragePath();
    if (p && *p) return std::filesystem::path(p);
    p = SDL_GetAndroidInternalStoragePath();  // no external storage (rare): the app's private folder
    return p ? std::filesystem::path(p) : std::filesystem::path("/data/local/tmp/Cha-Cha");
}

bool android_has_all_files_access() {
    bool granted = false;
    return call_activity("hasAllFilesAccess", "()Z", &granted) && granted;
}

void android_request_all_files_access() { call_activity("requestAllFilesAccess", "()V", nullptr); }

}  // namespace mh3u::boot
#endif
