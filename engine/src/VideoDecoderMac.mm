// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

// 8.6d: video on macOS through AVFoundation: an AVAssetReader hands over
// 32-bit BGRA frames from the system's decoders (H.264 and HEVC always).
// AVAssetReader can't seek, so a seek starts a new reader at that time.
//
// Written to build with or without ARC. The synchronous AVAsset properties
// used here are deprecated in favour of async loading; on a decoding thread
// that is already allowed to block, the synchronous forms are the simpler fit.

#include "milkdawp/engine/VideoDecoder.h"

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unistd.h>

#if __has_feature(objc_arc)
#define MDW_RETAIN(x) (x)
#define MDW_RELEASE(x)
#else
#define MDW_RETAIN(x) [(x) retain]
#define MDW_RELEASE(x) [(x) release]
#endif

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

namespace milkdawp::engine {

namespace {

class AVFoundationDecoder final : public VideoDecoder {
public:
  ~AVFoundationDecoder() override {
    closeReader();
    if (track_ != nil) {
      MDW_RELEASE(track_);
    }
    if (asset_ != nil) {
      MDW_RELEASE(asset_);
    }
  }

  bool open(const juce::File& file, std::string& error) {
    @autoreleasepool {
      NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:file.getFullPathName().toRawUTF8()]];
      AVURLAsset* asset = [AVURLAsset URLAssetWithURL:url options:nil];
      NSArray<AVAssetTrack*>* tracks = [asset tracksWithMediaType:AVMediaTypeVideo];
      if (tracks.count == 0) {
        error = "\"" + file.getFileName().toStdString() + "\" has no video macOS can play";
        return false;
      }
      asset_ = MDW_RETAIN(asset);
      track_ = MDW_RETAIN(tracks.firstObject);
      const double duration = CMTimeGetSeconds(asset.duration);
      duration_ = std::isfinite(duration) ? duration : 0.0;
      if (!startReader(kCMTimeZero)) {
        error = "macOS can't decode \"" + file.getFileName().toStdString() + "\"";
        return false;
      }
      return true;
    }
  }

  double durationSeconds() const override { return duration_; }

  bool next(MediaFrame& frame, double& timestampSeconds) override {
    @autoreleasepool {
      if (output_ == nil) {
        return false;
      }
      CMSampleBufferRef sample = [output_ copyNextSampleBuffer];
      if (sample == nullptr) {
        return false; // the end
      }
      bool ok = false;
      CVImageBufferRef image = CMSampleBufferGetImageBuffer(sample);
      if (image != nullptr && CVPixelBufferLockBaseAddress(image, kCVPixelBufferLock_ReadOnly) == kCVReturnSuccess) {
        const int width = static_cast<int>(CVPixelBufferGetWidth(image));
        const int height = static_cast<int>(CVPixelBufferGetHeight(image));
        const std::size_t stride = CVPixelBufferGetBytesPerRow(image);
        const auto* base = static_cast<const std::uint8_t*>(CVPixelBufferGetBaseAddress(image));
        if (base != nullptr && width > 0 && height > 0) {
          frame.width = width;
          frame.height = height;
          frame.rgba.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
          for (int y = 0; y < height; ++y) {
            const std::uint8_t* in = base + static_cast<std::size_t>(y) * stride;
            // MediaFrame rows go bottom up.
            std::uint8_t* out =
                frame.rgba.data() + static_cast<std::size_t>(height - 1 - y) * static_cast<std::size_t>(width) * 4;
            for (int x = 0; x < width; ++x, in += 4, out += 4) {
              out[0] = in[2]; // BGRA in memory
              out[1] = in[1];
              out[2] = in[0];
              out[3] = 255;
            }
          }
          timestampSeconds = CMTimeGetSeconds(CMSampleBufferGetPresentationTimeStamp(sample));
          ok = true;
        }
        CVPixelBufferUnlockBaseAddress(image, kCVPixelBufferLock_ReadOnly);
      }
      CFRelease(sample);
      return ok;
    }
  }

  bool seek(double seconds) override {
    @autoreleasepool {
      return startReader(CMTimeMakeWithSeconds(std::max(seconds, 0.0), 600));
    }
  }

private:
  bool startReader(CMTime from) {
    closeReader();
    NSError* error = nil;
    AVAssetReader* reader = [AVAssetReader assetReaderWithAsset:asset_ error:&error];
    if (reader == nil) {
      return false;
    }
    NSDictionary* settings = @{(id)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA)};
    AVAssetReaderTrackOutput* output = [AVAssetReaderTrackOutput assetReaderTrackOutputWithTrack:track_
                                                                                  outputSettings:settings];
    output.alwaysCopiesSampleData = NO;
    if (![reader canAddOutput:output]) {
      return false;
    }
    [reader addOutput:output];
    reader.timeRange = CMTimeRangeMake(from, kCMTimePositiveInfinity);
    if (![reader startReading]) {
      return false;
    }
    reader_ = MDW_RETAIN(reader);
    output_ = MDW_RETAIN(output);
    return true;
  }

  void closeReader() {
    if (reader_ != nil) {
      [reader_ cancelReading];
      MDW_RELEASE(reader_);
      reader_ = nil;
    }
    if (output_ != nil) {
      MDW_RELEASE(output_);
      output_ = nil;
    }
  }

  AVURLAsset* asset_ = nil;
  AVAssetTrack* track_ = nil;
  AVAssetReader* reader_ = nil;
  AVAssetReaderTrackOutput* output_ = nil;
  double duration_ = 0.0;
};

} // namespace

