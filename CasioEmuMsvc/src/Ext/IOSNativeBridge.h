#ifndef iOSNativeBridge_h
#define iOSNativeBridge_h

// These are the C++ callbacks that your native engine will implement
// (Equivalent to the 'native' methods in the Android Game.java)
extern "C" void onFileSelected(const char* path, const unsigned char* data, int dataLength);
extern "C" void onFileSaved(const char* path);
extern "C" void onFolderSelected(const char* path);
extern "C" void onFolderSaved(const char* path);
extern "C" void onImportFailed();
extern "C" void onExportFailed();

extern "C" void onAppCreate();
extern "C" void onAppResume();
extern "C" void onAppPause();
extern "C" void onAppBackground();
extern "C" void onAppForeground();
extern "C" void onAppTerminate();

extern "C" float getSafeTop();
extern "C" float getSafeBottom();
extern "C" float getSafeLeft();
extern "C" float getSafeRight();

// Functions callable from C++
extern "C" void nativeVibrate(long milliseconds);
extern "C" void onNativeCrash(const char* message);
extern "C" void openFileDialog();
// NOTE: filePath must already exist on disk with the real bytes to export.
// iOS's export/share picker can only hand off a file that already has
// content -- unlike the Win32/macOS "Save As" panels, there is no dialog
// that just returns an arbitrary writable destination path. Callers should
// write their data to a private temp file first (see
// SystemDialogs::SaveFileDialog in SysDialog.cpp), then pass that finished
// file's path here so the user can choose where it actually goes (Files,
// iCloud Drive, AirDrop, etc).
extern "C" void saveFileDialog(const char* filePath);
extern "C" void openFolderDialog();
extern "C" void saveFolderDialog();

// Home Screen shortcut creation via Quick Actions
// (UIApplicationShortcutItem / UIApplication.shortcutItems): long-pressing
// the app's own icon shows up to 4 of these, and tapping one launches
// straight into the model it names. This is a fully native, synchronous,
// offline call -- no network, no profile install, no Safari involved.
// Returns true once the shortcut was actually added to
// UIApplication.shortcutItems.
//   modelIdentifier - the model's folder name under "models/" (see
//                      casioemu::StartupUi::Model::path); must not be
//                      NULL/empty.
//   shortcutName    - display label for the Quick Action; falls back to
//                      modelIdentifier if NULL/empty.
//   iconPathOrNull  - unused (kept for call-site compatibility across
//                      platforms); Quick Actions use a fixed icon.
extern "C" bool presentCreateHomeScreenShortcut(const char* modelIdentifier, const char* shortcutName, const char* iconPathOrNull);

// Home Screen shortcut creation via a Web Clip Configuration Profile
// (.mobileconfig): unlike Quick Actions above, this produces a real,
// separate icon that sits directly on the Home Screen and launches the app
// with no long-press required. iOS has no public API for a sideloaded app
// to place that icon itself -- the only supported path is generating a
// signed-or-unsigned .mobileconfig payload and having the user install it
// once through Settings > General > VPN & Device Management (System
// Settings prompts for this automatically once the file reaches the
// device, typically via the share sheet this function opens). The profile
// itself points at casioemu://launch?model=<id>, matching what
// ShortcutLaunch.h already knows how to decode.
//   modelIdentifier - the model's folder name under "models/"; must not be
//                      NULL/empty.
//   shortcutName    - display label under the new Home Screen icon; falls
//                      back to modelIdentifier if NULL/empty.
// Returns true once the .mobileconfig was written to disk and the share
// sheet was presented -- NOT once the user has actually finished installing
// it (iOS gives no callback for that; the user returns to the app
// afterward on their own).
extern "C" bool presentCreateHomeScreenWebClip(const char* modelIdentifier, const char* shortcutName);

#endif /* iOSNativeBridge_h */
