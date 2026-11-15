#include "controls/controller.h"

#include <cmath>

#ifdef USE_SDL3
#include <SDL3/SDL_events.h>
#else
#include <SDL.h>
#endif

#ifndef USE_SDL1
#include "controls/devices/game_controller.h"
#endif
#include "controls/devices/joystick.h"
#include "controls/devices/kbcontroller.h"
#include "controls/input_seats.hpp"
#include "controls/seat_input.h"
#include "engine/demomode.h"
#include "utils/sdl_compat.h"

namespace devilution {

void UnlockControllerState(const SDL_Event &event)
{
#ifndef USE_SDL1
	GameController *const controller = GameController::Get(event);
	if (controller != nullptr) {
		controller->UnlockTriggerState();
	}
#endif
	Joystick *const joystick = Joystick::Get(event);
	if (joystick != nullptr) {
		joystick->UnlockHatState();
	}
}

namespace {

StaticVector<ControllerButtonEvent, 4> ToControllerButtonEventsImpl(const SDL_Event &event)
{
	ControllerButtonEvent result { ControllerButton_NONE, false };
	switch (event.type) {
	case SDL_EVENT_JOYSTICK_BUTTON_UP:
	case SDL_EVENT_KEY_UP:
#ifndef USE_SDL1
	case SDL_EVENT_GAMEPAD_BUTTON_UP:
#endif
		result.up = true;
		break;
	default:
		break;
	}
#if HAS_KBCTRL == 1
	if (!demo::IsRunning()) {
		result.button = KbCtrlToControllerButton(event);
		if (result.button != ControllerButton_NONE)
			return { result };
	}
#endif
#ifndef USE_SDL1
	GameController *const controller = GameController::Get(event);
	if (controller != nullptr) {
		result.button = controller->ToControllerButton(event);
		if (result.button != ControllerButton_NONE) {
			if (result.button == ControllerButton_AXIS_TRIGGERLEFT || result.button == ControllerButton_AXIS_TRIGGERRIGHT) {
				result.up = !controller->IsPressed(result.button);
			}
			return { result };
		}
	}
#endif

	const Joystick *joystick = Joystick::Get(event);
	if (joystick != nullptr) {
		return devilution::Joystick::ToControllerButtonEvents(event);
	}

	return { result };
}

} // namespace

uint8_t SeatForControllerEvent(const SDL_Event &event)
{
	const uint8_t noSeat = InputSeats::NoSeat;
#ifndef USE_SDL1
	if (const GameController *controller = GameController::Get(event); controller != nullptr) {
		const uint8_t seat = InputSeats::Instance().SeatFor(static_cast<uint32_t>(controller->instance_id()));
		return seat == noSeat ? 0 : seat;
	}
#endif
	if (const Joystick *joystick = Joystick::Get(event); joystick != nullptr) {
		const uint8_t seat = InputSeats::Instance().SeatFor(static_cast<uint32_t>(joystick->instance_id()));
		return seat == noSeat ? 0 : seat;
	}
	return 0; // Keyboard and virtual-pad input belong to the first seat.
}

StaticVector<ControllerButtonEvent, 4> ToControllerButtonEvents(const SDL_Event &event)
{
	StaticVector<ControllerButtonEvent, 4> events = ToControllerButtonEventsImpl(event);
	const uint8_t seat = SeatForControllerEvent(event);
	for (const ControllerButtonEvent &ctrlEvent : events)
		SeatInput::Instance().OnButton(seat, ctrlEvent.button, ctrlEvent.up);
	return events;
}

bool IsControllerButtonPressedOnSeat(uint8_t seat, ControllerButton button)
{
	return SeatInput::Instance().IsPressed(seat, button);
}

bool IsControllerButtonComboPressedOnSeat(uint8_t seat, ControllerButtonCombo combo)
{
	return SeatInput::Instance().IsComboPressed(seat, combo);
}

bool IsControllerButtonPressed(ControllerButton button)
{
#ifndef USE_SDL1
	if (GameController::IsPressedOnAnyController(button))
		return true;
#endif
#if HAS_KBCTRL == 1
	if (!demo::IsRunning() && IsKbCtrlButtonPressed(button))
		return true;
#endif
	return Joystick::IsPressedOnAnyJoystick(button);
}

bool IsControllerButtonComboPressed(ControllerButtonCombo combo)
{
	return IsControllerButtonPressed(combo.button)
	    && (combo.modifier == ControllerButton_NONE || IsControllerButtonPressed(combo.modifier));
}

bool HandleControllerAddedOrRemovedEvent(const SDL_Event &event)
{
#ifndef USE_SDL1
	switch (event.type) {
	case SDL_EVENT_GAMEPAD_ADDED:
		GameController::Add(SDLC_EventGamepadDevice(event).which);
		break;
	case SDL_EVENT_GAMEPAD_REMOVED:
		GameController::Remove(SDLC_EventGamepadDevice(event).which);
		break;
	case SDL_EVENT_JOYSTICK_ADDED:
		Joystick::Add(event.jdevice.which);
		break;
	case SDL_EVENT_JOYSTICK_REMOVED:
		Joystick::Remove(event.jdevice.which);
		break;
	default:
		return false;
	}
	return true;
#else
	return false;
#endif
}

} // namespace devilution
