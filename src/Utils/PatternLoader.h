#pragma once
#include <windows.h>
#include <string>

// Pattern-file based function locator.
//
// Call Load() once per module (steamclient64.dll, steamui.dll) during DLL
// init before any hooks are installed.  It computes the SHA-256 of the DLL
// on disk, checks the local cache under <steam>/opensteamtool/pattern/, and
// downloads the matching TOML from the steam-monitor GitHub repo if needed.
//
// Each hook then calls FindPattern() instead of the old ByteSearch / FIND_SIG.
// After all hooks are installed, call ReportMissingFunctions() to surface any
// functions that had no entry in the pattern file.

namespace PatternLoader {

    // Load pattern file for `module`.
    //   dllPath  = full on-disk path of the DLL (used for SHA-256 and error messages)
    //   ghSubdir = "steamclient" or "steamui" (selects the sub-path on GitHub)
    // Synchronous — may perform a network request.
    // Returns false and shows a popup if the file cannot be obtained or parsed.
    bool Load(HMODULE module, const std::string& dllPath, const std::string& ghSubdir);

    // Look up funcName in the pattern map for `module`.
    // Priority: RVA offset → byte-signature scan.
    // If neither succeeds, the name is recorded for ReportMissingFunctions().
    void* FindPattern(HMODULE module, const char* funcName);

    // Show a single popup listing every function that FindPattern() could not
    // locate.  Call this once after all hooks are installed.
    void ReportMissingFunctions();

} // namespace PatternLoader