std::unique_ptr<VideoDecoder> VideoDecoder::open(const juce::File& file, std::string& error) {
  auto decoder = std::make_unique<AVFoundationDecoder>();
  if (!decoder->open(file, error)) {
    return nullptr;
  }
  return decoder;
}

bool VideoDecoder::supported() noexcept { return true; }

juce::File VideoDecoder::writeTestClip(const juce::File& directory) {
  constexpr int kWidth = 160;
  constexpr int kHeight = 96;
  constexpr int kFps = 10;
  const auto file = directory.getNonexistentChildFile("milkdawp-test-clip", ".mp4");
  bool ok = false;
  @autoreleasepool {
    NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:file.getFullPathName().toRawUTF8()]];
    NSError* error = nil;
    AVAssetWriter* writer = [AVAssetWriter assetWriterWithURL:url fileType:AVFileTypeMPEG4 error:&error];
    if (writer != nil) {
      NSDictionary* settings = @{AVVideoCodecKey : AVVideoCodecTypeH264, AVVideoWidthKey : @(kWidth),
                                 AVVideoHeightKey : @(kHeight)};
      AVAssetWriterInput* input = [AVAssetWriterInput assetWriterInputWithMediaType:AVMediaTypeVideo
                                                                     outputSettings:settings];
      input.expectsMediaDataInRealTime = NO;
      NSDictionary* attributes = @{(id)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA),
                                   (id)kCVPixelBufferWidthKey : @(kWidth), (id)kCVPixelBufferHeightKey : @(kHeight)};
      AVAssetWriterInputPixelBufferAdaptor* adaptor =
          [AVAssetWriterInputPixelBufferAdaptor assetWriterInputPixelBufferAdaptorWithAssetWriterInput:input
                                                                           sourcePixelBufferAttributes:attributes];
      ok = [writer canAddInput:input];
      if (ok) {
        [writer addInput:input];
        ok = [writer startWriting];
      }
      if (ok) {
        [writer startSessionAtSourceTime:kCMTimeZero];
      }
      for (int i = 0; ok && i < 2 * kFps; ++i) {
        for (int wait = 0; !input.readyForMoreMediaData && wait < 2000; ++wait) {
          usleep(1000);
        }
        CVPixelBufferRef pixels = nullptr;
        if (CVPixelBufferCreate(kCFAllocatorDefault, kWidth, kHeight, kCVPixelFormatType_32BGRA,
                                (__bridge CFDictionaryRef)attributes, &pixels) != kCVReturnSuccess) {
          ok = false;
          break;
        }
        CVPixelBufferLockBaseAddress(pixels, 0);
        auto* base = static_cast<std::uint8_t*>(CVPixelBufferGetBaseAddress(pixels));
        const std::size_t stride = CVPixelBufferGetBytesPerRow(pixels);
        const bool red = i < kFps;
        for (int y = 0; y < kHeight; ++y) {
          std::uint8_t* row = base + static_cast<std::size_t>(y) * stride;
          for (int x = 0; x < kWidth; ++x, row += 4) {
            row[0] = red ? 0 : 220; // B
            row[1] = 0;
            row[2] = red ? 220 : 0; // R
            row[3] = 255;
          }
        }
        CVPixelBufferUnlockBaseAddress(pixels, 0);
        ok = [adaptor appendPixelBuffer:pixels withPresentationTime:CMTimeMake(i, kFps)];
        CVPixelBufferRelease(pixels);
      }
      if (ok) {
        [input markAsFinished];
        dispatch_semaphore_t done = dispatch_semaphore_create(0);
        [writer finishWritingWithCompletionHandler:^{
          dispatch_semaphore_signal(done);
        }];
        dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, static_cast<int64_t>(20 * NSEC_PER_SEC)));
#if !__has_feature(objc_arc)
        dispatch_release(done);
#endif
        ok = writer.status == AVAssetWriterStatusCompleted;
      }
    }
  }
  if (!ok) {
    file.deleteFile(); // no H.264 encoder here (a VM without one, say): no clip
  }
  return file;
}

} // namespace milkdawp::engine

#pragma clang diagnostic pop
