#include "controls/input_seats.hpp"

namespace devilution {

namespace {
InputSeats instance;
} // namespace

InputSeats &InputSeats::Instance()
{
	return instance;
}

uint8_t InputSeats::Assign(uint32_t instanceId)
{
	for (uint8_t seat = 0; seat < MaxSeats; seat++) {
		if (seats_[seat].used && seats_[seat].instanceId == instanceId)
			return seat;
	}
	for (uint8_t seat = 0; seat < MaxSeats; seat++) {
		if (seats_[seat].used)
			continue;
		seats_[seat].instanceId = instanceId;
		seats_[seat].used = true;
		return seat;
	}
	return NoSeat;
}

void InputSeats::Release(uint32_t instanceId)
{
	for (Seat &seat : seats_) {
		if (!seat.used || seat.instanceId != instanceId)
			continue;
		seat.used = false;
		seat.instanceId = 0;
		seat.generation++;
		return;
	}
}

uint8_t InputSeats::SeatFor(uint32_t instanceId) const
{
	for (uint8_t seat = 0; seat < MaxSeats; seat++) {
		if (seats_[seat].used && seats_[seat].instanceId == instanceId)
			return seat;
	}
	return NoSeat;
}

std::optional<uint32_t> InputSeats::DeviceFor(uint8_t seat) const
{
	if (seat >= MaxSeats || !seats_[seat].used)
		return std::nullopt;
	return seats_[seat].instanceId;
}

uint32_t InputSeats::Generation(uint8_t seat) const
{
	if (seat >= MaxSeats)
		return 0;
	return seats_[seat].generation;
}

uint8_t InputSeats::SeatCount() const
{
	uint8_t count = 0;
	for (const Seat &seat : seats_) {
		if (seat.used)
			count++;
	}
	return count;
}

void InputSeats::Reset()
{
	seats_ = {};
}

} // namespace devilution