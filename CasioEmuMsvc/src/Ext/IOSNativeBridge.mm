#include <TargetConditionals.h>
#if TARGET_OS_IPHONE
#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#import <AudioToolbox/AudioToolbox.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

// Include the header we just made
#include "iOSNativeBridge.h"

// Singleton to act as the UIDocumentPickerDelegate
@interface iOSNativeBridge : NSObject <UIDocumentPickerDelegate>
+ (instancetype)sharedInstance;
@end

@implementation iOSNativeBridge

+ (instancetype)sharedInstance {
    static iOSNativeBridge *instance = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        instance = [[iOSNativeBridge alloc] init];
    });
    return instance;
}

- (instancetype)init {
    self = [super init];
    if (self) {
        [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(applicationDidBecomeActive:) name:UIApplicationDidBecomeActiveNotification object:nil];
        [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(applicationWillResignActive:) name:UIApplicationWillResignActiveNotification object:nil];
        [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(applicationDidEnterBackground:) name:UIApplicationDidEnterBackgroundNotification object:nil];
        [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(applicationWillEnterForeground:) name:UIApplicationWillEnterForegroundNotification object:nil];
        [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(applicationWillTerminate:) name:UIApplicationWillTerminateNotification object:nil];
        onAppCreate();
    }
    return self;
}

- (void)dealloc {
    [[NSNotificationCenter defaultCenter] removeObserver:self];
}

#pragma mark - App Lifecycle Observers

- (void)applicationDidBecomeActive:(NSNotification *)notification {
    onAppResume();
}

- (void)applicationWillResignActive:(NSNotification *)notification {
    onAppPause();
}

- (void)applicationDidEnterBackground:(NSNotification *)notification {
    onAppBackground();
}

- (void)applicationWillEnterForeground:(NSNotification *)notification {
    onAppForeground();
}

- (void)applicationWillTerminate:(NSNotification *)notification {
    onAppTerminate();
}



// Get the root view controller to present dialogs
- (UIViewController*)rootViewController {
    UIWindowScene *scene = (UIWindowScene *)UIApplication.sharedApplication.connectedScenes.allObjects.firstObject;
    
    for (UIWindow *window in scene.windows) {
        if (window.isKeyWindow) {
            return window.rootViewController;
        }
    }
    
    return scene.windows.firstObject.rootViewController;
}

#pragma mark - System Dialogs (File & Folder Pickers)


float getSafeTop() {
    if (@available(iOS 11.0, *)) {
        UIWindow *window = UIApplication.sharedApplication.windows.firstObject;
        return window.safeAreaInsets.top;
    }
    return 20.0f;
}

- (void)openFileDialog {
    dispatch_async(dispatch_get_main_queue(), ^{
        // kUTTypeItem was deprecated in iOS 14 and removed in iOS 16+.
        // Use UTTypeItem from UniformTypeIdentifiers (already imported above).
        UIDocumentPickerViewController *picker;
        picker = [[UIDocumentPickerViewController alloc]
            initForOpeningContentTypes:@[UTTypeItem]];
        picker.delegate = self;
        picker.allowsMultipleSelection = NO;
        [[self rootViewController] presentViewController:picker animated:YES completion:nil];
    });
}

- (void)saveFileDialog:(NSString*)preferredName {
    dispatch_async(dispatch_get_main_queue(), ^{
        UIDocumentPickerViewController *picker;
        picker = [[UIDocumentPickerViewController alloc]
            initForExportingURLs:@[] asCopy:NO];
        picker.delegate = self;
        // allowsContentCreation does not exist on UIDocumentPickerViewController
        [[self rootViewController] presentViewController:picker animated:YES completion:nil];
    });
}

- (void)openFolderDialog {
    dispatch_async(dispatch_get_main_queue(), ^{
        // kUTTypeFolder was deprecated in iOS 14 and removed in iOS 16+.
        UIDocumentPickerViewController *picker;
        picker = [[UIDocumentPickerViewController alloc]
            initForOpeningContentTypes:@[UTTypeFolder]];
        picker.delegate = self;
        [[self rootViewController] presentViewController:picker animated:YES completion:nil];
    });
}

- (void)saveFolderDialog {
    // iOS doesn't distinguish between open/save folder, just open a folder picker
    [self openFolderDialog];
}

#pragma mark - UIDocumentPickerDelegate

