/// @file entries.cpp
/// @brief Win32 FileSystem native entry metadata and deletion helpers.

#include "filesystem/platform/win32/internal/helpers.h"

namespace GameWIP::FileSystem::Detail::Platform::Win32
{
    /// ------------------------------------------------------------
    /// Handle metadata and deletion
    /// ------------------------------------------------------------

    /// @brief Marks an open stable handle for POSIX-style deletion when supported.
    [[nodiscard]] IO::Types::Status markHandleForDeletion(HANDLE handle, ErrorCode fallback) noexcept
    {
        FILE_DISPOSITION_INFO disposition{};
        disposition.DeleteFile = TRUE;

        if(SetFileInformationByHandle(handle, FileDispositionInfo, &disposition, sizeof(disposition)) == FALSE)
        {
            return makeLastErrorStatus(fallback);
        }

        return IO::successStatus();
    }

    /// @brief Updates the read-only attribute through an already-stable handle.
    [[nodiscard]] IO::Types::Status setHandleReadOnly(HANDLE handle, bool readOnly) noexcept
    {
        FILE_BASIC_INFO basicInfo{};

        if(GetFileInformationByHandleEx(handle, FileBasicInfo, &basicInfo, sizeof(basicInfo)) == FALSE)
        {
            return makeLastErrorStatus(ErrorCode::StatFailed);
        }

        if(readOnly)
        {
            basicInfo.FileAttributes |= FILE_ATTRIBUTE_READONLY;
        }
        else
        {
            basicInfo.FileAttributes &= kAttributesWithoutReadOnly;

            if(basicInfo.FileAttributes == 0)
            {
                basicInfo.FileAttributes = FILE_ATTRIBUTE_NORMAL;
            }
        }

        if(SetFileInformationByHandle(handle, FileBasicInfo, &basicInfo, sizeof(basicInfo)) == FALSE)
        {
            return makeLastErrorStatus(ErrorCode::StatFailed);
        }

        return IO::successStatus();
    }

    /// @brief Copies portable timestamps and read-only state between stable handles.
    [[nodiscard]] IO::Types::Status copyHandleBasicMetadata(HANDLE source, HANDLE destination) noexcept
    {
        FILE_BASIC_INFO sourceInfo{};

        if(GetFileInformationByHandleEx(source, FileBasicInfo, &sourceInfo, sizeof(sourceInfo)) == FALSE)
        {
            return makeLastErrorStatus(ErrorCode::StatFailed);
        }

        FILE_BASIC_INFO destinationInfo{};

        if(GetFileInformationByHandleEx(destination, FileBasicInfo, &destinationInfo, sizeof(destinationInfo)) == FALSE)
        {
            return makeLastErrorStatus(ErrorCode::StatFailed);
        }
        destinationInfo.LastWriteTime = sourceInfo.LastWriteTime;

        if((sourceInfo.FileAttributes & FILE_ATTRIBUTE_READONLY) != 0)
        {
            destinationInfo.FileAttributes |= FILE_ATTRIBUTE_READONLY;
        }
        else
        {
            destinationInfo.FileAttributes &= kAttributesWithoutReadOnly;

            if(destinationInfo.FileAttributes == 0)
            {
                destinationInfo.FileAttributes = FILE_ATTRIBUTE_NORMAL;
            }
        }

        if(SetFileInformationByHandle(destination, FileBasicInfo, &destinationInfo, sizeof(destinationInfo)) == FALSE)
        {
            return makeLastErrorStatus(ErrorCode::StatFailed);
        }

        return IO::successStatus();
    }

} // namespace GameWIP::FileSystem::Detail::Platform::Win32
