// Cha-Cha's activity: SDL3's SDLActivity (it loads libSDL3.so and libmain.so and runs SDL_main on its own thread),
// plus the storage permission the native side asks about (runtime/boot/android_platform.cpp). docs/android.md
package org.chacha.mh3u;

import android.Manifest;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.provider.Settings;

import org.libsdl.app.SDLActivity;

public class ChaChaActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL3", "main" };
    }

    // Whether files anywhere on the shared storage can be read by path (the dump the user picked or keeps in Download).
    public boolean hasAllFilesAccess() {
        if (Build.VERSION.SDK_INT >= 30) return Environment.isExternalStorageManager();
        return checkSelfPermission(Manifest.permission.READ_EXTERNAL_STORAGE) == PackageManager.PERMISSION_GRANTED;
    }

    // The system screen that grants it (Android 11+), or the permission prompt (Android 10).
    public void requestAllFilesAccess() {
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                if (Build.VERSION.SDK_INT >= 30) {
                    try {
                        startActivity(new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                                                 Uri.parse("package:" + getPackageName())));
                    } catch (Exception e) {
                        startActivity(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
                    }
                } else {
                    requestPermissions(new String[] { Manifest.permission.READ_EXTERNAL_STORAGE,
                                                      Manifest.permission.WRITE_EXTERNAL_STORAGE }, 0x4348);
                }
            }
        });
    }
}
