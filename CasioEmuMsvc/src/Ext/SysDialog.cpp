#include "SysDialog.h"

#ifdef _WIN32
#include <windows.h>
#include <shobjidl.h>
#include <filesystem>
#include <functional>

void SystemDialogs::OpenFileDialog(std::function<void(std::filesystem::path)> callback) {
    IFileDialog* pfd;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&pfd));
    if (SUCCEEDED(hr)) {
        DWORD dwOptions;
        if (SUCCEEDED(pfd->GetOptions(&dwOptions))) {
            pfd->SetOptions(dwOptions | FOS_FORCEFILESYSTEM);
        }

        COMDLG_FILTERSPEC rgSpec[] = {
            {L"All Files", L"*.*"}};
        pfd->SetFileTypes(1, rgSpec);

        if (SUCCEEDED(pfd->Show(NULL))) {
            IShellItem* psi;
            if (SUCCEEDED(pfd->GetResult(&psi))) {
                PWSTR path;
                if (SUCCEEDED(psi->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                    callback(std::filesystem::path(path));
                    CoTaskMemFree(path);
                }
                psi->Release();
            }
        }
        pfd->Release();
    }
}

void SystemDialogs::SaveFileDialog(std::string preferred_name, std::function<void(std::filesystem::path)> callback) {
    IFileSaveDialog* pfd;
    HRESULT hr = CoCreateInstance(CLSID_FileSaveDialog, NULL, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&pfd));
    if (SUCCEEDED(hr)) {
        DWORD dwOptions;
        if (SUCCEEDED(pfd->GetOptions(&dwOptions))) {
            pfd->SetOptions(dwOptions | FOS_FORCEFILESYSTEM);
        }

        if (!preferred_name.empty()) {
            int needed = MultiByteToWideChar(CP_UTF8, 0, preferred_name.c_str(), -1, NULL, 0);
            if (needed > 0) {
                std::wstring wname(needed - 1, 0);
                MultiByteToWideChar(CP_UTF8, 0, preferred_name.c_str(), -1, wname.data(), needed);
                pfd->SetFileName(wname.c_str());
            }
        }

        COMDLG_FILTERSPEC rgSpec[] = {
            {L"All Files", L"*.*"}};
        pfd->SetFileTypes(1, rgSpec);

        if (SUCCEEDED(pfd->Show(NULL))) {
            IShellItem* psi;
            if (SUCCEEDED(pfd->GetResult(&psi))) {
                PWSTR path;
                if (SUCCEEDED(psi->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                    callback(std::filesystem::path(path));
                    CoTaskMemFree(path);
                }
                psi->Release();
            }
        }
        pfd->Release();
    }
}

