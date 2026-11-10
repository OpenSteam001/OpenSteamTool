// Execute the REAL IPCLoader.cpp with synthetic responses, offline.
// Stub only network retrieval and warning UI; no Steam process or DLLs are loaded.
#include "Utils/SteamMetadata/IPCLoader.h"
#include "Utils/SteamMetadata/RemoteToml.h"
#include "Utils/SteamMetadata/SteamDiagnostics.h"
#include <iostream>
#include <string>

namespace {
std::string response;
bool available = true;
int failures = 0;
int checks = 0;
}

namespace RemoteToml {
Result Fetch(const Request&) {
    Result r;
    r.ok = available;
    r.body = response;
    r.sha256 = "synthetic-not-a-steam-hash";
    return r;
}
}
namespace SteamDiagnostics {
void ShowWarning(std::string, std::string) {}
}

static void Check(const char* name, const char* toml, bool expectSuccess,
                  size_t expectedMethods) {
    response = toml;
    available = true;
    const bool succeeded = IPCLoader::Load("synthetic-test-path");
    const size_t count = IPCLoader::MethodCount();
    const bool ok = succeeded == expectSuccess && count == expectedMethods;
    std::cout << name << ": " << (ok ? "PASS" : "FAIL")
              << " actual_success=" << succeeded << " methods=" << count << '\n';
    ++checks;
    if (!ok) ++failures;
}

int main() {
    Check("empty TOML", "", false, 0);
    Check("unrecognized interface", "[Unknown.TestMethod]\nfuncHash = '0x12'\n", false, 0);
    Check("known interface, no methods", "[IClientUser]\n", false, 0);
    Check("invalid method hash", "[IClientUser.TestMethod]\nfuncHash = 'bad hash'\n", false, 0);
    Check("malformed TOML", "[IClientUser\n", false, 0);
    Check("valid method", "[IClientUser.TestMethod]\nfuncHash = '0x12'\n", true, 1);
    Check("mixed known and unknown", "[Unknown.TestMethod]\nfuncHash = '0x34'\n[IClientUser.TestMethod]\nfuncHash = '0x12'\n", true, 1);
    response = "[IClientUser.TestMethod]\nfuncHash = '0x12'\n";
    available = false;
    const bool unavailableOk = !IPCLoader::Load("synthetic-test-path") && IPCLoader::MethodCount() == 0;
    std::cout << "unavailable metadata: " << (unavailableOk ? "PASS" : "FAIL") << '\n';
    ++checks;
    if (!unavailableOk) ++failures;
    std::cout << "TOTAL=" << checks << " FAILURES=" << failures << '\n';
    return failures == 0 ? 0 : 1;
}
