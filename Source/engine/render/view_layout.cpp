#include "engine/render/view_layout.hpp"

#include <algorithm>

namespace devilution {

namespace {

constexpr int MainPanelWidth = 640;

Rectangle MakeRect(int x, int y, int w, int h)
{
	return Rectangle { { x, y }, { w, h } };
}

} // namespace

ViewLayout CalculateViewLayout(Size output, uint8_t seatCount, int panelHeight)
{
	ViewLayout layout;
	layout.output = output;

	const int reservedPanelHeight = std::clamp(panelHeight, 0, output.height);
	const int panelWidth = std::min<int>(MainPanelWidth, output.width);
	layout.panel = MakeRect((output.width - panelWidth) / 2, output.height - reservedPanelHeight, panelWidth, reservedPanelHeight);
	layout.viewport = { output.width, output.height - reservedPanelHeight };

	const uint8_t cells = std::clamp<uint8_t>(seatCount, 1, 4);
	layout.cells.reserve(cells);

	if (cells <= 2) {
		for (uint8_t seat = 0; seat < cells; seat++) {
			const int x = seat * (layout.viewport.width / cells);
			const int w = (seat + 1 == cells) ? layout.viewport.width - x : layout.viewport.width / cells;
			layout.cells.push_back(MakeRect(x, 0, w, layout.viewport.height));
		}
		return layout;
	}

	// 2x2 grid; the third seat leaves the bottom-right cell unused.
	for (uint8_t seat = 0; seat < cells; seat++) {
		const int x = (seat % 2) * (layout.viewport.width / 2);
		const int y = (seat / 2) * (layout.viewport.height / 2);
		const int w = (seat % 2 == 1) ? layout.viewport.width - x : layout.viewport.width / 2;
		const int h = (seat / 2 == 1) ? layout.viewport.height - y : layout.viewport.height / 2;
		layout.cells.push_back(MakeRect(x, y, w, h));
	}
	return layout;
}

} // namespace devilution
