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
extern "C" void saveFileDialog(const char* preferredName);
extern "C" void openFolderDialog();
extern "C" void saveFolderDialog();

#endif /* iOSNativeBridge_h */
