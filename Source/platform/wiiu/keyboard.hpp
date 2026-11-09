#pragma once

#include <string_view>

/**
 * @brief Arms text input for a `UiEdit` field on Wii U.
 *
 * The field itself (rendering, typing, backspace, confirmation) is handled by
 * the Diablo UI; this only enables SDL text events so an attached USB keyboard
 * types directly into it. No system on-screen keyboard is shown.
 *
 * @param guide_text Hint text for the field (unused: the UI renders it)
 * @param initial_text Current value (unused: the UI owns the buffer)
 * @param max_length Maximum bytes of input (unused: the UI enforces it)
 */
void wiiu_start_text_input(std::string_view guide_text, std::string_view initial_text, unsigned max_length);
