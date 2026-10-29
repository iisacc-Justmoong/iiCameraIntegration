#include <iiCameraIntegration/Media.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace iiCameraIntegration;

namespace {
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
template<class T> T take(Result<T>&& value, const std::string& label) {
    if (!value) throw std::runtime_error(label + ": " + value.error().message);
    return std::move(*value);
}

// Independent equations from the luma coefficients, not swscale's lookup tables.
// Limited-range 8-bit Y' uses [16,235], Cb/Cr use 128 +/- 112.
std::array<double, 3> referenceRgb(int y, int cb, int cr, double kr, double kb, bool full) {
    const double luma = full ? y : (y - 16) * 255.0 / 219.0;
    const double blueDifference = (cb - 128) * (full ? 1.0 : 255.0 / 224.0);
    const double redDifference = (cr - 128) * (full ? 1.0 : 255.0 / 224.0);
    const double kg = 1 - kr - kb;
    return {std::clamp(luma + 2 * (1 - kr) * redDifference, 0.0, 255.0),
            std::clamp(luma - 2 * kb * (1 - kb) / kg * blueDifference -
                             2 * kr * (1 - kr) / kg * redDifference, 0.0, 255.0),
            std::clamp(luma + 2 * (1 - kb) * blueDifference, 0.0, 255.0)};
}

MediaSource source(int y, int cb, int cr, const std::string& matrix, bool full,
                   const std::string& transfer = "iec61966-2-1", const std::string& primaries = "bt709") {
    MediaSource result;
    result.inputFormat = "lavfi";
    result.location = "nullsrc=size=16x16:rate=1,format=yuv444p,geq=lum=" + std::to_string(y) +
        ":cb=" + std::to_string(cb) + ":cr=" + std::to_string(cr) +
        ",setparams=colorspace=" + matrix + ":range=" + (full ? "full" : "limited") +
        ":color_trc=" + transfer + ":color_primaries=" + primaries;
    return result;
}

void verifyPixels(const RgbFrame& frame, const std::array<double, 3>& expected, const std::string& label,
                  double tolerance = 2.0) {
    require(frame.dimensions.width == 16 && frame.dimensions.height == 16 &&
            frame.rowStride == 48 && frame.pixels.size() == 16 * 16 * 3, label + " RGB24 geometry");
    for (std::size_t pixel = 0; pixel < 256; ++pixel) {
        for (std::size_t channel = 0; channel < 3; ++channel) {
            if (std::abs(frame.pixels[pixel * 3 + channel] - expected[channel]) > tolerance)
                throw std::runtime_error(label + " independent pixel oracle failed, channel=" +
                    std::to_string(channel) + " expected=" + std::to_string(expected[channel]) +
                    " actual=" + std::to_string(frame.pixels[pixel * 3 + channel]));
        }
    }
}

void write(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream stream(path, std::ios::binary);
    require(stream.good(), "open raw fixture");
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    require(stream.good(), "write raw fixture");
}
}

