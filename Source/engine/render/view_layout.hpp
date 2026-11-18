#pragma once

#include <cstdint>
#include <vector>

#include "engine/rectangle.hpp"

namespace devilution {

/**
 * @brief Where each local seat's view goes in one presented frame.
 *
 * The shared main panel keeps its legacy 640-wide size and stays centred at the
 * bottom. The remaining viewport is split into equal cells: one view per seat
 * for one or two seats, a 2x2 grid for three or four. Cells never overlap and
 * always stay inside the viewport.
 */
struct ViewLayout {
	Size output;
	Size viewport;
	Rectangle panel;
	std::vector<Rectangle> cells;

	[[nodiscard]] const Rectangle &cellFor(uint8_t seat) const { return cells[seat]; }
};

/**
 * @param output        presented frame size (logical pixels)
 * @param seatCount     number of local seats (1..4)
 * @param panelHeight   height reserved for the shared main panel
 */
[[nodiscard]] ViewLayout CalculateViewLayout(Size output, uint8_t seatCount, int panelHeight);

} // namespace devilution
