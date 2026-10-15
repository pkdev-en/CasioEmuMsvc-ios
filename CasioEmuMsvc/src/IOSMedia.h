#pragma once
// iOS counterparts of the Android helpers in ScreenOutput.cpp
// (MediaStore screenshot save, clipboard copy, MediaCodec MP4 recording).
// Implemented in IOSMedia.mm (Objective-C++, ARC). Pixel data is RGBA, byte order R,G,B,A.
#include <cstdint>

extern "C" {
// Saves the image to the Photos library (async, add-only permission). Returns true if the request was started.
bool iosSaveImageToPhotos(const uint8_t* rgba, int width, int height, int pitch);
// Puts the image on the system clipboard (UIPasteboard).
bool iosCopyImageToClipboard(const uint8_t* rgba, int width, int height, int pitch);
// Adds a finished video file to the Photos library (async).
bool iosSaveVideoToPhotos(const char* path);

// H.264/MP4 writer. Create returns nullptr on failure. Width/height are rounded down to even numbers.
void* iosVideoWriterCreate(const char* path, int width, int height, int fps);
bool iosVideoWriterAppendRGBA(void* writer, const uint8_t* rgba, int pitch);
// Finishes the file (blocks up to ~5 s), saves it to Photos and frees the writer.
bool iosVideoWriterFinish(void* writer);
}
