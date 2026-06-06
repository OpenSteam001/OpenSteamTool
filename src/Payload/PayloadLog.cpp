#include "PayloadLog.h"

#ifdef OPENSTEAMTOOL_LOGGING_ENABLED

#include <atomic>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>

namespace {
    std::filesystem::path g_path;
    std::mutex            g_mutex;
    std::atomic_bool      g_ready{false};
}

namespace PayloadLog {
    void Init(HMODULE self) {
        // The DLL sits in Steam's root, so this lands beside the host's logs.
        wchar_t dll[MAX_PATH] = {};
        if (!GetModuleFileNameW(self, dll, MAX_PATH)) return;
        auto dir = std::filesystem::path(dll).parent_path() / "opensteamtool" / "payload";

        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        g_path = dir / (std::to_string(GetCurrentProcessId()) + ".log");
        g_ready.store(true);
    }

    void Write(const std::string& line) {
        if (!g_ready.load()) return;
        std::lock_guard<std::mutex> lock(g_mutex);
        std::ofstream f(g_path, std::ios::app | std::ios::binary);
        if (!f) return;
        std::time_t t = std::time(nullptr);
        std::tm tm{};
        localtime_s(&tm, &t);
        char ts[32];
        std::strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm);
        f << "[" << ts << "] [tid=" << GetCurrentThreadId() << "] " << line << "\n";
    }
}

#endif  // OPENSTEAMTOOL_LOGGING_ENABLED