void SystemDialogs::OpenFolderDialog(std::function<void(std::filesystem::path)> callback) {
    IFileDialog* pfd;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&pfd));
    if (SUCCEEDED(hr)) {
        DWORD dwOptions;
        if (SUCCEEDED(pfd->GetOptions(&dwOptions))) {
            pfd->SetOptions(dwOptions | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
        }

        if (SUCCEEDED(pfd->Show(NULL))) {
            IShellItem* psi;
            if (SUCCEEDED(pfd->GetResult(&psi))) {
                PWSTR path;
                if (SUCCEEDED(psi->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                    callback(std::filesystem::path(path));
                    CoTaskMemFree(path);
                }
                psi->Release();
            }
        }
        pfd->Release();
    }
}

void SystemDialogs::SaveFolderDialog(std::function<void(std::filesystem::path)> callback) {
    IFileDialog* pfd;
    HRESULT hr = CoCreateInstance(CLSID_FileSaveDialog, NULL, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&pfd));
    if (SUCCEEDED(hr)) {
        DWORD dwOptions;
        if (SUCCEEDED(pfd->GetOptions(&dwOptions))) {
            pfd->SetOptions(dwOptions | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
        }

        if (SUCCEEDED(pfd->Show(NULL))) {
            IShellItem* psi;
            if (SUCCEEDED(pfd->GetResult(&psi))) {
                PWSTR path;
                if (SUCCEEDED(psi->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                    callback(std::filesystem::path(path));
                    CoTaskMemFree(path);
                }
                psi->Release();
            }
        }
        pfd->Release();
    }
}
#endif

#ifdef __ANDROID__
#include <jni.h>
#include <android/log.h>
#include <SDL.h>
#include <SDL_system.h>
#include <fstream>

std::function<void(std::filesystem::path)> SystemDialogs::fileOpenCallback;
std::function<void(std::filesystem::path)> SystemDialogs::fileSaveCallback;
std::function<void(std::filesystem::path)> SystemDialogs::folderOpenCallback;
std::function<void(std::filesystem::path)> SystemDialogs::folderSaveCallback;

void WriteFile(const std::filesystem::path& path, const std::vector<unsigned char>& data) {
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Cannot open file for writing");
    }
    file.write(reinterpret_cast<const char*>(data.data()), data.size());
}

static bool GetJNIEnv(JNIEnv **env) {
    *env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    return (*env != NULL);
}

void SystemDialogs::OpenFileDialog(std::function<void(std::filesystem::path)> callback) {
    fileOpenCallback = callback;
    JNIEnv *env;
    if (!GetJNIEnv(&env)) return;

    jobject activity = (jobject)SDL_AndroidGetActivity();
    if (!activity) return;

    jclass systemDialogsClass = env->FindClass("com/tele/u8emulator/SystemDialogs");
    if (!systemDialogsClass) {
        env->DeleteLocalRef(activity);
        return;
    }

    jmethodID openFileMethod = env->GetStaticMethodID(systemDialogsClass, "openFileDialog", "(Landroid/app/Activity;)V");
    if (!openFileMethod) {
        env->DeleteLocalRef(systemDialogsClass);
        env->DeleteLocalRef(activity);
        return;
    }

    env->CallStaticVoidMethod(systemDialogsClass, openFileMethod, activity);
    env->DeleteLocalRef(systemDialogsClass);
    env->DeleteLocalRef(activity);
}

void SystemDialogs::SaveFileDialog(std::string preferred_name, std::function<void(std::filesystem::path)> callback) {
    fileSaveCallback = callback;
    JNIEnv *env;
    if (!GetJNIEnv(&env)) return;

    jobject activity = (jobject)SDL_AndroidGetActivity();
    if (!activity) return;

    jclass systemDialogsClass = env->FindClass("com/tele/u8emulator/SystemDialogs");
    if (!systemDialogsClass) {
        env->DeleteLocalRef(activity);
        return;
    }

    jmethodID saveFileMethod = env->GetStaticMethodID(systemDialogsClass, "saveFileDialog", "(Landroid/app/Activity;Ljava/lang/String;)V");
    if (!saveFileMethod) {
        env->DeleteLocalRef(systemDialogsClass);
        env->DeleteLocalRef(activity);
        return;
    }

    jstring jPreferredName = env->NewStringUTF(preferred_name.c_str());
    env->CallStaticVoidMethod(systemDialogsClass, saveFileMethod, activity, jPreferredName);
    env->DeleteLocalRef(jPreferredName);
    env->DeleteLocalRef(systemDialogsClass);
    env->DeleteLocalRef(activity);
}

void SystemDialogs::OpenFolderDialog(std::function<void(std::filesystem::path)> callback) {
    folderOpenCallback = callback;
    JNIEnv *env;
    if (!GetJNIEnv(&env)) return;

    jobject activity = (jobject)SDL_AndroidGetActivity();
    if (!activity) return;

    jclass systemDialogsClass = env->FindClass("com/tele/u8emulator/SystemDialogs");
    if (!systemDialogsClass) {
        env->DeleteLocalRef(activity);
        return;
    }

    jmethodID openFolderMethod = env->GetStaticMethodID(systemDialogsClass, "openFolderDialog", "(Landroid/app/Activity;)V");
    if (!openFolderMethod) {
        env->DeleteLocalRef(systemDialogsClass);
        env->DeleteLocalRef(activity);
        return;
    }

    env->CallStaticVoidMethod(systemDialogsClass, openFolderMethod, activity);
    env->DeleteLocalRef(systemDialogsClass);
    env->DeleteLocalRef(activity);
}

void SystemDialogs::SaveFolderDialog(std::function<void(std::filesystem::path)> callback) {
    folderSaveCallback = callback;
    JNIEnv *env;
    if (!GetJNIEnv(&env)) return;

    jobject activity = (jobject)SDL_AndroidGetActivity();
    if (!activity) return;

    jclass systemDialogsClass = env->FindClass("com/tele/u8emulator/SystemDialogs");
    if (!systemDialogsClass) {
        env->DeleteLocalRef(activity);
        return;
    }

    jmethodID saveFolderMethod = env->GetStaticMethodID(systemDialogsClass, "saveFolderDialog", "(Landroid/app/Activity;)V");
    if (!saveFolderMethod) {
        env->DeleteLocalRef(systemDialogsClass);
        env->DeleteLocalRef(activity);
        return;
    }

    env->CallStaticVoidMethod(systemDialogsClass, saveFolderMethod, activity);
    env->DeleteLocalRef(systemDialogsClass);
    env->DeleteLocalRef(activity);
}

extern "C" {
    JNIEXPORT void JNICALL Java_com_tele_u8emulator_Game_onFileSelected(JNIEnv* env, jclass clazz, jstring path, jbyteArray data) {
        if (SystemDialogs::fileOpenCallback) {
            const char* cPath = env->GetStringUTFChars(path, nullptr);
            jbyte* bytes = env->GetByteArrayElements(data, nullptr);
            jsize length = env->GetArrayLength(data);
            
            if (bytes == nullptr || length == 0) {
                SDL_Log("Error: Received empty or null data");
                if (bytes) env->ReleaseByteArrayElements(data, bytes, JNI_ABORT);
                if (cPath) env->ReleaseStringUTFChars(path, cPath);
                return;
            }
    
            std::vector<unsigned char> fileData(bytes, bytes + length);
            std::filesystem::path tempDir = "./tmp";
            std::filesystem::create_directories(tempDir);
            std::filesystem::path fileName = std::filesystem::path(cPath).filename();
            std::filesystem::path tempPath = tempDir / fileName;
    
            try {
                std::ofstream test(tempPath, std::ios::binary);
                if (!test) throw std::runtime_error("Cannot create temp file for writing");
                test.close();
                
                WriteFile(tempPath, fileData);
                SystemDialogs::fileOpenCallback(tempPath);
                
                std::error_code ec;
                std::filesystem::remove(tempPath, ec);
                std::filesystem::remove(tempDir, ec);
            }
            catch (const std::exception& e) {
                SDL_Log("Failed to write temp file: %s", e.what());
            }
    
            env->ReleaseByteArrayElements(data, bytes, JNI_ABORT);
            env->ReleaseStringUTFChars(path, cPath);
        }
    }

    JNIEXPORT void JNICALL Java_com_tele_u8emulator_Game_onFolderSelected(JNIEnv* env, jclass clazz, jstring path) {
        if (SystemDialogs::folderOpenCallback) {
            const char* cPath = env->GetStringUTFChars(path, nullptr);
            SystemDialogs::folderOpenCallback(std::filesystem::path(cPath));
            env->ReleaseStringUTFChars(path, cPath);
        }
    }

    JNIEXPORT void JNICALL Java_com_tele_u8emulator_Game_onFolderSaved(JNIEnv* env, jclass clazz, jstring path) {
        if (SystemDialogs::folderSaveCallback) {
            const char* cPath = env->GetStringUTFChars(path, nullptr);
            SystemDialogs::folderSaveCallback(std::filesystem::path(cPath));
            env->ReleaseStringUTFChars(path, cPath);
        }
    }
    
    JNIEXPORT void JNICALL Java_com_tele_u8emulator_Game_onExportFailed(JNIEnv* env, jclass clazz) { SDL_Log("Export failed"); }
    JNIEXPORT void JNICALL Java_com_tele_u8emulator_Game_onFileSaved(JNIEnv* env, jclass clazz, jstring uri) {
        if (SystemDialogs::fileSaveCallback) {
            const char* cUri = env->GetStringUTFChars(uri, nullptr);
            SystemDialogs::fileSaveCallback(std::filesystem::path(cUri));
            env->ReleaseStringUTFChars(uri, cUri);
        }
    }
    JNIEXPORT void JNICALL Java_com_tele_u8emulator_Game_onImportFailed(JNIEnv* env, jclass clazz) { SDL_Log("Import failed"); }
}
#endif

// --- KHỐI THÊM MỚI: HỖ TRỢ NATIVE MACOS VÀ IOS ---
#ifdef __APPLE__
#include <TargetConditionals.h>
#include <iostream>
#include <cstdio>
#include <memory>
#include <array>
#include <algorithm>
#include <filesystem>
#include <functional>

std::function<void(std::filesystem::path)> SystemDialogs::fileOpenCallback;
std::function<void(std::filesystem::path)> SystemDialogs::fileSaveCallback;
std::function<void(std::filesystem::path)> SystemDialogs::folderOpenCallback;
std::function<void(std::filesystem::path)> SystemDialogs::folderSaveCallback;

#if TARGET_OS_IPHONE

#import <UIKit/UIKit.h>
#import <objc/runtime.h>

// NOTE: the file on disk is "IOSNativeBridge.h" (capital I). The include
// below must match that case exactly -- macOS's default case-insensitive
// filesystem hides a mismatch here, but it breaks on a case-sensitive one
// (e.g. Linux cross-builds, or a case-sensitive APFS volume).
#include "IOSNativeBridge.h"

@interface SysDialogDelegate : NSObject <UIDocumentPickerDelegate>
@property (nonatomic, assign) std::function<void(std::filesystem::path)> callback;
@end

@implementation SysDialogDelegate
- (void)documentPicker:(UIDocumentPickerViewController *)controller didPickDocumentsAtURLs:(NSArray<NSURL *> *)urls {
    if (urls.count > 0 && self.callback) {
        NSURL *url = urls.firstObject;
        BOOL secured = [url startAccessingSecurityScopedResource];
        
        NSString *tempDir = NSTemporaryDirectory();
        NSString *fileName = [url lastPathComponent];
        NSString *tempPath = [tempDir stringByAppendingPathComponent:fileName];
        
        NSFileManager *fm = [NSFileManager defaultManager];
        if ([fm fileExistsAtPath:tempPath]) {
            [fm removeItemAtPath:tempPath error:nil];
        }
        [fm copyItemAtURL:url toURL:[NSURL fileURLWithPath:tempPath] error:nil];
        
        if (secured) {
            [url stopAccessingSecurityScopedResource];
        }
        
        auto callback = self.callback;
        // The document picker is still in the middle of dismissing itself when
        // this delegate method fires. If the callback presents another UIKit
        // view (e.g. an error alert via SDL_ShowSimpleMessageBox), it ends up
        // racing with that dismissal, which can leave the new alert's buttons
        // unresponsive. Defer to the next run loop turn so the picker has
        // fully finished dismissing before anything else is presented.
        dispatch_async(dispatch_get_main_queue(), ^{
            callback(std::filesystem::path([tempPath UTF8String]));
        });
    }
}
@end

static UIViewController* getRootVC() {
    UIWindow *keyWindow = nil;
    for (UIWindow *window in UIApplication.sharedApplication.windows) {
        if (window.isKeyWindow) {
            keyWindow = window;
            break;
        }
    }
    return keyWindow.rootViewController;
}

void SystemDialogs::OpenFileDialog(std::function<void(std::filesystem::path)> callback) {
    SysDialogDelegate *delegate = [[SysDialogDelegate alloc] init];
    delegate.callback = callback;
    
    UIDocumentPickerViewController *picker = [[UIDocumentPickerViewController alloc] initWithDocumentTypes:@[@"public.data", @"public.content"] inMode:UIDocumentPickerModeImport];
    picker.delegate = delegate;
    
    // Retain the delegate by associating it with the picker (simple hack to keep it alive without ARC issues)
    objc_setAssociatedObject(picker, "SysDialogDelegate", delegate, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    
    UIViewController *vc = getRootVC();
    [vc presentViewController:picker animated:YES completion:nil];
}

void SystemDialogs::SaveFileDialog(std::string preferred_name, std::function<void(std::filesystem::path)> callback) {
    // iOS has no dialog that hands back an arbitrary writable destination
    // path the way the Win32/macOS "Save As" panels do -- a
    // UIDocumentPickerViewController's export mode can only hand off a file
    // that already exists on disk with real content. So instead we:
    //   1. Compute a private temp file path (no picker needed for this --
    //      the app's own temp directory is always writable).
    //   2. Call the caller's callback with that temp path *synchronously*,
    //      exactly like the other platforms do, so the existing call sites
    //      (RomPackage export, snapshot export, asm export, ...) can write
    //      their bytes with a plain std::ofstream, unmodified.
    //   3. Once that write has finished, hand the now-populated temp file to
    //      the native export/share picker (IOSNativeBridge.mm's
    //      saveFileDialog:) so the user can choose the real destination
    //      (Files, iCloud Drive, AirDrop, etc).
    if (!callback) return;

    std::error_code ec;
    std::filesystem::path tempDir = std::filesystem::temp_directory_path(ec);
    if (ec) {
        std::cerr << "[SystemDialogs] SaveFileDialog: no temp directory available: " << ec.message() << std::endl;
        return;
    }
    tempDir /= "casioemu_export";
    // Start from a clean slate so a stale file from a previous export never
    // lingers around or gets offered up by mistake.
    std::filesystem::remove_all(tempDir, ec);
    std::filesystem::create_directories(tempDir, ec);
    if (ec) {
        std::cerr << "[SystemDialogs] SaveFileDialog: cannot create temp directory: " << ec.message() << std::endl;
        return;
    }

    std::string safeName = preferred_name.empty() ? std::string("export.dat") : preferred_name;
    std::filesystem::path tempPath = tempDir / safeName;

    callback(tempPath); // caller writes the real bytes here (e.g. Binary::Write)

    if (std::filesystem::exists(tempPath, ec)) {
        saveFileDialog(tempPath.string().c_str());
    } else {
        std::cerr << "[SystemDialogs] SaveFileDialog: caller did not write " << tempPath << ", nothing to export.\n";
    }
}

void SystemDialogs::OpenFolderDialog(std::function<void(std::filesystem::path)> callback) {
    SysDialogDelegate *delegate = [[SysDialogDelegate alloc] init];
    delegate.callback = callback;
    
    UIDocumentPickerViewController *picker = [[UIDocumentPickerViewController alloc] initWithDocumentTypes:@[@"public.folder"] inMode:UIDocumentPickerModeOpen];
    picker.delegate = delegate;
    objc_setAssociatedObject(picker, "SysDialogDelegate", delegate, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    
    UIViewController *vc = getRootVC();
    [vc presentViewController:picker animated:YES completion:nil];
}

void SystemDialogs::SaveFolderDialog(std::function<void(std::filesystem::path)> callback) {
    OpenFolderDialog(callback);
}

#else

// MACOS implementation


static std::string exec_apple_script(const std::string& script) {
    std::string cmd = "osascript -e " + script;
    std::array<char, 128> buffer;
    std::string result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);
    if (!pipe) return "";
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    // Xử lý xuống dòng thu được từ terminal mac
    result.erase(std::remove(result.begin(), result.end(), '\n'), result.end());
    result.erase(std::remove(result.begin(), result.end(), '\r'), result.end());
    
    // AppleScript trả về đường dẫn dạng POSIX đôi khi chứa "alias " hoặc định dạng kiểu cũ, 
    // Nhưng gọi với 'POSIX path of' sẽ trả về dạng chuỗi /path/to/file sạch.
    return result;
}

// Hàm escape chuỗi ký tự tránh lỗi chuỗi trong AppleScript
static std::string escape_mac_string(const std::string& str) {
    std::string escaped;
    for (char c : str) {
        if (c == '"' || c == '\\') escaped += '\\';
        escaped += c;
    }
    return escaped;
}

void SystemDialogs::OpenFileDialog(std::function<void(std::filesystem::path)> callback) {
    // Kéo cửa sổ dialog lên trước (Frontmost) để không bị ẩn sau cửa sổ game/emulator
    std::string script = "'choose file with prompt \"Select a file:\"'";
    std::string path = exec_apple_script("text item 1 of (exec statement \"tell application \\\"System Events\\\" to set frontmost of (every process whose visible is true) to true\")"); 
    // Chạy script lấy POSIX path
    path = exec_apple_script("'POSIX path of (choose file)' 2>/dev/null");
    if (!path.empty()) callback(std::filesystem::path(path));
}

void SystemDialogs::SaveFileDialog(std::string preferred_name, std::function<void(std::filesystem::path)> callback) {
    std::string safe_name = escape_mac_string(preferred_name);
    std::string script = "'POSIX path of (choose file name default name \"" + safe_name + "\" with prompt \"Save As:\")' 2>/dev/null";
    std::string path = exec_apple_script(script);
    if (!path.empty()) callback(std::filesystem::path(path));
}

void SystemDialogs::OpenFolderDialog(std::function<void(std::filesystem::path)> callback) {
    std::string path = exec_apple_script("'POSIX path of (choose folder with prompt \"Select a folder:\")' 2>/dev/null");
    if (!path.empty()) callback(std::filesystem::path(path));
}

void SystemDialogs::SaveFolderDialog(std::function<void(std::filesystem::path)> callback) {
    // Trên Mac choose folder có thể dùng chung cho cả chọn nơi save folder
    OpenFolderDialog(callback);
}

// --- SỬA ĐỔI KHỐI ĐỂ TRÁNH XUNG ĐỘT VỚI MACOS ---
#endif
#elif !defined(_WIN32) && !defined(__ANDROID__) && !defined(__APPLE__)
#include <iostream>
#include <cstdio>
#include <memory>
#include <array>
#include <algorithm>

std::function<void(std::filesystem::path)> SystemDialogs::fileOpenCallback;
std::function<void(std::filesystem::path)> SystemDialogs::fileSaveCallback;
std::function<void(std::filesystem::path)> SystemDialogs::folderOpenCallback;
std::function<void(std::filesystem::path)> SystemDialogs::folderSaveCallback;

bool command_exists(const char* cmd) {
    std::string check_cmd = "command -v ";
    check_cmd += cmd;
    check_cmd += " > /dev/null 2>&1";
    return system(check_cmd.c_str()) == 0;
}

std::string escape_shell_arg(const std::string& arg) {
    std::string escaped = "'";
    for (char c : arg) {
        if (c == '\'') {
            escaped += "'\\''";
        } else {
            escaped += c;
        }
    }
    escaped += "'";
    return escaped;
}

std::string exec_and_get_output(const char* cmd) {
    std::array<char, 128> buffer;
    std::string result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd, "r"), pclose);
    if (!pipe) {
        throw std::runtime_error("popen() failed!");
    }
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    result.erase(std::remove(result.begin(), result.end(), '\n'), result.end());
    result.erase(std::remove(result.begin(), result.end(), '\r'), result.end());
    return result;
}

