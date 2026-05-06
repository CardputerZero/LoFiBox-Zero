// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <string_view>

#include "runtime/runtime_command.h"

namespace lofibox::app {
class LibraryEnrichProvider;
class LibraryQueryProvider;
} // namespace lofibox::app

namespace lofibox::webui {

class WebUiRuntimeAdapter;

// Maps HTTP requests to runtime operations.
class WebUiHttpRouter {
public:
    explicit WebUiHttpRouter(WebUiRuntimeAdapter& adapter) noexcept;

    void setLibraryQueryProvider(app::LibraryQueryProvider* provider) noexcept;
    void setLibraryEnrichProvider(app::LibraryEnrichProvider* provider) noexcept;

    std::string handleRequest(std::string_view method, std::string_view path, std::string_view body);

    bool parseWebUiCommand(std::string_view json_body, runtime::RuntimeCommand& command);

private:
    WebUiRuntimeAdapter& adapter_;
    app::LibraryQueryProvider* library_provider_{nullptr};
    app::LibraryEnrichProvider* enrich_provider_{nullptr};
};

} // namespace lofibox::webui
