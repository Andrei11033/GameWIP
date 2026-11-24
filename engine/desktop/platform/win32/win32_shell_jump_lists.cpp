/// @file win32_shell_jump_lists.cpp
/// @brief Win32 Jump List backend.

#include "desktop/platform/shell.h"

#include "desktop/platform/win32/internal/test_hooks.h"
#include "desktop/platform/win32/internal/win32_com.h"
#include "desktop/platform/win32/internal/win32_shell_launch.h"
#include "desktop/platform/win32/internal/win32_window_backend.h"
#include "desktop/platform/win32/internal/win32_unicode.h"

#include <objbase.h>
#include <propkey.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <windows.h>

#include <new>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace GameWIP::Desktop::Platform
{
    using IO::Types::ErrorCode;

    namespace
    {
        /// ------------------------------------------------------------
        /// Jump List entries
        /// ------------------------------------------------------------

        [[nodiscard]] IO::Types::Status createJumpLink(const Types::JumpLists::Task &task, ComPtr<IShellLinkW> &out)
        {
            // Create the native link and convert its launch action.
            HRESULT result = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(out.put()));

            if(FAILED(result))
            {
                return statusFromHResult(result);
            }

            std::wstring executable;
            std::wstring arguments;
            IO::Types::Status status = makeLaunchCommand(task.action, false, executable, arguments);

            if(!status.ok())
            {
                return status;
            }

            if(arguments.empty())
            {
                return IO::makeStatus(ErrorCode::InvalidArgument);
            }

            // Set the launch target and command-line arguments.
            result = out->SetPath(executable.c_str());

            if(FAILED(result))
            {
                return statusFromHResult(result);
            }

            result = out->SetArguments(arguments.c_str());

            if(FAILED(result))
            {
                return statusFromHResult(result);
            }

            // Add the title, description, and icon metadata.
            std::wstring title;
            DWORD nativeCode = ERROR_SUCCESS;

            if(!utf8ToUtf16(task.title, title, nativeCode))
            {
                return IO::makeStatus(ErrorCode::EncodingFailed, nativeCode);
            }

            ComPtr<IPropertyStore> properties;
            result = out->QueryInterface(IID_PPV_ARGS(properties.put()));

            if(FAILED(result))
            {
                return statusFromHResult(result);
            }

            PROPVARIANT titleValue{};
            titleValue.vt = VT_LPWSTR;
            titleValue.pwszVal = title.data(); // SetValue copies this borrowed string.
            result = properties->SetValue(PKEY_Title, titleValue);

            if(FAILED(result))
            {
                return statusFromHResult(result);
            }

            result = properties->Commit();

            if(FAILED(result))
            {
                return statusFromHResult(result);
            }

            std::wstring description;

            if(!utf8ToUtf16(task.description, description, nativeCode))
            {
                return IO::makeStatus(ErrorCode::EncodingFailed, nativeCode);
            }

            result = out->SetDescription(description.c_str());

            if(FAILED(result))
            {
                return statusFromHResult(result);
            }

            result = out->SetIconLocation(executable.c_str(), 0);
            return FAILED(result) ? statusFromHResult(result) : IO::successStatus();
        }

        [[nodiscard]] IO::Types::Status createJumpTaskArray(std::span<const Types::JumpLists::Task> tasks, ComPtr<IObjectArray> &out)
        {
            ComPtr<IObjectCollection> collection;
            HRESULT result = CoCreateInstance(CLSID_EnumerableObjectCollection, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(collection.put()));

            if(FAILED(result))
            {
                return statusFromHResult(result);
            }

            // Build the link collection from the task descriptors.
            for(const Types::JumpLists::Task &task : tasks)
            {
                ComPtr<IShellLinkW> link;
                IO::Types::Status status = createJumpLink(task, link);

                if(!status.ok())
                {
                    return status;
                }

                result = collection->AddObject(link.get());

                if(FAILED(result))
                {
                    return statusFromHResult(result);
                }
            }

            // Expose the collection through the shell task interface.
            result = collection->QueryInterface(IID_PPV_ARGS(out.put()));

            if(FAILED(result))
            {
                return statusFromHResult(result);
            }

            return IO::successStatus();
        }

        [[nodiscard]] IO::Types::Status appendJumpTasks(
            ICustomDestinationList *list,
            std::wstring_view category,
            std::span<const Types::JumpLists::Task> tasks)
        {
            ComPtr<IObjectArray> objects;
            const IO::Types::Status status = createJumpTaskArray(tasks, objects);

            if(!status.ok())
            {
                return status;
            }

            // Append the completed task array under its category label.
            std::wstring label(category);
            const HRESULT result = list->AppendCategory(label.c_str(), objects.get());
            return FAILED(result) ? statusFromHResult(result) : IO::successStatus();
        }

        /// @brief Commits or cancels one ICustomDestinationList transaction safely.
        class JumpListTransaction final
        {
        public:
            explicit JumpListTransaction(ICustomDestinationList &list) noexcept
                : list_(&list)
            {
            }

            JumpListTransaction(const JumpListTransaction &) = delete;
            JumpListTransaction &operator=(const JumpListTransaction &) = delete;

            ~JumpListTransaction() noexcept
            {
                if(!committed_)
                {
                    static_cast<void>(list_->AbortList());
                }
            }

            void commit() noexcept
            {
                committed_ = true;
            }

        private:
            ICustomDestinationList *list_ = nullptr;
            bool committed_ = false;
        };

    } // namespace

    /// ------------------------------------------------------------
    /// Jump Lists
    /// ------------------------------------------------------------

} // namespace GameWIP::Desktop::Platform

