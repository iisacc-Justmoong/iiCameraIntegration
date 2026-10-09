#include <iiCameraIntergration/Media.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

using namespace iiCameraIntergration;

namespace {
#define CHECK(expression) do { if (!(expression)) throw std::runtime_error( \
    std::string("Check failed: ") + #expression + " at line " + std::to_string(__LINE__)); } while (false)

struct ExpectedStream {
    MediaType type;
    std::string_view codec;
    int sampleRate{};
};

struct Fixture {
    std::string_view filename;
    std::vector<std::string_view> containers;
    std::vector<ExpectedStream> streams;
    bool movingVideo{};
    bool title{};
};

constexpr std::string_view FixtureTitle = "iiCamera recognition fixture";

bool formatToken(std::string_view names, std::string_view expected) {
    while (!names.empty()) {
        const auto separator = names.find(',');
        if (names.substr(0, separator) == expected) return true;
        if (separator == std::string_view::npos) break;
        names.remove_prefix(separator + 1);
    }
    return false;
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char letter) {
        return static_cast<char>(std::tolower(letter));
    });
    return value;
}

const CodecInfo& catalogCodec(const std::vector<CodecInfo>& catalog, const ExpectedStream& expected) {
    const auto codec = std::find_if(catalog.begin(), catalog.end(), [&](const auto& entry) {
        return entry.name == expected.codec;
    });
    CHECK(codec != catalog.end());
    CHECK(codec->id > 0 && codec->type == expected.type);
    CHECK(!codec->description.empty());
    return *codec;
}

void checkInfo(const MediaInfo& info, const Fixture& fixture, const std::vector<CodecInfo>& catalog) {
    CHECK(!info.formatName.empty() && !info.formatDescription.empty());
    CHECK(std::any_of(fixture.containers.begin(), fixture.containers.end(), [&](auto container) {
        return formatToken(info.formatName, container);
    }));
    CHECK(info.streams.size() == fixture.streams.size());
    CHECK(info.streamParametersComplete);
    CHECK(info.seekable);
    std::vector<int> streamIndexes;
    for (const auto& expected : fixture.streams) {
        const auto stream = std::find_if(info.streams.begin(), info.streams.end(), [&](const auto& entry) {
            return entry.type == expected.type && entry.codecName == expected.codec;
        });
        CHECK(stream != info.streams.end());
        const auto& codec = catalogCodec(catalog, expected);
        CHECK(stream->codecId == codec.id);
        CHECK(stream->parametersComplete);
        CHECK(stream->index >= 0);
        CHECK(std::find(streamIndexes.begin(), streamIndexes.end(), stream->index) == streamIndexes.end());
        streamIndexes.push_back(stream->index);
        CHECK(stream->timeBase.numerator > 0 && stream->timeBase.denominator > 0);
        if (expected.type == MediaType::Video) {
            CHECK(stream->dimensions.width == 64 && stream->dimensions.height == 48);
            CHECK(!stream->pixelFormat.empty());
            if (fixture.movingVideo) {
                CHECK(stream->frameRate.numerator > 0 && stream->frameRate.denominator > 0);
                CHECK(std::abs(stream->frameRate.value() - 10) < 1e-9);
            }
        } else if (expected.type == MediaType::Audio) {
            CHECK(stream->sampleRate == expected.sampleRate);
            CHECK(stream->channels == 1);
            CHECK(!stream->sampleFormat.empty() && !stream->channelLayout.empty());
        }
    }
    if (fixture.title) {
        const auto title = std::find_if(info.metadata.begin(), info.metadata.end(), [](const auto& entry) {
            return lower(entry.first) == "title";
        });
        CHECK(title != info.metadata.end() && title->second == FixtureTitle);
        CHECK(info.duration && info.duration->count() > 0);
        CHECK(info.duration->count() <= 1'000'000);
    }
}

std::vector<std::byte> readFixture(const std::filesystem::path& path) {
    const auto size = std::filesystem::file_size(path);
    CHECK(size > 0 && size <= 16 * 1024 * 1024);
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    std::ifstream input(path, std::ios::binary);
    CHECK(input.is_open());
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    CHECK(input.gcount() == static_cast<std::streamsize>(bytes.size()));
    return bytes;
}

