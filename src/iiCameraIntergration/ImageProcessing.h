#pragma once
#include <iiCameraIntergration/ImageProcessingExport.h>
#include <iiCameraIntergration/Media.h>
#include <iiGraphicAdjust/Develop.h>
#include <iiGraphicAdjust/PhotoOperations.h>

namespace iiCameraIntergration {
// Metadata requires tagged Rec.709 primaries and sRGB/Rec.709/linear transfer.
// Explicit encodings supply only missing tags; known conflicting tags are rejected.
enum class FrameEncoding { Metadata, Srgb, Rec709, LinearSrgb };
enum class PhotoResampler { Box, Bilinear, Mitchell, Lanczos3 };
struct PhotoResize { std::size_t width{}, height{}; PhotoResampler resampler{PhotoResampler::Lanczos3}; };
struct PhotoProcessingOptions {
    FrameEncoding encoding{FrameEncoding::Metadata};
    iiGraphicAdjust::ExifOrientation orientation{iiGraphicAdjust::ExifOrientation::TopLeft};
    std::optional<iiGraphicAdjust::PixelRect> crop;
    iiGraphicAdjust::DevelopOptions development;
    std::optional<PhotoResize> resize;
    iiGraphicAdjust::HistogramOptions histogram;
};
struct ProcessedPhoto {
    iiGraphicAdjust::Image image;
    iiGraphicAdjust::ImageHistogram histogram;
    Metadata sourceMetadata;
    Metadata processingMetadata;
    std::optional<std::int64_t> presentationTimestamp;
    Rational timeBase;
};
[[nodiscard]] IICAMERAIMAGEPROCESSING_EXPORT Result<ProcessedPhoto> processPhoto(
    const RgbFrame& frame, const PhotoProcessingOptions& options = {});
[[nodiscard]] IICAMERAIMAGEPROCESSING_EXPORT Result<ProcessedPhoto> captureAndProcessPhoto(
    const MediaSource& source, const PhotoProcessingOptions& options = {},
    const DecodeOptions& decode = {});
// Export normalized sRGB pixels as packed RGB24 for host encoders; alpha must be opaque.
[[nodiscard]] IICAMERAIMAGEPROCESSING_EXPORT Result<RgbFrame> photoFrame(const ProcessedPhoto& photo);
}
