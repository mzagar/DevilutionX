#include <gtest/gtest.h>

#include "engine/render/view_layout.hpp"

namespace devilution {
namespace {

bool Inside(const Rectangle &outer, const Rectangle &inner)
{
	return inner.position.x >= outer.position.x
	    && inner.position.y >= outer.position.y
	    && inner.position.x + inner.size.width <= outer.position.x + outer.size.width
	    && inner.position.y + inner.size.height <= outer.position.y + outer.size.height;
}

bool Overlaps(const Rectangle &a, const Rectangle &b)
{
	return a.position.x < b.position.x + b.size.width
	    && b.position.x < a.position.x + a.size.width
	    && a.position.y < b.position.y + b.size.height
	    && b.position.y < a.position.y + a.size.height;
}

void ExpectSaneLayout(Size output, uint8_t seats, int panelHeight)
{
	const ViewLayout layout = CalculateViewLayout(output, seats, panelHeight);
	ASSERT_EQ(layout.cells.size(), seats);
	EXPECT_TRUE(Inside(Rectangle { { 0, 0 }, output }, layout.panel));
	EXPECT_EQ(layout.panel.size.height, panelHeight);
	EXPECT_EQ(layout.viewport.height, output.height - panelHeight);

	for (size_t i = 0; i < layout.cells.size(); i++) {
		EXPECT_TRUE(Inside(Rectangle { { 0, 0 }, layout.viewport }, layout.cells[i])) << "seat " << i;
		EXPECT_GT(layout.cells[i].size.width, 0) << "seat " << i;
		EXPECT_GT(layout.cells[i].size.height, 0) << "seat " << i;
		for (size_t j = i + 1; j < layout.cells.size(); j++)
			EXPECT_FALSE(Overlaps(layout.cells[i], layout.cells[j])) << i << " vs " << j;
	}
}

TEST(ViewLayout, OneSeatFillsTheViewport)
{
	const ViewLayout layout = CalculateViewLayout({ 1280, 720 }, 1, 128);
	ASSERT_EQ(layout.cells.size(), 1);
	EXPECT_EQ(layout.cells[0].size.width, 1280);
	EXPECT_EQ(layout.cells[0].size.height, 592);
}

TEST(ViewLayout, TwoSeatsAreSideBySide)
{
	const ViewLayout layout = CalculateViewLayout({ 1280, 720 }, 2, 128);
	ASSERT_EQ(layout.cells.size(), 2);
	EXPECT_EQ(layout.cells[0].position.x, 0);
	EXPECT_EQ(layout.cells[1].position.x, 640);
	EXPECT_EQ(layout.cells[0].size.width, 640);
	EXPECT_EQ(layout.cells[1].size.width, 640);
	EXPECT_EQ(layout.cells[0].size.height, 592);
}

TEST(ViewLayout, FourSeatsFormAGrid)
{
	const ViewLayout layout = CalculateViewLayout({ 1280, 720 }, 4, 128);
	ASSERT_EQ(layout.cells.size(), 4);
	EXPECT_EQ(layout.cells[0].position, (Point { 0, 0 }));
	EXPECT_EQ(layout.cells[1].position, (Point { 640, 0 }));
	EXPECT_EQ(layout.cells[2].position, (Point { 0, 296 }));
	EXPECT_EQ(layout.cells[3].position, (Point { 640, 296 }));
	EXPECT_EQ(layout.cells[0].size, (Size { 640, 296 }));
}

TEST(ViewLayout, ThreeSeatsLeaveTheFourthCellUnused)
{
	const ViewLayout layout = CalculateViewLayout({ 1280, 720 }, 3, 128);
	ASSERT_EQ(layout.cells.size(), 3);
	EXPECT_EQ(layout.cells[2].position, (Point { 0, 296 }));
}

TEST(ViewLayout, LegacyResolutionStaysInBounds)
{
	ExpectSaneLayout({ 640, 480 }, 1, 128);
	ExpectSaneLayout({ 640, 480 }, 2, 128);
	ExpectSaneLayout({ 640, 480 }, 3, 128);
	ExpectSaneLayout({ 640, 480 }, 4, 128);
}

TEST(ViewLayout, WidescreenStaysInBounds)
{
	ExpectSaneLayout({ 1280, 720 }, 1, 128);
	ExpectSaneLayout({ 1280, 720 }, 2, 128);
	ExpectSaneLayout({ 1280, 720 }, 3, 128);
	ExpectSaneLayout({ 1280, 720 }, 4, 128);
}

TEST(ViewLayout, OddSizesRemainNonOverlapping)
{
	ExpectSaneLayout({ 1279, 719 }, 2, 127);
	ExpectSaneLayout({ 641, 481 }, 3, 127);
	ExpectSaneLayout({ 1919, 1080 }, 4, 128);
}

} // namespace
} // namespace devilution
