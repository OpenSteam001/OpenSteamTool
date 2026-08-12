#include "include/Dialog.h"

#include <windows.h>

namespace OSTPlatform::Dialog {

void ShowWarning(std::string title, std::string message) {
    MessageBoxA(nullptr, message.c_str(), title.c_str(), MB_OK | MB_ICONWARNING | MB_TOPMOST);
}

bool ShowConfirm(std::string title, std::string message) {
    return MessageBoxA(nullptr, message.c_str(), title.c_str(),
                       MB_YESNO | MB_ICONINFORMATION | MB_TOPMOST) == IDYES;
}

} // namespace OSTPlatform::Dialog
