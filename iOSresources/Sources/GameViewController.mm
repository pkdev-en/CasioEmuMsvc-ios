// GameViewController.mm
// Lớp vỏ Objective-C++ mỏng nhất có thể — chỉ gọi UIKit/Foundation API,
// toàn bộ logic thật nằm trong game_controller_core (C++ thuần).
// Port lại từ app/src/main/java/com/tele/u8emulator/Game.java

#import "GameViewController.h"
#import <UserNotifications/UserNotifications.h>
#import <Photos/Photos.h>
#import "game_controller_core.hpp"

#include <memory>
#include <vector>
#include <string>

static NSString *const kNotificationChannelId = @"emu_channel";
static NSString *const kNotificationRunningId = @"emu_running";
static NSString *const kNotificationStoppedId = @"emu_stopped";

@interface GameViewController ()
@property (nonatomic, strong, nullable) NSTimer *backgroundTimer;
@property (nonatomic, assign) NSTimeInterval pauseStartTime;
@end

@implementation GameViewController {
    std::unique_ptr<game_core::GameController> _core;
}

#pragma mark - Lifecycle (tương đương onCreate/onResume/onPause của Game.java)

- (void)viewDidLoad {
    [super viewDidLoad];

    __weak GameViewController *weakSelf = self;
    game_core::GameControllerCallbacks callbacks;

    callbacks.showNotification = [weakSelf](const std::string &title, const std::string &body) {
        [weakSelf postLocalNotificationWithId:kNotificationRunningId
                                         title:[NSString stringWithUTF8String:title.c_str()]
                                          body:[NSString stringWithUTF8String:body.c_str()]];
    };
    callbacks.cancelRunningNotification = [weakSelf]() {
        [weakSelf cancelNotificationWithId:kNotificationRunningId];
    };
    callbacks.showStoppedNotification = [weakSelf](const std::string &title, const std::string &body) {
        [weakSelf postLocalNotificationWithId:kNotificationStoppedId
                                         title:[NSString stringWithUTF8String:title.c_str()]
                                          body:[NSString stringWithUTF8String:body.c_str()]];
    };
    callbacks.finishAndExit = [weakSelf]() {
        dispatch_async(dispatch_get_main_queue(), ^{
            // iOS không cho app tự thoát (App Store guideline) — mô phỏng bằng cách
            // tạm dừng vòng lặp emulator; core C++ (không thuộc phần này) sẽ nhận lệnh dừng.
            [weakSelf handleEmulationForceStopped];
        });
    };
    callbacks.showToast = [weakSelf](const std::string &message) {
        dispatch_async(dispatch_get_main_queue(), ^{
            [weakSelf showEphemeralMessage:[NSString stringWithUTF8String:message.c_str()]];
        });
    };
    callbacks.onImportFailed = [weakSelf]() {
        dispatch_async(dispatch_get_main_queue(), ^{
            [weakSelf showEphemeralMessage:@"Import failed"];
        });
    };
    callbacks.onExportFailed = [weakSelf]() {
        dispatch_async(dispatch_get_main_queue(), ^{
            [weakSelf showEphemeralMessage:@"Export failed"];
        });
    };
    callbacks.onFileSelected = [weakSelf](const std::string &path, const std::vector<uint8_t> &data) {
        // Điểm nối vào core emulator thật (nạp ROM) — để trống ở đây vì
        // phần lõi C++ emulator nằm ngoài phạm vi file này.
        (void)path;
        (void)data;
    };
    callbacks.onFileSaved = [weakSelf](const std::string &uriOrPath) {
        dispatch_async(dispatch_get_main_queue(), ^{
            [weakSelf showEphemeralMessage:@"File saved"];
        });
    };
    callbacks.onFolderSelected = [weakSelf](const std::string &path) { (void)path; };
    callbacks.onFolderSaved = [weakSelf](const std::string &path) { (void)path; };

    _core = std::make_unique<game_core::GameController>(std::move(callbacks));

    [self requestNotificationPermission];
    [self setImmersiveMode];

    std::string modelPath = self.modelPath ? std::string([self.modelPath UTF8String]) : std::string();
    _core->onCreate(modelPath);

    [self extractBundledAssetsIfNeeded];

    [[NSNotificationCenter defaultCenter] addObserver:self
                                              selector:@selector(handleAppDidEnterBackground)
                                                  name:UIApplicationDidEnterBackgroundNotification
                                                object:nil];
    [[NSNotificationCenter defaultCenter] addObserver:self
                                              selector:@selector(handleAppWillEnterForeground)
                                                  name:UIApplicationWillEnterForegroundNotification
                                                object:nil];
}

