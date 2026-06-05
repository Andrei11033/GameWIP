/// @file
/// @brief Planned public header for the GameWIP FileSystem library.
///
/// Contract stub only. GameWIP::FileSystem is not implemented yet.

#pragma once

namespace GameWIP::FileSystem
{

    /// @brief Passive FileSystem data shapes planned for the future implementation.
    namespace Types
    {

        enum class FileKind;
        enum class FileAccess;
        enum class FileCreateMode;
        enum class FileInitialPosition;
        enum class WriteMode;

        struct FileShareOptions;
        struct OpenFileOptions;
        struct OpenReaderOptions;
        struct OpenWriterOptions;
        struct FileStat;
        struct StatResult;
        struct ExistsResult;
        struct ReadFileOptions;
        struct WriteFileOptions;
        struct AppendFileOptions;
        struct AtomicWriteOptions;
        struct CreateDirectoryOptions;
        struct ListDirectoryOptions;
        struct DirectoryEntry;
        struct ListDirectoryResult;
        struct RemoveOptions;
        struct RemoveTreeOptions;
        struct RemoveTreeResult;
        struct MoveOptions;
        struct CopyFileOptions;
        struct ReadBytesResult;
        struct ReadTextResult;
        struct OpenFileResult;
        struct OpenReaderResult;
        struct OpenWriterResult;

    } // namespace Types

    class File;
    class FileReader;
    class FileWriter;

    // Planned API only:
    // - openFile/openReader/openWriter
    // - readAllBytes/readAllText
    // - writeAllBytes/writeAllText
    // - appendBytes/appendText
    // - writeFileAtomic/writeTextFileAtomic
    // - createDirectory/createDirectories
    // - exists/stat/listDirectory
    // - removeFile/removeEmptyDirectory/removeDirectoryTree
    // - movePath/copyFile

} // namespace GameWIP::FileSystem
