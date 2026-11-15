#pragma once

#include <array>
#include <cstdint>
#include <optional>

namespace devilution {

/**
 * @brief Assigns controller instance IDs to local player seats.
 *
 * This is the ownership boundary the split-screen input work needs: SDL reports
 * devices by instance ID (stable while connected), while gameplay actions belong
 * to a seat. Assignment happens on connect, before any motion or button state is
 * mutated, so routing decisions never depend on SDL enumeration order or on a
 * controller's name/GUID.
 *
 * A released seat keeps its generation counter; events carrying the old
 * instance ID are therefore never mistaken for the new occupant of that seat.
 */
class InputSeats {
public:
	static constexpr uint8_t NoSeat = 0xFF;
	static constexpr uint8_t MaxSeats = 4;

	static InputSeats &Instance();

	/// Assigns the lowest free seat. Returns NoSeat when all seats are taken.
	uint8_t Assign(uint32_t instanceId);
	void Release(uint32_t instanceId);

	[[nodiscard]] uint8_t SeatFor(uint32_t instanceId) const;
	[[nodiscard]] std::optional<uint32_t> DeviceFor(uint8_t seat) const;
	[[nodiscard]] uint32_t Generation(uint8_t seat) const;
	[[nodiscard]] uint8_t SeatCount() const;

	/// Test hook: forget every binding.
	void Reset();

private:
	struct Seat {
		uint32_t instanceId = 0;
		uint32_t generation = 0;
		bool used = false;
	};

	std::array<Seat, MaxSeats> seats_ {};
};

} // namespace devilution