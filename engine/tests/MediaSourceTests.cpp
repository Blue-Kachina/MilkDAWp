// SPDX-FileCopyrightText: 2026 The MilkDAWp contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

// Phase 8.6a: still-image media sources.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <juce_graphics/juce_graphics.h>

#include "milkdawp/engine/MediaSource.h"

using namespace milkdawp::engine;
using Catch::Approx;

TEST_CASE("coverUvScale fills the frame and crops the overflow", "[engine][media]") {
  const auto same = coverUvScale(1920, 1080, 1280, 720);
  CHECK(same.x == Approx(1.0f));
  CHECK(same.y == Approx(1.0f));

  const auto square = coverUvScale(1000, 1000, 1600, 900); // a square image on a wide frame
  CHECK(square.x == Approx(1.0f));
  CHECK(square.y == Approx(900.0f / 1600.0f)); // only the middle 56% of its height shows

  const auto panorama = coverUvScale(4000, 1000, 1600, 900);
  CHECK(panorama.x == Approx((1600.0f / 900.0f) / 4.0f));
  CHECK(panorama.y == Approx(1.0f));

  CHECK(coverUvScale(0, 0, 100, 100).x == 1.0f); // nothing to scale
}

TEST_CASE("ImageMediaSource loads an image bottom row first, with its alpha", "[engine][media]") {
  // 2 x 2: top row red and half-transparent green, bottom row blue and white.
  juce::Image image(juce::Image::ARGB, 2, 2, true);
  image.setPixelAt(0, 0, juce::Colour(255, 0, 0));
  image.setPixelAt(1, 0, juce::Colour(static_cast<juce::uint8>(0), 255, 0, static_cast<juce::uint8>(128)));
  image.setPixelAt(0, 1, juce::Colour(0, 0, 255));
  image.setPixelAt(1, 1, juce::Colour(255, 255, 255));
  const auto file = juce::File::createTempFile(".png");
  {
    juce::FileOutputStream out(file);
    REQUIRE(out.openedOk());
    juce::PNGImageFormat png;
    REQUIRE(png.writeImageToStream(image, out));
  }

  std::string error;
  const auto source = ImageMediaSource::load(file, error);
  file.deleteFile();
  REQUIRE(source != nullptr);
  CHECK(error.empty());
  std::uint64_t serial = 0;
  const auto frame = source->latestFrame(serial);
  REQUIRE(frame != nullptr);
  CHECK(serial != 0);
  REQUIRE(frame->width == 2);
  REQUIRE(frame->height == 2);
  const auto pixel = [&](int x, int y) { return frame->rgba.data() + (y * 2 + x) * 4; };
  // Row 0 is the image's bottom row.
  CHECK(pixel(0, 0)[2] == 255); // blue
  CHECK(pixel(1, 0)[0] == 255); // white
  CHECK(pixel(0, 1)[0] == 255); // red, top left
  INFO("green pixel " << int(pixel(1, 1)[0]) << "," << int(pixel(1, 1)[1]) << "," << int(pixel(1, 1)[2]) << ","
                      << int(pixel(1, 1)[3]));
  // Green, not darkened by its transparency (that would be ~128). JUCE keeps
  // images premultiplied, so 8-bit rounding leaves it a few steps short of 255.
  CHECK(pixel(1, 1)[1] >= 245);
  CHECK(pixel(1, 1)[3] >= 127);
  CHECK(pixel(1, 1)[3] <= 129);
  CHECK(source->description().find(".png") != std::string::npos);
}

TEST_CASE("frameFromImage converts camera-style RGB frames, and scales big ones down", "[engine][media]") {
  // Cameras hand over RGB images (no alpha): 4 x 2, top row red, bottom row blue.
  juce::Image image(juce::Image::RGB, 4, 2, true);
  for (int x = 0; x < 4; ++x) {
    image.setPixelAt(x, 0, juce::Colour(255, 0, 0));
    image.setPixelAt(x, 1, juce::Colour(0, 0, 255));
  }
  const auto frame = frameFromImage(image, 2048);
  REQUIRE(frame.width == 4);
  REQUIRE(frame.height == 2);
  CHECK(frame.rgba[2] == 255);                    // first stored row: the image's bottom, blue
  CHECK(frame.rgba[3] == 255);                    // opaque
  CHECK(frame.rgba[(1 * 4 + 0) * 4 + 0] == 255);  // second stored row: red

  const auto small = frameFromImage(juce::Image(juce::Image::RGB, 4000, 1000, true), 1280);
  CHECK(small.width == 1280);
  CHECK(small.height == 320);
  CHECK(frameFromImage(juce::Image(), 1280).width == 0);
}

TEST_CASE("frameFromYuyv converts webcam YUYV (BT.601, limited range)", "[engine][media]") {
  // 4 x 2: top row white then black (Y 235 / 16, no colour), bottom row pure
  // red (Y 81, U 90, V 240). Rows padded to a 12-byte stride.
  const std::uint8_t yuyv[] = {
      235, 128, 235, 128, 16, 128, 16, 128, 0, 0, 0, 0,  // top: white, white, black, black
      81,  90,  81,  240, 81, 90,  81, 240, 0, 0, 0, 0,  // bottom: red x 4
  };
  const auto frame = frameFromYuyv(yuyv, 4, 2, 12);
  REQUIRE(frame.width == 4);
  REQUIRE(frame.height == 2);
  const auto pixel = [&](int x, int y) { return frame.rgba.data() + (y * 4 + x) * 4; };
  // Stored bottom row first: row 0 is the red one.
  CHECK(pixel(0, 0)[0] >= 250);
  CHECK(pixel(0, 0)[1] <= 5);
  CHECK(pixel(0, 0)[2] <= 5);
  CHECK(pixel(0, 1)[0] == 255); // white, top left
  CHECK(pixel(1, 1)[2] == 255);
  CHECK(pixel(2, 1)[0] == 0);   // black
  CHECK(pixel(3, 1)[3] == 255); // opaque
  CHECK(frameFromYuyv(yuyv, 4, 2, 4).width == 0); // a stride shorter than a row is refused
}

TEST_CASE("Camera media paths", "[engine][media]") {
  const auto path = cameraMediaPath("USB Camera (1)");
  CHECK(isCameraMediaPath(path));
  CHECK(cameraDeviceName(path) == "USB Camera (1)");
  CHECK(mediaSourceDisplayName(path) == "Camera: USB Camera (1)");
  CHECK_FALSE(isCameraMediaPath("C:/Pictures/camera:roll.png"));
  CHECK(cameraDeviceName("C:/Pictures/logo.png").isEmpty());
  CHECK(mediaSourceDisplayName("C:/Pictures/logo.png") == "logo.png");
  CHECK(mediaSourceDisplayName("").isEmpty());

  // A camera that isn't attached is an error, and nothing is opened.
  std::string error;
  CHECK(openMediaSource(cameraMediaPath("No Such Camera 8.6b"), error) == nullptr);
  CHECK_FALSE(error.empty());
}

TEST_CASE("openMediaSource: nothing for no path, an error for a bad one", "[engine][media]") {
  std::string error;
  CHECK(openMediaSource("", error) == nullptr);
  CHECK(error.empty());
  CHECK(openMediaSource("Z:/definitely/not/here.png", error) == nullptr);
  CHECK_FALSE(error.empty());

  const auto notAnImage = juce::File::createTempFile(".png");
  notAnImage.replaceWithText("hello");
  error.clear();
  CHECK(openMediaSource(notAnImage.getFullPathName().toStdString(), error) == nullptr);
  CHECK_FALSE(error.empty());
  notAnImage.deleteFile();
}