#if DESKTOP_INTERNAL_TEST_HOOKS

namespace GameWIP::Desktop::Internal::Win32
{
    using namespace GameWIP::Desktop::Platform;

    IO::Types::Status testShellApartment() noexcept
    {
        ApartmentLease apartment;
        const IO::Types::Status &status = apartment.status();
        return status.ok() ? IO::successStatus() : IO::makeStatus(status.code, status.nativeCode);
    }

    IO::Types::Status createJumpLinkForTests(const Types::JumpLists::Task &task, void *&out) noexcept
    {
        out = nullptr;
        ApartmentLease apartment;

        if(!apartment.status().ok())
        {
            return apartment.status();
        }

        try
        {
            ComPtr<IShellLinkW> link;
            const IO::Types::Status status = createJumpLink(task, link);

            if(status.ok())
            {
                link->AddRef();
                out = link.get();
            }

            return status;
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
} // namespace GameWIP::Desktop::Internal::Win32
#endif

namespace GameWIP::Desktop::Platform
{

    IO::Types::Status publishJumpLists(const Types::JumpLists::Description &description) noexcept
    {
        ApartmentLease apartment;

        if(!apartment.status().ok())
        {
            return apartment.status();
        }

        try
        {
            // Convert recent items before opening the native list transaction.
            std::vector<std::wstring> recentItems;
            recentItems.reserve(description.recentItems.size());

            for(const Types::Shell::TargetView &target : description.recentItems)
            {
                std::wstring recent;
                DWORD nativeCode = ERROR_SUCCESS;

                if(const auto *path = std::get_if<Types::Shell::PathTargetView>(&target))
                {
                    recent = path->path.get().wstring();
                }
                else if(const auto *uri = std::get_if<Types::Shell::UriTargetView>(&target))
                {
                    if(!utf8ToUtf16(uri->uri, recent, nativeCode))
                    {
                        return IO::makeStatus(ErrorCode::EncodingFailed, nativeCode);
                    }
                }
                else
                {
                    return IO::makeStatus(ErrorCode::InvalidArgument);
                }

                recentItems.push_back(std::move(recent));
            }

            // Begin the shell destination-list transaction.
            ComPtr<ICustomDestinationList> list;
            HRESULT result = CoCreateInstance(CLSID_DestinationList, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(list.put()));

            if(FAILED(result))
            {
                return statusFromHResult(result);
            }

            UINT maximumSlots = 0;
            ComPtr<IObjectArray> removed;
            result = list->BeginList(&maximumSlots, IID_PPV_ARGS(removed.put()));

            if(FAILED(result))
            {
                return statusFromHResult(result);
            }

            JumpListTransaction transaction(*list.get());

            // Publish tasks and named categories.
            if(!description.tasks.empty())
            {
                ComPtr<IObjectArray> taskArray;
                IO::Types::Status status = createJumpTaskArray(description.tasks, taskArray);

                if(!status.ok())
                {
                    return status;
                }

                // Add the completed task array to the user's Jump List.
                result = list->AddUserTasks(taskArray.get());

                if(FAILED(result))
                {
                    return statusFromHResult(result);
                }
            }

            for(const Types::JumpLists::Category &category : description.categories)
            {
                std::wstring label;
                DWORD nativeCode = ERROR_SUCCESS;

                if(!utf8ToUtf16(category.label, label, nativeCode))
                {
                    return IO::makeStatus(ErrorCode::EncodingFailed, nativeCode);
                }

                IO::Types::Status status = appendJumpTasks(list.get(), label, category.tasks);

                if(!status.ok())
                {
                    return status;
                }
            }

            // Commit before adding recent items to shell history.
            result = list->CommitList();

            if(FAILED(result))
            {
                return statusFromHResult(result);
            }
            transaction.commit();

            for(const std::wstring &recent : recentItems)
            {
                SHAddToRecentDocs(SHARD_PATHW, recent.c_str());
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

} // namespace GameWIP::Desktop::Platform
