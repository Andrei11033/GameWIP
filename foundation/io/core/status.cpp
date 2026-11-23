/// @file status.cpp
/// @brief IO status helpers.

#include "io/status.h"

#include <utility>

namespace GameWIP::IO
{
    /// ------------------------------------------------------------
    /// Status helpers
    /// ------------------------------------------------------------

    Types::Status makeStatus(Types::ErrorCode code, std::int64_t nativeCode, std::string message) noexcept
    {
        return {.code = code, .nativeCode = nativeCode, .message = std::move(message)};
    }

    Types::Status successStatus() noexcept
    {
        return {};
    }

    std::string_view errorCodeName(Types::ErrorCode code) noexcept
    {
        switch(code)
        {
        case Types::ErrorCode::Success:
            return "Success";

        case Types::ErrorCode::InvalidArgument:
            return "InvalidArgument";
        case Types::ErrorCode::Unsupported:
            return "Unsupported";
        case Types::ErrorCode::NotOpen:
            return "NotOpen";
        case Types::ErrorCode::AlreadyOpen:
            return "AlreadyOpen";

        case Types::ErrorCode::NotFound:
            return "NotFound";
        case Types::ErrorCode::AlreadyExists:
            return "AlreadyExists";
        case Types::ErrorCode::PermissionDenied:
            return "PermissionDenied";
        case Types::ErrorCode::PathTooLong:
            return "PathTooLong";

        case Types::ErrorCode::IsDirectory:
            return "IsDirectory";
        case Types::ErrorCode::NotDirectory:
            return "NotDirectory";
        case Types::ErrorCode::NotSeekable:
            return "NotSeekable";
        case Types::ErrorCode::EndOfStream:
            return "EndOfStream";

        case Types::ErrorCode::OpenFailed:
            return "OpenFailed";
        case Types::ErrorCode::ReadFailed:
            return "ReadFailed";
        case Types::ErrorCode::WriteFailed:
            return "WriteFailed";
        case Types::ErrorCode::FlushFailed:
            return "FlushFailed";
        case Types::ErrorCode::CloseFailed:
            return "CloseFailed";
        case Types::ErrorCode::SeekFailed:
            return "SeekFailed";
        case Types::ErrorCode::StatFailed:
            return "StatFailed";
        case Types::ErrorCode::RemoveFailed:
            return "RemoveFailed";
        case Types::ErrorCode::ReplaceFailed:
            return "ReplaceFailed";
        case Types::ErrorCode::CopyFailed:
            return "CopyFailed";
        case Types::ErrorCode::MoveFailed:
            return "MoveFailed";
        case Types::ErrorCode::ResizeFailed:
            return "ResizeFailed";
        case Types::ErrorCode::LockFailed:
            return "LockFailed";
        case Types::ErrorCode::UnlockFailed:
            return "UnlockFailed";
        case Types::ErrorCode::DirectoryCreateFailed:
            return "DirectoryCreateFailed";
        case Types::ErrorCode::DirectoryListFailed:
            return "DirectoryListFailed";
        case Types::ErrorCode::DirectoryNotEmpty:
            return "DirectoryNotEmpty";

        case Types::ErrorCode::PartialRead:
            return "PartialRead";
        case Types::ErrorCode::PartialWrite:
            return "PartialWrite";
        case Types::ErrorCode::SizeLimitExceeded:
            return "SizeLimitExceeded";
        case Types::ErrorCode::OutOfMemory:
            return "OutOfMemory";

        case Types::ErrorCode::ResourceBusy:
            return "ResourceBusy";
        case Types::ErrorCode::StorageFull:
            return "StorageFull";
        case Types::ErrorCode::BrokenPipe:
            return "BrokenPipe";
        case Types::ErrorCode::Interrupted:
            return "Interrupted";

        case Types::ErrorCode::EncodingFailed:
            return "EncodingFailed";
        case Types::ErrorCode::NativeFailure:
            return "NativeFailure";
        case Types::ErrorCode::Unknown:
            return "Unknown";
        }

        return "Unknown";
    }

} // namespace GameWIP::IO
