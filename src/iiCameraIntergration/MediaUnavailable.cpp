#include <iiCameraIntergration/Media.h>

#include <utility>

namespace iiCameraIntergration {
namespace {
Error unavailable() {
    return Error{ErrorCode::Unavailable, "iiCameraIntergration was built without the FFmpeg media backend", 0};
}
}

struct PacketCapture::Impl {};
PacketCapture::PacketCapture(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
PacketCapture::PacketCapture(PacketCapture&&) noexcept = default;
PacketCapture& PacketCapture::operator=(PacketCapture&&) noexcept = default;
PacketCapture::~PacketCapture() = default;
Result<PacketCapture> PacketCapture::open(const MediaSource&) { return std::unexpected(unavailable()); }
const MediaInfo& PacketCapture::metadata() const noexcept { static const MediaInfo empty; return empty; }
Result<MediaPacket> PacketCapture::read() { return std::unexpected(unavailable()); }
void PacketCapture::cancel() noexcept {}
Result<std::vector<CodecInfo>> codecCatalog() { return std::unexpected(unavailable()); }
Result<std::vector<FormatInfo>> formatCatalog() { return std::unexpected(unavailable()); }
Result<MediaBackendInfo> mediaBackendInfo() { return std::unexpected(unavailable()); }
Result<MediaInfo> probeMedia(const MediaSource&) { return std::unexpected(unavailable()); }
Result<MediaInfo> probeMediaBytes(std::span<const std::byte>, std::chrono::milliseconds) {
    return std::unexpected(unavailable());
}
Result<RgbFrame> decodeRgbFrame(const MediaSource&, const DecodeOptions&) { return std::unexpected(unavailable()); }
Result<RecordingReport> recordMedia(const MediaSource&, const std::filesystem::path&, const RecordingOptions&) {
    return std::unexpected(unavailable());
}
} // namespace iiCameraIntergration
