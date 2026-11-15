#include "controls/seat_input.h"

namespace devilution {

namespace {
SeatInput instance;
} // namespace

SeatInput &SeatInput::Instance()
{
	return instance;
}

void SeatInput::OnButton(uint8_t seat, ControllerButton button, bool up)
{
	if (seat >= MaxSeats)
		return;
	if (button == ControllerButton_NONE || button == ControllerButton_IGNORE)
		return;
	if (static_cast<std::size_t>(button) >= ButtonCount)
		return;
	pressed_[seat][static_cast<std::size_t>(button)] = !up;
}

bool SeatInput::IsPressed(uint8_t seat, ControllerButton button) const
{
	if (seat >= MaxSeats || button == ControllerButton_NONE || button == ControllerButton_IGNORE)
		return false;
	if (static_cast<std::size_t>(button) >= ButtonCount)
		return false;
	return pressed_[seat][static_cast<std::size_t>(button)];
}

bool SeatInput::IsComboPressed(uint8_t seat, ControllerButtonCombo combo) const
{
	if (combo.button == ControllerButton_NONE || !IsPressed(seat, combo.button))
		return false;
	return combo.modifier == ControllerButton_NONE || IsPressed(seat, combo.modifier);
}

void SeatInput::Reset(uint8_t seat)
{
	if (seat >= MaxSeats)
		return;
	pressed_[seat].fill(false);
}

void SeatInput::ResetAll()
{
	for (auto &seat : pressed_)
		seat.fill(false);
}

} // namespace devilution