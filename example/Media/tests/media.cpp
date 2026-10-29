#include <iiCameraIntegration/Media.h>
#include <iiFileProviderStorage.h>
#include "FileProviderPort.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

using namespace iiCameraIntegration;
namespace {
void require(bool condition, const std::string& description) {
    if (!condition) throw std::runtime_error(description);
}
template<class T> T take(Result<T>&& result, const std::string& description) {
    if (!result) throw std::runtime_error(description + ": " + result.error().message);
    return std::move(*result);
}
std::vector<std::byte> readBytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    require(input.good(), "open test bytes");
    std::vector<char> characters{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    std::vector<std::byte> bytes(characters.size());
    std::memcpy(bytes.data(), characters.data(), characters.size());
    return bytes;
}
struct Cleanup {
    std::vector<std::filesystem::path> files;
    ~Cleanup() { for (const auto& file : files) { std::error_code ignored; std::filesystem::remove(file, ignored); } }
};
}

int main(int argc, char** argv) {
    try {
        require(argc == 3, "usage: media-test fixture.mkv output-directory");
        const MediaSource source{argv[1]};
        const auto directory = std::filesystem::path(argv[2]);
        const auto unique = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        Cleanup cleanup;
        auto path = [&](const std::string& label, const std::string& extension = ".mkv") {
            auto result = directory / ("ii-camera-" + label + "-" + unique + extension);
            cleanup.files.push_back(result);
            return result;
        };

        const auto backend = take(mediaBackendInfo(), "backend provenance");
        require(backend.name == "FFmpeg" && backend.licenses.contains("libavcodec") &&
                backend.versions.contains("libavformat"), "report actual native versions and licenses");
        const auto codecs = take(codecCatalog(), "codec descriptors");
        require(codecs.size() > 100, "catalog must come from FFmpeg descriptor enumeration");
        std::set<int> identifiers;
        for (const auto& codec : codecs) require(identifiers.insert(codec.id).second, "unique codec descriptors");
        const auto ffv1 = std::find_if(codecs.begin(), codecs.end(), [](const auto& codec) { return codec.name == "ffv1"; });
        require(ffv1 != codecs.end() && ffv1->lossless && !ffv1->decoders.empty(), "FFV1 recognition and decoder availability");
        const auto formats = take(formatCatalog(), "input/output format catalog");
        require(formats.size() > 100, "catalog must enumerate actual formats");
        require(std::any_of(formats.begin(), formats.end(), [](const auto& format) {
            return format.name.find("matroska") != std::string::npos && format.inputAvailable;
        }), "Matroska input detection available");
        require(std::any_of(formats.begin(), formats.end(), [](const auto& format) {
            return format.name == "matroska" && format.outputAvailable;
        }), "Matroska output available");

        const auto info = take(probeMedia(source), "probe real fixture content");
        require(info.formatName.find("matroska") != std::string::npos && info.streams.size() == 2 &&
                info.streamParametersComplete, "video/audio container identification and parameter presence");
        const auto video = std::find_if(info.streams.begin(), info.streams.end(), [](const auto& stream) { return stream.type == MediaType::Video; });
        const auto audio = std::find_if(info.streams.begin(), info.streams.end(), [](const auto& stream) { return stream.type == MediaType::Audio; });
        require(video != info.streams.end() && video->codecName == "ffv1" && video->dimensions.width == 64 &&
                video->dimensions.height == 48 && video->timeBase.value() > 0, "video codec, dimensions and time base");
        require(audio != info.streams.end() && audio->codecName == "pcm_s16le" && audio->sampleRate == 8000 &&
                audio->channels == 1 && !audio->channelLayout.empty(), "audio codec, rate and layout");
        const auto bytes = readBytes(argv[1]);
        const auto byteInfo = take(probeMediaBytes(bytes), "probe bytes without filename");
        require(byteInfo.formatName == info.formatName && byteInfo.streams.size() == info.streams.size(), "content-only recognition matches file");
        const auto wrongExtension = path("wrong-extension", ".mp3");
        iiFileProvider::native::copyFile(argv[1], wrongExtension);
        const auto wrongInfo = take(probeMedia(MediaSource{wrongExtension.string()}), "content detection despite misleading extension");
        require(wrongInfo.formatName == info.formatName, "file extension is not authoritative");
        MediaSource withUnknownOption = source;
        withUnknownOption.options["ii_camera_test_unknown_option"] = "arbitrary-value";
        const auto optionInfo = take(probeMedia(withUnknownOption), "preserve arbitrary FFmpeg options");
        require(optionInfo.unusedInputOptions.at("ii_camera_test_unknown_option") == "arbitrary-value", "unconsumed option remains observable");

        auto capture = take(PacketCapture::open(source), "open packet capture");
        require(capture.metadata().streams.size() == 2, "capture keeps stream metadata");
        std::size_t packets = 0;
        std::set<int> streams;
        bool sawTimestamp = false;
        for (;;) {
            auto packet = capture.read();
            if (!packet) { require(packet.error().code == ErrorCode::EndOfStream, "read terminates with explicit EOF"); break; }
            require(!packet->payload.empty() && packet->timeBase.value() > 0, "capture packet owns bytes and time base");
            streams.insert(packet->streamIndex);
            sawTimestamp |= packet->presentationTimestamp.has_value();
            require(++packets < 10'000, "fixture packet count stays finite");
        }
        require(packets > 6 && streams.size() == 2 && sawTimestamp, "capture actual video/audio packets and timestamps");
        auto cancelledCapture = take(PacketCapture::open(source), "open cancel test");
        cancelledCapture.cancel();
        auto cancelledPacket = cancelledCapture.read();
        require(!cancelledPacket && cancelledPacket.error().code == ErrorCode::Cancelled, "session cancellation is explicit");
        auto moved = std::move(cancelledCapture);
        auto movedFromPacket = cancelledCapture.read();
        require(!movedFromPacket && movedFromPacket.error().code == ErrorCode::InvalidArgument, "moved-from session fails safely");

        const auto frame = take(decodeRgbFrame(source), "decode first fixture video frame");
        require(frame.dimensions.width == 64 && frame.dimensions.height == 48 && frame.rowStride == 64 * 3 &&
                frame.pixels.size() == 64 * 48 * 3, "RGB24 dimensions, packing and storage");
        std::set<std::uint32_t> colors;
        for (std::size_t i = 0; i < frame.pixels.size(); i += 3)
            colors.insert((frame.pixels[i] << 16) | (frame.pixels[i + 1] << 8) | frame.pixels[i + 2]);
        require(colors.size() > 8 && *std::min_element(frame.pixels.begin(), frame.pixels.end()) == 0 &&
                *std::max_element(frame.pixels.begin(), frame.pixels.end()) == 255, "decode patterned pixels, not just a sized allocation");
        DecodeOptions tiny;
        tiny.maximumPixels = 1;
        require(!decodeRgbFrame(source, tiny), "oversized decoded image is rejected");
        DecodeOptions cancelledDecode;
        cancelledDecode.cancelled = [] { return true; };
        auto noFrame = decodeRgbFrame(source, cancelledDecode);
        require(!noFrame && noFrame.error().code == ErrorCode::Cancelled, "cancel before decode");

        const auto remux = path("remux");
        RecordingOptions outputOptions;
        outputOptions.options["ii_camera_unknown_output_option"] = "observable-value";
        auto files = camera_media::fileProviderPort();
        const auto report = take(recordMedia(source, remux, files, outputOptions), "record compressed streams");
        require(report.packets == packets && report.fileBytes == std::filesystem::file_size(remux) &&
                report.stopReason == RecordingStopReason::EndOfStream, "record complete packet set with actual output size");
        require(report.unusedOutputOptions.at("ii_camera_unknown_output_option") == "observable-value",
                "unused muxer option remains observable");
        const auto recorded = take(probeMedia(MediaSource{remux.string()}), "reopen recorded container");
        require(recorded.streams.size() == 2, "record retains video and audio");
        const auto recordedFrame = take(decodeRgbFrame(MediaSource{remux.string()}), "decode recorded media");
        require(recordedFrame.pixels == frame.pixels, "lossless remux preserves decoded pixel content");
        const auto originalOutputBytes = readBytes(remux);
        auto overwrite = recordMedia(source, remux, files);
        require(!overwrite && overwrite.error().code == ErrorCode::Busy && readBytes(remux) == originalOutputBytes,
                "exclusive output creation preserves existing output byte-for-byte");

        RecordingOptions onePacket;
        onePacket.maximumPackets = 1;
        const auto limitedPath = path("limited");
        const auto limited = take(recordMedia(source, limitedPath, files, onePacket), "bounded recording");
        require(limited.packets == 1 && limited.stopReason == RecordingStopReason::PacketLimit &&
                probeMedia(MediaSource{limitedPath.string()}).has_value(), "packet limit finalizes a readable container");
        RecordingOptions shortDuration;
        shortDuration.maximumDuration = std::chrono::milliseconds{100};
        const auto shortPath = path("short");
        const auto shortReport = take(recordMedia(source, shortPath, files, shortDuration), "duration bounded recording");
        require(shortReport.stopReason == RecordingStopReason::DurationLimit && shortReport.packets < packets,
                "media timestamps enforce duration limit for fast file input");
        RecordingOptions insufficientPayload;
        insufficientPayload.maximumPayloadBytes = 1;
        const auto insufficientPath = path("insufficient");
        require(!recordMedia(source, insufficientPath, files, insufficientPayload) && !std::filesystem::exists(insufficientPath),
                "empty limited recording removes partial output");
        RecordingOptions cancelledRecording;
        const auto cancelledPath = path("cancelled");
        bool opened = false;
        auto cancelFiles = files;
        const auto originalCreate = cancelFiles.createExclusive;
        cancelFiles.createExclusive = [&](const auto& target) -> Result<std::FILE*> {
            auto stream = originalCreate(target);
            opened = stream.has_value();
            return stream;
        };
        cancelledRecording.cancelled = [&] { return opened; };
        const auto failedRecording = recordMedia(source, cancelledPath, cancelFiles, cancelledRecording);
        require(!failedRecording && failedRecording.error().code == ErrorCode::Cancelled && !std::filesystem::exists(cancelledPath),
                "cancel after file creation removes partial recording");
        const auto incompatible = path("incompatible", ".mp3");
        const auto failedMuxer = recordMedia(source, incompatible, files);
        require(!failedMuxer && failedMuxer.error().code == ErrorCode::Unsupported && !std::filesystem::exists(incompatible),
                "incompatible container refuses codecs without creating output");

        const auto missingPort = recordMedia(source, path("missing-port"), RecordingFilePort{});
        require(!missingPort && missingPort.error().code == ErrorCode::InvalidArgument,
                "a missing file port fails before creating output");
        bool removed = false;
        RecordingFilePort rejected{
            [](const auto&) -> Result<std::FILE*> { return std::unexpected(Error{ErrorCode::Busy, "fixture busy"}); },
            [](const auto&) -> Result<void> { return {}; },
            [&](const auto&) { removed = true; }
        };
        auto rejectedRecording = recordMedia(source, path("rejected"), rejected);
        require(!rejectedRecording && rejectedRecording.error().code == ErrorCode::Busy && !removed,
                "creation errors propagate and never delete an unowned output");
        RecordingFilePort throwing{
            [](const auto&) -> Result<std::FILE*> { throw std::runtime_error("fixture exception"); },
            [](const auto&) -> Result<void> { return {}; },
            [&](const auto&) { removed = true; }
        };
        auto throwingRecording = recordMedia(source, path("throwing"), throwing);
        require(!throwingRecording && throwingRecording.error().code == ErrorCode::Io && !removed,
                "file creation exceptions become errors without deleting unowned output");
        RecordingFilePort nullStream{
            [](const auto&) -> Result<std::FILE*> { return static_cast<std::FILE*>(nullptr); },
            [](const auto&) -> Result<void> { return {}; },
            [&](const auto&) { removed = true; }
        };
        auto nullRecording = recordMedia(source, path("null"), nullStream);
        require(!nullRecording && nullRecording.error().code == ErrorCode::Io && !removed,
                "a null stream fails without deleting an unowned output");
        auto throwingCleanup = files;
        const auto removeOwned = throwingCleanup.removeIncomplete;
        throwingCleanup.removeIncomplete = [&, removeOwned](const auto& createdPath) {
            removeOwned(createdPath);
            removed = true;
            throw std::runtime_error("fixture cleanup exception");
        };
        const auto cleanupPath = path("throwing-cleanup");
        RecordingOptions cleanupCancel;
        bool cleanupOpened = false;
        const auto cleanupCreate = throwingCleanup.createExclusive;
        throwingCleanup.createExclusive = [&](const auto& target) -> Result<std::FILE*> {
            auto stream = cleanupCreate(target);
            cleanupOpened = stream.has_value();
            return stream;
        };
        cleanupCancel.cancelled = [&] { return cleanupOpened; };
        auto cleanupResult = recordMedia(source, cleanupPath, throwingCleanup, cleanupCancel);
        require(!cleanupResult && cleanupResult.error().code == ErrorCode::Cancelled && removed &&
                !std::filesystem::exists(cleanupPath), "throwing cleanup does not escape or leave the owned file");

        const auto racePath = path("publication-race");
        auto raceFiles = files;
        const auto publish = raceFiles.publishExclusive;
        raceFiles.publishExclusive = [&](const auto& target) -> Result<void> {
            iiFileProvider::native::File::create(target, "other-writer");
            return publish(target);
        };
        auto race = recordMedia(source, racePath, raceFiles);
        const auto raceBytes = readBytes(racePath);
        require(!race && race.error().code == ErrorCode::Busy &&
                std::string(reinterpret_cast<const char*>(raceBytes.data()), raceBytes.size()) == "other-writer",
                "publication race preserves another writer's destination");
        const auto publishFailurePath = path("publication-exception");
        auto publishFailure = files;
        bool publicationCleaned = false;
        const auto discard = publishFailure.removeIncomplete;
        publishFailure.publishExclusive = [](const auto&) -> Result<void> {
            throw std::runtime_error("fixture publication exception");
        };
        publishFailure.removeIncomplete = [&](const auto& target) {
            discard(target);
            publicationCleaned = true;
        };
        auto publicationError = recordMedia(source, publishFailurePath, publishFailure);
        require(!publicationError && publicationError.error().code == ErrorCode::Io && publicationCleaned &&
                !std::filesystem::exists(publishFailurePath), "publication exceptions discard staged output");

        MediaSource synthetic;
        synthetic.inputFormat = "lavfi";
        synthetic.location = "color=c=red:size=16x16:rate=10:duration=0.2";
        const auto red = take(decodeRgbFrame(synthetic), "decode synthetic lavfi source");
        require(red.pixels[0] > 200 && red.pixels[1] < 40 && red.pixels[2] < 40, "synthetic source yields red pixel values");
        auto generatedCapture = take(PacketCapture::open(synthetic), "capture synthetic source packets");
        auto generatedPacket = generatedCapture.read();
        const auto wrapped = generatedCapture.metadata().streams.front().codecName == "wrapped_avframe";
        if (wrapped)
            require(!generatedPacket && generatedPacket.error().code == ErrorCode::Unsupported,
                    "native pointer-bearing packets are not exposed as owned media bytes");
        else require(generatedPacket.has_value(), "lavfi capture reads actual generated packet");
        require(!probeMedia(MediaSource{}), "empty source rejected");
        MediaSource missingFormat = source;
        missingFormat.inputFormat = "ii-camera-missing-demuxer";
        auto unsupported = probeMedia(missingFormat);
        require(!unsupported && unsupported.error().code == ErrorCode::Unsupported, "missing demuxer distinguished");
        std::vector<std::byte> junk(4096, std::byte{'x'});
        auto invalid = probeMediaBytes(junk);
        require(!invalid && invalid.error().code == ErrorCode::InvalidMedia, "random bytes are not misidentified as media");
        require(!probeMediaBytes({}), "empty byte input rejected");
        std::cout << "FFmpeg descriptors=" << codecs.size() << " formats=" << formats.size()
                  << " captured_packets=" << packets << " synthetic_lavfi=verified physical_camera=unverified\n";
        return 0;
    } catch (const std::exception& failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
