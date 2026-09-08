#pragma once

// Path globs for config `files` and `ignores` (docs/rules.md "Config").

#include <string_view>

namespace fastlint::lint {

/**
 * Matches `path` (forward slashes, relative to the config) against a glob
 * with `*` (within a segment), `?`, `**` (any number of segments) and `{a,b}`
 * alternatives. Dotfiles match `*` as they do in ESLint. With
 * `caseInsensitive`, ASCII letters match regardless of case, as a
 * case-insensitive filesystem compares paths (Windows).
 */
bool globMatch(std::string_view pattern,
               std::string_view path,
               bool caseInsensitive = false);

} // namespace fastlint::lint
