#include "Config.h"
#include "Log.h"
#include <toml++/toml.hpp>
#include <filesystem>

namespace Config {

    void Load(const std::string& configPath) {
        std::filesystem::path p(configPath);
        logDir = (p.parent_path() / "opensteamtool").string();

        if (!std::filesystem::exists(configPath)) {
            LOG_INFO("Config file not found, using defaults");
            return;
        }

        try {
            auto tbl = toml::parse_file(configPath);

            // [manifest]
            if (auto manifest = tbl["manifest"].as_table()) {
                if (auto val = (*manifest)["url"].value<std::string>()) {
                    if (*val == "wudrm")
                        manifestUrl = ManifestUrl::Wudrm;
                }
                if (auto val = (*manifest)["timeout_resolve_ms"].value<int64_t>())
                    manifestTimeoutResolve = static_cast<DWORD>(*val);
                if (auto val = (*manifest)["timeout_connect_ms"].value<int64_t>())
                    manifestTimeoutConnect = static_cast<DWORD>(*val);
                if (auto val = (*manifest)["timeout_send_ms"].value<int64_t>())
                    manifestTimeoutSend = static_cast<DWORD>(*val);
                if (auto val = (*manifest)["timeout_recv_ms"].value<int64_t>())
                    manifestTimeoutRecv = static_cast<DWORD>(*val);
            }

            // [log]
            if (auto log = tbl["log"].as_table()) {
                if (auto val = (*log)["level"].value<std::string>()) {
                    if (*val == "trace")           logLevel = LogLevel::Trace;
                    else if (*val == "debug")       logLevel = LogLevel::Debug;
                    else if (*val == "info")        logLevel = LogLevel::Info;
                    else if (*val == "warn")        logLevel = LogLevel::Warn;
                    else if (*val == "error")       logLevel = LogLevel::Error;
                }
            }

            // [lua]
            if (auto lua = tbl["lua"].as_table()) {
                if (auto arr = (*lua)["paths"].as_array()) {
                    for (auto& elem : *arr) {
                        if (auto str = elem.value<std::string>()) {
                            luaPaths.push_back(*str);
                        }
                    }
                }
            }

            // [pattern]
            if (auto pattern = tbl["pattern"].as_table()) {
                if (auto val = (*pattern)["mirror"].value<std::string>()) {
                    patternMirror = *val;
                    // Strip a trailing slash so PatternLoader can append
                    // "/<subdir>/<sha>.toml" without producing "//".
                    while (!patternMirror.empty() && patternMirror.back() == '/')
                        patternMirror.pop_back();
                }
            }

            LOG_INFO("Config loaded: manifest.url={} log.level={} lua.paths={} pattern.mirror={}",
                     manifestUrl == ManifestUrl::Wudrm ? "wudrm" : "steamrun",
                     [&](){
                         switch (logLevel) {
                         case LogLevel::Trace: return "trace";
                         case LogLevel::Debug: return "debug";
                         case LogLevel::Info:  return "info";
                         case LogLevel::Warn:  return "warn";
                         case LogLevel::Error: return "error";
                         default: return "???";
                         }
                     }(),
                     (uint32_t)luaPaths.size(),
                     patternMirror.empty() ? "<default>" : patternMirror);

        } catch (const toml::parse_error& e) {
            LOG_WARN("Config parse error: {}", e.what());
        } catch (...) {
            LOG_WARN("Config load failed, using defaults");
        }
    }

}
