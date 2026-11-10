#pragma once

// Pure, dependency-free policy for rejecting empty parsed IPC metadata.
// Keeping the policy separate permits tests with synthetic interfaces.
namespace IPCMetadataValidation {
    template <typename Interfaces>
    bool HasUsableMethods(const Interfaces& interfaces) {
        for (const auto& iface : interfaces) {
            if (!iface.methods.empty())
                return true;
        }
        return false;
    }
}
