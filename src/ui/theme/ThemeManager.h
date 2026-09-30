// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/util/Error.h"
#include "core/util/Yaml.h"
#include "ui/theme/BoardTheme.h"

#include <filesystem>
#include <string>
#include <vector>

namespace pchess::ui {

// Board theme descriptors (design notes §19), one YAML file per theme:
//
//   name: Walnut            # required, unique
//   mode: color             # color | mono (default color)
//   light: "#e8dfc9"        # colors: "#rrggbb" or a gray level 0..255
//   dark: "#8f9b72"
//   selected / last_move / check / marker / coord_light / coord_dark / background / ink / frame
//   selected_pattern: outline  # tint | outline | hatch | dots | double_border
//   last_move_pattern / check_pattern
//   tint_markers: true
//
// `light_gray: 255` is accepted for any color key (the mono spelling of §19). Keys left out come
// from Paper (color) or ClassicMono (mono). Unknown keys and bad values are errors with the line.
Result<BoardTheme> parseTheme(const util::YamlNode& doc);
Result<BoardTheme> loadThemeFile(const std::filesystem::path& file);
std::string themeToYaml(const BoardTheme& theme);

// Built-in themes plus the YAML themes of assets/themes/ (D9: themes are swappable units).
// A malformed file never stops the app (§32): it is skipped and listed in problems().
class ThemeManager {
public:
    struct Problem {
        std::filesystem::path file;
        std::string message;
    };

    ThemeManager();

    // Loads every *.yaml of `dir` (sorted by file name); a missing directory is not an error.
    // Returns the number of themes added.
    int loadDirectory(const std::filesystem::path& dir);
    // Adds one theme; false (and a problem) when the name is already taken.
    bool add(BoardTheme theme, const std::filesystem::path& file = {});

    const std::vector<BoardTheme>& themes() const { return themes_; }
    std::vector<std::string> names(bool mono) const;
    const BoardTheme* find(const std::string& name) const;
    // The named theme when it exists and matches the mode, else the mode's default.
    BoardTheme resolve(const std::string& name, bool mono) const;
    const std::vector<Problem>& problems() const { return problems_; }

private:
    std::vector<BoardTheme> themes_;
    std::vector<Problem> problems_;
};

} // namespace pchess::ui
