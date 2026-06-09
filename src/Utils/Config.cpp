#include "Config.h"
#include "Log.h"
#include "ManifestClient.h"
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
                    if (!ManifestClient::SetProvider(*val))
                        LOG_WARN("Unknown manifest.url \"{}\", keeping default", *val);
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

            // [remote]
            if (auto remote = tbl["remote"].as_table()) {
                if (auto val = (*remote)["url_template"].value<std::string>()) {
                    remoteUrlTemplate = *val;
                }
            }

            LOG_INFO("Config loaded: manifest.url={} log.level={} lua.paths={} remote.url_template={}",
                     ManifestClient::ActiveProviderName(),
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
                     remoteUrlTemplate.empty() ? "<default>" : remoteUrlTemplate);

        } catch (const toml::parse_error& e) {
            LOG_WARN("Config parse error: {}", e.what());
        } catch (...) {
            LOG_WARN("Config load failed, using defaults");
        }
    }

}