- (void)dealloc {
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    [self.backgroundTimer invalidate];
}

- (void)handleAppWillEnterForeground {
    [self.backgroundTimer invalidate];
    self.backgroundTimer = nil;
    if (_core) _core->onResume();
}

- (void)handleAppDidEnterBackground {
    if (_core) _core->onPause();
    self.pauseStartTime = [NSDate timeIntervalSinceReferenceDate];

    [self.backgroundTimer invalidate];
    self.backgroundTimer = [NSTimer scheduledTimerWithTimeInterval:1.0
                                                              target:self
                                                            selector:@selector(backgroundTick)
                                                            userInfo:nil
                                                             repeats:YES];
}

- (void)backgroundTick {
    if (!_core) return;
    NSTimeInterval elapsedSec = [NSDate timeIntervalSinceReferenceDate] - self.pauseStartTime;
    long elapsedMs = (long)(elapsedSec * 1000.0);
    _core->tick(elapsedMs);
    if (_core->isStoppingEmulation()) {
        [self.backgroundTimer invalidate];
        self.backgroundTimer = nil;
    }
}

- (void)handleEmulationForceStopped {
    // Chỗ dừng vòng lặp CPU / audio của emulator core thật sự.
    // (Không thực thi ở đây vì nằm ngoài phạm vi lớp UI này.)
}

#pragma mark - Immersive mode (tương đương setImmersiveMode() của Game.java)

- (void)setImmersiveMode {
    self.overrideUserInterfaceStyle = UIUserInterfaceStyleDark;
    if (@available(iOS 11.0, *)) {
        [self setNeedsUpdateOfHomeIndicatorAutoHidden];
        [self setNeedsStatusBarAppearanceUpdate];
    }
}

- (BOOL)prefersHomeIndicatorAutoHidden {
    return YES;
}

- (BOOL)prefersStatusBarHidden {
    return YES;
}

- (UIRectEdge)preferredScreenEdgesDeferringSystemGestures {
    return UIRectEdgeAll;
}

#pragma mark - Notification helpers (tương đương createNotificationChannel / NotificationCompat)

- (void)requestNotificationPermission {
    UNUserNotificationCenter *center = [UNUserNotificationCenter currentNotificationCenter];
    UNAuthorizationOptions options = UNAuthorizationOptionAlert | UNAuthorizationOptionSound;
    [center requestAuthorizationWithOptions:options
                           completionHandler:^(BOOL granted, NSError * _Nullable error) {
        (void)granted;
        (void)error;
    }];
}

- (void)postLocalNotificationWithId:(NSString *)identifier title:(NSString *)title body:(NSString *)body {
    UNMutableNotificationContent *content = [[UNMutableNotificationContent alloc] init];
    content.title = title;
    content.body = body;
    content.sound = nil;

    UNNotificationRequest *request = [UNNotificationRequest requestWithIdentifier:identifier
                                                                            content:content
                                                                            trigger:nil];
    [[UNUserNotificationCenter currentNotificationCenter] addNotificationRequest:request
                                                             withCompletionHandler:nil];
}

- (void)cancelNotificationWithId:(NSString *)identifier {
    [[UNUserNotificationCenter currentNotificationCenter]
        removeDeliveredNotificationsWithIdentifiers:@[identifier]];
}

