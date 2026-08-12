#pragma once

#include <string>

namespace OSTPlatform::Dialog {

    void ShowWarning(std::string title, std::string message);

    // Modal Yes/No prompt. Returns true when the user chooses "Yes".
    // Blocks the calling thread until dismissed — never call from DllMain.
    bool ShowConfirm(std::string title, std::string message);

} // namespace OSTPlatform::Dialog
