// SPDX-License-Identifier: GPL-3.0-or-later

#include "plugins/skin_plugin_adapter.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>

namespace lofibox::plugins {
namespace {

std::string readFile(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        return {};
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

bool skipWhitespace(std::string_view json, std::size_t& pos)
{
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == '\r')) {
        ++pos;
    }
    return pos < json.size();
}

std::string extractStringValue(std::string_view json, std::size_t& pos)
{
    if (pos >= json.size() || json[pos] != '"') {
        return {};
    }
    ++pos;
    std::string result;
    while (pos < json.size() && json[pos] != '"') {
        if (json[pos] == '\\' && pos + 1 < json.size()) {
            ++pos;
            switch (json[pos]) {
            case '"': result += '"'; break;
            case '\\': result += '\\'; break;
            case 'n': result += '\n'; break;
            default: result += json[pos]; break;
            }
            ++pos;
        } else {
            result += json[pos++];
        }
    }
    if (pos < json.size()) {
        ++pos;
    }
    return result;
}

int extractIntValue(std::string_view json, std::size_t& pos)
{
    skipWhitespace(json, pos);
    int value = 0;
    bool negative = false;
    if (pos < json.size() && json[pos] == '-') {
        negative = true;
        ++pos;
    }
    while (pos < json.size() && std::isdigit(static_cast<unsigned char>(json[pos]))) {
        value = (value * 10) + (json[pos] - '0');
        ++pos;
    }
    return negative ? -value : value;
}

bool extractBoolValue(std::string_view json, std::size_t& pos)
{
    skipWhitespace(json, pos);
    if (pos + 4 <= json.size() && json.substr(pos, 4) == "true") {
        pos += 4;
        return true;
    }
    if (pos + 5 <= json.size() && json.substr(pos, 5) == "false") {
        pos += 5;
        return false;
    }
    return false;
}

void skipValue(std::string_view json, std::size_t& pos)
{
    skipWhitespace(json, pos);
    if (pos >= json.size()) {
        return;
    }
    if (json[pos] == '"') {
        extractStringValue(json, pos);
    } else if (json[pos] == '{') {
        int depth = 1;
        ++pos;
        while (pos < json.size() && depth > 0) {
            if (json[pos] == '{') ++depth;
            else if (json[pos] == '}') --depth;
            else if (json[pos] == '"') extractStringValue(json, pos);
            ++pos;
        }
    } else if (json[pos] == '[') {
        int depth = 1;
        ++pos;
        while (pos < json.size() && depth > 0) {
            if (json[pos] == '[') ++depth;
            else if (json[pos] == ']') --depth;
            else if (json[pos] == '"') extractStringValue(json, pos);
            ++pos;
        }
    } else {
        while (pos < json.size() && json[pos] != ',' && json[pos] != '}' && json[pos] != ']') {
            ++pos;
        }
    }
}

std::string findStringField(std::string_view json, std::string_view field_name)
{
    std::string search = "\"" + std::string(field_name) + "\"";
    std::size_t pos = json.find(search);
    if (pos == std::string::npos) {
        return {};
    }
    pos += search.size();
    skipWhitespace(json, pos);
    if (pos < json.size() && json[pos] == ':') {
        ++pos;
        skipWhitespace(json, pos);
        return extractStringValue(json, pos);
    }
    return {};
}

std::optional<int> findIntField(std::string_view json, std::string_view field_name)
{
    std::string search = "\"" + std::string(field_name) + "\"";
    std::size_t pos = json.find(search);
    if (pos == std::string::npos) {
        return std::nullopt;
    }
    pos += search.size();
    skipWhitespace(json, pos);
    if (pos < json.size() && json[pos] == ':') {
        ++pos;
        return extractIntValue(json, pos);
    }
    return std::nullopt;
}

std::optional<bool> findBoolField(std::string_view json, std::string_view field_name)
{
    std::string search = "\"" + std::string(field_name) + "\"";
    std::size_t pos = json.find(search);
    if (pos == std::string::npos) {
        return std::nullopt;
    }
    pos += search.size();
    skipWhitespace(json, pos);
    if (pos < json.size() && json[pos] == ':') {
        ++pos;
        return extractBoolValue(json, pos);
    }
    return std::nullopt;
}

std::optional<std::string> findNestedStringField(std::string_view json, std::string_view parent, std::string_view field)
{
    std::string search = "\"" + std::string(parent) + "\"";
    std::size_t pos = json.find(search);
    if (pos == std::string::npos) {
        return std::nullopt;
    }
    pos += search.size();
    skipWhitespace(json, pos);
    if (pos < json.size() && json[pos] == ':') {
        ++pos;
        skipWhitespace(json, pos);
        if (pos < json.size() && json[pos] == '{') {
            ++pos;
            auto block = json.substr(pos);
            auto result = findStringField(block, field);
            if (!result.empty()) {
                return std::string(result);
            }
        }
    }
    return std::nullopt;
}

std::optional<int> findNestedIntField(std::string_view json, std::string_view parent, std::string_view field)
{
    std::string search = "\"" + std::string(parent) + "\"";
    std::size_t pos = json.find(search);
    if (pos == std::string::npos) {
        return std::nullopt;
    }
    pos += search.size();
    skipWhitespace(json, pos);
    if (pos < json.size() && json[pos] == ':') {
        ++pos;
        skipWhitespace(json, pos);
        if (pos < json.size() && json[pos] == '{') {
            ++pos;
            auto block = json.substr(pos);
            return findIntField(block, field);
        }
    }
    return std::nullopt;
}

} // namespace

