/**
 * @file keyboard.cpp
 * @brief Wii U text input — skips system swkbd, uses SDL events directly.
 *
 * On Wii U, SDL_StartTextInput() launches the system swkbd which has
 * a broken confirm button on Cemu 2.6 macOS.
 *
 * This function simply enables SDL_TEXTINPUT events and returns immediately.
 * The game's standard event loop (in UiEdit) collects input and handles
 * Enter/Backspace — just like the PC version.
 */
#include "platform/wiiu/keyboard.hpp"

#include <SDL.h>

void wiiu_start_text_input(std::string_view, std::string_view, unsigned)
{
	// Enable text input events without launching the system swkbd.
	SDL_EventState(SDL_TEXTINPUT, SDL_ENABLE);
}