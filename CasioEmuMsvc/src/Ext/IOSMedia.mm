// iOS media helpers: screenshot -> Photos / clipboard, MP4 screen recording (AVAssetWriter).
// Compiled with -fobjc-arc (see CMakeLists.txt). Empty translation unit on macOS.
#import <TargetConditionals.h>
#if TARGET_OS_IPHONE

#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <Photos/Photos.h>

#include "IOSMedia.h"

namespace {

struct VideoWriter {
    AVAssetWriter* writer = nil;
    AVAssetWriterInput* input = nil;
    AVAssetWriterInputPixelBufferAdaptor* adaptor = nil;
    NSString* path = nil;
    int width = 0;   // even, cropped size written to the file
    int height = 0;
    int fps = 30;
    int64_t frameIndex = 0;
};

// Runs `changes` inside a Photos change block once add-only access is available.
void performWithPhotos(void (^changes)(void), NSString* what) {
    void (^run)(void) = ^{
        [[PHPhotoLibrary sharedPhotoLibrary] performChanges:changes
                                          completionHandler:^(BOOL success, NSError* _Nullable error) {
            NSLog(@"[IOSMedia] save %@ to Photos: %@ %@", what, success ? @"ok" : @"failed", error ? error.localizedDescription : @"");
        }];
    };
    PHAuthorizationStatus status = [PHPhotoLibrary authorizationStatusForAccessLevel:PHAccessLevelAddOnly];
    if (status == PHAuthorizationStatusAuthorized || status == PHAuthorizationStatusLimited) {
        run();
    } else if (status == PHAuthorizationStatusNotDetermined) {
        [PHPhotoLibrary requestAuthorizationForAccessLevel:PHAccessLevelAddOnly
                                                   handler:^(PHAuthorizationStatus newStatus) {
            if (newStatus == PHAuthorizationStatusAuthorized || newStatus == PHAuthorizationStatusLimited) {
                run();
            } else {
                NSLog(@"[IOSMedia] Photos access was not granted, %@ not saved", what);
            }
        }];
    } else {
        NSLog(@"[IOSMedia] Photos access denied, %@ not saved", what);
    }
}

UIImage* makeImage(const uint8_t* rgba, int width, int height, int pitch) {
    if (!rgba || width <= 0 || height <= 0 || pitch < width * 4) return nil;
    NSData* data = [NSData dataWithBytes:rgba length:(NSUInteger)pitch * (NSUInteger)height];
    CGDataProviderRef provider = CGDataProviderCreateWithCFData((__bridge CFDataRef)data);
    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
    CGImageRef cgImage = CGImageCreate((size_t)width, (size_t)height, 8, 32, (size_t)pitch, colorSpace,
                                       (CGBitmapInfo)kCGImageAlphaNoneSkipLast, provider, NULL, false,
                                       kCGRenderingIntentDefault);
    UIImage* image = cgImage ? [UIImage imageWithCGImage:cgImage] : nil;
    if (cgImage) CGImageRelease(cgImage);
    CGColorSpaceRelease(colorSpace);
    CGDataProviderRelease(provider);
    return image;
}

} // namespace

extern "C" bool iosSaveImageToPhotos(const uint8_t* rgba, int width, int height, int pitch) {
    UIImage* image = makeImage(rgba, width, height, pitch);
    NSData* png = image ? UIImagePNGRepresentation(image) : nil;
    if (!png) return false;
    performWithPhotos(^{
        PHAssetCreationRequest* request = [PHAssetCreationRequest creationRequestForAsset];
        [request addResourceWithType:PHAssetResourceTypePhoto data:png options:nil];
    }, @"screenshot");
    return true;
}

extern "C" bool iosCopyImageToClipboard(const uint8_t* rgba, int width, int height, int pitch) {
    UIImage* image = makeImage(rgba, width, height, pitch);
    if (!image) return false;
    dispatch_async(dispatch_get_main_queue(), ^{
        [UIPasteboard generalPasteboard].image = image;
    });
    return true;
}

extern "C" bool iosSaveVideoToPhotos(const char* path) {
    if (!path) return false;
    NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path]];
    performWithPhotos(^{
        [PHAssetChangeRequest creationRequestForAssetFromVideoAtFileURL:url];
    }, @"video");
    return true;
}

extern "C" void* iosVideoWriterCreate(const char* path, int width, int height, int fps) {
    if (!path || width < 2 || height < 2) return nullptr;
    const int evenWidth = width & ~1;   // H.264 needs even dimensions
    const int evenHeight = height & ~1;

    NSString* pathString = [NSString stringWithUTF8String:path];
    NSURL* url = [NSURL fileURLWithPath:pathString];
    [[NSFileManager defaultManager] removeItemAtURL:url error:nil];

    NSError* error = nil;
    AVAssetWriter* writer = [[AVAssetWriter alloc] initWithURL:url fileType:AVFileTypeMPEG4 error:&error];
    if (!writer) {
        NSLog(@"[IOSMedia] AVAssetWriter init failed: %@", error);
        return nullptr;
    }
    NSDictionary* videoSettings = @{
        AVVideoCodecKey : AVVideoCodecTypeH264,
        AVVideoWidthKey : @(evenWidth),
        AVVideoHeightKey : @(evenHeight),
    };
    AVAssetWriterInput* input = [AVAssetWriterInput assetWriterInputWithMediaType:AVMediaTypeVideo
                                                                   outputSettings:videoSettings];
    input.expectsMediaDataInRealTime = YES;
    NSDictionary* bufferAttributes = @{
        (id)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA),
        (id)kCVPixelBufferWidthKey : @(evenWidth),
        (id)kCVPixelBufferHeightKey : @(evenHeight),
    };
    AVAssetWriterInputPixelBufferAdaptor* adaptor =
        [AVAssetWriterInputPixelBufferAdaptor assetWriterInputPixelBufferAdaptorWithAssetWriterInput:input
                                                                         sourcePixelBufferAttributes:bufferAttributes];
    if (![writer canAddInput:input]) {
        NSLog(@"[IOSMedia] cannot add video input");
        return nullptr;
    }
    [writer addInput:input];
    if (![writer startWriting]) {
        NSLog(@"[IOSMedia] startWriting failed: %@", writer.error);
        return nullptr;
    }
    [writer startSessionAtSourceTime:kCMTimeZero];

    VideoWriter* vw = new VideoWriter();
    vw->writer = writer;
    vw->input = input;
    vw->adaptor = adaptor;
    vw->path = pathString;
    vw->width = evenWidth;
    vw->height = evenHeight;
    vw->fps = fps > 0 ? fps : 30;
    return vw;
}

