// SPDX-License-Identifier: GPL-3.0-or-later

#include <cassert>
#include <sstream>
#include <string>
#include <vector>

#include "targets/cli_options.h"

#ifndef LOFIBOX_VERSION
#error "LOFIBOX_VERSION must be inherited from the core compile definitions"
#endif

namespace {

bool run(std::vector<std::string> args, std::string& out_text)
{
    std::vector<char*> argv{};
    argv.reserve(args.size());
    for (auto& arg : args) {
        argv.push_back(arg.data());
    }
    std::ostringstream out{};
    const bool handled = lofibox::targets::handleCommonCliOptions(static_cast<int>(argv.size()), argv.data(), out);
    out_text = out.str();
    return handled;
}

} // namespace

int main()
{
    const std::string expected_version{LOFIBOX_VERSION};
    const std::string expected_json = "{\"name\":\"lofibox\",\"version\":\"" + expected_version + "\"}\n";

    std::string out{};
    bool handled = run({"lofibox", "--version"}, out);
    assert(handled);
    assert(out == "lofibox " + expected_version + "\n");

    handled = run({"lofibox", "--version", "--json"}, out);
    assert(handled);
    assert(out == expected_json);

    handled = run({"lofibox", "version", "--json"}, out);
    assert(handled);
    assert(out == expected_json);

    handled = run({"lofibox", "--json", "version"}, out);
    assert(handled);
    assert(out == expected_json);

    handled = run({"lofibox", "--version", "--porcelain"}, out);
    assert(handled);
    assert(out == "version\t" + expected_version + "\n");

    handled = run({"lofibox", "--quiet", "version"}, out);
    assert(handled);
    assert(out.empty());

    handled = run({"lofibox", "source", "list"}, out);
    assert(!handled);

    handled = run({"lofibox", "help"}, out);
    assert(handled);
    assert(out.find("Usage:") != std::string::npos);
    assert(out.find("Direct commands:") != std::string::npos);

    handled = run({"lofibox", "source", "--help"}, out);
    assert(handled);
    assert(out.find("LoFiBox command: source") != std::string::npos);
    assert(out.find("Requires runtime: no") != std::string::npos);

    handled = run({"lofibox", "tui", "--help"}, out);
    assert(!handled);

    handled = run({"lofibox", "commands", "--json"}, out);
    assert(handled);
    assert(out.find("\"commands\"") != std::string::npos);
    assert(out.find("\"command\":\"metadata\"") != std::string::npos);
    assert(out.find("\"examples\"") != std::string::npos);
    assert(out.find("\"recovery\"") != std::string::npos);

    handled = run({"lofibox", "help", "runtime", "playback", "--json"}, out);
    assert(handled);
    assert(out.find("\"command\":\"runtime playback\"") != std::string::npos);
    assert(out.find("\"requires_runtime\":true") != std::string::npos);
    assert(out.find("\"mutates\":false") != std::string::npos);
    assert(out.find("\"fields\"") != std::string::npos);

    handled = run({"lofibox", "runtime", "playback", "--schema"}, out);
    assert(handled);
    assert(out.find("\"command\":\"runtime playback\"") != std::string::npos);
    assert(out.find("\"usage\"") != std::string::npos);

    return 0;
}
