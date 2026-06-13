#ifndef JMARKOVJUNIOR_GEN_CONSTS_H
#define JMARKOVJUNIOR_GEN_CONSTS_H

#include <cstdint>
#include <array>
#include <string_view>

namespace jmj
{
    constexpr uint8_t NGridValues = 16;

    inline constexpr std::array<std::array<float, 3>, NGridValues> GridColors = {{
        {0.00f, 0.00f, 0.00f},
        {0.50f, 0.50f, 0.50f},
        {1.00f, 1.00f, 1.00f},
        {1.00f, 0.00f, 0.00f},
        {0.00f, 1.00f, 0.00f},
        {0.00f, 0.00f, 1.00f},
        {1.00f, 1.00f, 0.00f},
        {1.00f, 0.00f, 1.00f},
        {0.00f, 1.00f, 1.00f},
        {1.00f, 0.50f, 0.00f},
        {1.00f, 0.00f, 0.50f},
        {0.00f, 0.50f, 0.20f},
        {0.00f, 0.20f, 0.50f},
        {0.50f, 0.20f, 0.00f},
        {1.00f, 0.90f, 0.80f},
        {0.70f, 0.85f, 1.00f}
	}};

    inline constexpr std::string_view GridChars = "bgwRGBYMTOPLINES";

    inline constexpr std::array<std::string_view, NGridValues> GridNames = {
        "Black", "Gray", "White",
        "Red", "Green", "Blue",
        "Yellow", "Magenta", "Teal",
        "Orange", "Pink", "Olive",
        "Indigo", "Brown",
        "Beige", "SkyBlue"
    };

    // A simple maze-generator algorithm that works in any number of dimensions; useful to verify your IPC code.
    inline constexpr std::string_view BasicMaze = "@markovjunior begin\n    @rewrite 1 b=>w\n    @rewrite wbb=>wgw\n        @rewrite g=>w\nend";
}

#endif
