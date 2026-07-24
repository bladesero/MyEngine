#include "Miscs/IconsManager.h"

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    std::filesystem::path iconRoot;
    std::string iconName;
    std::filesystem::path output;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if ((arg == "--icon-root" || arg == "--icon" || arg == "--output") && i + 1 < argc) {
            const std::string value = argv[++i];
            if (arg == "--icon-root")
                iconRoot = value;
            else if (arg == "--icon")
                iconName = value;
            else
                output = value;
        } else if (arg.rfind("--icon-root=", 0) == 0) {
            iconRoot = arg.substr(std::string("--icon-root=").size());
        } else if (arg.rfind("--icon=", 0) == 0) {
            iconName = arg.substr(std::string("--icon=").size());
        } else if (arg.rfind("--output=", 0) == 0) {
            output = arg.substr(std::string("--output=").size());
        } else {
            std::cerr << "Unknown or incomplete argument: " << arg << '\n';
            return 2;
        }
    }
    if (iconName.empty() || output.empty()) {
        std::cerr << "Usage: MyEngineIconTool --icon-root <dir> --icon <name> --output <file.ico>\n";
        return 2;
    }
    if (!iconRoot.empty())
        IconsManager::Get().SetIconRoot(iconRoot);
    const std::vector<int> sizes{16, 24, 32, 48, 64, 128, 256};
    if (!IconsManager::Get().WriteIco(iconName, output, sizes, IconColor::White())) {
        std::cerr << "Failed to write icon: " << output.string() << '\n';
        return 1;
    }
    return 0;
}
