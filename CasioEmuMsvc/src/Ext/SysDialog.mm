#ifdef __APPLE__
#include "SysDialog.h"
#include <TargetConditionals.h>
#include <string>
#include <filesystem>
#include <functional>
#include <iostream>

#if TARGET_OS_IPHONE
// ============================================================================
// iOS — uses UIDocumentPickerViewController (Files/iCloud Drive/AirDrop).
// The C++ side calls SystemDialogs::OpenFileDialog etc.; the actual native
// picker is presented on top of SDL's UIWindow by reaching into
// UIApplication.sharedApplication.windows.
// ============================================================================
#import <UIKit/UIKit.h>
#import <objc/runtime.h>
#include "IOSNativeBridge.h"

// Static callback storage — the C++ side exposes these via SystemDialogs.h
// and they get assigned before the picker is shown. The C functions in
// casioemu.cpp's iOS block (onFileSelected / onFileSaved / etc.) also reach
// into these to forward events from IOSNativeBridge.mm.
std::function<void(std::filesystem::path)> SystemDialogs::fileOpenCallback;
std::function<void(std::filesystem::path)> SystemDialogs::fileSaveCallback;
std::function<void(std::filesystem::path)> SystemDialogs::folderOpenCallback;
std::function<void(std::filesystem::path)> SystemDialogs::folderSaveCallback;

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
        // The picker is still dismissing itself when this delegate fires; if
        // the callback presents another UIKit view (e.g. an error alert via
        // SDL_ShowSimpleMessageBox), it can race the dismissal and leave the
        // new alert's buttons unresponsive. Defer one run-loop turn.
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
    objc_setAssociatedObject(picker, "SysDialogDelegate", delegate, OBJC_ASSOCIATION_RETAIN_NONATOMIC);

    UIViewController *vc = getRootVC();
    [vc presentViewController:picker animated:YES completion:nil];
}

void SystemDialogs::SaveFileDialog(std::string preferred_name, std::function<void(std::filesystem::path)> callback) {
    // iOS has no dialog that hands back an arbitrary writable destination
    // path the way the Win32/macOS "Save As" panels do. UIDocumentPicker's
    // export mode can only hand off a file that already exists on disk with
    // real content. So:
    //   1. Compute a private temp file path (no picker needed — the app's
    //      own temp directory is always writable).
    //   2. Call the caller's callback *synchronously* with that temp path
    //      so existing call sites (RomPackage export, snapshot export, asm
    //      export, ...) can write their bytes with a plain std::ofstream,
    //      unmodified.
    //   3. Once that write finishes, hand the populated temp file to the
    //      native export/share picker (IOSNativeBridge.mm's
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
    std::filesystem::remove_all(tempDir, ec);
    std::filesystem::create_directories(tempDir, ec);
    if (ec) {
        std::cerr << "[SystemDialogs] SaveFileDialog: cannot create temp directory: " << ec.message() << std::endl;
        return;
    }

    std::string safeName = preferred_name.empty() ? std::string("export.dat") : preferred_name;
    std::filesystem::path tempPath = tempDir / safeName;

    callback(tempPath); // caller writes the real bytes here

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
// ============================================================================
// macOS — uses NSOpenPanel / NSSavePanel (AppKit).
// ============================================================================
#import <AppKit/AppKit.h>

void SystemDialogs::OpenFileDialog(std::function<void(std::filesystem::path)> callback) {
    @autoreleasepool {
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        [panel setCanChooseFiles:YES];
        [panel setCanChooseDirectories:NO];
        [panel setAllowsMultipleSelection:NO];
        if ([panel runModal] == NSModalResponseOK) {
            NSURL* url = [[panel URLs] objectAtIndex:0];
            callback(std::filesystem::path([[url path] UTF8String]));
        }
    }
}

void SystemDialogs::SaveFileDialog(std::string preferred_name, std::function<void(std::filesystem::path)> callback) {
    @autoreleasepool {
        NSSavePanel* panel = [NSSavePanel savePanel];
        [panel setNameFieldStringValue:[NSString stringWithUTF8String:preferred_name.c_str()]];
        if ([panel runModal] == NSModalResponseOK) {
            NSURL* url = [panel URL];
            callback(std::filesystem::path([[url path] UTF8String]));
        }
    }
}

void SystemDialogs::OpenFolderDialog(std::function<void(std::filesystem::path)> callback) {
    @autoreleasepool {
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        [panel setCanChooseFiles:NO];
        [panel setCanChooseDirectories:YES];
        [panel setAllowsMultipleSelection:NO];
        if ([panel runModal] == NSModalResponseOK) {
            NSURL* url = [[panel URLs] objectAtIndex:0];
            callback(std::filesystem::path([[url path] UTF8String]));
        }
    }
}

void SystemDialogs::SaveFolderDialog(std::function<void(std::filesystem::path)> callback) {
    OpenFolderDialog(callback);
}
#endif
#endif