- (void)documentPicker:(UIDocumentPickerViewController *)controller didPickDocumentsAtURLs:(NSArray<NSURL *> *)urls {
    if (urls.count == 0) return;

    NSURL *url = urls.firstObject;
    UIDocumentPickerMode mode = controller.documentPickerMode;

    // Defer the actual processing (and any resulting error alert) to the
    // next run loop turn. Doing this synchronously here races with the
    // document picker's own dismissal animation, which can leave a
    // subsequently-presented alert's buttons unresponsive.
    dispatch_async(dispatch_get_main_queue(), ^{
        [url startAccessingSecurityScopedResource]; // Required for iOS file access

        NSString *path = url.path;

        // Check if it's a directory (Folder)
        NSError *error = nil;
        NSDictionary *attrs = [[NSFileManager defaultManager] attributesOfItemAtPath:path error:&error];
        BOOL isDir = (attrs.fileType == NSFileTypeDirectory);

        if (isDir) {
            if (mode == UIDocumentPickerModeOpen) {
                onFolderSelected(path.UTF8String);
            } else {
                onFolderSaved(path.UTF8String);
            }
        } else {
            // It's a file, read the data
            NSData *fileData = [NSData dataWithContentsOfURL:url options:0 error:&error];
            if (fileData && !error) {
                if (mode == UIDocumentPickerModeOpen) {
                    onFileSelected(path.UTF8String, (const unsigned char*)fileData.bytes, (int)fileData.length);
                } else {
                    onFileSaved(path.UTF8String);
                }
            } else {
                onImportFailed();
            }
        }

        [url stopAccessingSecurityScopedResource];
    });
}

- (void)documentPickerWasCancelled:(UIDocumentPickerViewController *)controller {
    // User cancelled the dialog
    onImportFailed();
}

@end

#pragma mark - C++ Bridge Functions

void nativeVibrate(long milliseconds) {
    dispatch_async(dispatch_get_main_queue(), ^{
        // iOS doesn't support exact ms vibration like Android.
        // We map the duration to Human Interface Guidelines Haptics
        if (milliseconds < 100) {
            UIImpactFeedbackGenerator *generator = [[UIImpactFeedbackGenerator alloc] initWithStyle:UIImpactFeedbackStyleLight];
            [generator impactOccurred];
        } else if (milliseconds < 300) {
            UIImpactFeedbackGenerator *generator = [[UIImpactFeedbackGenerator alloc] initWithStyle:UIImpactFeedbackStyleMedium];
            [generator impactOccurred];
        } else {
            // Long vibrations usually indicate errors/notifications
            UINotificationFeedbackGenerator *generator = [[UINotificationFeedbackGenerator alloc] init];
            [generator notificationOccurred:UINotificationFeedbackTypeError];
        }
    });
}

void onNativeCrash(const char* message) {
    NSString *msg = [NSString stringWithUTF8String:message];
    
    dispatch_async(dispatch_get_main_queue(), ^{
        UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"Crash Detected"
                                                                       message:msg
                                                                preferredStyle:UIAlertControllerStyleAlert];
        
        UIAlertAction *copyAction = [UIAlertAction actionWithTitle:@"Copy" style:UIAlertActionStyleDefault handler:^(UIAlertAction * action) {
            // Copy to clipboard
            [UIPasteboard generalPasteboard].string = msg;
            
            // Exit cleanly
            exit(0);
        }];
        
        UIAlertAction *closeAction = [UIAlertAction actionWithTitle:@"Close" style:UIAlertActionStyleCancel handler:^(UIAlertAction * action) {
            exit(0);
        }];
        
        [alert addAction:copyAction];
        [alert addAction:closeAction];
        
        [[iOSNativeBridge sharedInstance].rootViewController presentViewController:alert animated:YES completion:nil];
    });
}

void openFileDialog() {
    [[iOSNativeBridge sharedInstance] openFileDialog];
}

void saveFileDialog(const char* preferredName) {
    NSString *name = preferredName ? [NSString stringWithUTF8String:preferredName] : @"Untitled";
    [[iOSNativeBridge sharedInstance] saveFileDialog:name];
}

void openFolderDialog() {
    [[iOSNativeBridge sharedInstance] openFolderDialog];
}

void saveFolderDialog() {
    [[iOSNativeBridge sharedInstance] saveFolderDialog];
}
#endif
