#include "include/DirectoryWatch.h"

#include "include/Encoding.h"
#include "include/Log.h"

#include "Windows/Handles.h"

#include <windows.h>

#include <algorithm>
#include <string_view>
#include <utility>

namespace OSTPlatform::DirectoryWatch {
namespace {

ChangeAction FromWindowsAction(DWORD action) {
    switch (action) {
    case FILE_ACTION_ADDED: return ChangeAction::Added;
    case FILE_ACTION_REMOVED: return ChangeAction::Removed;
    case FILE_ACTION_MODIFIED: return ChangeAction::Modified;
    case FILE_ACTION_RENAMED_OLD_NAME: return ChangeAction::RenamedOldName;
    case FILE_ACTION_RENAMED_NEW_NAME: return ChangeAction::RenamedNewName;
    default: return ChangeAction::Modified;
    }
}

} // namespace

struct Watch::Impl {
    std::string directory;
    Windows::UniqueFileHandle dir;
    Windows::UniqueHandle event;
    OVERLAPPED overlapped{};
    std::vector<char> buffer;
    bool readPending = false;

    void Close() {
        if (dir) {
            CancelIo(dir.get());
        }
        dir.Reset();
        event.Reset();
        overlapped = {};
        buffer.clear();
        readPending = false;
        directory.clear();
    }
};

Watch::Watch() : impl_(std::make_unique<Impl>()) {}
Watch::~Watch() = default;
Watch::Watch(Watch&&) noexcept = default;
Watch& Watch::operator=(Watch&&) noexcept = default;

bool Watch::Open(const std::string& directory, uint32_t bufferSize) {
    impl_->Close();

    impl_->event.Reset(CreateEventW(nullptr, FALSE, FALSE, nullptr));
    if (!impl_->event) {
        OSTP_LOG_WARN("DirectoryWatch: CreateEvent failed for '{}' (error={})", directory, GetLastError());
        return false;
    }

    const std::wstring nativeDirectory = Encoding::Utf8ToWide(directory);
    impl_->dir.Reset(CreateFileW(
        nativeDirectory.c_str(),
        FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
        nullptr));

    if (!impl_->dir) {
        OSTP_LOG_WARN("DirectoryWatch: CreateFile failed for '{}' (error={})", directory, GetLastError());
        impl_->Close();
        return false;
    }

    impl_->directory = directory;
    impl_->buffer.assign(bufferSize, 0);
    impl_->overlapped = {};
    impl_->overlapped.hEvent = impl_->event.get();
    return true;
}

bool Watch::IssueRead() {
    if (!IsOpen()) return false;
    if (impl_->readPending) return true;

    HANDLE event = impl_->event.get();
    impl_->overlapped = {};
    impl_->overlapped.hEvent = event;

    DWORD dummy = 0;
    if (!ReadDirectoryChangesW(
            impl_->dir.get(),
            impl_->buffer.data(),
            static_cast<DWORD>(impl_->buffer.size()),
            TRUE,
            FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE,
            &dummy,
            &impl_->overlapped,
            nullptr)) {
        if (GetLastError() != ERROR_IO_PENDING) {
            OSTP_LOG_WARN("DirectoryWatch: ReadDirectoryChangesW failed for '{}' (error={})",
                          impl_->directory, GetLastError());
            impl_->readPending = false;
            return false;
        }
    }

    impl_->readPending = true;
    return true;
}

std::vector<Change> Watch::Drain() {
    std::vector<Change> changes;
    if (!IsOpen() || !impl_->readPending) return changes;

    DWORD bytesReturned = 0;
    if (!GetOverlappedResult(impl_->dir.get(), &impl_->overlapped, &bytesReturned, FALSE) ||
        bytesReturned == 0) {
        impl_->readPending = false;
        return changes;
    }

    impl_->readPending = false;
    const FILE_NOTIFY_INFORMATION* info =
        reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(impl_->buffer.data());

    while (info) {
        std::wstring_view fileName(info->FileName, info->FileNameLength / sizeof(wchar_t));
        std::string relativePath = Encoding::WideToUtf8(fileName);
        if (!relativePath.empty()) {
            changes.push_back({std::move(relativePath), FromWindowsAction(info->Action)});
        }

        if (info->NextEntryOffset == 0) break;
        info = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(
            reinterpret_cast<const char*>(info) + info->NextEntryOffset);
    }

    return changes;
}

void Watch::Cancel() {
    impl_->Close();
}

bool Watch::IsOpen() const {
    return impl_ && impl_->dir && impl_->event;
}

const std::string& Watch::Directory() const {
    return impl_->directory;
}

WaitResult WaitAny(std::span<Watch*> watches, uint32_t timeoutMs) {
    std::vector<HANDLE> events;
    std::vector<size_t> originalIndexes;
    events.reserve(watches.size());
    originalIndexes.reserve(watches.size());

    for (size_t i = 0; i < watches.size(); ++i) {
        Watch* watch = watches[i];
        if (!watch || !watch->IsOpen()) continue;
        events.push_back(watch->impl_->event.get());
        originalIndexes.push_back(i);
    }

    if (events.empty()) {
        return {WaitStatus::Failed, 0};
    }

    const DWORD result = WaitForMultipleObjects(
        static_cast<DWORD>(events.size()),
        events.data(),
        FALSE,
        timeoutMs);

    if (result == WAIT_TIMEOUT) return {WaitStatus::Timeout, 0};
    if (result >= WAIT_OBJECT_0 && result < WAIT_OBJECT_0 + events.size()) {
        return {WaitStatus::Signaled, originalIndexes[static_cast<size_t>(result - WAIT_OBJECT_0)]};
    }

    OSTP_LOG_WARN("DirectoryWatch: WaitForMultipleObjects failed/result={} error={}", result, GetLastError());
    return {WaitStatus::Failed, 0};
}

} // namespace OSTPlatform::DirectoryWatch