extern "C" bool iosVideoWriterAppendRGBA(void* handle, const uint8_t* rgba, int pitch) {
    VideoWriter* vw = static_cast<VideoWriter*>(handle);
    if (!vw || !rgba || pitch < vw->width * 4) return false;
    if (vw->writer.status != AVAssetWriterStatusWriting) return false;
    if (!vw->input.readyForMoreMediaData) return true;   // encoder busy: drop this frame, keep recording

    CVPixelBufferRef buffer = NULL;
    CVReturn result = CVPixelBufferPoolCreatePixelBuffer(NULL, vw->adaptor.pixelBufferPool, &buffer);
    if (result != kCVReturnSuccess || !buffer) return false;

    CVPixelBufferLockBaseAddress(buffer, 0);
    uint8_t* dst = static_cast<uint8_t*>(CVPixelBufferGetBaseAddress(buffer));
    const size_t dstPitch = CVPixelBufferGetBytesPerRow(buffer);
    for (int y = 0; y < vw->height; ++y) {
        const uint8_t* s = rgba + (size_t)y * (size_t)pitch;
        uint8_t* d = dst + (size_t)y * dstPitch;
        for (int x = 0; x < vw->width; ++x) {   // RGBA -> BGRA
            d[0] = s[2];
            d[1] = s[1];
            d[2] = s[0];
            d[3] = 255;
            s += 4;
            d += 4;
        }
    }
    CVPixelBufferUnlockBaseAddress(buffer, 0);

    const BOOL ok = [vw->adaptor appendPixelBuffer:buffer withPresentationTime:CMTimeMake(vw->frameIndex, vw->fps)];
    CVPixelBufferRelease(buffer);
    if (ok) ++vw->frameIndex;
    return ok;
}

extern "C" bool iosVideoWriterFinish(void* handle) {
    VideoWriter* vw = static_cast<VideoWriter*>(handle);
    if (!vw) return false;
    bool ok = false;
    NSString* path = vw->path;
    if (vw->writer.status == AVAssetWriterStatusWriting) {
        [vw->input markAsFinished];
        dispatch_semaphore_t done = dispatch_semaphore_create(0);
        [vw->writer finishWritingWithCompletionHandler:^{
            dispatch_semaphore_signal(done);
        }];
        dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC));
        ok = vw->writer.status == AVAssetWriterStatusCompleted;
        if (!ok) NSLog(@"[IOSMedia] finishWriting failed: %@", vw->writer.error);
    }
    delete vw;
    if (ok) iosSaveVideoToPhotos(path.UTF8String);
    return ok;
}

extern "C" int iosHttpGet(const char* url, int timeoutSeconds, std::string* body) {
    if (!url) return -1;
    NSURL* nsUrl = [NSURL URLWithString:[NSString stringWithUTF8String:url]];
    if (!nsUrl) return -1;
    const NSTimeInterval timeout = timeoutSeconds > 0 ? timeoutSeconds : 10;

    NSMutableURLRequest* request = [NSMutableURLRequest requestWithURL:nsUrl
                                                           cachePolicy:NSURLRequestReloadIgnoringLocalCacheData
                                                       timeoutInterval:timeout];
    [request setValue:@"application/vnd.github+json" forHTTPHeaderField:@"Accept"];
    [request setValue:@"CasioEmuMsvc-update-checker" forHTTPHeaderField:@"User-Agent"];

    __block int status = -1;
    __block NSData* result = nil;
    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    NSURLSessionDataTask* task = [[NSURLSession sharedSession] dataTaskWithRequest:request
        completionHandler:^(NSData* _Nullable data, NSURLResponse* _Nullable response, NSError* _Nullable error) {
            if (!error && [response isKindOfClass:[NSHTTPURLResponse class]]) {
                status = (int)((NSHTTPURLResponse*)response).statusCode;
                result = data;
            }
            dispatch_semaphore_signal(done);
        }];
    [task resume];
    if (dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, (int64_t)(timeout + 2) * NSEC_PER_SEC)) != 0) {
        [task cancel];
        return -1;
    }
    if (status < 0 || (result && result.length > 1024 * 1024)) return -1;
    if (body && result) body->assign(static_cast<const char*>(result.bytes), (size_t)result.length);
    return status;
}

#endif // TARGET_OS_IPHONE
