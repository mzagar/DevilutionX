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

void SeatInput::OnAxis(uint8_t seat, SeatAxis axis, float value)
{
	if (seat >= MaxSeats || axis >= SeatAxisCount)
		return;
	axes_[seat][axis] = value;
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

float SeatInput::Axis(uint8_t seat, SeatAxis axis) const
{
	if (seat >= MaxSeats || axis >= SeatAxisCount)
		return 0;
	return axes_[seat][axis];
}

void SeatInput::Reset(uint8_t seat)
{
	if (seat >= MaxSeats)
		return;
	pressed_[seat].fill(false);
	axes_[seat].fill(0);
}

void SeatInput::ResetAll()
{
	for (auto &seat : pressed_)
		seat.fill(false);
	for (auto &seat : axes_)
		seat.fill(0);
}

} // namespace devilution