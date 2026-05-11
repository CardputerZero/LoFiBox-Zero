// SPDX-License-Identifier: GPL-3.0-or-later

#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <string>
#include <vector>

#ifndef LOFIBOX_PROJECT_SOURCE_DIR
#error "LOFIBOX_PROJECT_SOURCE_DIR must be defined"
#endif

#ifndef LOFIBOX_PROJECT_VERSION
#error "LOFIBOX_PROJECT_VERSION must be defined"
#endif

namespace {

std::string readTextFile(const std::filesystem::path& path)
{
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("Unable to open " + path.string());
    }
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

std::string firstLine(const std::string& text)
{
    const auto end = text.find('\n');
    auto line = text.substr(0, end == std::string::npos ? std::string::npos : end);
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    return line;
}

bool contains(const std::string& text, const std::string& needle)
{
    return text.find(needle) != std::string::npos;
}

} // namespace

int main()
{
    const auto project_root = std::filesystem::path{LOFIBOX_PROJECT_SOURCE_DIR};
    const auto project_version = std::string{LOFIBOX_PROJECT_VERSION};

    try {
        const auto changelog = readTextFile(project_root / "debian" / "changelog");
        const auto expected_changelog_prefix = "lofibox (" + project_version + "-";
        if (firstLine(changelog).rfind(expected_changelog_prefix, 0) != 0) {
            std::cerr << "Expected debian/changelog to start with " << expected_changelog_prefix
                      << ", got: " << firstLine(changelog) << "\n";
            return 1;
        }

        const auto man_template = readTextFile(project_root / "data" / "man" / "lofibox.1.in");
        if (!contains(man_template, "LoFiBox @PROJECT_VERSION@")) {
            std::cerr << "Expected manpage template to consume CMake PROJECT_VERSION.\n";
            return 1;
        }
        if (std::regex_search(man_template, std::regex{R"(LoFiBox 0\.[0-9]+\.[0-9]+)"})) {
            std::cerr << "Manpage template must not hardcode a released version string.\n";
            return 1;
        }

        const auto orig_tarball_script = readTextFile(project_root / "scripts" / "create-orig-tarball.ps1");
        if (!contains(orig_tarball_script, "CMakeLists.txt")
            || !contains(orig_tarball_script, "debian\\changelog")
            || !contains(orig_tarball_script, "does not match")) {
            std::cerr << "Expected orig tarball helper to derive and validate the project version.\n";
            return 1;
        }
        if (std::regex_search(orig_tarball_script, std::regex{R"rx(\[string\]\$Version\s*=\s*"0\.[0-9]+\.[0-9]+")rx"})) {
            std::cerr << "Orig tarball helper must not default to a hand-written version literal.\n";
            return 1;
        }

        const std::vector<std::filesystem::path> dynamic_artifact_paths{
            project_root / ".github" / "workflows" / "linux-ci.yml",
            project_root / "scripts" / "run-debian-package-validation.ps1",
            project_root / "scripts" / "run-dpkg-buildpackage.ps1",
            project_root / "scripts" / "create-orig-tarball.ps1",
        };
        const auto hardcoded_package_artifact = std::regex{
            R"(lofibox_0\.[0-9]+\.[0-9]+(?:-[0-9]+(?:~[A-Za-z0-9]+)?)?_[A-Za-z0-9]+(?:\.(?:deb|changes)))"};
        for (const auto& path : dynamic_artifact_paths) {
            const auto text = readTextFile(path);
            if (std::regex_search(text, hardcoded_package_artifact)) {
                std::cerr << "Packaging scripts and CI must not hardcode versioned Debian artifacts: "
                          << path.string() << "\n";
                return 1;
            }
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        return 1;
    }

    return 0;
}