void terminal_fallback(const std::string& prompt, const std::function<void(std::filesystem::path)>& callback) {
    std::cout << "\n[INFO] No graphical file dialog tool (zenity/kdialog) found." << std::endl;
    std::cout << prompt;
    std::string path_str;
    std::getline(std::cin, path_str);
    if (!path_str.empty()) {
        callback(std::filesystem::path(path_str));
    } else {
        std::cout << "[INFO] Canceled." << std::endl;
    }
}

void SystemDialogs::OpenFileDialog(std::function<void(std::filesystem::path)> callback) {
    std::string cmd;
    if (command_exists("zenity")) {
        cmd = "zenity --file-selection";
    } else if (command_exists("kdialog")) {
        cmd = "kdialog --getopenfilename";
    }

    if (!cmd.empty()) {
        std::string path = exec_and_get_output(cmd.c_str());
        if (!path.empty()) {
            callback(path);
        }
    } else {
        terminal_fallback("Please enter the full path to the file: ", callback);
    }
}

void SystemDialogs::SaveFileDialog(std::string preferred_name, std::function<void(std::filesystem::path)> callback) {
    std::string cmd;
    std::string safe_preferred_name = escape_shell_arg(preferred_name);
    if (command_exists("zenity")) {
        cmd = "zenity --file-selection --save --confirm-overwrite --filename=" + safe_preferred_name;
    } else if (command_exists("kdialog")) {
        cmd = "kdialog --getsavefilename " + safe_preferred_name;
    }

    if (!cmd.empty()) {
        std::string path = exec_and_get_output(cmd.c_str());
        if (!path.empty()) {
            callback(path);
        }
    } else {
        terminal_fallback("Please enter the full path to save the file: ", callback);
    }
}

void SystemDialogs::OpenFolderDialog(std::function<void(std::filesystem::path)> callback) {
    std::string cmd;
    if (command_exists("zenity")) {
        cmd = "zenity --file-selection --directory";
    } else if (command_exists("kdialog")) {
        cmd = "kdialog --getexistingdirectory";
    }

    if (!cmd.empty()) {
        std::string path = exec_and_get_output(cmd.c_str());
        if (!path.empty()) {
            callback(path);
        }
    } else {
        terminal_fallback("Please enter the full path to the folder: ", callback);
    }
}

void SystemDialogs::SaveFolderDialog(std::function<void(std::filesystem::path)> callback) {
    OpenFolderDialog(callback);
}
#endif
