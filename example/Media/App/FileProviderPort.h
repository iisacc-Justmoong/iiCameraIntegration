#pragma once
#include <iiCameraIntegration/Media.h>
#include <iiFileProviderNative.h>
#include <iiFileProviderPresentation.h>
#include <iiFileProviderStorage.h>
#include <cerrno>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace camera_media {
inline iiCameraIntegration::RecordingFilePort fileProviderPort() {
    using namespace iiCameraIntegration;
    struct State {
        std::mutex mutex;
        std::unique_ptr<iiFileProvider::native::StagedFile> staged;
        std::filesystem::path requested, destination;
    };
    auto state = std::make_shared<State>();
    return {
        [state](const std::filesystem::path& path) -> Result<std::FILE*> {
            std::lock_guard lock(state->mutex);
            if (state->staged) return std::unexpected(Error{ErrorCode::Busy, "Recording port is already in use"});
            std::error_code code;
            auto destination = std::filesystem::absolute(path, code);
            if (code) return std::unexpected(Error{ErrorCode::Io, "Cannot resolve recording destination", code.value()});
            const auto status = std::filesystem::symlink_status(destination, code);
            if (code && code != std::errc::no_such_file_or_directory)
                return std::unexpected(Error{ErrorCode::Io, "Cannot inspect recording destination", code.value()});
            if (!code && status.type() != std::filesystem::file_type::not_found)
                return std::unexpected(Error{ErrorCode::Busy, "Recording destination already exists"});
            state->requested = path;
            state->destination = destination;
            auto staged = std::make_unique<iiFileProvider::native::StagedFile>(destination);
#ifdef _WIN32
            HANDLE handle = iiFileProvider::native::createFile(staged->path().c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (handle == INVALID_HANDLE_VALUE)
                return std::unexpected(Error{ErrorCode::Io, "Cannot open staged recording", static_cast<int>(GetLastError())});
            const int descriptor = _open_osfhandle(reinterpret_cast<intptr_t>(handle), _O_WRONLY | _O_BINARY);
            if (descriptor == -1) {
                const int error = errno;
                CloseHandle(handle);
                return std::unexpected(Error{ErrorCode::Io, "Cannot open recording descriptor", error});
            }
            auto* stream = _fdopen(descriptor, "wb");
            if (!stream) {
                const int error = errno;
                _close(descriptor);
                return std::unexpected(Error{ErrorCode::Io, "Cannot open recording stream", error});
            }
#else
            auto* stream = iiFileProvider::native::openStream(staged->path().c_str(), "wb");
            if (!stream) {
                const int error = errno;
                return std::unexpected(Error{ErrorCode::Io, "Cannot open staged recording", error});
            }
#endif
            state->staged = std::move(staged);
            return stream;
        },
        [state](const std::filesystem::path& path) -> Result<void> {
            std::lock_guard lock(state->mutex);
            if (!state->staged || path != state->requested)
                return std::unexpected(Error{ErrorCode::InvalidArgument, "Recording port has no matching staged output"});
#ifdef _WIN32
            if (!iiFileProvider::native::moveFile(state->staged->path().c_str(), state->destination.c_str(), MOVEFILE_WRITE_THROUGH)) {
                const auto error = GetLastError();
                return std::unexpected(Error{error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS
                    ? ErrorCode::Busy : ErrorCode::Io, "Cannot publish recording without overwrite", static_cast<int>(error)});
            }
#else
            try { iiFileProvider::native::createHardLink(state->staged->path(), state->destination); }
            catch (const std::filesystem::filesystem_error& error) {
                return std::unexpected(Error{error.code() == std::errc::file_exists ? ErrorCode::Busy : ErrorCode::Io,
                    "Cannot publish recording without overwrite: " + error.code().message(), error.code().value()});
            }
#endif
            state->staged.reset();
            iiFileProvider::native::Presentation::enqueue(state->destination);
            return {};
        },
        [state](const std::filesystem::path& path) {
            std::lock_guard lock(state->mutex);
            if (state->staged && path == state->requested) state->staged.reset();
        }
    };
}
} // namespace camera_media
