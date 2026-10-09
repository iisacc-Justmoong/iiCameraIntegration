#include <iiCameraIntergration/Media.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/codec_desc.h>
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
#include <libavutil/samplefmt.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <limits>
#include <map>
#include <mutex>
#include <string_view>
#include <utility>

namespace iiCameraIntergration {
namespace {

using Clock = std::chrono::steady_clock;
constexpr std::size_t MaximumPacketBytes = 256 * 1024 * 1024;

std::string text(const char* value) { return value ? value : ""; }
bool containsNull(const std::string& value) { return value.find('\0') != std::string::npos; }
bool validMetadata(const Metadata& values) {
    return std::all_of(values.begin(), values.end(), [](const auto& entry) {
        return !entry.first.empty() && !containsNull(entry.first) && !containsNull(entry.second);
    });
}

Error error(ErrorCode code, std::string message, int native = 0) {
    return Error{code, std::move(message), native};
}

struct InterruptState {
    std::atomic<bool> cancelled{};
    Clock::time_point deadline{Clock::time_point::max()};
    Clock::time_point hardDeadline{Clock::time_point::max()};
    std::function<bool()> cancellation;
    bool timedOut{};
    bool callbackFailed{};

    void arm(std::chrono::milliseconds timeout) {
        timedOut = false;
        deadline = std::min(Clock::now() + timeout, hardDeadline);
    }
    static int check(void* opaque) noexcept {
        auto& self = *static_cast<InterruptState*>(opaque);
        if (self.cancelled.load(std::memory_order_relaxed)) return 1;
        if (self.cancellation) {
            try {
                if (self.cancellation()) {
                    self.cancelled.store(true, std::memory_order_relaxed);
                    return 1;
                }
            } catch (...) {
                self.callbackFailed = true;
                return 1;
            }
        }
        if (Clock::now() >= self.deadline) {
            self.timedOut = true;
            return 1;
        }
        return 0;
    }
};

Error backendError(int native, std::string operation, const InterruptState* state = nullptr) {
    if (state && state->callbackFailed)
        return error(ErrorCode::BackendFailure, operation + ": cancellation callback threw an exception", native);
    if (state && state->cancelled.load(std::memory_order_relaxed))
        return error(ErrorCode::Cancelled, operation + ": cancelled", native);
    if (state && state->timedOut)
        return error(ErrorCode::Timeout, operation + ": timeout", native);
    ErrorCode code = ErrorCode::BackendFailure;
    if (native == AVERROR_EOF) code = ErrorCode::EndOfStream;
    else if (native == AVERROR_INVALIDDATA) code = ErrorCode::InvalidMedia;
    else if (native == AVERROR(ENOENT)) code = ErrorCode::NotFound;
    else if (native == AVERROR(EACCES) || native == AVERROR(EPERM)) code = ErrorCode::PermissionDenied;
    else if (native == AVERROR(EINVAL)) code = ErrorCode::InvalidArgument;
    else if (native == AVERROR(ETIMEDOUT)) code = ErrorCode::Timeout;
    else if (native == AVERROR(EAGAIN) || native == AVERROR(EBUSY) || native == AVERROR(EEXIST)) code = ErrorCode::Busy;
    else if (native == AVERROR(ENOSYS) || native == AVERROR_DECODER_NOT_FOUND || native == AVERROR_MUXER_NOT_FOUND)
        code = ErrorCode::Unsupported;
    else if (native == AVERROR(EIO) || native == AVERROR(ENOSPC)) code = ErrorCode::Io;
    char buffer[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(native, buffer, sizeof buffer);
    return error(code, operation + ": " + buffer, native);
}

Result<void> initialize() {
    static std::once_flag flag;
    static int result = 0;
    std::call_once(flag, [] {
        avdevice_register_all();
        result = avformat_network_init();
    });
    if (result < 0) return std::unexpected(backendError(result, "initialize FFmpeg networking"));
    return {};
}

struct Dictionary {
    AVDictionary* value{};
    ~Dictionary() { av_dict_free(&value); }
    Result<void> set(const Metadata& entries) {
        for (const auto& [key, valueText] : entries) {
            int result = av_dict_set(&value, key.c_str(), valueText.c_str(), 0);
            if (result < 0) return std::unexpected(backendError(result, "copy FFmpeg option"));
        }
        return {};
    }
};

Metadata metadata(AVDictionary* values) {
    Metadata result;
    const AVDictionaryEntry* entry = nullptr;
    while ((entry = av_dict_get(values, "", entry, AV_DICT_IGNORE_SUFFIX)))
        result.emplace(entry->key, entry->value);
    return result;
}

MediaType type(AVMediaType value) {
    switch (value) {
    case AVMEDIA_TYPE_VIDEO: return MediaType::Video;
    case AVMEDIA_TYPE_AUDIO: return MediaType::Audio;
    case AVMEDIA_TYPE_SUBTITLE: return MediaType::Subtitle;
    case AVMEDIA_TYPE_DATA: return MediaType::Data;
    case AVMEDIA_TYPE_ATTACHMENT: return MediaType::Attachment;
    default: return MediaType::Unknown;
    }
}

Rational rational(AVRational value) { return Rational{value.num, value.den}; }
std::optional<std::int64_t> timestamp(std::int64_t value) {
    if (value == AV_NOPTS_VALUE) return std::nullopt;
    return value;
}

std::vector<std::string> split(const char* values) {
    std::vector<std::string> result;
    if (!values) return result;
    std::string_view remaining(values);
    while (!remaining.empty()) {
        const auto position = remaining.find(',');
        result.emplace_back(remaining.substr(0, position));
        if (position == std::string_view::npos) break;
        remaining.remove_prefix(position + 1);
    }
    return result;
}

MediaInfo inspect(AVFormatContext* context, const Metadata& unusedOptions) {
    MediaInfo result;
    result.formatName = text(context->iformat->name);
    result.formatDescription = text(context->iformat->long_name);
    if (context->duration != AV_NOPTS_VALUE)
        result.duration = std::chrono::microseconds{context->duration};
    result.bitRate = context->bit_rate;
    result.seekable = context->pb && (context->pb->seekable & AVIO_SEEKABLE_NORMAL);
    result.metadata = metadata(context->metadata);
    result.unusedInputOptions = unusedOptions;
    for (unsigned i = 0; i < context->nb_streams; ++i) {
        const auto* stream = context->streams[i];
        const auto* parameters = stream->codecpar;
        StreamInfo value;
        value.index = static_cast<int>(i);
        value.type = type(parameters->codec_type);
        value.codecId = parameters->codec_id;
        value.codecName = text(avcodec_get_name(parameters->codec_id));
        value.codecTag = parameters->codec_tag;
        if (parameters->extradata && parameters->extradata_size > 0) {
            const auto* begin = reinterpret_cast<const std::byte*>(parameters->extradata);
            value.codecExtraData.assign(begin, begin + parameters->extradata_size);
        }
        value.parametersComplete = parameters->codec_id != AV_CODEC_ID_NONE;
        if (parameters->codec_type == AVMEDIA_TYPE_VIDEO)
            value.parametersComplete = value.parametersComplete && parameters->width > 0 && parameters->height > 0;
        else if (parameters->codec_type == AVMEDIA_TYPE_AUDIO)
            value.parametersComplete = value.parametersComplete && parameters->sample_rate > 0 && parameters->ch_layout.nb_channels > 0;
        value.timeBase = rational(stream->time_base);
        value.frameRate = rational(av_guess_frame_rate(context, context->streams[i], nullptr));
        value.sampleAspectRatio = rational(parameters->sample_aspect_ratio);
        value.startTime = timestamp(stream->start_time);
        value.duration = timestamp(stream->duration);
        value.bitRate = parameters->bit_rate;
        value.dimensions = Size{static_cast<std::uint32_t>(std::max(0, parameters->width)),
                                static_cast<std::uint32_t>(std::max(0, parameters->height))};
        if (parameters->codec_type == AVMEDIA_TYPE_VIDEO)
            value.pixelFormat = text(av_get_pix_fmt_name(static_cast<AVPixelFormat>(parameters->format)));
        value.colorRange = text(av_color_range_name(parameters->color_range));
        value.colorSpace = text(av_color_space_name(parameters->color_space));
        value.colorTransfer = text(av_color_transfer_name(parameters->color_trc));
        value.colorPrimaries = text(av_color_primaries_name(parameters->color_primaries));
        value.sampleRate = parameters->sample_rate;
        value.channels = parameters->ch_layout.nb_channels;
        if (parameters->codec_type == AVMEDIA_TYPE_AUDIO)
            value.sampleFormat = text(av_get_sample_fmt_name(static_cast<AVSampleFormat>(parameters->format)));
        char layout[256]{};
        if (parameters->ch_layout.nb_channels && av_channel_layout_describe(&parameters->ch_layout, layout, sizeof layout) >= 0)
            value.channelLayout = layout;
        value.metadata = metadata(stream->metadata);
        result.streams.push_back(std::move(value));
    }
    result.streamParametersComplete = !result.streams.empty() &&
        std::all_of(result.streams.begin(), result.streams.end(), [](const auto& stream) { return stream.parametersComplete; });
    return result;
}

Result<void> validateStreams(AVFormatContext* context) {
    for (unsigned i = 0; i < context->nb_streams; ++i) {
        const auto* parameters = context->streams[i]->codecpar;
        if (parameters->extradata_size < 0 || parameters->extradata_size > 16 * 1024 * 1024 ||
            (parameters->extradata_size > 0 && !parameters->extradata))
            return std::unexpected(error(ErrorCode::InvalidMedia, "invalid or excessive codec initialization data"));
    }
    return {};
}

struct ByteInput {
    std::span<const std::byte> bytes;
    std::size_t position{};
    static int read(void* opaque, std::uint8_t* output, int requested) {
        auto& self = *static_cast<ByteInput*>(opaque);
        if (requested <= 0) return AVERROR(EINVAL);
        const auto count = std::min(static_cast<std::size_t>(requested), self.bytes.size() - self.position);
        if (!count) return AVERROR_EOF;
        std::memcpy(output, self.bytes.data() + self.position, count);
        self.position += count;
        return static_cast<int>(count);
    }
    static std::int64_t seek(void* opaque, std::int64_t offset, int whence) {
        auto& self = *static_cast<ByteInput*>(opaque);
        whence &= ~AVSEEK_FORCE;
        if (whence == AVSEEK_SIZE) return static_cast<std::int64_t>(self.bytes.size());
        std::int64_t origin = 0;
        if (whence == SEEK_CUR) origin = static_cast<std::int64_t>(self.position);
        else if (whence == SEEK_END) origin = static_cast<std::int64_t>(self.bytes.size());
        else if (whence != SEEK_SET) return AVERROR(EINVAL);
        if ((offset > 0 && origin > std::numeric_limits<std::int64_t>::max() - offset) ||
            (offset < 0 && origin < std::numeric_limits<std::int64_t>::min() - offset)) return AVERROR(EINVAL);
        const auto position = origin + offset;
        if (position < 0 || static_cast<std::uint64_t>(position) > self.bytes.size()) return AVERROR(EINVAL);
        self.position = static_cast<std::size_t>(position);
        return position;
    }
};

void freeIo(AVIOContext*& io) {
    if (io) {
        av_freep(&io->buffer);
        avio_context_free(&io);
    }
}

struct Input {
    AVFormatContext* context{};
    AVIOContext* customIo{};
    ByteInput bytes;
    InterruptState interrupt;
    std::chrono::milliseconds timeout{30'000};
    Metadata unusedOptions;
    ~Input() {
        avformat_close_input(&context);
        freeIo(customIo);
    }
};

Result<void> validateSource(const MediaSource& source) {
    if (source.location.empty() || containsNull(source.location) || containsNull(source.inputFormat) ||
        !validMetadata(source.options) || source.timeout.count() <= 0 ||
        source.timeout > std::chrono::hours{24} || source.maximumProbeBytes < 32 ||
        source.maximumAnalysisDuration.count() < 0 || source.maximumAnalysisDuration > std::chrono::hours{24})
        return std::unexpected(error(ErrorCode::InvalidArgument, "invalid source, options, timeout, or probe limits"));
    return {};
}

Result<std::unique_ptr<Input>> openInput(const MediaSource& source,
    std::function<bool()> cancellation = {}, Clock::time_point hardDeadline = Clock::time_point::max()) {
    if (auto valid = validateSource(source); !valid) return std::unexpected(valid.error());
    if (auto ready = initialize(); !ready) return std::unexpected(ready.error());
    const AVInputFormat* format = nullptr;
    if (!source.inputFormat.empty()) {
        format = av_find_input_format(source.inputFormat.c_str());
        if (!format) return std::unexpected(error(ErrorCode::Unsupported, "input format is unavailable: " + source.inputFormat));
    }
    auto result = std::make_unique<Input>();
    result->timeout = source.timeout;
    result->interrupt.cancellation = std::move(cancellation);
    result->interrupt.hardDeadline = hardDeadline;
    result->interrupt.arm(source.timeout);
    result->context = avformat_alloc_context();
    if (!result->context) return std::unexpected(error(ErrorCode::BackendFailure, "allocate input context"));
    result->context->interrupt_callback = AVIOInterruptCB{InterruptState::check, &result->interrupt};
    result->context->probesize = source.maximumProbeBytes;
    result->context->max_analyze_duration = std::chrono::duration_cast<std::chrono::microseconds>(source.maximumAnalysisDuration).count();
    Dictionary options;
    if (auto copied = options.set(source.options); !copied) return std::unexpected(copied.error());
    if (InterruptState::check(&result->interrupt)) return std::unexpected(backendError(AVERROR_EXIT, "open input", &result->interrupt));
    int status = avformat_open_input(&result->context, source.location.c_str(), format, &options.value);
    if (status < 0) return std::unexpected(backendError(status, "open input", &result->interrupt));
    result->unusedOptions = metadata(options.value);
    status = avformat_find_stream_info(result->context, nullptr);
    if (status < 0) return std::unexpected(backendError(status, "inspect input streams", &result->interrupt));
    if (InterruptState::check(&result->interrupt)) return std::unexpected(backendError(AVERROR_EXIT, "inspect input streams", &result->interrupt));
    if (auto valid = validateStreams(result->context); !valid) return std::unexpected(valid.error());
    return result;
}

struct PacketDeleter { void operator()(AVPacket* packet) const { av_packet_free(&packet); } };
struct FrameDeleter { void operator()(AVFrame* frame) const { av_frame_free(&frame); } };
struct CodecDeleter { void operator()(AVCodecContext* codec) const { avcodec_free_context(&codec); } };
struct ScaleDeleter { void operator()(SwsContext* scale) const { sws_freeContext(scale); } };
using NativePacket = std::unique_ptr<AVPacket, PacketDeleter>;
using NativeFrame = std::unique_ptr<AVFrame, FrameDeleter>;
using NativeCodec = std::unique_ptr<AVCodecContext, CodecDeleter>;

struct DecodedColor {
    AVColorSpace sourceSpace{AVCOL_SPC_UNSPECIFIED};
    AVColorRange sourceRange{AVCOL_RANGE_UNSPECIFIED};
    AVColorTransferCharacteristic transfer{AVCOL_TRC_UNSPECIFIED};
    AVColorPrimaries primaries{AVCOL_PRI_UNSPECIFIED};
    int coefficients{SWS_CS_DEFAULT};
    bool matrixConversion{};
    bool grayInput{};
    bool spaceAssumed{};
    bool rangeAssumed{};
};

// RGB24 remains in the input transfer/primaries. This only resolves the encoded
// YUV matrix and code-value range; it is not an ICC, transfer, or gamut transform.
Result<DecodedColor> decodedColor(const AVFrame* frame, const AVCodecParameters* parameters) {
    const auto* descriptor = av_pix_fmt_desc_get(static_cast<AVPixelFormat>(frame->format));
    if (!descriptor) return std::unexpected(error(ErrorCode::Unsupported, "unknown decoded pixel format"));
    if (descriptor->flags & (AV_PIX_FMT_FLAG_BAYER | AV_PIX_FMT_FLAG_XYZ))
        return std::unexpected(error(ErrorCode::Unsupported, "RGB24 helper does not implement a sensor RAW or XYZ color transform"));
    DecodedColor result;
    result.sourceSpace = frame->colorspace == AVCOL_SPC_UNSPECIFIED ? parameters->color_space : frame->colorspace;
    result.sourceRange = frame->color_range == AVCOL_RANGE_UNSPECIFIED ? parameters->color_range : frame->color_range;
    result.transfer = frame->color_trc == AVCOL_TRC_UNSPECIFIED ? parameters->color_trc : frame->color_trc;
    result.primaries = frame->color_primaries == AVCOL_PRI_UNSPECIFIED ? parameters->color_primaries : frame->color_primaries;
    const bool rgb = descriptor->flags & (AV_PIX_FMT_FLAG_RGB | AV_PIX_FMT_FLAG_PAL);
    const bool gray = !rgb && descriptor->nb_components <= 2;
    result.matrixConversion = !rgb;
    result.grayInput = gray;
    if (result.sourceSpace == AVCOL_SPC_UNSPECIFIED) {
        result.spaceAssumed = true;
        result.sourceSpace = rgb ? AVCOL_SPC_RGB : AVCOL_SPC_BT470BG;
    }
    if (result.sourceRange == AVCOL_RANGE_UNSPECIFIED) {
        result.rangeAssumed = true;
        const bool jpegPixels = std::string_view(descriptor->name).starts_with("yuvj");
        result.sourceRange = rgb || gray || jpegPixels ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG;
    }
    if (result.sourceRange != AVCOL_RANGE_JPEG && result.sourceRange != AVCOL_RANGE_MPEG)
        return std::unexpected(error(ErrorCode::Unsupported, "unsupported decoded color range"));
    if (!result.matrixConversion && result.sourceRange != AVCOL_RANGE_JPEG)
        return std::unexpected(error(ErrorCode::Unsupported, "RGB24 helper does not implement limited-range RGB expansion"));
    if (result.matrixConversion) {
        switch (result.sourceSpace) {
        case AVCOL_SPC_RGB:
            if (!gray) return std::unexpected(error(ErrorCode::Unsupported, "a YUV frame cannot use RGB matrix coefficients"));
            break;
        case AVCOL_SPC_BT709: result.coefficients = SWS_CS_ITU709; break;
        case AVCOL_SPC_FCC: result.coefficients = SWS_CS_FCC; break;
        case AVCOL_SPC_BT470BG:
        case AVCOL_SPC_SMPTE170M: result.coefficients = SWS_CS_ITU601; break;
        case AVCOL_SPC_SMPTE240M: result.coefficients = SWS_CS_SMPTE240M; break;
        case AVCOL_SPC_BT2020_NCL: result.coefficients = SWS_CS_BT2020; break;
        default:
            return std::unexpected(error(ErrorCode::Unsupported,
                "RGB24 helper does not implement the decoded YUV color matrix: " + text(av_color_space_name(result.sourceSpace))));
        }
    }
    return result;
}

bool streamHasIccProfile(const AVStream* stream) {
#if LIBAVCODEC_VERSION_MAJOR >= 61
    return av_packet_side_data_get(stream->codecpar->coded_side_data,
        stream->codecpar->nb_coded_side_data, AV_PKT_DATA_ICC_PROFILE) != nullptr;
#else
    std::size_t size{};
    return av_stream_get_side_data(stream, AV_PKT_DATA_ICC_PROFILE, &size) != nullptr;
#endif
}

Result<void> readNative(Input& input, AVPacket* packet,
                        Clock::time_point readDeadline = Clock::time_point::max()) {
    input.interrupt.arm(input.timeout);
    input.interrupt.deadline = std::min(input.interrupt.deadline, readDeadline);
    if (InterruptState::check(&input.interrupt)) return std::unexpected(backendError(AVERROR_EXIT, "read input packet", &input.interrupt));
    const int status = av_read_frame(input.context, packet);
    if (status < 0) return std::unexpected(backendError(status, "read input packet", &input.interrupt));
    if (packet->stream_index < 0 || static_cast<unsigned>(packet->stream_index) >= input.context->nb_streams ||
        packet->size < 0 || static_cast<std::size_t>(packet->size) > MaximumPacketBytes)
        return std::unexpected(error(ErrorCode::InvalidMedia, "invalid stream index or excessive packet size"));
    if (InterruptState::check(&input.interrupt)) return std::unexpected(backendError(AVERROR_EXIT, "read input packet", &input.interrupt));
    return {};
}

std::string version(unsigned value) {
    return std::to_string(value >> 16) + "." + std::to_string((value >> 8) & 255) + "." + std::to_string(value & 255);
}

struct Output {
    AVFormatContext* context{};
    AVIOContext* io{};
    std::FILE* file{};
    std::filesystem::path destination;
    bool committed{};
    InterruptState* interrupt{};
    ~Output() {
        if (context) { context->pb = nullptr; avformat_free_context(context); }
        freeIo(io);
        if (file) std::fclose(file);
        if (!committed && !destination.empty()) {
            std::error_code ignored;
            std::filesystem::remove(destination, ignored);
        }
    }
    static int write(void* opaque, const std::uint8_t* data, int size) {
        auto& self = *static_cast<Output*>(opaque);
        if (InterruptState::check(self.interrupt)) return AVERROR_EXIT;
        const auto written = std::fwrite(data, 1, static_cast<std::size_t>(size), self.file);
        if (written != static_cast<std::size_t>(size)) return AVERROR(errno ? errno : EIO);
        return size;
    }
    static std::int64_t seek(void* opaque, std::int64_t offset, int whence) {
        auto& self = *static_cast<Output*>(opaque);
        whence &= ~AVSEEK_FORCE;
#ifdef _WIN32
        const auto position = _ftelli64(self.file);
        if (whence == AVSEEK_SIZE) {
            if (_fseeki64(self.file, 0, SEEK_END) != 0) return AVERROR(errno);
            const auto size = _ftelli64(self.file);
            if (_fseeki64(self.file, position, SEEK_SET) != 0) return AVERROR(errno);
            return size;
        }
        if (_fseeki64(self.file, offset, whence) != 0) return AVERROR(errno);
        return _ftelli64(self.file);
#else
        const auto position = ftello(self.file);
        if (whence == AVSEEK_SIZE) {
            if (fseeko(self.file, 0, SEEK_END) != 0) return AVERROR(errno);
            const auto size = ftello(self.file);
            if (fseeko(self.file, position, SEEK_SET) != 0) return AVERROR(errno);
            return size;
        }
        if (fseeko(self.file, static_cast<off_t>(offset), whence) != 0) return AVERROR(errno);
        return ftello(self.file);
#endif
    }
};

} // namespace

struct PacketCapture::Impl {
    std::unique_ptr<Input> input;
    MediaInfo info;
};

PacketCapture::PacketCapture(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
PacketCapture::PacketCapture(PacketCapture&&) noexcept = default;
PacketCapture& PacketCapture::operator=(PacketCapture&&) noexcept = default;
PacketCapture::~PacketCapture() = default;

Result<PacketCapture> PacketCapture::open(const MediaSource& source) {
    auto input = openInput(source);
    if (!input) return std::unexpected(input.error());
    auto impl = std::make_unique<Impl>();
    impl->input = std::move(*input);
    impl->info = inspect(impl->input->context, impl->input->unusedOptions);
    return PacketCapture{std::move(impl)};
}

const MediaInfo& PacketCapture::metadata() const noexcept {
    static const MediaInfo empty;
    return impl_ ? impl_->info : empty;
}

Result<MediaPacket> PacketCapture::read() {
    if (!impl_) return std::unexpected(error(ErrorCode::InvalidArgument, "capture session was moved from"));
    NativePacket packet{av_packet_alloc()};
    if (!packet) return std::unexpected(error(ErrorCode::BackendFailure, "allocate packet"));
    if (auto result = readNative(*impl_->input, packet.get()); !result) return std::unexpected(result.error());
    if (impl_->input->context->streams[packet->stream_index]->codecpar->codec_id == AV_CODEC_ID_WRAPPED_AVFRAME)
        return std::unexpected(error(ErrorCode::Unsupported,
            "wrapped_avframe packets contain native pointers; use decodeRgbFrame for owned RGB pixels"));
    MediaPacket result;
    result.streamIndex = packet->stream_index;
    result.timeBase = rational(impl_->input->context->streams[packet->stream_index]->time_base);
    result.presentationTimestamp = timestamp(packet->pts);
    result.decodingTimestamp = timestamp(packet->dts);
    result.duration = packet->duration;
    result.keyFrame = packet->flags & AV_PKT_FLAG_KEY;
    if (packet->size) {
        const auto* begin = reinterpret_cast<const std::byte*>(packet->data);
        result.payload.assign(begin, begin + packet->size);
    }
    return result;
}

void PacketCapture::cancel() noexcept {
    if (impl_) impl_->input->interrupt.cancelled.store(true, std::memory_order_relaxed);
}

Result<std::vector<CodecInfo>> codecCatalog() {
    if (auto ready = initialize(); !ready) return std::unexpected(ready.error());
    std::map<int, std::pair<std::vector<std::string>, std::vector<std::string>>> implementations;
    void* iterator = nullptr;
    while (const auto* codec = av_codec_iterate(&iterator)) {
        auto& value = implementations[codec->id];
        if (av_codec_is_decoder(codec)) value.first.emplace_back(text(codec->name));
        if (av_codec_is_encoder(codec)) value.second.emplace_back(text(codec->name));
    }
    std::vector<CodecInfo> result;
    const AVCodecDescriptor* descriptor = nullptr;
    while ((descriptor = avcodec_descriptor_next(descriptor))) {
        CodecInfo value;
        value.id = descriptor->id;
        value.name = text(descriptor->name);
        value.description = text(descriptor->long_name);
        value.type = type(descriptor->type);
        if (descriptor->mime_types)
            for (const char* const* mime = descriptor->mime_types; *mime; ++mime) value.mimeTypes.emplace_back(*mime);
        value.decoders = implementations[descriptor->id].first;
        value.encoders = implementations[descriptor->id].second;
        value.lossy = descriptor->props & AV_CODEC_PROP_LOSSY;
        value.lossless = descriptor->props & AV_CODEC_PROP_LOSSLESS;
        value.intraOnly = descriptor->props & AV_CODEC_PROP_INTRA_ONLY;
        result.push_back(std::move(value));
    }
    return result;
}

Result<std::vector<FormatInfo>> formatCatalog() {
    if (auto ready = initialize(); !ready) return std::unexpected(ready.error());
    std::map<std::string, FormatInfo, std::less<>> catalog;
    void* iterator = nullptr;
    while (const auto* format = av_demuxer_iterate(&iterator)) {
        auto& value = catalog[text(format->name)];
        value.name = text(format->name);
        value.description = text(format->long_name);
        value.extensions = split(format->extensions);
        value.mimeTypes = split(format->mime_type);
        value.inputAvailable = true;
    }
    iterator = nullptr;
    while (const auto* format = av_muxer_iterate(&iterator)) {
        auto& value = catalog[text(format->name)];
        value.name = text(format->name);
        if (value.description.empty()) value.description = text(format->long_name);
        if (value.extensions.empty()) value.extensions = split(format->extensions);
        if (value.mimeTypes.empty()) value.mimeTypes = split(format->mime_type);
        value.outputAvailable = true;
    }
    std::vector<FormatInfo> result;
    for (auto& [key, value] : catalog) result.push_back(std::move(value));
    return result;
}

Result<MediaBackendInfo> mediaBackendInfo() {
    MediaBackendInfo result;
    result.name = "FFmpeg";
    result.versions = {{"libavformat", version(avformat_version())}, {"libavcodec", version(avcodec_version())},
                       {"libavdevice", version(avdevice_version())}, {"libavutil", version(avutil_version())},
                       {"libswscale", version(swscale_version())}};
    result.licenses = {{"libavformat", text(avformat_license())}, {"libavcodec", text(avcodec_license())},
                       {"libavdevice", text(avdevice_license())}, {"libavutil", text(avutil_license())},
                       {"libswscale", text(swscale_license())}};
    result.configuration = text(avcodec_configuration());
    return result;
}

Result<MediaInfo> probeMedia(const MediaSource& source) {
    auto input = openInput(source);
    if (!input) return std::unexpected(input.error());
    return inspect((*input)->context, (*input)->unusedOptions);
}

Result<MediaInfo> probeMediaBytes(std::span<const std::byte> bytes, std::chrono::milliseconds timeout) {
    if (bytes.empty() || bytes.size() > static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max()) ||
        timeout.count() <= 0 || timeout > std::chrono::hours{24})
        return std::unexpected(error(ErrorCode::InvalidArgument, "empty bytes, excessive size, or invalid timeout"));
    if (auto ready = initialize(); !ready) return std::unexpected(ready.error());
    auto input = std::make_unique<Input>();
    input->bytes.bytes = bytes;
    input->interrupt.arm(timeout);
    input->context = avformat_alloc_context();
    if (!input->context) return std::unexpected(error(ErrorCode::BackendFailure, "allocate byte input context"));
    auto* buffer = static_cast<std::uint8_t*>(av_malloc(32 * 1024));
    if (!buffer) return std::unexpected(error(ErrorCode::BackendFailure, "allocate byte input buffer"));
    input->customIo = avio_alloc_context(buffer, 32 * 1024, 0, &input->bytes, ByteInput::read, nullptr, ByteInput::seek);
    if (!input->customIo) {
        av_free(buffer);
        return std::unexpected(error(ErrorCode::BackendFailure, "allocate byte input IO"));
    }
    input->context->pb = input->customIo;
    input->context->flags |= AVFMT_FLAG_CUSTOM_IO;
    input->context->interrupt_callback = AVIOInterruptCB{InterruptState::check, &input->interrupt};
    input->context->probesize = 8 * 1024 * 1024;
    input->context->max_analyze_duration = 5 * AV_TIME_BASE;
    int status = avformat_open_input(&input->context, nullptr, nullptr, nullptr);
    if (status < 0) return std::unexpected(backendError(status, "recognize media bytes", &input->interrupt));
    status = avformat_find_stream_info(input->context, nullptr);
    if (status < 0) return std::unexpected(backendError(status, "inspect media bytes", &input->interrupt));
    if (InterruptState::check(&input->interrupt)) return std::unexpected(backendError(AVERROR_EXIT, "inspect media bytes", &input->interrupt));
    if (auto valid = validateStreams(input->context); !valid) return std::unexpected(valid.error());
    return inspect(input->context, {});
}

Result<RgbFrame> decodeRgbFrame(const MediaSource& source, const DecodeOptions& options) {
    if (options.streamIndex < -1 || !options.maximumPixels || !options.maximumPackets ||
        options.timeout.count() <= 0 || options.timeout > std::chrono::hours{24})
        return std::unexpected(error(ErrorCode::InvalidArgument, "invalid decode limits or stream index"));
    const auto deadline = Clock::now() + options.timeout;
    auto opened = openInput(source, options.cancelled, deadline);
    if (!opened) return std::unexpected(opened.error());
    auto& input = **opened;
    int index = options.streamIndex;
    if (index < 0) index = av_find_best_stream(input.context, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (index < 0 || static_cast<unsigned>(index) >= input.context->nb_streams ||
        input.context->streams[index]->codecpar->codec_type != AVMEDIA_TYPE_VIDEO)
        return std::unexpected(error(ErrorCode::Unsupported, "requested video stream is unavailable"));
    auto* stream = input.context->streams[index];
    const auto* decoder = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!decoder) return std::unexpected(error(ErrorCode::Unsupported, "video decoder is unavailable"));
    NativeCodec codec{avcodec_alloc_context3(decoder)};
    NativeFrame frame{av_frame_alloc()};
    NativePacket packet{av_packet_alloc()};
    if (!codec || !frame || !packet) return std::unexpected(error(ErrorCode::BackendFailure, "allocate decoder buffers"));
    int status = avcodec_parameters_to_context(codec.get(), stream->codecpar);
    if (status < 0) return std::unexpected(backendError(status, "copy decoder parameters"));
    codec->max_pixels = static_cast<std::int64_t>(std::min(options.maximumPixels,
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())));
    status = avcodec_open2(codec.get(), decoder, nullptr);
    if (status < 0) return std::unexpected(backendError(status, "open video decoder"));
    bool draining = false;
    std::uint64_t packets = 0;
    for (;;) {
        input.interrupt.arm(input.timeout);
        if (InterruptState::check(&input.interrupt)) return std::unexpected(backendError(AVERROR_EXIT, "decode video frame", &input.interrupt));
        status = avcodec_receive_frame(codec.get(), frame.get());
        if (status == 0) break;
        if (status == AVERROR_EOF) return std::unexpected(error(ErrorCode::EndOfStream, "no video frame decoded"));
        if (status != AVERROR(EAGAIN)) return std::unexpected(backendError(status, "decode video frame"));
        if (draining) return std::unexpected(error(ErrorCode::InvalidMedia, "decoder requested a packet after draining"));
        bool submitted = false;
        while (!submitted) {
            if (packets >= options.maximumPackets)
                return std::unexpected(error(ErrorCode::Timeout, "decode packet limit reached before a frame"));
            auto read = readNative(input, packet.get());
            if (!read) {
                if (read.error().code != ErrorCode::EndOfStream) return std::unexpected(read.error());
                status = avcodec_send_packet(codec.get(), nullptr);
                draining = true;
                submitted = true;
            } else {
                ++packets;
                if (packet->stream_index == index) {
                    status = avcodec_send_packet(codec.get(), packet.get());
                    submitted = true;
                }
                av_packet_unref(packet.get());
            }
            if (submitted && status < 0) return std::unexpected(backendError(status, "submit video packet"));
        }
    }
    if (frame->width <= 0 || frame->height <= 0 ||
        static_cast<std::uint64_t>(frame->width) * frame->height > options.maximumPixels ||
        static_cast<std::uint64_t>(frame->width) * frame->height > std::numeric_limits<std::size_t>::max() / 3 ||
        frame->width > std::numeric_limits<int>::max() / 3)
        return std::unexpected(error(ErrorCode::InvalidMedia, "decoded frame dimensions exceed limits"));
    auto color = decodedColor(frame.get(), stream->codecpar);
    if (!color) return std::unexpected(color.error());
    RgbFrame result;
    result.dimensions = Size{static_cast<std::uint32_t>(frame->width), static_cast<std::uint32_t>(frame->height)};
    result.rowStride = static_cast<std::size_t>(frame->width) * 3;
    result.pixels.resize(result.rowStride * frame->height);
    std::unique_ptr<SwsContext, ScaleDeleter> scaler{sws_getContext(frame->width, frame->height,
        static_cast<AVPixelFormat>(frame->format), frame->width, frame->height, AV_PIX_FMT_RGB24,
        SWS_BILINEAR, nullptr, nullptr, nullptr)};
    if (!scaler) return std::unexpected(error(ErrorCode::Unsupported, "pixel format cannot be converted to RGB24"));
    if (color->matrixConversion) {
        const auto* coefficients = sws_getCoefficients(color->coefficients);
        // Gray-to-RGB fast paths ignore limited-range expansion in some FFmpeg
        // versions. Normalize their full-range 8-bit code values explicitly below.
        const bool scalerFullRange = color->grayInput || color->sourceRange == AVCOL_RANGE_JPEG;
        status = sws_setColorspaceDetails(scaler.get(), coefficients, scalerFullRange,
            coefficients, 1, 0, 1 << 16, 1 << 16);
        if (status < 0) return std::unexpected(backendError(status, "configure decoded color matrix and range"));
    }
    std::uint8_t* planes[]{result.pixels.data(), nullptr, nullptr, nullptr};
    int strides[]{static_cast<int>(result.rowStride), 0, 0, 0};
    status = sws_scale(scaler.get(), frame->data, frame->linesize, 0, frame->height, planes, strides);
    if (status != frame->height) return std::unexpected(error(ErrorCode::BackendFailure, "incomplete RGB conversion", status));
    if (color->grayInput && color->sourceRange == AVCOL_RANGE_MPEG) {
        for (auto& value : result.pixels)
            value = value <= 16 ? 0 : value >= 235 ? 255 :
                static_cast<std::uint8_t>(((static_cast<int>(value) - 16) * 255 + 109) / 219);
    }
    if (InterruptState::check(&input.interrupt)) return std::unexpected(backendError(AVERROR_EXIT, "convert video frame", &input.interrupt));
    result.presentationTimestamp = timestamp(frame->best_effort_timestamp);
    result.timeBase = rational(stream->time_base);
    result.metadata = metadata(frame->metadata);
    result.colorSpace = text(av_color_space_name(AVCOL_SPC_RGB));
    result.colorRange = text(av_color_range_name(AVCOL_RANGE_JPEG));
    result.colorTransfer = text(av_color_transfer_name(color->transfer));
    result.colorPrimaries = text(av_color_primaries_name(color->primaries));
    result.metadata["iiCamera.color.sourceSpace"] = text(av_color_space_name(color->sourceSpace));
    result.metadata["iiCamera.color.sourceRange"] = text(av_color_range_name(color->sourceRange));
    result.metadata["iiCamera.color.sourceSpaceAssumed"] = color->spaceAssumed ? "true" : "false";
    result.metadata["iiCamera.color.sourceRangeAssumed"] = color->rangeAssumed ? "true" : "false";
    result.metadata["iiCamera.color.transferConverted"] = "false";
    result.metadata["iiCamera.color.primariesConverted"] = "false";
    result.metadata["iiCamera.color.precision"] = "8-bit";
    result.metadata["iiCamera.color.hdrPreserved"] = "false";
    result.metadata["iiCamera.color.iccProfilePresent"] =
        av_frame_get_side_data(frame.get(), AV_FRAME_DATA_ICC_PROFILE) || streamHasIccProfile(stream) ? "true" : "false";
    return result;
}

Result<RecordingReport> recordMedia(const MediaSource& source, const std::filesystem::path& destination,
                                   const RecordingOptions& options) {
    if (destination.empty() || containsNull(destination.string()) || containsNull(options.outputFormat) ||
        !validMetadata(options.options) || options.maximumDuration.count() <= 0 ||
        options.maximumDuration > std::chrono::hours{24} || !options.maximumPackets || !options.maximumPayloadBytes ||
        options.timeout.count() <= 0 || options.timeout > std::chrono::hours{24})
        return std::unexpected(error(ErrorCode::InvalidArgument, "invalid destination, options, or recording limits"));
    const auto started = Clock::now();
    auto opened = openInput(source, options.cancelled, started + options.timeout);
    if (!opened) return std::unexpected(opened.error());
    auto& input = **opened;
    Output output;
    output.interrupt = &input.interrupt;
    int status = avformat_alloc_output_context2(&output.context, nullptr,
        options.outputFormat.empty() ? nullptr : options.outputFormat.c_str(), destination.string().c_str());
    if (status < 0 || !output.context)
        return std::unexpected(backendError(status < 0 ? status : AVERROR_MUXER_NOT_FOUND, "select output format"));
    if (output.context->oformat->flags & AVFMT_NOFILE)
        return std::unexpected(error(ErrorCode::Unsupported, "recording requires a local single-file muxer"));
    // Muxers that open auxiliary paths cannot honor the single-file no-overwrite contract.
    const auto muxer = std::string_view(output.context->oformat->name);
    if (muxer == "hls" || muxer == "dash" || muxer == "segment" || muxer == "stream_segment" ||
        muxer == "ssegment" || muxer == "image2" || muxer == "webm_chunk" || muxer == "tee")
        return std::unexpected(error(ErrorCode::Unsupported, "multi-file muxers are not supported by recordMedia"));
    if (!input.context->nb_streams) return std::unexpected(error(ErrorCode::InvalidMedia, "input has no streams"));
    output.context->interrupt_callback = AVIOInterruptCB{InterruptState::check, &input.interrupt};
    std::vector<int> streamMap(input.context->nb_streams, -1);
    unsigned mapped = 0;
    for (unsigned i = 0; i < input.context->nb_streams; ++i) {
        const auto* sourceStream = input.context->streams[i];
        const auto mediaType = sourceStream->codecpar->codec_type;
        if (mediaType != AVMEDIA_TYPE_VIDEO && mediaType != AVMEDIA_TYPE_AUDIO && mediaType != AVMEDIA_TYPE_SUBTITLE) continue;
        const int compatible = avformat_query_codec(output.context->oformat, sourceStream->codecpar->codec_id, FF_COMPLIANCE_NORMAL);
        if (compatible == 0) return std::unexpected(error(ErrorCode::Unsupported, "output container rejects input codec " + text(avcodec_get_name(sourceStream->codecpar->codec_id))));
        auto* targetStream = avformat_new_stream(output.context, nullptr);
        if (!targetStream) return std::unexpected(error(ErrorCode::BackendFailure, "allocate output stream"));
        status = avcodec_parameters_copy(targetStream->codecpar, sourceStream->codecpar);
        if (status < 0) return std::unexpected(backendError(status, "copy output stream parameters"));
        targetStream->codecpar->codec_tag = 0;
        targetStream->time_base = sourceStream->time_base;
        targetStream->avg_frame_rate = sourceStream->avg_frame_rate;
        targetStream->disposition = sourceStream->disposition;
        status = av_dict_copy(&targetStream->metadata, sourceStream->metadata, 0);
        if (status < 0) return std::unexpected(backendError(status, "copy stream metadata"));
        streamMap[i] = static_cast<int>(mapped++);
    }
    if (!mapped) return std::unexpected(error(ErrorCode::Unsupported, "input has no video, audio, or subtitle stream to record"));
    status = av_dict_copy(&output.context->metadata, input.context->metadata, 0);
    if (status < 0) return std::unexpected(backendError(status, "copy container metadata"));
    Dictionary dictionary;
    if (auto copied = dictionary.set(options.options); !copied) return std::unexpected(copied.error());
#ifdef _WIN32
    output.file = _wfopen(destination.c_str(), L"wbx");
#else
    output.file = std::fopen(destination.c_str(), "wbx");
#endif
    if (!output.file) return std::unexpected(backendError(AVERROR(errno ? errno : EIO), "create exclusive recording file"));
    output.destination = destination;
    auto* buffer = static_cast<std::uint8_t*>(av_malloc(32 * 1024));
    if (!buffer) return std::unexpected(error(ErrorCode::BackendFailure, "allocate recording IO buffer"));
    output.io = avio_alloc_context(buffer, 32 * 1024, 1, &output, nullptr, Output::write, Output::seek);
    if (!output.io) {
        av_free(buffer);
        return std::unexpected(error(ErrorCode::BackendFailure, "allocate recording IO"));
    }
    output.context->pb = output.io;
    output.context->flags |= AVFMT_FLAG_CUSTOM_IO;
    status = avformat_write_header(output.context, &dictionary.value);
    if (status < 0) return std::unexpected(backendError(status, "write recording header", &input.interrupt));
    RecordingReport result;
    result.destination = destination;
    result.unusedOutputOptions = metadata(dictionary.value);
    NativePacket packet{av_packet_alloc()};
    if (!packet) return std::unexpected(error(ErrorCode::BackendFailure, "allocate recording packet"));
    std::optional<std::int64_t> firstTime;
    const auto durationMicroseconds = std::chrono::duration_cast<std::chrono::microseconds>(options.maximumDuration).count();
    const auto captureStarted = Clock::now();
    const auto captureDeadline = captureStarted + options.maximumDuration;
    for (;;) {
        if (result.packets >= options.maximumPackets) { result.stopReason = RecordingStopReason::PacketLimit; break; }
        if (Clock::now() - captureStarted >= options.maximumDuration) { result.stopReason = RecordingStopReason::DurationLimit; break; }
        auto read = readNative(input, packet.get(), captureDeadline);
        if (!read) {
            if (read.error().code == ErrorCode::EndOfStream) { result.stopReason = RecordingStopReason::EndOfStream; break; }
            if (read.error().code == ErrorCode::Timeout && Clock::now() >= captureDeadline &&
                captureDeadline < input.interrupt.hardDeadline) {
                result.stopReason = RecordingStopReason::DurationLimit;
                break;
            }
            return std::unexpected(read.error());
        }
        const int sourceIndex = packet->stream_index;
        const int targetIndex = streamMap[sourceIndex];
        if (targetIndex < 0) { av_packet_unref(packet.get()); continue; }
        const auto* sourceStream = input.context->streams[sourceIndex];
        const auto packetTime = packet->dts != AV_NOPTS_VALUE ? packet->dts : packet->pts;
        if (packetTime != AV_NOPTS_VALUE) {
            const auto micros = av_rescale_q(packetTime, sourceStream->time_base, AVRational{1, AV_TIME_BASE});
            if (!firstTime) firstTime = micros;
            // Avoid signed subtraction overflow on malformed timestamps.
            if (micros >= *firstTime && static_cast<std::uint64_t>(micros) - static_cast<std::uint64_t>(*firstTime) >=
                static_cast<std::uint64_t>(durationMicroseconds)) {
                result.stopReason = RecordingStopReason::DurationLimit;
                break;
            }
        }
        const auto payloadSize = static_cast<std::uint64_t>(packet->size);
        if (payloadSize > options.maximumPayloadBytes - result.payloadBytes) {
            result.stopReason = RecordingStopReason::PayloadLimit;
            break;
        }
        packet->stream_index = targetIndex;
        packet->pos = -1;
        av_packet_rescale_ts(packet.get(), sourceStream->time_base, output.context->streams[targetIndex]->time_base);
        status = av_interleaved_write_frame(output.context, packet.get());
        if (status < 0) return std::unexpected(backendError(status, "write recording packet", &input.interrupt));
        ++result.packets;
        result.payloadBytes += payloadSize;
        av_packet_unref(packet.get());
    }
    if (!result.packets) return std::unexpected(error(ErrorCode::InvalidMedia, "no packets fit recording limits"));
    input.interrupt.arm(input.timeout);
    if (InterruptState::check(&input.interrupt)) return std::unexpected(backendError(AVERROR_EXIT, "finalize recording", &input.interrupt));
    status = av_write_trailer(output.context);
    if (status < 0) return std::unexpected(backendError(status, "write recording trailer", &input.interrupt));
    avio_flush(output.io);
    if (output.io->error < 0) return std::unexpected(backendError(output.io->error, "flush recording IO", &input.interrupt));
    if (std::fflush(output.file) != 0) return std::unexpected(backendError(AVERROR(errno ? errno : EIO), "flush recording file"));
    std::error_code sizeError;
    result.fileBytes = std::filesystem::file_size(destination, sizeError);
    if (sizeError) return std::unexpected(error(ErrorCode::Io, "read finalized recording size: " + sizeError.message(), sizeError.value()));
    if (std::fclose(output.file) != 0) {
        output.file = nullptr;
        return std::unexpected(backendError(AVERROR(errno ? errno : EIO), "close recording file"));
    }
    output.file = nullptr;
    output.committed = true;
    return result;
}

} // namespace iiCameraIntergration
