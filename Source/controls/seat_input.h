#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "controls/controller_buttons.h"
#include "controls/input_seats.hpp"

namespace devilution {

/**
 * @brief Per-seat controller button state.
 *
 * Every local player owns one seat, and every seat owns its own pressed-button
 * set. Queries are explicit about the seat, so a modifier held on one pad can
 * never combine with a button on another pad.
 *
 * The global (seat-agnostic) queries stay for menu navigation, which legitimately
 * accepts any connected pad.
 */
class SeatInput {
public:
	static constexpr uint8_t MaxSeats = InputSeats::MaxSeats;
	static constexpr std::size_t ButtonCount = static_cast<std::size_t>(LAST) + 1;

	/// Stick axes, in the engine's sign convention (up/left are negative).
	enum SeatAxis : uint8_t {
		SeatAxisLeftX,
		SeatAxisLeftY,
		SeatAxisRightX,
		SeatAxisRightY,
		SeatAxisCount,
	};

	static SeatInput &Instance();

	void OnButton(uint8_t seat, ControllerButton button, bool up);
	/// Raw axis value in the engine's convention, before deadzone scaling.
	void OnAxis(uint8_t seat, SeatAxis axis, float value);

	[[nodiscard]] bool IsPressed(uint8_t seat, ControllerButton button) const;
	/// Both the button and the modifier must be pressed on the same seat.
	[[nodiscard]] bool IsComboPressed(uint8_t seat, ControllerButtonCombo combo) const;
	[[nodiscard]] float Axis(uint8_t seat, SeatAxis axis) const;

	/// Clears a seat, e.g. when its controller disconnects.
	void Reset(uint8_t seat);
	void ResetAll();

private:
	std::array<std::array<bool, ButtonCount>, MaxSeats> pressed_ {};
	std::array<std::array<float, SeatAxisCount>, MaxSeats> axes_ {};
};

} // namespace devilution