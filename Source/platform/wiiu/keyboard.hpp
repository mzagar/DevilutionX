#pragma once

#include <string_view>

/**
 * @brief Prompts the user for text input on Wii U.
 *
 * Uses the standard SDL keyboard event loop instead of the system swkbd
 * (which has a broken confirm button on Cemu).
 *
 * @param guide_text Hint text to display if the input is empty
 * @param initial_text Optional prefilled value
 * @param max_length Maximum bytes of input
 */
void wiiu_start_text_input(std::string_view guide_text, std::string_view initial_text, unsigned max_length);