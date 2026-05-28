#include "dllmain.h"
#include "FileWatcher.h"
#include "LuaConfig.h"
#include "Hook/Hooks_Package.h"
#include "Log.h"
#include <thread>
#include <atomic>
#include <vector>
#include <string>
#include <unordered_map>

namespace FileWatcher {
    static std::atomic<bool> g_running{false};
    static std::thread g_watcherThread;
    static std::vector<std::string> g_watchDirs;

    static const DWORD kDebounceMs = 500;

    struct FileChange {
        std::string path;
        DWORD action;   // FILE_ACTION_ADDED / MODIFIED / REMOVED
    };

    // Merge new changes into accumulated map, keeping last action per file.
    static void MergeChanges(
        std::unordered_map<std::string, DWORD>& accumulated,
        std::vector<std::string>& order,
        const std::vector<FileChange>& newChanges)
    {
        for (const auto& ch : newChanges) {
            if (accumulated.find(ch.path) == accumulated.end())
                order.push_back(ch.path);
            accumulated[ch.path] = ch.action;
        }
    }

    static void ProcessChanges(
        const std::unordered_map<std::string, DWORD>& accumulated,
        const std::vector<std::string>& order)
    {
        LOG_PACKAGE_INFO("Processing {} Lua file change(s)", order.size());
        for (const auto& path : order) {
            DWORD action = accumulated.at(path);
            if (action == FILE_ACTION_REMOVED) {
                LuaConfig::UnloadFile(path);
            } else {
                // ADDED or MODIFIED — ParseFile internally calls UnloadFile first.
                LuaConfig::ParseFile(path);
            }
        }

        Hooks_Package::NotifyLicenseChanged();
        LOG_PACKAGE_INFO("Refresh completed");
    }

    // Collects .lua file changes from the notification buffer.
    // Deduplicates by filename — keeps only the last action per file.
    static std::vector<FileChange> CollectLuaChanges(
        const char* buffer, DWORD bytesReturned, const std::string& dir)
    {
        // Use a map to deduplicate: filename -> last action
        std::unordered_map<std::string, DWORD> seen;
        std::vector<std::string> order; // preserve first-seen order

        const FILE_NOTIFY_INFORMATION* info =
            reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(buffer);
        while (info) {
            if (info->Action == FILE_ACTION_ADDED
                || info->Action == FILE_ACTION_MODIFIED
                || info->Action == FILE_ACTION_REMOVED) {
                std::wstring_view fname(info->FileName, info->FileNameLength / sizeof(wchar_t));
                if (fname.size() >= 4 && fname.substr(fname.size() - 4) == L".lua") {
                    std::string name(fname.begin(), fname.end());
                    if (seen.find(name) == seen.end())
                        order.push_back(name);
                    seen[name] = info->Action; // last action wins
                }
            }
            if (info->NextEntryOffset == 0) break;
            info = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(
                reinterpret_cast<const char*>(info) + info->NextEntryOffset);
        }

        std::vector<FileChange> result;
        for (const auto& name : order) {
            DWORD action = seen[name];
            LOG_PACKAGE_INFO("Lua file {}: {}",
                action == FILE_ACTION_ADDED ? "added" :
                action == FILE_ACTION_MODIFIED ? "modified" : "removed", name);
            result.push_back({dir + "\\" + name, action});
        }
        return result;
    }

    static bool IssueRead(HANDLE dir, char* buf, DWORD bufSize, OVERLAPPED* ov) {
        DWORD dummy = 0;
        if (!ReadDirectoryChangesW(dir, buf, bufSize, FALSE,
                                   FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE,
                                   &dummy, ov, nullptr)) {
            if (GetLastError() != ERROR_IO_PENDING) {
                LOG_PACKAGE_WARN("ReadDirectoryChangesW failed: {}", GetLastError());
                return false;
            }
        }
        return true;
    }

