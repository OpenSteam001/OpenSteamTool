#include "Utils/SteamMetadata/IPCMetadataValidation.h"
#include <cassert>
#include <vector>

struct SyntheticInterface { std::vector<int> methods; };

int main() {
    using IPCMetadataValidation::HasUsableMethods;
    assert(!HasUsableMethods(std::vector<SyntheticInterface>{}));
    assert(!HasUsableMethods(std::vector<SyntheticInterface>{SyntheticInterface{}, SyntheticInterface{}}));
    assert(HasUsableMethods(std::vector<SyntheticInterface>{SyntheticInterface{{1}}}));
    assert(HasUsableMethods(std::vector<SyntheticInterface>{SyntheticInterface{}, SyntheticInterface{{1, 2}}}));
    return 0;
}