int main(int argc, char** argv) {
    try {
        require(argc == 2, "usage: color-decode-test output-directory");
        const std::filesystem::path directory{argv[1]};
        std::filesystem::create_directories(directory);
        struct Matrix { const char* name; double kr, kb; };
        constexpr Matrix matrices[]{
            {"bt709", .2126, .0722}, {"fcc", .30, .11}, {"bt470bg", .299, .114},
            {"smpte170m", .299, .114}, {"smpte240m", .212, .087}, {"bt2020nc", .2627, .0593}
        };
        std::size_t cases{};
        std::vector<std::uint8_t> rec601, rec709;
        for (const auto& matrix : matrices) {
            for (bool full : {false, true}) {
                const auto label = std::string{matrix.name} + (full ? " full" : " limited");
                const auto frame = take(decodeRgbFrame(source(100, 100, 170, matrix.name, full)), label);
                verifyPixels(frame, referenceRgb(100, 100, 170, matrix.kr, matrix.kb, full), label);
                require(frame.colorSpace == "gbr" && frame.colorRange == "pc" &&
                        frame.colorTransfer == "iec61966-2-1" && frame.colorPrimaries == "bt709",
                        label + " output color contract");
                require(frame.metadata.at("iiCamera.color.sourceSpace") == matrix.name &&
                        frame.metadata.at("iiCamera.color.sourceRange") == (full ? "pc" : "tv") &&
                        frame.metadata.at("iiCamera.color.sourceSpaceAssumed") == "false" &&
                        frame.metadata.at("iiCamera.color.sourceRangeAssumed") == "false",
                        label + " source tags retained");
                if (full && std::string{matrix.name} == "bt709") rec709 = frame.pixels;
                if (full && std::string{matrix.name} == "bt470bg") rec601 = frame.pixels;
                ++cases;
            }
        }
        require(rec601 != rec709, "same encoded YUV must distinguish Rec601 and Rec709 matrices");
        for (bool full : {false, true}) {
            for (int y : full ? std::array{0, 255} : std::array{16, 235}) {
                const auto label = std::string{full ? "full" : "limited"} + " neutral endpoint " + std::to_string(y);
                const auto frame = take(decodeRgbFrame(source(y, 128, 128, "bt709", full)), label);
                verifyPixels(frame, referenceRgb(y, 128, 128, .2126, .0722, full), label, 1.0);
                ++cases;
            }
        }
        for (bool full : {false, true}) {
            MediaSource graySource;
            graySource.inputFormat = "lavfi";
            graySource.location = "nullsrc=size=16x16:rate=1,format=gray,geq=lum=100,"
                "setparams=colorspace=bt709:range=" + std::string{full ? "full" : "limited"};
            const auto gray = take(decodeRgbFrame(graySource), "gray range conversion");
            verifyPixels(gray, referenceRgb(100, 128, 128, .2126, .0722, full),
                         full ? "full gray" : "limited gray", 1.0);
            ++cases;
        }

        const auto rawPath = directory / "untagged-yuv444p.raw";
        std::vector<std::uint8_t> raw(256 * 3);
        std::fill_n(raw.begin(), 256, 100);
        std::fill_n(raw.begin() + 256, 256, 100);
        std::fill_n(raw.begin() + 512, 256, 170);
        write(rawPath, raw);
        const MediaSource rawSource{.location = rawPath.string(), .inputFormat = "rawvideo",
            .options = {{"video_size", "16x16"}, {"pixel_format", "yuv444p"}, {"framerate", "1"}}};
        const auto assumed = take(decodeRgbFrame(rawSource), "untagged raw YUV decode");
        verifyPixels(assumed, referenceRgb(100, 100, 170, .299, .114, false), "untagged BT601 limited fallback");
        require(assumed.metadata.at("iiCamera.color.sourceSpaceAssumed") == "true" &&
                assumed.metadata.at("iiCamera.color.sourceRangeAssumed") == "true" &&
                assumed.colorTransfer == "unknown" && assumed.colorPrimaries == "unknown",
                "missing color facts remain observable and transfer/primaries are not invented");
        ++cases;

        MediaSource rgbSource;
        rgbSource.inputFormat = "lavfi";
        rgbSource.location = "nullsrc=size=16x16:rate=1,format=gbrp,geq=r=91:g=52:b=203,"
            "setparams=colorspace=gbr:range=full:color_trc=linear:color_primaries=bt2020";
        const auto rgb = take(decodeRgbFrame(rgbSource), "tagged RGB passthrough");
        verifyPixels(rgb, {91, 52, 203}, "source RGB code values", 0);
        require(rgb.colorTransfer == "linear" && rgb.colorPrimaries == "bt2020" &&
                rgb.metadata.at("iiCamera.color.transferConverted") == "false" &&
                rgb.metadata.at("iiCamera.color.primariesConverted") == "false",
                "RGB is not silently transfer- or gamut-converted to sRGB");
        ++cases;

        const auto hdr = take(decodeRgbFrame(source(100, 100, 170, "bt2020nc", false, "smpte2084", "bt2020")),
                              "PQ-tagged input decode");
        require(hdr.colorTransfer == "smpte2084" && hdr.colorPrimaries == "bt2020" &&
                hdr.metadata.at("iiCamera.color.precision") == "8-bit" &&
                hdr.metadata.at("iiCamera.color.hdrPreserved") == "false",
                "quantized PQ input remains PQ-tagged with explicit lack of HDR preservation");
        ++cases;
        for (const auto* matrix : {"bt2020c", "ictcp", "chroma-derived-nc", "ycgco"}) {
            const auto rejected = decodeRgbFrame(source(100, 100, 170, matrix, false));
            require(!rejected && rejected.error().code == ErrorCode::Unsupported,
                    std::string{matrix} + " unsupported transform must not fall back to a wrong matrix");
            ++cases;
        }
        rgbSource.location = "nullsrc=size=16x16:rate=1,format=gbrp,geq=r=91:g=52:b=203,"
            "setparams=colorspace=gbr:range=limited";
        const auto limitedRgb = decodeRgbFrame(rgbSource);
        require(!limitedRgb && limitedRgb.error().code == ErrorCode::Unsupported,
                "unsupported limited-range RGB must not be mislabeled full-range");
        ++cases;
        std::cout << "color_decode_cases=" << cases << " independent_YUV_oracles=verified physical_camera=unverified\n";
        return 0;
    } catch (const std::exception& failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
