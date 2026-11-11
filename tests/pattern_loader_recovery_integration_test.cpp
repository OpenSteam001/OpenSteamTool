// Offline integration tests of production PatternLoader.cpp. Synthetic images only.
#include "Utils/SteamMetadata/PatternLoader.h"
#include "Utils/SteamMetadata/RemoteToml.h"
#include "Utils/SteamMetadata/SteamDiagnostics.h"
#include "Utils/Support/FnvHash.h"
#include "OSTPlatform/include/Memory.h"
#include <array>
#include <iostream>
#include <sstream>
#include <string>

namespace {
std::array<uint8_t,32> firstImage{};
std::array<uint8_t,32> secondImage{};
std::string response;
bool remoteAvailable = true;
int failures=0;
int checks=0;
auto firstModule = static_cast<OSTPlatform::DynamicLibrary::ModuleHandle>(firstImage.data());
auto secondModule = static_cast<OSTPlatform::DynamicLibrary::ModuleHandle>(secondImage.data());

void Check(const char* name, bool ok) {
    std::cout << name << ": " << (ok ? "PASS" : "FAIL") << '\n';
    ++checks;
    if (!ok) ++failures;
}
std::string MetadataFor(const char* function, const char* rva) {
    std::ostringstream o;
    o << "[\"0x" << std::hex << Fnv1aHash(function) << "\"]\n"
      << "name = \"" << function << "\"\n"
      << "rva = \"" << rva << "\"\n";
    return o.str();
}
}
namespace RemoteToml {
Result Fetch(const Request&) {
    Result r;
    r.ok=remoteAvailable;
    r.body=response;
    r.sha256="synthetic-not-a-steam-hash";
    return r;
}
}
namespace SteamDiagnostics {
void ShowWarning(std::string, std::string) {}
}
namespace OSTPlatform::Memory {
std::optional<ModuleImage> GetModuleImage(DynamicLibrary::ModuleHandle module) {
    if (module==firstModule) return ModuleImage{firstImage.data(),firstImage.size()};
    if (module==secondModule) return ModuleImage{secondImage.data(),secondImage.size()};
    return std::nullopt;
}
}
int main() {
    constexpr const char* function="SyntheticFunction";
    remoteAvailable=false;
    Check("initial missing metadata", !PatternLoader::Load(firstModule,"synthetic.dll","synthetic"));
    Check("missing metadata prevents lookup", PatternLoader::FindPattern(firstModule,function)==nullptr);
    remoteAvailable=true;
    response=MetadataFor(function,"0x2");
    Check("recovery accepts valid metadata", PatternLoader::Load(firstModule,"synthetic.dll","synthetic"));
    Check("recovered RVA resolves inside synthetic image",
          PatternLoader::FindPattern(firstModule,function)==firstImage.data()+2);
    response=MetadataFor(function,"0x1000");
    Check("new metadata loads for bounds validation", PatternLoader::Load(firstModule,"synthetic.dll","synthetic"));
    Check("out of range RVA rejected", PatternLoader::FindPattern(firstModule,function)==nullptr);
    response=MetadataFor(function,"0x3");
    Check("second module accepts independent metadata", PatternLoader::Load(secondModule,"synthetic.dll","synthetic"));
    Check("second module resolves independently",
          PatternLoader::FindPattern(secondModule,function)==secondImage.data()+3);
    remoteAvailable=false;
    Check("failed retry rejected", !PatternLoader::Load(firstModule,"synthetic.dll","synthetic"));
    Check("failed retry does not return stale RVA", PatternLoader::FindPattern(firstModule,function)==nullptr);
    Check("second module stays usable after first fails",
          PatternLoader::FindPattern(secondModule,function)==secondImage.data()+3);
    remoteAvailable=true;
    response="invalid [ toml";
    Check("malformed metadata rejected", !PatternLoader::Load(firstModule,"synthetic.dll","synthetic"));
    Check("malformed retry retains no old map", PatternLoader::FindPattern(firstModule,function)==nullptr);
    response=MetadataFor(function,"0x4");
    Check("recovery after malformed metadata succeeds", PatternLoader::Load(firstModule,"synthetic.dll","synthetic"));
    Check("final recovered RVA resolves", PatternLoader::FindPattern(firstModule,function)==firstImage.data()+4);
    std::cout << "TOTAL=" << checks << " FAILURES=" << failures << '\n';
    return failures ? 1 : 0;
}
