#pragma once

#ifdef USE_SDL3
#include <SDL3/SDL_events.h>
#else
#include <SDL.h>
#endif

#include <cstdint>

#include "controller_buttons.h"
#include "utils/static_vector.hpp"

namespace devilution {

// Must be called exactly once at the start of each SDL input event.
void UnlockControllerState(const SDL_Event &event);

StaticVector<ControllerButtonEvent, 4> ToControllerButtonEvents(const SDL_Event &event);

bool IsControllerButtonPressed(ControllerButton button);
bool IsControllerButtonComboPressed(ControllerButtonCombo combo);

// Seat-scoped variants: both the button and the modifier must belong to the same seat.
bool IsControllerButtonPressedOnSeat(uint8_t seat, ControllerButton button);
bool IsControllerButtonComboPressedOnSeat(uint8_t seat, ControllerButtonCombo combo);

bool HandleControllerAddedOrRemovedEvent(const SDL_Event &event);

// Resolves the local seat that owns an SDL event. Unknown devices and keyboard
// input map to seat 0, which keeps single-player behaviour unchanged.
uint8_t SeatForControllerEvent(const SDL_Event &event);

} // namespace devilution