    static void WatcherThread() {
        const size_t numDirs = g_watchDirs.size();
        if (numDirs == 0) {
            LOG_PACKAGE_WARN("No directories configured for watcher");
            return;
        }

        std::vector<HANDLE> dirHandles(numDirs, nullptr);
        std::vector<OVERLAPPED> overlapped(numDirs);
        std::vector<HANDLE> events(numDirs, nullptr);
        std::vector<std::vector<char>> buffers(numDirs);

        for (size_t i = 0; i < numDirs; ++i) {
            events[i] = CreateEventA(nullptr, FALSE, FALSE, nullptr);
            overlapped[i].hEvent = events[i];
            buffers[i].resize(65536);

            dirHandles[i] = CreateFileA(
                g_watchDirs[i].c_str(),
                FILE_LIST_DIRECTORY,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                nullptr, OPEN_EXISTING,
                FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
                nullptr);

            if (dirHandles[i] == INVALID_HANDLE_VALUE) {
                LOG_PACKAGE_WARN("Failed to open: {} (err={})", g_watchDirs[i], GetLastError());
                dirHandles[i] = nullptr;
                continue;
            }

            if (!IssueRead(dirHandles[i], buffers[i].data(),
                           static_cast<DWORD>(buffers[i].size()), &overlapped[i])) {
                CloseHandle(dirHandles[i]);
                dirHandles[i] = nullptr;
                continue;
            }

            LOG_PACKAGE_INFO("Watching: {}", g_watchDirs[i]);
        }

        bool allFailed = true;
        for (auto& h : dirHandles) if (h) { allFailed = false; break; }
        if (allFailed) {
            LOG_PACKAGE_WARN("No directories could be opened");
            for (auto& e : events) if (e) CloseHandle(e);
            return;
        }

        while (g_running) {
            DWORD waitResult = WaitForMultipleObjects(
                static_cast<DWORD>(numDirs), events.data(), FALSE, 1000);

            if (!g_running) break;
            if (waitResult == WAIT_TIMEOUT) continue;
            if (waitResult < WAIT_OBJECT_0 || waitResult >= WAIT_OBJECT_0 + numDirs) continue;

            // First event arrived — collect it and start debounce window.
            // Keep draining events until no new events arrive within kDebounceMs.
            std::unordered_map<std::string, DWORD> accumulated;
            std::vector<std::string> order;

            auto drainEvent = [&](size_t idx) {
                HANDLE dir = dirHandles[idx];
                if (!dir) return;
                DWORD bytesReturned = 0;
                if (GetOverlappedResult(dir, &overlapped[idx], &bytesReturned, FALSE)
                    && bytesReturned > 0) {
                    auto changes = CollectLuaChanges(
                        buffers[idx].data(), bytesReturned, g_watchDirs[idx]);
                    MergeChanges(accumulated, order, changes);
                }
                // Re-arm immediately so we catch events during debounce
                IssueRead(dir, buffers[idx].data(),
                          static_cast<DWORD>(buffers[idx].size()), &overlapped[idx]);
            };

            drainEvent(waitResult - WAIT_OBJECT_0);

            // Debounce loop: keep waiting for more events
            while (g_running) {
                DWORD debounceResult = WaitForMultipleObjects(
                    static_cast<DWORD>(numDirs), events.data(), FALSE, kDebounceMs);
                if (!g_running) break;
                if (debounceResult == WAIT_TIMEOUT) break; // quiet period — done accumulating
                if (debounceResult < WAIT_OBJECT_0 || debounceResult >= WAIT_OBJECT_0 + numDirs) break;
                drainEvent(debounceResult - WAIT_OBJECT_0);
            }

            if (!order.empty())
                ProcessChanges(accumulated, order);
        }

        for (auto& h : dirHandles) if (h) CloseHandle(h);
        for (auto& e : events) if (e) CloseHandle(e);
        LOG_PACKAGE_INFO("Stopped");
    }

    void Start(const std::vector<std::string>& directories) {
        if (directories.empty()) {
            LOG_PACKAGE_WARN("No directories configured for watcher start");
            return;
        }

        std::vector<std::string> effectiveDirs = directories;
        if (effectiveDirs.size() > MAXIMUM_WAIT_OBJECTS) {
            LOG_PACKAGE_WARN("Too many watch directories: {} (max supported by WaitForMultipleObjects: {})",
                             effectiveDirs.size(), MAXIMUM_WAIT_OBJECTS);
            effectiveDirs.resize(MAXIMUM_WAIT_OBJECTS);
        }

        if (g_running.exchange(true)) {
            LOG_PACKAGE_WARN("Already running");
            return;
        }
        g_watchDirs = effectiveDirs;
        g_watcherThread = std::thread(WatcherThread);
    }

    void Stop() {
        if (!g_running) return;
        g_running = false;
        if (g_watcherThread.joinable()) {
            g_watcherThread.join();
        }
    }
}