bool SkinPluginAdapter::parsePaletteColor(const std::string& hex, core::Color& out) const
{
    if (hex.size() != 7 || hex[0] != '#') {
        return false;
    }
    unsigned int r = 0, g = 0, b = 0;
    if (std::sscanf(hex.c_str() + 1, "%02x%02x%02x", &r, &g, &b) != 3) {
        return false;
    }
    out = core::rgba(static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g), static_cast<std::uint8_t>(b));
    return true;
}

bool SkinPluginAdapter::loadSkinFromJson(const std::filesystem::path& skin_json_path)
{
    const auto content = readFile(skin_json_path);
    if (content.empty()) {
        return false;
    }

    const auto get_color = [&](std::string_view parent, std::string_view field, core::Color& out) {
        const auto hex = findNestedStringField(content, parent, field);
        if (hex) {
            parsePaletteColor(*hex, out);
        }
    };

    get_color("palette", "background", theme_.palette.background);
    get_color("palette", "panel0", theme_.palette.panel0);
    get_color("palette", "panel", theme_.palette.panel1);
    get_color("palette", "panel1", theme_.palette.panel1);
    get_color("palette", "panel2", theme_.palette.panel2);
    get_color("palette", "chrome_topbar0", theme_.palette.chrome_topbar0);
    get_color("palette", "chrome_topbar1", theme_.palette.chrome_topbar1);
    get_color("palette", "divider", theme_.palette.divider);
    get_color("palette", "primary", theme_.palette.text_primary);
    get_color("palette", "text_primary", theme_.palette.text_primary);
    get_color("palette", "secondary", theme_.palette.text_secondary);
    get_color("palette", "text_secondary", theme_.palette.text_secondary);
    get_color("palette", "text_muted", theme_.palette.text_muted);
    get_color("palette", "focus_fill", theme_.palette.focus_fill);
    get_color("palette", "focus_edge", theme_.palette.focus_edge);
    get_color("palette", "accent", theme_.palette.progress);
    get_color("palette", "progress", theme_.palette.progress);
    get_color("palette", "progress_strong", theme_.palette.progress_strong);
    get_color("palette", "good", theme_.palette.good);
    get_color("palette", "warn", theme_.palette.warn);
    get_color("palette", "danger", theme_.palette.bad);
    get_color("palette", "bad", theme_.palette.bad);

    if (auto val = findNestedStringField(content, "palette", "background");
        val && !findNestedStringField(content, "palette", "panel0")) {
        theme_.palette.panel0 = core::rgba(
            static_cast<std::uint8_t>(std::clamp(static_cast<int>(theme_.palette.background.r) + 5, 0, 255)),
            static_cast<std::uint8_t>(std::clamp(static_cast<int>(theme_.palette.background.g) + 6, 0, 255)),
            static_cast<std::uint8_t>(std::clamp(static_cast<int>(theme_.palette.background.b) + 7, 0, 255)));
        theme_.palette.panel2 = core::rgba(
            static_cast<std::uint8_t>(std::clamp(static_cast<int>(theme_.palette.background.r) + 18, 0, 255)),
            static_cast<std::uint8_t>(std::clamp(static_cast<int>(theme_.palette.background.g) + 21, 0, 255)),
            static_cast<std::uint8_t>(std::clamp(static_cast<int>(theme_.palette.background.b) + 25, 0, 255)));
    }

    const auto set_spacing = [&](std::string_view field, int& target) {
        if (const auto value = findNestedIntField(content, "spacing", field)) {
            target = std::max(1, *value);
        }
    };
    set_spacing("top_bar_height", theme_.spacing.top_bar_height);
    set_spacing("list_top", theme_.spacing.list_top);
    set_spacing("list_row_height", theme_.spacing.list_row_height);
    set_spacing("max_visible_rows", theme_.spacing.max_visible_rows);
    set_spacing("list_inset", theme_.spacing.list_inset);

    return true;
}

bool SkinPluginAdapter::loadSkinFromDir(const std::filesystem::path& plugin_dir)
{
    auto skin_path = plugin_dir / "skin.json";
    if (!std::filesystem::exists(skin_path)) {
        skin_path = plugin_dir / "plugin.json";
    }
    if (!std::filesystem::exists(skin_path)) {
        return false;
    }
    return loadSkinFromJson(skin_path);
}

} // namespace lofibox::plugins