#pragma mark - Asset extraction (tương đương extractAssets() / checkAndExtractPluginAssets())

- (void)extractBundledAssetsIfNeeded {
    NSString *resourcePath = [[NSBundle mainBundle] resourcePath];
    NSString *documentsPath = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory,
                                                                    NSUserDomainMask, YES).firstObject;
    if (!resourcePath || !documentsPath) return;

    NSFileManager *fm = [NSFileManager defaultManager];
    NSString *marker = [documentsPath stringByAppendingPathComponent:@".assets_extracted"];
    if ([fm fileExistsAtPath:marker]) return;

    NSString *bundledRoms = [resourcePath stringByAppendingPathComponent:@"roms"];
    NSString *destRoms = [documentsPath stringByAppendingPathComponent:@"roms"];
    if ([fm fileExistsAtPath:bundledRoms] && ![fm fileExistsAtPath:destRoms]) {
        NSError *error = nil;
        [fm copyItemAtPath:bundledRoms toPath:destRoms error:&error];
    }

    [fm createFileAtPath:marker contents:[NSData data] attributes:nil];
}

#pragma mark - File picker result handling (tương đương onActivityResult)

- (void)documentPicker:(UIDocumentPickerViewController *)controller
    didPickDocumentsAtURLs:(NSArray<NSURL *> *)urls {
    if (urls.count == 0 || !_core) return;
    NSURL *url = urls.firstObject;

    BOOL accessGranted = [url startAccessingSecurityScopedResource];
    NSData *data = [NSData dataWithContentsOfURL:url];
    if (accessGranted) {
        [url stopAccessingSecurityScopedResource];
    }

    std::string path = std::string([url.path UTF8String]);
    if (data) {
        const uint8_t *bytes = static_cast<const uint8_t *>(data.bytes);
        std::vector<uint8_t> vec(bytes, bytes + data.length);
        _core->handlePickerResult(game_core::PendingRequestType::ImportFile, path, vec, true);
    } else {
        _core->handlePickerResult(game_core::PendingRequestType::ImportFile, path, {}, false);
    }
}

#pragma mark - Screenshot export (tương đương saveImageToMediaStore)

- (BOOL)saveScreenshotWithPixels:(const uint8_t *)pixels
                            width:(int)width
                           height:(int)height
                            pitch:(int)pitch
                         filename:(NSString *)filename {
    if (!_core) return NO;
    std::string fname = std::string([filename UTF8String]);
    if (!_core->saveScreenshot(pixels, width, height, pitch, fname)) {
        return NO;
    }

    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
    CGContextRef context = CGBitmapContextCreate((void *)pixels, width, height, 8, pitch,
                                                  colorSpace,
                                                  kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
    CGImageRef cgImage = CGBitmapContextCreateImage(context);
    UIImage *image = [UIImage imageWithCGImage:cgImage];

    CGImageRelease(cgImage);
    CGContextRelease(context);
    CGColorSpaceRelease(colorSpace);

    if (!image) return NO;

    [PHPhotoLibrary requestAuthorization:^(PHAuthorizationStatus status) {
        if (status != PHAuthorizationStatusAuthorized) return;
        UIImageWriteToSavedPhotosAlbum(image, nil, nil, nil);
        dispatch_async(dispatch_get_main_queue(), ^{
            [self showEphemeralMessage:@"Screenshot saved to Photos"];
        });
    }];

    return YES;
}

#pragma mark - Misc UI helper (tương đương Toast.makeText)

- (void)showEphemeralMessage:(NSString *)message {
    UIAlertController *alert = [UIAlertController alertControllerWithTitle:nil
                                                                     message:message
                                                              preferredStyle:UIAlertControllerStyleAlert];
    [self presentViewController:alert animated:YES completion:^{
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(1.2 * NSEC_PER_SEC)),
                        dispatch_get_main_queue(), ^{
            [alert dismissViewControllerAnimated:YES completion:nil];
        });
    }];
}

@end
