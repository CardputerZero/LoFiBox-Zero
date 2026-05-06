// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace lofibox::webui::json {

// === Low-level JSON building helpers (zero-dependency, ostringstream-based) ===
//
// These helpers do NOT prepend commas.  Callers are responsible for writing
// comma separators between fields.  The convention is:
//   - Write '{' to start an object.
//   - Write the first field without a leading comma.
//   - Before each subsequent field, call `separator(out)` to emit ','.
//   - Use openObject / closeObject / openArray / closeArray for nesting.

// Emit a comma separator between JSON fields.
void separator(std::ostringstream& out);

void appendString(std::ostringstream& out, std::string_view key, std::string_view value);
void appendBool(std::ostringstream& out, std::string_view key, bool value);
void appendInt(std::ostringstream& out, std::string_view key, int value);
void appendInt64(std::ostringstream& out, std::string_view key, std::int64_t value);
void appendDouble(std::ostringstream& out, std::string_view key, double value);

// Open a nested object: "key":{
void openObject(std::ostringstream& out, std::string_view key);
// Close a nested object
void closeObject(std::ostringstream& out);

// Open a nested array: "key":[
void openArray(std::ostringstream& out, std::string_view key);
// Close a nested array
void closeArray(std::ostringstream& out);

void appendStringArray(std::ostringstream& out, std::string_view key, const std::vector<std::string>& values);
void appendIntArray(std::ostringstream& out, std::string_view key, const std::vector<int>& values);

// Escape a string for JSON embedding
std::string escape(std::string_view text);

// === HTTP header building ===

std::string httpOk(std::string_view content_type, std::size_t content_length);
std::string httpBadRequest(std::string_view message);
std::string httpNotFound();
std::string httpMethodNotAllowed();
std::string httpServerError(std::string_view message);

} // namespace lofibox::webui::json
