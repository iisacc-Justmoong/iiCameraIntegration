#pragma once

#include <iiCameraIntergration/Export.h>
#include <iiCameraIntergration/Types.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace iiCameraIntergration {

enum class MediaType { Unknown, Video, Audio, Subtitle, Data, Attachment };

struct CodecInfo {
    int id{};
    std::string name;
    std::string description;
    MediaType type{MediaType::Unknown};
    std::vector<std::string> mimeTypes;
    std::vector<std::string> decoders;
    std::vector<std::string> encoders;
    bool lossy{};
    bool lossless{};
    bool intraOnly{};
};

struct FormatInfo {
    std::string name;
    std::string description;
    std::vector<std::string> extensions;
    std::vector<std::string> mimeTypes;
    bool inputAvailable{};
    bool outputAvailable{};
};

struct MediaBackendInfo {
    std::string name;
    Metadata versions;
    Metadata licenses;
    std::string configuration;
};

struct StreamInfo {
    int index{};
    MediaType type{MediaType::Unknown};
    int codecId{};
    std::string codecName;
    std::uint32_t codecTag{};
    // Decoder initialization data, e.g. AVC/HEVC configuration records.
    std::vector<std::byte> codecExtraData;
    // Required parameter presence only; this does not establish bitstream integrity.
    bool parametersComplete{};
    Rational timeBase;
    Rational frameRate;
    Rational sampleAspectRatio;
    std::optional<std::int64_t> startTime;
    std::optional<std::int64_t> duration;
    std::int64_t bitRate{};
    Size dimensions;
    std::string pixelFormat;
    std::string colorRange;
    std::string colorSpace;
    std::string colorTransfer;
    std::string colorPrimaries;
    int sampleRate{};
    int channels{};
    std::string sampleFormat;
    std::string channelLayout;
    Metadata metadata;
};

struct MediaInfo {
    std::string formatName;
    std::string formatDescription;
    std::optional<std::chrono::microseconds> duration;
    std::int64_t bitRate{};
    bool seekable{};
    bool streamParametersComplete{};
    Metadata metadata;
    // Unknown backend options remain observable instead of being silently discarded.
    Metadata unusedInputOptions;
    std::vector<StreamInfo> streams;
};

struct MediaSource {
    // A file path, network URL, device address, or lavfi graph.
    std::string location;
    // Empty means content detection. Devices and raw streams require an explicit demuxer.
    std::string inputFormat;
    Metadata options;
    std::chrono::milliseconds timeout{30'000};
    std::int64_t maximumProbeBytes{8 * 1024 * 1024};
    std::chrono::milliseconds maximumAnalysisDuration{5'000};
};

struct MediaPacket {
    int streamIndex{};
    Rational timeBase;
    std::optional<std::int64_t> presentationTimestamp;
    std::optional<std::int64_t> decodingTimestamp;
    std::int64_t duration{};
    bool keyFrame{};
    std::vector<std::byte> payload;
};

// A session owns its native input. Move-only; read/metadata need caller serialization.
// cancel() may run concurrently with read(). Interrupts are cooperative in FFmpeg.
class IICAMERAINTERGRATION_EXPORT PacketCapture final {
public:
    static Result<PacketCapture> open(const MediaSource& source);
    PacketCapture(PacketCapture&&) noexcept;
    PacketCapture& operator=(PacketCapture&&) noexcept;
    ~PacketCapture();
    PacketCapture(const PacketCapture&) = delete;
    PacketCapture& operator=(const PacketCapture&) = delete;

    [[nodiscard]] const MediaInfo& metadata() const noexcept;
    [[nodiscard]] Result<MediaPacket> read();
    void cancel() noexcept;

private:
    struct Impl;
    explicit PacketCapture(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};

struct DecodeOptions {
    int streamIndex{-1};
    std::uint64_t maximumPixels{100'000'000};
    std::uint64_t maximumPackets{100'000};
    std::chrono::milliseconds timeout{30'000};
    std::function<bool()> cancelled;
};

struct RgbFrame {
    Size dimensions;
    std::size_t rowStride{};
    std::vector<std::uint8_t> pixels;
    std::optional<std::int64_t> presentationTimestamp;
    Rational timeBase;
    Metadata metadata;
    // Output is full-range RGB24. YUV matrix/range conversion preserves the source
    // transfer and primaries; these fields do not imply sRGB or HDR preservation.
    // FFmpeg names are used ("gbr", "pc", "iec61966-2-1", "bt709", "unknown").
    std::string colorSpace;
    std::string colorRange;
    std::string colorTransfer;
    std::string colorPrimaries;
};

struct RecordingOptions {
    // Empty selects a muxer from the destination name. No existing file is overwritten.
    std::string outputFormat;
    Metadata options;
    std::chrono::milliseconds maximumDuration{10'000};
    std::uint64_t maximumPackets{100'000};
    std::uint64_t maximumPayloadBytes{1024ULL * 1024 * 1024};
    std::chrono::milliseconds timeout{30'000};
    std::function<bool()> cancelled;
};

enum class RecordingStopReason { EndOfStream, DurationLimit, PacketLimit, PayloadLimit };

struct RecordingReport {
    std::filesystem::path destination;
    std::uint64_t packets{};
    std::uint64_t payloadBytes{};
    std::uint64_t fileBytes{};
    RecordingStopReason stopReason{RecordingStopReason::EndOfStream};
    Metadata unusedOutputOptions;
};

// Descriptors describe recognized identities; encoders/decoders describe this build.
[[nodiscard]] IICAMERAINTERGRATION_EXPORT Result<std::vector<CodecInfo>> codecCatalog();
[[nodiscard]] IICAMERAINTERGRATION_EXPORT Result<std::vector<FormatInfo>> formatCatalog();
[[nodiscard]] IICAMERAINTERGRATION_EXPORT Result<MediaBackendInfo> mediaBackendInfo();
[[nodiscard]] IICAMERAINTERGRATION_EXPORT Result<MediaInfo> probeMedia(const MediaSource& source);
// Reads caller-owned bytes synchronously with a seekable custom input, without an extension hint.
[[nodiscard]] IICAMERAINTERGRATION_EXPORT Result<MediaInfo> probeMediaBytes(
    std::span<const std::byte> bytes, std::chrono::milliseconds timeout = std::chrono::milliseconds{30'000});
[[nodiscard]] IICAMERAINTERGRATION_EXPORT Result<RgbFrame> decodeRgbFrame(
    const MediaSource& source, const DecodeOptions& options = {});
// Copies compressed streams to a compatible container. It does not transcode codecs.
[[nodiscard]] IICAMERAINTERGRATION_EXPORT Result<RecordingReport> recordMedia(
    const MediaSource& source, const std::filesystem::path& destination,
    const RecordingOptions& options = {});

} // namespace iiCameraIntergration
