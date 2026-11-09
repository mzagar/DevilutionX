/**
 * @file keyboard.cpp
 * @brief Wii U text input — standard SDL keyboard events (no system swkbd).
 *
 * The Diablo UI already contains the whole text editor: the `UiEdit` item
 * renders the field, handles SDL_TEXTINPUT/SDL_KEYDOWN (letters, backspace,
 * arrows, Enter) and confirms the dialog. This function only has to keep SDL
 * text events enabled — exactly the PC path — so an attached USB keyboard
 * (read through `nsyskbd` by SDL's Wii U video backend) types straight into the
 * field.
 *
 * The system swkbd is deliberately not used:
 *  - It is a separate on-screen keyboard overlay, not the game's own dialog.
 *  - Cemu's swkbd HLE never hides itself on confirm and never clears its
 *    "OK pressed" flag, so SDL re-sends the confirmed string every frame.
 *
 * Cemu also does not emulate `nsyskbd` at all (`KBDSetup` is an unsupported lib
 * call), so in Cemu no SDL keyboard events are ever produced; use hardware or
 * the debug text injection documented in `docs/wiiu-progress.md` there.
 */
#include "platform/wiiu/keyboard.hpp"

#include <SDL.h>

#include <coreinit/debug.h>

void wiiu_start_text_input(std::string_view, std::string_view, unsigned)
{
	// Enable text input events without launching the system swkbd.
	SDL_EventState(SDL_TEXTINPUT, SDL_ENABLE);
	OSReport("WIIUKBD: text input armed (SDL events)\n");
}
