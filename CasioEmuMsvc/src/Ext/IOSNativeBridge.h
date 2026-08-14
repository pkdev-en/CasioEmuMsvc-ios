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

// Home Screen shortcut creation, following the same process LiveContainer
// (https://github.com/LiveContainer/LiveContainer) uses: build a WebClip
// configuration profile whose URL calls back into this app via the
// casioemu:// scheme, then hand it to an in-app Safari view so iOS routes it
// into the system "Install Profile" flow. Returns true once the profile was
// built and handed to Safari -- actual installation still depends on the
// user completing the Settings prompt, exactly like on LiveContainer.
//   modelIdentifier - the model's folder name under "models/" (see
//                      casioemu::StartupUi::Model::path); must not be
//                      NULL/empty.
//   shortcutName    - display label for the Home Screen icon; falls back to
//                      modelIdentifier if NULL/empty.
//   iconPathOrNull  - optional path to a custom icon image; pass NULL or ""
//                      to use the app's own icon.
extern "C" bool presentCreateHomeScreenShortcut(const char* modelIdentifier, const char* shortcutName, const char* iconPathOrNull);

#endif /* iOSNativeBridge_h */