template<class T>
void expectError(const Result<T>& result, ErrorCode expected) {
    CHECK(!result);
    CHECK(result.error().code == expected);
    CHECK(!result.error().message.empty());
}

void recognition(const std::filesystem::path& directory) {
    const auto catalog = codecCatalog();
    CHECK(catalog && !catalog->empty());
    const std::array<Fixture, 7> fixtures{
        Fixture{"still.png", {"png_pipe", "image2"}, {{MediaType::Video, "png"}}},
        Fixture{"still.jpg", {"jpeg_pipe", "image2"}, {{MediaType::Video, "mjpeg"}}},
        Fixture{"audio.wav", {"wav"}, {{MediaType::Audio, "pcm_s16le", 8000}}, false, true},
        Fixture{"audio.flac", {"flac"}, {{MediaType::Audio, "flac", 8000}}, false, true},
        Fixture{"clip.mp4", {"mp4", "mov"}, {{MediaType::Video, "h264"}, {MediaType::Audio, "aac", 48000}}, true, true},
        Fixture{"clip.webm", {"webm", "matroska"}, {{MediaType::Video, "vp9"}, {MediaType::Audio, "opus", 48000}}, true, true},
        Fixture{"original.mkv", {"matroska"}, {{MediaType::Video, "ffv1"}, {MediaType::Audio, "pcm_s16le", 8000}}, true, true}
    };
    for (const auto& fixture : fixtures) {
        const auto path = directory / fixture.filename;
        const auto fromPath = probeMedia(MediaSource{path.string()});
        if (!fromPath) throw std::runtime_error("probeMedia " + path.string() + ": " + fromPath.error().message);
        checkInfo(*fromPath, fixture, *catalog);
        const auto bytes = readFixture(path);
        const auto fromBytes = probeMediaBytes(bytes);
        if (!fromBytes) throw std::runtime_error("probeMediaBytes " + path.string() + ": " + fromBytes.error().message);
        checkInfo(*fromBytes, fixture, *catalog);
        const auto misleading = directory / (std::string(fixture.filename) + ".txt");
        std::filesystem::copy_file(path, misleading, std::filesystem::copy_options::overwrite_existing);
        const auto fromMisleadingPath = probeMedia(MediaSource{misleading.string()});
        if (!fromMisleadingPath) throw std::runtime_error("probe misleading extension " + misleading.string() + ": " + fromMisleadingPath.error().message);
        checkInfo(*fromMisleadingPath, fixture, *catalog);
        std::cout << fixture.filename << ": file, bytes, and misleading extension recognized\n";
    }
    expectError(probeMediaBytes({}), ErrorCode::InvalidArgument);
    const std::array<std::byte, 64> unknown{};
    expectError(probeMediaBytes(unknown), ErrorCode::InvalidMedia);
    // A signature can establish identity without supplying usable stream parameters.
    const std::array<std::byte, 8> incompletePng{
        std::byte{0x89}, std::byte{0x50}, std::byte{0x4e}, std::byte{0x47},
        std::byte{0x0d}, std::byte{0x0a}, std::byte{0x1a}, std::byte{0x0a}
    };
    const auto signatureOnly = probeMediaBytes(incompletePng);
    CHECK(signatureOnly);
    CHECK(formatToken(signatureOnly->formatName, "png_pipe"));
    CHECK(!signatureOnly->streamParametersComplete);
    CHECK(signatureOnly->streams.size() == 1);
    const auto& incompleteStream = signatureOnly->streams.front();
    CHECK(incompleteStream.codecName == "png" && incompleteStream.type == MediaType::Video);
    CHECK(!incompleteStream.parametersComplete);
    CHECK(incompleteStream.dimensions.width == 0 && incompleteStream.dimensions.height == 0);
}
}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: recognition_tests <fixture-directory>\n";
        return 2;
    }
    try {
        recognition(argv[1]);
        std::cout << "Cross-format media recognition passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
