/// @file win32_shell_registration.cpp
/// @brief Win32 file and URI registration backend.

#include "desktop/internal/shell_platform.h"

#include "desktop/internal/desktop_test_hooks.h"
#include "desktop/platform/win32/internal/win32_shell_launch.h"
#include "desktop/platform/win32/internal/win32_window_backend.h"
#include "desktop/platform/win32/internal/win32_unicode.h"

#include <shellapi.h>
#include <shlobj.h>
#include <winreg.h>
#include <windows.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <new>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace GameWIP::Desktop::Detail::Platform
{
    using IO::Types::ErrorCode;

    namespace
    {
        /// ------------------------------------------------------------
        /// Registry key operations
        /// ------------------------------------------------------------

        /// @brief Owns one Win32 registry key handle and its close operation.
        class RegistryKey final
        {
        public:
            RegistryKey() noexcept = default;
            RegistryKey(const RegistryKey &) = delete;
            RegistryKey &operator=(const RegistryKey &) = delete;
            ~RegistryKey() noexcept
            {
                reset();
            }

            HKEY *put() noexcept
            {
                if(value_ != nullptr)
                {
                    RegCloseKey(value_);
                    value_ = nullptr;
                }

                return &value_;
            }

            [[nodiscard]] HKEY get() const noexcept
            {
                return value_;
            }

            void reset() noexcept
            {
                if(value_ != nullptr)
                {
                    RegCloseKey(value_);
                    value_ = nullptr;
                }
            }

            explicit operator bool() const noexcept
            {
                return value_ != nullptr;
            }

        private:
            HKEY value_ = nullptr;
        };

        [[nodiscard]] IO::Types::Status registryStatus(LONG code, std::string_view operation)
        {
            if(code == ERROR_SUCCESS)
            {
                return IO::successStatus();
            }

            ErrorCode portable = ErrorCode::NativeFailure;

            if(code == ERROR_FILE_NOT_FOUND)
            {
                portable = ErrorCode::NotFound;
            }
            else if(code == ERROR_ACCESS_DENIED)
            {
                portable = ErrorCode::PermissionDenied;
            }
            else if(code == ERROR_ALREADY_EXISTS)
            {
                portable = ErrorCode::AlreadyExists;
            }
            else if(code == ERROR_OUTOFMEMORY)
            {
                portable = ErrorCode::OutOfMemory;
            }
            std::string message(operation);
            return IO::makeStatus(portable, code, std::move(message));
        }

        [[nodiscard]] IO::Types::Status openRegistryKey(HKEY root, std::wstring_view path, REGSAM access, RegistryKey &key, bool create)
        {
            LONG result = ERROR_SUCCESS;

            if(create)
            {
                DWORD disposition = 0;
                result =
                    RegCreateKeyExW(root, std::wstring(path).c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE, access, nullptr, key.put(), &disposition);
            }
            else
            {
                result = RegOpenKeyExW(root, std::wstring(path).c_str(), 0, access, key.put());
            }

            return result == ERROR_SUCCESS ? IO::successStatus() : registryStatus(result, "registry key operation");
        }

        [[nodiscard]] IO::Types::Status readRegistryString(HKEY key, std::wstring_view name, std::wstring &value)
        {
            DWORD type = 0;
            DWORD size = 0;
            LONG result = RegGetValueW(key, nullptr, std::wstring(name).c_str(), RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, &type, nullptr, &size);

            if(result != ERROR_SUCCESS)
            {
                return registryStatus(result, "read registry value");
            }

            try
            {
                value.resize(size / sizeof(wchar_t));
                result = RegGetValueW(key, nullptr, std::wstring(name).c_str(), RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, &type, value.data(), &size);

                if(result != ERROR_SUCCESS)
                {
                    return registryStatus(result, "read registry value");
                }
                // The size query may reserve an extra terminator; only the returned bytes are the value.
                value.resize(size / sizeof(wchar_t));

                if(!value.empty() && value.back() == L'\0')
                {
                    value.pop_back();
                }

                return IO::successStatus();
            }
            catch(const std::bad_alloc &)
            {
                return IO::makeStatus(ErrorCode::OutOfMemory);
            }
            catch(...)
            {
                return IO::makeStatus(ErrorCode::Unknown);
            }
        }

        [[nodiscard]] IO::Types::Status deleteRegistryTree(HKEY key, std::wstring_view name, std::string_view operation)
        {
#if DESKTOP_INTERNAL_TEST_HOOKS
            if(Detail::consumeFailure(TestHooks::FailurePoint::ShellRegistryDelete))
            {
                return registryStatus(ERROR_ACCESS_DENIED, operation);
            }

#endif
            const LONG removed = RegDeleteTreeW(key, std::wstring(name).c_str());
            return removed == ERROR_FILE_NOT_FOUND ? IO::successStatus() : registryStatus(removed, operation);
        }

        [[nodiscard]] IO::Types::Status deleteRegistryKey(HKEY root, std::wstring_view path, std::string_view operation)
        {
#if DESKTOP_INTERNAL_TEST_HOOKS
            if(Detail::consumeFailure(TestHooks::FailurePoint::ShellRegistryDelete))
            {
                return registryStatus(ERROR_ACCESS_DENIED, operation);
            }

#endif
            const LONG removed = RegDeleteKeyW(root, std::wstring(path).c_str());
            return removed == ERROR_FILE_NOT_FOUND ? IO::successStatus() : registryStatus(removed, operation);
        }

        [[nodiscard]] IO::Types::Status removeRegistryValue(HKEY key, std::wstring_view name)
        {
#if DESKTOP_INTERNAL_TEST_HOOKS
            if(Detail::consumeFailure(TestHooks::FailurePoint::ShellRegistryDelete))
            {
                return registryStatus(ERROR_ACCESS_DENIED, "remove owned registration value");
            }

#endif
            const LONG removed = RegDeleteValueW(key, std::wstring(name).c_str());
            return removed == ERROR_FILE_NOT_FOUND ? IO::successStatus() : registryStatus(removed, "remove owned registration value");
        }

        /// @brief Removes an empty key without deleting other handlers or metadata.
        [[nodiscard]] IO::Types::Status pruneEmptyRegistrationKey(const std::wstring &path)
        {
            RegistryKey key;
            IO::Types::Status status = openRegistryKey(HKEY_CURRENT_USER, path, KEY_READ, key, false);

            if(status.code == ErrorCode::NotFound)
            {
                return IO::successStatus();
            }

            if(!status.ok())
            {
                return status;
            }

            DWORD values = 0;
            DWORD children = 0;
            const LONG queried =
                RegQueryInfoKeyW(key.get(), nullptr, nullptr, nullptr, &children, nullptr, nullptr, &values, nullptr, nullptr, nullptr, nullptr);

            if(queried != ERROR_SUCCESS)
            {
                return registryStatus(queried, "inspect registration cleanup key");
            }

            if(values != 0 || children != 0)
            {
                return IO::successStatus();
            }

            key.reset();
            return deleteRegistryKey(HKEY_CURRENT_USER, path, "remove empty registration key");
        }

        /// ------------------------------------------------------------
        /// Registration rollback
        /// ------------------------------------------------------------

        std::atomic_uint64_t nextRegistryBackupIdentity{1};

        /// @brief Captures owned registry state so failed shell registration can roll back.
        class RegistryTreeBackup final
        {
        public:
            RegistryTreeBackup(std::wstring targetPath, std::wstring programPath, std::wstring applicationPath = {})
                : targetPath_(std::move(targetPath))
                , programPath_(std::move(programPath))
                , applicationPath_(std::move(applicationPath))
            {
                const std::uint64_t identity = nextRegistryBackupIdentity.fetch_add(1, std::memory_order_relaxed);
                backupPath_ = L"Software\\GameWIP\\ShellRollback." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(identity);
            }

            RegistryTreeBackup(const RegistryTreeBackup &) = delete;
            RegistryTreeBackup &operator=(const RegistryTreeBackup &) = delete;

            ~RegistryTreeBackup() noexcept
            {
                if(active_)
                {
                    rollbackBestEffort();
                }
                else
                {
                    cleanupBestEffort();
                }
            }

            [[nodiscard]] IO::Types::Status capture()
            {
                // Capture each owned tree before registration changes begin.
                IO::Types::Status status = openRegistryKey(HKEY_CURRENT_USER, backupPath_, KEY_READ | KEY_WRITE, backupRoot_, true);

                if(!status.ok())
                {
                    return status;
                }

                status = captureTree(targetPath_, L"Target", targetCaptured_);

                if(!status.ok())
                {
                    return status;
                }

                status = captureTree(programPath_, L"Program", programCaptured_);

                if(!status.ok())
                {
                    return status;
                }

                if(!applicationPath_.empty())
                {
                    status = captureTree(applicationPath_, L"Application", applicationCaptured_);

                    if(!status.ok())
                    {
                        return status;
                    }
                }
                active_ = true;
                return IO::successStatus();
            }

            [[nodiscard]] IO::Types::Status commit()
            {
                active_ = false;
                return cleanup();
            }

        private:
            [[nodiscard]] IO::Types::Status captureTree(std::wstring_view sourcePath, std::wstring_view slot, bool &captured)
            {
                RegistryKey source;
                const LONG openResult = RegOpenKeyExW(HKEY_CURRENT_USER, std::wstring(sourcePath).c_str(), 0, KEY_READ, source.put());

                if(openResult == ERROR_FILE_NOT_FOUND)
                {
                    captured = false;
                    return IO::successStatus();
                }

                if(openResult != ERROR_SUCCESS)
                {
                    return registryStatus(openResult, "open registry backup source");
                }

                RegistryKey destination;
                // RegCopyTreeW copies the DACL as well as the values and subkeys.
                IO::Types::Status status = openRegistryKey(backupRoot_.get(), slot, KEY_READ | KEY_WRITE | WRITE_DAC, destination, true);

                if(!status.ok())
                {
                    return status;
                }

                const LONG copyResult = RegCopyTreeW(source.get(), nullptr, destination.get());

                if(copyResult != ERROR_SUCCESS)
                {
                    return registryStatus(copyResult, "copy registry backup source");
                }

                captured = true;
                return IO::successStatus();
            }

            [[nodiscard]] IO::Types::Status restoreTree(std::wstring_view destinationPath, std::wstring_view slot, bool captured)
            {
                const IO::Types::Status deleteStatus = deleteRegistryTree(HKEY_CURRENT_USER, destinationPath, "remove registry replacement");

                if(!deleteStatus.ok())
                {
                    return deleteStatus;
                }

                if(!captured)
                {
                    return IO::successStatus();
                }

                RegistryKey source;
                IO::Types::Status status = openRegistryKey(backupRoot_.get(), slot, KEY_READ, source, false);

                if(!status.ok())
                {
                    return status;
                }

                RegistryKey destination;
                status = openRegistryKey(HKEY_CURRENT_USER, destinationPath, KEY_READ | KEY_WRITE | WRITE_DAC, destination, true);

                if(!status.ok())
                {
                    return status;
                }

                const LONG copyResult = RegCopyTreeW(source.get(), nullptr, destination.get());
                return copyResult == ERROR_SUCCESS ? IO::successStatus() : registryStatus(copyResult, "restore registry replacement");
            }

            void rollbackBestEffort() noexcept
            {
                try
                {
                    // Restore prior target, program, and application state.
                    static_cast<void>(restoreTree(programPath_, L"Program", programCaptured_));
                    static_cast<void>(restoreTree(targetPath_, L"Target", targetCaptured_));

                    if(!applicationPath_.empty())
                    {
                        static_cast<void>(restoreTree(applicationPath_, L"Application", applicationCaptured_));
                    }
                }
                catch(...)
                {
                    active_ = false;
                }
                active_ = false;

                // Remove the temporary backup after rollback completes.
                cleanupBestEffort();
            }

            [[nodiscard]] IO::Types::Status cleanup()
            {
                backupRoot_.reset();
                return deleteRegistryTree(HKEY_CURRENT_USER, backupPath_, "remove shell registration rollback backup");
            }

            void cleanupBestEffort() noexcept
            {
                try
                {
                    static_cast<void>(cleanup());
                }
                catch(...)
                {
                    return;
                }
            }

            // Registration trees protected by this transaction
            std::wstring targetPath_;
            std::wstring programPath_;
            std::wstring applicationPath_;

            // Temporary backup storage
            std::wstring backupPath_;
            RegistryKey backupRoot_;

            // Captured-tree and rollback state
            bool targetCaptured_ = false;
            bool programCaptured_ = false;
            bool applicationCaptured_ = false;
            bool active_ = false;
        };

        /// ------------------------------------------------------------
        /// Registration metadata and ownership
        /// ------------------------------------------------------------

        [[nodiscard]] IO::Types::Status writeRegistryString(HKEY key, std::wstring_view name, std::wstring_view value)
        {
            const std::wstring nameCopy(name);
            const DWORD bytes = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
            const LONG result =
                RegSetValueExW(key, nameCopy.empty() ? nullptr : nameCopy.c_str(), 0, REG_SZ, reinterpret_cast<const BYTE *>(value.data()), bytes);
            return result == ERROR_SUCCESS ? IO::successStatus() : registryStatus(result, "write registry value");
        }

        [[nodiscard]] IO::Types::Status normalizeTarget(
            std::string_view input,
            bool extension,
            std::string &normalizedUtf8,
            std::wstring &normalizedUtf16) noexcept
        {
            try
            {
                // Normalize optional prefixes and delimiters.
                normalizedUtf8.assign(input);

                if(extension && normalizedUtf8.starts_with('*'))
                {
                    normalizedUtf8.erase(0, 1);
                }

                if(extension && !normalizedUtf8.starts_with('.'))
                {
                    normalizedUtf8.insert(normalizedUtf8.begin(), '.');
                }

                if(!extension && normalizedUtf8.ends_with(':'))
                {
                    normalizedUtf8.pop_back();
                }

                // Reject invalid characters and malformed URI scheme starts.
                if(normalizedUtf8.empty() || normalizedUtf8.find_first_of("\\/:*?\"<>|\0") != std::string::npos)
                {
                    return IO::makeStatus(ErrorCode::InvalidArgument);
                }
                const std::size_t start = extension ? 1U : 0U;

                if(!extension && (start >= normalizedUtf8.size() || ((normalizedUtf8[start] < 'A' || normalizedUtf8[start] > 'Z') &&
                                                                     (normalizedUtf8[start] < 'a' || normalizedUtf8[start] > 'z'))))
                {
                    return IO::makeStatus(ErrorCode::InvalidArgument);
                }

                // Canonicalize ASCII case before preparing the native spelling.
                for(char &character : normalizedUtf8)
                {
                    if(static_cast<unsigned char>(character) >= 0x80U)
                    {
                        return IO::makeStatus(ErrorCode::InvalidArgument);
                    }

                    if(character >= 'A' && character <= 'Z')
                    {
                        character = static_cast<char>(character - 'A' + 'a');
                    }
                }
                DWORD nativeCode = ERROR_SUCCESS;

                // Convert the validated target for registry access.
                if(!utf8ToUtf16(normalizedUtf8, normalizedUtf16, nativeCode))
                {
                    return IO::makeStatus(ErrorCode::EncodingFailed, nativeCode);
                }
                return IO::successStatus();
            }
            catch(const std::bad_alloc &)
            {
                return IO::makeStatus(ErrorCode::OutOfMemory);
            }
            catch(...)
            {
                return IO::makeStatus(ErrorCode::Unknown);
            }
        }

        [[nodiscard]] std::wstring registrationProgId(bool extension, std::string_view ownerKey, std::string_view normalizedTarget)
        {
            std::wstring result = extension ? L"GameWIP.File." : L"GameWIP.Uri.";
            DWORD nativeCode = ERROR_SUCCESS;
            std::wstring owner;
            std::wstring target;

            if(!utf8ToUtf16(ownerKey, owner, nativeCode) || !utf8ToUtf16(normalizedTarget, target, nativeCode))
            {
                return {};
            }
            result += owner;
            result.push_back(L'.');
            result += target;

            for(wchar_t &character : result)
            {
                if((character < L'A' || character > L'Z') && (character < L'a' || character > L'z') && (character < L'0' || character > L'9') &&
                   character != L'.' && character != L'_' && character != L'-')
                {
                    character = L'_';
                }
            }

            return result;
        }

        [[nodiscard]] IO::Types::Status readOwner(HKEY targetKey, std::string &owner, bool &hasOwner)
        {
            std::wstring ownerUtf16;
            IO::Types::Status status = readRegistryString(targetKey, L"GameWIP.OwnerKey", ownerUtf16);

            if(!status.ok())
            {
                hasOwner = false;

                if(status.code == ErrorCode::NotFound)
                {
                    return IO::successStatus();
                }
                return status;
            }

            DWORD nativeCode = ERROR_SUCCESS;

            if(!utf16ToUtf8(ownerUtf16, owner, nativeCode))
            {
                return IO::makeStatus(ErrorCode::EncodingFailed, nativeCode);
            }

            hasOwner = true;
            return IO::successStatus();
        }

        [[nodiscard]] IO::Types::Status writeLaunchCommand(HKEY commandKey, const Types::Shell::LaunchActionView &action)
        {
            std::wstring executable;
            std::wstring arguments;
            IO::Types::Status status = makeLaunchCommand(action, true, executable, arguments);

            if(!status.ok())
            {
                return status;
            }

            std::wstring command = L"\"";
            command += executable;
            command += L'\"';

            if(!arguments.empty())
            {
                command.push_back(L' ');
                command += arguments;
            }

            return writeRegistryString(commandKey, L"", command);
        }

        [[nodiscard]] IO::Types::Status applicationRegistrationIdentity(
            const Types::Shell::LaunchActionView &action,
            std::wstring &applicationName,
            std::wstring &applicationPath)
        {
            try
            {
                std::filesystem::path executable = action.executable.path.get();
                executable.make_preferred();
                applicationName = executable.filename().wstring();

                if(applicationName.empty() || applicationName == L"." || applicationName == L".." ||
                   applicationName.find_first_of(L"\\/") != std::wstring::npos)
                {
                    return IO::makeStatus(ErrorCode::InvalidArgument);
                }
                applicationPath = L"Software\\Classes\\Applications\\" + applicationName;
                return IO::successStatus();
            }
            catch(const std::bad_alloc &)
            {
                return IO::makeStatus(ErrorCode::OutOfMemory);
            }
            catch(...)
            {
                return IO::makeStatus(ErrorCode::Unknown);
            }
        }

        [[nodiscard]] IO::Types::Status inspectApplicationRegistration(
            std::wstring_view applicationPath,
            std::string_view ownerKey,
            bool &exists,
            bool &sameOwner,
            std::string &existingOwner)
        {
            exists = false;
            sameOwner = false;
            existingOwner.clear();
            RegistryKey existing;
            const LONG opened = RegOpenKeyExW(HKEY_CURRENT_USER, std::wstring(applicationPath).c_str(), 0, KEY_READ, existing.put());

            if(opened == ERROR_FILE_NOT_FOUND)
            {
                return IO::successStatus();
            }

            if(opened != ERROR_SUCCESS)
            {
                return registryStatus(opened, "inspect application registration");
            }

            bool hasOwner = false;
            const IO::Types::Status ownerStatus = readOwner(existing.get(), existingOwner, hasOwner);

            if(!ownerStatus.ok())
            {
                return ownerStatus;
            }

            exists = true;
            sameOwner = hasOwner && existingOwner == ownerKey;
            return IO::successStatus();
        }

        [[nodiscard]] IO::Types::Status writeApplicationRegistration(
            const std::wstring &applicationName,
            const std::wstring &applicationPath,
            std::string_view ownerKey,
            std::string_view displayName,
            std::string_view extension,
            const std::optional<Types::Registration::Icon> &icon,
            const Types::Shell::LaunchActionView &action)
        {
            // Check ownership before writing application-wide settings.
            bool exists = false;
            bool sameOwner = false;
            std::string existingOwner;
            IO::Types::Status status = inspectApplicationRegistration(applicationPath, ownerKey, exists, sameOwner, existingOwner);

            if(!status.ok())
            {
                return status;
            }

            if(exists && !sameOwner)
            {
                return registryStatus(ERROR_ALREADY_EXISTS, "application registration is owned by another registration");
            }

            RegistryKey application;
            status = openRegistryKey(HKEY_CURRENT_USER, applicationPath, KEY_READ | KEY_WRITE, application, true);

            if(!status.ok())
            {
                return status;
            }

            // Write display metadata and the optional icon.
            DWORD nativeCode = ERROR_SUCCESS;
            std::wstring owner;
            std::wstring display;

            if(!utf8ToUtf16(ownerKey, owner, nativeCode) || !utf8ToUtf16(displayName, display, nativeCode))
            {
                return IO::makeStatus(ErrorCode::EncodingFailed, nativeCode);
            }

            status = writeRegistryString(application.get(), L"GameWIP.OwnerKey", owner);

            if(!status.ok())
            {
                return status;
            }

            status = writeRegistryString(application.get(), L"", display);

            if(!status.ok())
            {
                return status;
            }

            status = writeRegistryString(application.get(), L"FriendlyAppName", display);

            if(!status.ok())
            {
                return status;
            }

            status = writeRegistryString(application.get(), L"ApplicationName", display);

            if(!status.ok())
            {
                return status;
            }

            status = writeRegistryString(application.get(), L"ApplicationDescription", display);

            if(!status.ok())
            {
                return status;
            }

            status = deleteRegistryTree(application.get(), L"DefaultIcon", "remove previous application icon");

            if(!status.ok())
            {
                return status;
            }

            if(icon.has_value())
            {
                RegistryKey iconKey;
                status = openRegistryKey(application.get(), L"DefaultIcon", KEY_READ | KEY_WRITE, iconKey, true);

                if(!status.ok())
                {
                    return status;
                }
                std::wstring path = icon->path.path.get().wstring();
                std::wstring iconValue = L"\"" + path + L"\"," + std::to_wstring(icon->index);
                status = writeRegistryString(iconKey.get(), L"", iconValue);

                if(!status.ok())
                {
                    return status;
                }
            }

            // Configure the default open action.
            RegistryKey shell;
            status = openRegistryKey(application.get(), L"shell", KEY_READ | KEY_WRITE, shell, true);

            if(!status.ok())
            {
                return status;
            }

            RegistryKey open;
            status = openRegistryKey(shell.get(), L"open", KEY_READ | KEY_WRITE, open, true);

            if(!status.ok())
            {
                return status;
            }

            status = writeRegistryString(open.get(), L"", L"Open");

            if(!status.ok())
            {
                return status;
            }

            RegistryKey command;
            status = openRegistryKey(open.get(), L"command", KEY_READ | KEY_WRITE, command, true);

            if(!status.ok())
            {
                return status;
            }

            const std::array<Types::Shell::LaunchArgumentView, 1> arguments{Types::Shell::LaunchPlaceholder::SelectedPath};
            const Types::Shell::LaunchActionView genericAction{action.executable, arguments};
            status = writeLaunchCommand(command.get(), genericAction);

            if(!status.ok())
            {
                return status;
            }

            // Advertise the file type supported by this application.
            if(!extension.empty())
            {
                DWORD extensionCode = ERROR_SUCCESS;
                std::wstring extensionWide;

                if(!utf8ToUtf16(extension, extensionWide, extensionCode))
                {
                    return IO::makeStatus(ErrorCode::EncodingFailed, extensionCode);
                }
                RegistryKey supportedTypes;
                status = openRegistryKey(application.get(), L"SupportedTypes", KEY_READ | KEY_WRITE, supportedTypes, true);

                if(!status.ok())
                {
                    return status;
                }

                status = registryStatus(
                    RegSetValueExW(supportedTypes.get(), extensionWide.c_str(), 0, REG_SZ, reinterpret_cast<const BYTE *>(L""), sizeof(wchar_t)),
                    "write application supported type");

                if(!status.ok())
                {
                    return status;
                }
            }
            static_cast<void>(applicationName);
            return IO::successStatus();
        }

        [[nodiscard]] IO::Types::Status removeApplicationRegistration(
            std::wstring_view applicationName,
            std::string_view ownerKey,
            std::string_view extension)
        {
            // Confirm ownership before editing application registration.
            if(applicationName.empty())
            {
                return IO::successStatus();
            }
            const std::wstring applicationPath = L"Software\\Classes\\Applications\\" + std::wstring(applicationName);
            bool exists = false;
            bool sameOwner = false;
            std::string existingOwner;
            IO::Types::Status status = inspectApplicationRegistration(applicationPath, ownerKey, exists, sameOwner, existingOwner);

            if(!status.ok())
            {
                return status;
            }

            if(!exists)
            {
                return IO::successStatus();
            }

            if(!sameOwner)
            {
                return registryStatus(ERROR_ALREADY_EXISTS, "application registration is owned by another registration");
            }

            RegistryKey application;
            status = openRegistryKey(HKEY_CURRENT_USER, applicationPath, KEY_READ | KEY_WRITE, application, false);

            if(!status.ok())
            {
                return status;
            }

            DWORD nativeCode = ERROR_SUCCESS;
            std::wstring extensionWide;

            if(!utf8ToUtf16(extension, extensionWide, nativeCode))
            {
                return IO::makeStatus(ErrorCode::EncodingFailed, nativeCode);
            }

            const std::wstring supportedPath = applicationPath + L"\\SupportedTypes";
            RegistryKey supportedTypes;
            status = openRegistryKey(HKEY_CURRENT_USER, supportedPath, KEY_READ | KEY_WRITE, supportedTypes, false);

            if(status.ok())
            {
                status = removeRegistryValue(supportedTypes.get(), extensionWide);

                if(!status.ok())
                {
                    return status;
                }
            }
            else if(status.code != ErrorCode::NotFound)
            {
                return status;
            }

            status = pruneEmptyRegistrationKey(supportedPath);

            if(!status.ok())
            {
                return status;
            }

            // Keep shared application metadata while other file types remain.
            RegistryKey remainingTypes;
            status = openRegistryKey(HKEY_CURRENT_USER, supportedPath, KEY_READ, remainingTypes, false);

            if(status.ok())
            {
                return IO::successStatus();
            }

            if(status.code != ErrorCode::NotFound)
            {
                return status;
            }

            // Remove application metadata after its final file type is gone.
            status = deleteRegistryTree(application.get(), L"DefaultIcon", "remove owned application icon");

            if(!status.ok())
            {
                return status;
            }

            status = deleteRegistryTree(application.get(), L"shell", "remove owned application shell commands");

            if(!status.ok())
            {
                return status;
            }

            status = removeRegistryValue(application.get(), L"");

            if(!status.ok())
            {
                return status;
            }

            status = removeRegistryValue(application.get(), L"FriendlyAppName");

            if(!status.ok())
            {
                return status;
            }

            status = removeRegistryValue(application.get(), L"ApplicationName");

            if(!status.ok())
            {
                return status;
            }

            status = removeRegistryValue(application.get(), L"ApplicationDescription");

            if(!status.ok())
            {
                return status;
            }

            status = removeRegistryValue(application.get(), L"GameWIP.OwnerKey");

            if(!status.ok())
            {
                return status;
            }

            return pruneEmptyRegistrationKey(applicationPath);
        }

        [[nodiscard]] IO::Types::Status writeRegistrationProgram(
            std::wstring_view progId,
            std::string_view ownerKey,
            std::string_view displayName,
            std::string_view contentType,
            const std::optional<Types::Registration::Icon> &icon,
            const std::optional<Types::Shell::LaunchActionView> &defaultAction,
            std::span<const Types::Registration::Verb> verbs,
            std::wstring_view applicationName = {})
        {
            // Open the ProgID and encode its metadata.
            RegistryKey program;
            IO::Types::Status status =
                openRegistryKey(HKEY_CURRENT_USER, std::wstring(L"Software\\Classes\\") + std::wstring(progId), KEY_READ | KEY_WRITE, program, true);

            if(!status.ok())
            {
                return status;
            }

            DWORD nativeCode = ERROR_SUCCESS;
            std::wstring owner;
            std::wstring display;
            std::wstring type;

            if(!utf8ToUtf16(ownerKey, owner, nativeCode) || !utf8ToUtf16(displayName, display, nativeCode) ||
               (!contentType.empty() && !utf8ToUtf16(contentType, type, nativeCode)))
            {
                return IO::makeStatus(ErrorCode::EncodingFailed, nativeCode);
            }

            // Write ownership, display, and content metadata.
            status = writeRegistryString(program.get(), L"GameWIP.OwnerKey", owner);

            if(!status.ok())
            {
                return status;
            }

            if(!applicationName.empty())
            {
                status = writeRegistryString(program.get(), L"GameWIP.ApplicationKey", applicationName);
            }
            else
            {
                status = removeRegistryValue(program.get(), L"GameWIP.ApplicationKey");
            }

            if(!status.ok())
            {
                return status;
            }

            status = writeRegistryString(program.get(), L"", display);

            if(!status.ok())
            {
                return status;
            }

            status = writeRegistryString(program.get(), L"FriendlyAppName", display);

            if(!status.ok())
            {
                return status;
            }

            // Give the shell a display identity even when the executable has no version resource.
            RegistryKey application;
            status = openRegistryKey(program.get(), L"Application", KEY_READ | KEY_WRITE, application, true);

            if(status.ok())
            {
                status = writeRegistryString(application.get(), L"ApplicationName", display);
            }

            if(status.ok())
            {
                status = writeRegistryString(application.get(), L"ApplicationDescription", display);
            }

            if(!status.ok())
            {
                return status;
            }

            if(!type.empty())
            {
                status = writeRegistryString(program.get(), L"Content Type", type);

                if(!status.ok())
                {
                    return status;
                }
            }
            else
            {
                status = removeRegistryValue(program.get(), L"Content Type");

                if(!status.ok())
                {
                    return status;
                }
            }

            // Replace the default icon and shell commands.
            status = deleteRegistryTree(program.get(), L"DefaultIcon", "remove previous program icon");

            if(!status.ok())
            {
                return status;
            }

            if(icon.has_value())
            {
                RegistryKey iconKey;
                status = openRegistryKey(program.get(), L"DefaultIcon", KEY_READ | KEY_WRITE, iconKey, true);

                if(!status.ok())
                {
                    return status;
                }
                std::wstring path = icon->path.path.get().wstring();
                std::wstring iconValue = L"\"" + path + L"\"," + std::to_wstring(icon->index);
                status = writeRegistryString(iconKey.get(), L"", iconValue);

                if(!status.ok())
                {
                    return status;
                }
            }

            status = deleteRegistryTree(program.get(), L"shell", "remove previous program shell commands");

            if(!status.ok())
            {
                return status;
            }

            if(defaultAction.has_value() || !verbs.empty())
            {
                RegistryKey shell;
                status = openRegistryKey(program.get(), L"shell", KEY_READ | KEY_WRITE, shell, true);

                if(!status.ok())
                {
                    return status;
                }

                if(defaultAction.has_value())
                {
                    RegistryKey open;
                    status = openRegistryKey(shell.get(), L"open", KEY_READ | KEY_WRITE, open, true);

                    if(!status.ok())
                    {
                        return status;
                    }
                    std::wstring label = L"Open";
                    status = writeRegistryString(open.get(), L"", label);

                    if(!status.ok())
                    {
                        return status;
                    }
                    RegistryKey command;
                    status = openRegistryKey(open.get(), L"command", KEY_READ | KEY_WRITE, command, true);

                    if(!status.ok())
                    {
                        return status;
                    }

                    status = writeLaunchCommand(command.get(), *defaultAction);

                    if(!status.ok())
                    {
                        return status;
                    }
                }

                for(const Types::Registration::Verb &verb : verbs)
                {
                    DWORD verbCode = ERROR_SUCCESS;
                    std::wstring verbName;
                    std::wstring verbLabel;

                    if(!utf8ToUtf16(verb.name, verbName, verbCode) || !utf8ToUtf16(verb.label, verbLabel, verbCode) ||
                       verbName.find_first_of(L"\\/") != std::wstring::npos)
                    {
                        return IO::makeStatus(ErrorCode::InvalidArgument);
                    }
                    RegistryKey verbKey;
                    status = openRegistryKey(shell.get(), verbName, KEY_READ | KEY_WRITE, verbKey, true);

                    if(!status.ok())
                    {
                        return status;
                    }

                    status = writeRegistryString(verbKey.get(), L"", verbLabel);

                    if(!status.ok())
                    {
                        return status;
                    }
                    RegistryKey command;
                    status = openRegistryKey(verbKey.get(), L"command", KEY_READ | KEY_WRITE, command, true);

                    if(!status.ok())
                    {
                        return status;
                    }

                    status = writeLaunchCommand(command.get(), verb.action);

                    if(!status.ok())
                    {
                        return status;
                    }
                }
            }

            return IO::successStatus();
        }

        [[nodiscard]] IO::Types::Status inspectTarget(
            std::wstring_view targetPath,
            std::string_view ownerKey,
            bool &exists,
            bool &sameOwner,
            std::string &existingOwner)
        {
            RegistryKey key;
            IO::Types::Status status = openRegistryKey(HKEY_CURRENT_USER, targetPath, KEY_READ, key, false);

            if(!status.ok())
            {
                if(status.code == ErrorCode::NotFound)
                {
                    exists = false;
                    sameOwner = false;
                    existingOwner.clear();
                    return IO::successStatus();
                }
                return status;
            }

            exists = true;
            bool hasOwner = false;
            status = readOwner(key.get(), existingOwner, hasOwner);

            if(!status.ok())
            {
                return status;
            }

            std::wstring defaultValue;
            const IO::Types::Status defaultStatus = readRegistryString(key.get(), L"", defaultValue);

            if(!defaultStatus.ok() && defaultStatus.code != ErrorCode::NotFound)
            {
                return defaultStatus;
            }

            if(!hasOwner && defaultValue.empty())
            {
                exists = false;
            }
            sameOwner = hasOwner && existingOwner == ownerKey;
            return IO::successStatus();
        }

        [[nodiscard]] Types::Registration::Conflict makeConflict(Types::Registration::TargetKind kind, std::string target, std::string owner) noexcept
        {
            return {.kind = kind, .target = std::move(target), .ownerKey = std::move(owner)};
        }

        [[nodiscard]] IO::Types::Status appendApplicationConflict(
            std::wstring_view applicationName,
            std::string_view existingOwner,
            std::vector<Types::Registration::Conflict> &conflicts)
        {
            DWORD nativeCode = ERROR_SUCCESS;
            std::string applicationNameUtf8;

            if(!utf16ToUtf8(applicationName, applicationNameUtf8, nativeCode))
            {
                return IO::makeStatus(ErrorCode::EncodingFailed, nativeCode);
            }

            conflicts.push_back(
                makeConflict(Types::Registration::TargetKind::ApplicationRecord, std::move(applicationNameUtf8), std::string(existingOwner)));
            return IO::successStatus();
        }

        [[nodiscard]] Types::Registration::Result unregisterRegistrationTarget(
            std::string_view ownerKey,
            std::string_view input,
            bool extension) noexcept
        {
            Types::Registration::Result result{IO::successStatus(), {}, false};
            try
            {
                // Normalize the target and determine its current owner.
                std::string target;
                std::wstring targetWide;
                IO::Types::Status status = normalizeTarget(input, extension, target, targetWide);

                if(!status.ok())
                {
                    result.status = std::move(status);
                    return result;
                }
                const std::wstring targetPath = L"Software\\Classes\\" + targetWide;
                bool exists = false;
                bool sameOwner = false;
                std::string existingOwner;
                status = inspectTarget(targetPath, ownerKey, exists, sameOwner, existingOwner);

                if(!status.ok())
                {
                    result.status = std::move(status);
                    return result;
                }
                const Types::Registration::TargetKind kind =
                    extension ? Types::Registration::TargetKind::FileExtension : Types::Registration::TargetKind::UriScheme;

                if(exists && !sameOwner)
                {
                    result.conflicts.push_back(makeConflict(kind, target, existingOwner));
                }

                const std::wstring progId = registrationProgId(extension, ownerKey, target);
                const std::wstring progPath = L"Software\\Classes\\" + progId;
                bool ownProgram = false;
                std::wstring applicationName;
                std::wstring applicationPath;

                // Inspect the ProgID, then release it before rollback can replace it.
                RegistryKey program;
                status = openRegistryKey(HKEY_CURRENT_USER, progPath, KEY_READ, program, false);

                if(status.ok())
                {
                    std::string programOwner;
                    bool hasOwner = false;
                    status = readOwner(program.get(), programOwner, hasOwner);

                    if(!status.ok())
                    {
                        result.status = std::move(status);
                        return result;
                    }
                    ownProgram = hasOwner && programOwner == ownerKey;

                    if(ownProgram && extension)
                    {
                        const IO::Types::Status applicationStatus = readRegistryString(program.get(), L"GameWIP.ApplicationKey", applicationName);

                        if(!applicationStatus.ok() && applicationStatus.code != ErrorCode::NotFound)
                        {
                            result.status = applicationStatus;
                            return result;
                        }
                    }
                }
                else if(status.code != ErrorCode::NotFound)
                {
                    result.status = std::move(status);
                    return result;
                }
                // Do not keep the ProgID open while the transaction removes or restores its
                // subtree. This keeps rollback independent of registry handle-sharing behavior.
                program.reset();

                if(!sameOwner && !ownProgram)
                {
                    result.status = exists ? IO::makeStatus(ErrorCode::AlreadyExists) : IO::makeStatus(ErrorCode::NotFound);
                    return result;
                }

                // Check shared application metadata before changing either registration.
                if(extension && ownProgram && !applicationName.empty())
                {
                    applicationPath = L"Software\\Classes\\Applications\\" + applicationName;
                    bool applicationExists = false;
                    bool applicationSameOwner = false;
                    std::string applicationOwner;
                    status = inspectApplicationRegistration(applicationPath, ownerKey, applicationExists, applicationSameOwner, applicationOwner);

                    if(!status.ok())
                    {
                        result.status = std::move(status);
                        return result;
                    }

                    if(applicationExists && !applicationSameOwner)
                    {
                        status = appendApplicationConflict(applicationName, applicationOwner, result.conflicts);

                        if(!status.ok())
                        {
                            result.status = std::move(status);
                            return result;
                        }

                        result.status = registryStatus(ERROR_ALREADY_EXISTS, "application registration is owned by another registration");
                        return result;
                    }
                }

                // Snapshot all affected trees before removing registry entries.
                RegistryTreeBackup transaction(targetPath, progPath, applicationPath);
                status = transaction.capture();

                if(!status.ok())
                {
                    result.status = std::move(status);
                    return result;
                }

                if(extension && ownProgram && !applicationName.empty())
                {
                    status = removeApplicationRegistration(applicationName, ownerKey, target);

                    if(!status.ok())
                    {
                        result.status = std::move(status);
                        return result;
                    }
                }

                // Remove the target, owned ProgID, and unreferenced application metadata.
                if(extension)
                {
                    const std::wstring alternativesPath = targetPath + L"\\OpenWithProgids";
                    {
                        RegistryKey alternatives;
                        status = openRegistryKey(HKEY_CURRENT_USER, alternativesPath, KEY_READ | KEY_WRITE, alternatives, false);

                        if(status.ok())
                        {
                            status = removeRegistryValue(alternatives.get(), progId);
                        }

                        if(!status.ok() && status.code != ErrorCode::NotFound)
                        {
                            result.status = std::move(status);
                            return result;
                        }
                    }
                    status = pruneEmptyRegistrationKey(alternativesPath);

                    if(!status.ok())
                    {
                        result.status = std::move(status);
                        return result;
                    }

                    if(sameOwner)
                    {
                        RegistryKey targetKey;
                        status = openRegistryKey(HKEY_CURRENT_USER, targetPath, KEY_READ | KEY_WRITE, targetKey, false);

                        if(!status.ok())
                        {
                            result.status = std::move(status);
                            return result;
                        }
                        std::wstring currentProgram;
                        status = readRegistryString(targetKey.get(), L"", currentProgram);

                        if(status.ok() && currentProgram == progId)
                        {
                            status = removeRegistryValue(targetKey.get(), L"");
                        }

                        if(!status.ok() && status.code != ErrorCode::NotFound)
                        {
                            result.status = std::move(status);
                            return result;
                        }

                        status = removeRegistryValue(targetKey.get(), L"GameWIP.OwnerKey");

                        if(!status.ok())
                        {
                            result.status = std::move(status);
                            return result;
                        }
                    }
                    status = pruneEmptyRegistrationKey(targetPath);

                    if(!status.ok())
                    {
                        result.status = std::move(status);
                        return result;
                    }
                }
                else if(sameOwner)
                {
                    status = deleteRegistryTree(HKEY_CURRENT_USER, targetPath, "delete shell registration target");

                    if(!status.ok())
                    {
                        result.status = std::move(status);
                        return result;
                    }
                }

                if(ownProgram)
                {
                    status = deleteRegistryTree(HKEY_CURRENT_USER, progPath, "delete shell registration program");

                    if(!status.ok())
                    {
                        result.status = std::move(status);
                        return result;
                    }
                }

                // Commit the removal and refresh shell associations.
                result.replacedOwned = true;
                status = transaction.commit();

                if(!status.ok())
                {
                    result.status = std::move(status);
                    return result;
                }
                SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
                return result;
            }
            catch(const std::bad_alloc &)
            {
                result.status = IO::makeStatus(ErrorCode::OutOfMemory);
                return result;
            }
            catch(...)
            {
                result.status = IO::makeStatus(ErrorCode::Unknown);
                return result;
            }
        }

    } // namespace

    /// ------------------------------------------------------------
    /// File and URI registration
    /// ------------------------------------------------------------

    Types::Registration::Result registerFileExtension(const Types::Registration::FileExtensionDescription &description) noexcept
    {
        Types::Registration::Result result{IO::successStatus(), {}, false};
        try
        {
            // Normalize the target and resolve ownership conflicts.
            std::string target;
            std::wstring targetWide;
            IO::Types::Status status = normalizeTarget(description.extension, true, target, targetWide);

            if(!status.ok())
            {
                result.status = std::move(status);
                return result;
            }
            const std::wstring targetPath = L"Software\\Classes\\" + targetWide;
            bool exists = false;
            bool sameOwner = false;
            std::string existingOwner;
            status = inspectTarget(targetPath, description.ownerKey, exists, sameOwner, existingOwner);

            if(!status.ok())
            {
                result.status = std::move(status);
                return result;
            }

            if(exists && !sameOwner)
            {
                result.conflicts.push_back(makeConflict(Types::Registration::TargetKind::FileExtension, target, existingOwner));

                if(description.conflictPolicy == Types::Registration::ConflictPolicy::Report ||
                   description.conflictPolicy == Types::Registration::ConflictPolicy::ReplaceOwned)
                {
                    result.status = IO::makeStatus(ErrorCode::AlreadyExists);
                    return result;
                }
            }
            else if(exists && description.conflictPolicy == Types::Registration::ConflictPolicy::Report)
            {
                result.conflicts.push_back(makeConflict(Types::Registration::TargetKind::FileExtension, target, std::string(description.ownerKey)));
                result.status = IO::makeStatus(ErrorCode::AlreadyExists);
                return result;
            }

            // Prepare the ProgID and check an optional application registration.
            const std::wstring progId = registrationProgId(true, description.ownerKey, target);

            if(progId.empty())
            {
                result.status = IO::makeStatus(ErrorCode::EncodingFailed);
                return result;
            }
            const std::wstring progPath = L"Software\\Classes\\" + progId;
            std::wstring applicationName;
            std::wstring applicationPath;

            if(description.defaultAction.has_value())
            {
                status = applicationRegistrationIdentity(*description.defaultAction, applicationName, applicationPath);

                if(!status.ok())
                {
                    result.status = std::move(status);
                    return result;
                }
                bool applicationExists = false;
                bool applicationSameOwner = false;
                std::string applicationOwner;
                status =
                    inspectApplicationRegistration(applicationPath, description.ownerKey, applicationExists, applicationSameOwner, applicationOwner);

                if(!status.ok())
                {
                    result.status = std::move(status);
                    return result;
                }

                if(applicationExists && !applicationSameOwner)
                {
                    status = appendApplicationConflict(applicationName, applicationOwner, result.conflicts);

                    if(!status.ok())
                    {
                        result.status = std::move(status);
                        return result;
                    }

                    result.status = registryStatus(ERROR_ALREADY_EXISTS, "application registration is owned by another registration");
                    return result;
                }
            }

            // Write program and application entries transactionally.
            RegistryTreeBackup transaction(targetPath, progPath, applicationPath);
            status = transaction.capture();

            if(!status.ok())
            {
                result.status = std::move(status);
                return result;
            }

            status = writeRegistrationProgram(
                progId,
                description.ownerKey,
                description.displayName,
                description.contentType,
                description.icon,
                description.defaultAction,
                description.verbs,
                applicationName);

            if(!status.ok())
            {
                result.status = std::move(status);
                return result;
            }

            if(description.defaultAction.has_value())
            {
                status = writeApplicationRegistration(
                    applicationName,
                    applicationPath,
                    description.ownerKey,
                    description.displayName,
                    target,
                    description.icon,
                    *description.defaultAction);

                if(!status.ok())
                {
                    result.status = std::move(status);
                    return result;
                }
            }

            // Claim the extension only when it is unowned or already ours.
            if(!exists || sameOwner)
            {
                RegistryKey targetKey;
                status = openRegistryKey(HKEY_CURRENT_USER, targetPath, KEY_READ | KEY_WRITE, targetKey, true);

                if(!status.ok())
                {
                    result.status = std::move(status);
                    return result;
                }
                DWORD nativeCode = ERROR_SUCCESS;
                std::wstring owner;

                if(!utf8ToUtf16(description.ownerKey, owner, nativeCode))
                {
                    result.status = IO::makeStatus(ErrorCode::EncodingFailed, nativeCode);
                    return result;
                }

                status = writeRegistryString(targetKey.get(), L"GameWIP.OwnerKey", owner);

                if(!status.ok())
                {
                    result.status = std::move(status);
                    return result;
                }

                status = writeRegistryString(targetKey.get(), L"", progId);

                if(!status.ok())
                {
                    result.status = std::move(status);
                    return result;
                }
            }

            // Advertise this owner's handler even when a foreign default is retained.
            RegistryKey alternatives;
            status = openRegistryKey(HKEY_CURRENT_USER, targetPath + L"\\OpenWithProgids", KEY_READ | KEY_WRITE, alternatives, true);

            if(status.ok())
            {
                status = registryStatus(RegSetValueExW(alternatives.get(), progId.c_str(), 0, REG_NONE, nullptr, 0), "write Open with entry");
            }

            if(!status.ok())
            {
                result.status = std::move(status);
                return result;
            }

            // Commit the registration and notify the shell.
            result.replacedOwned = sameOwner;
            status = transaction.commit();

            if(!status.ok())
            {
                result.status = std::move(status);
                return result;
            }
            SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
            return result;
        }
        catch(const std::bad_alloc &)
        {
            result.status = IO::makeStatus(ErrorCode::OutOfMemory);
            return result;
        }
        catch(...)
        {
            result.status = IO::makeStatus(ErrorCode::Unknown);
            return result;
        }
    }

    Types::Registration::Result registerUriScheme(const Types::Registration::UriSchemeDescription &description) noexcept
    {
        Types::Registration::Result result{IO::successStatus(), {}, false};
        try
        {
            // Normalize the target and resolve ownership conflicts.
            std::string target;
            std::wstring targetWide;
            IO::Types::Status status = normalizeTarget(description.scheme, false, target, targetWide);

            if(!status.ok())
            {
                result.status = std::move(status);
                return result;
            }
            const std::wstring targetPath = L"Software\\Classes\\" + targetWide;
            bool exists = false;
            bool sameOwner = false;
            std::string existingOwner;
            status = inspectTarget(targetPath, description.ownerKey, exists, sameOwner, existingOwner);

            if(!status.ok())
            {
                result.status = std::move(status);
                return result;
            }

            if(exists && !sameOwner)
            {
                result.conflicts.push_back(makeConflict(Types::Registration::TargetKind::UriScheme, target, existingOwner));

                if(description.conflictPolicy == Types::Registration::ConflictPolicy::Report ||
                   description.conflictPolicy == Types::Registration::ConflictPolicy::ReplaceOwned)
                {
                    result.status = IO::makeStatus(ErrorCode::AlreadyExists);
                    return result;
                }
            }
            else if(exists && description.conflictPolicy == Types::Registration::ConflictPolicy::Report)
            {
                result.conflicts.push_back(makeConflict(Types::Registration::TargetKind::UriScheme, target, std::string(description.ownerKey)));
                result.status = IO::makeStatus(ErrorCode::AlreadyExists);
                return result;
            }

            // Create the program registration transactionally.
            const std::wstring progId = registrationProgId(false, description.ownerKey, target);

            if(progId.empty())
            {
                result.status = IO::makeStatus(ErrorCode::EncodingFailed);
                return result;
            }
            const std::wstring progPath = L"Software\\Classes\\" + progId;
            RegistryTreeBackup transaction(targetPath, progPath);
            status = transaction.capture();

            if(!status.ok())
            {
                result.status = std::move(status);
                return result;
            }

            status = writeRegistrationProgram(
                progId,
                description.ownerKey,
                description.displayName,
                {},
                description.icon,
                description.defaultAction,
                description.verbs);

            if(!status.ok())
            {
                result.status = std::move(status);
                return result;
            }

            // Claim the scheme only when it is unowned or already ours.
            if(!exists || sameOwner)
            {
                RegistryKey targetKey;
                status = openRegistryKey(HKEY_CURRENT_USER, targetPath, KEY_READ | KEY_WRITE, targetKey, true);

                if(!status.ok())
                {
                    result.status = std::move(status);
                    return result;
                }
                DWORD nativeCode = ERROR_SUCCESS;
                std::wstring owner;

                if(!utf8ToUtf16(description.ownerKey, owner, nativeCode))
                {
                    result.status = IO::makeStatus(ErrorCode::EncodingFailed, nativeCode);
                    return result;
                }

                status = writeRegistryString(targetKey.get(), L"GameWIP.OwnerKey", owner);

                if(!status.ok())
                {
                    result.status = std::move(status);
                    return result;
                }

                status = writeRegistryString(targetKey.get(), L"", progId);

                if(!status.ok())
                {
                    result.status = std::move(status);
                    return result;
                }

                status = writeRegistryString(targetKey.get(), L"URL Protocol", L"");

                if(!status.ok())
                {
                    result.status = std::move(status);
                    return result;
                }

                status = writeRegistrationProgram(
                    targetWide,
                    description.ownerKey,
                    std::string("URL:") + std::string(description.displayName),
                    {},
                    description.icon,
                    description.defaultAction,
                    description.verbs);

                if(status.ok())
                {
                    status = writeRegistryString(targetKey.get(), L"GameWIP.ProgId", progId);
                }

                if(!status.ok())
                {
                    result.status = std::move(status);
                    return result;
                }
            }

            // Commit the registration and notify the shell.
            result.replacedOwned = sameOwner;
            status = transaction.commit();

            if(!status.ok())
            {
                result.status = std::move(status);
                return result;
            }
            SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
            return result;
        }
        catch(const std::bad_alloc &)
        {
            result.status = IO::makeStatus(ErrorCode::OutOfMemory);
            return result;
        }
        catch(...)
        {
            result.status = IO::makeStatus(ErrorCode::Unknown);
            return result;
        }
    }

    Types::Registration::Result unregisterFileExtension(std::string_view ownerKey, std::string_view extension) noexcept
    {
        return unregisterRegistrationTarget(ownerKey, extension, true);
    }

    Types::Registration::Result unregisterUriScheme(std::string_view ownerKey, std::string_view scheme) noexcept
    {
        return unregisterRegistrationTarget(ownerKey, scheme, false);
    }

} // namespace GameWIP::Desktop::Detail::Platform
