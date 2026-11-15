#include <gtest/gtest.h>

#include "controls/seat_input.h"

namespace devilution {
namespace {

class SeatInputTest : public ::testing::Test {
protected:
	void SetUp() override { SeatInput::Instance().ResetAll(); }
	void TearDown() override { SeatInput::Instance().ResetAll(); }
};

TEST_F(SeatInputTest, ButtonsAreTrackedPerSeat)
{
	SeatInput &input = SeatInput::Instance();
	input.OnButton(0, ControllerButton_BUTTON_A, /*up=*/false);
	input.OnButton(1, ControllerButton_BUTTON_B, /*up=*/false);

	EXPECT_TRUE(input.IsPressed(0, ControllerButton_BUTTON_A));
	EXPECT_FALSE(input.IsPressed(0, ControllerButton_BUTTON_B));
	EXPECT_FALSE(input.IsPressed(1, ControllerButton_BUTTON_A));
	EXPECT_TRUE(input.IsPressed(1, ControllerButton_BUTTON_B));
}

TEST_F(SeatInputTest, ModifierDoesNotCrossSeats)
{
	SeatInput &input = SeatInput::Instance();
	// Pad A holds the modifier and presses A. Pad B only presses A.
	input.OnButton(0, ControllerButton_BUTTON_LEFTSHOULDER, /*up=*/false);
	input.OnButton(0, ControllerButton_BUTTON_A, /*up=*/false);
	input.OnButton(1, ControllerButton_BUTTON_A, /*up=*/false);

	const ControllerButtonCombo combo { ControllerButton_BUTTON_LEFTSHOULDER, ControllerButton_BUTTON_A };
	EXPECT_TRUE(input.IsComboPressed(0, combo));
	EXPECT_FALSE(input.IsComboPressed(1, combo));
}

TEST_F(SeatInputTest, ReleasingOneSeatDoesNotAffectAnother)
{
	SeatInput &input = SeatInput::Instance();
	input.OnButton(0, ControllerButton_BUTTON_A, /*up=*/false);
	input.OnButton(1, ControllerButton_BUTTON_A, /*up=*/false);

	input.OnButton(0, ControllerButton_BUTTON_A, /*up=*/true);
	EXPECT_FALSE(input.IsPressed(0, ControllerButton_BUTTON_A));
	EXPECT_TRUE(input.IsPressed(1, ControllerButton_BUTTON_A));
}

TEST_F(SeatInputTest, ResetClearsOnlyThatSeat)
{
	SeatInput &input = SeatInput::Instance();
	input.OnButton(0, ControllerButton_BUTTON_A, /*up=*/false);
	input.OnButton(1, ControllerButton_BUTTON_A, /*up=*/false);

	input.Reset(0);
	EXPECT_FALSE(input.IsPressed(0, ControllerButton_BUTTON_A));
	EXPECT_TRUE(input.IsPressed(1, ControllerButton_BUTTON_A));
}

TEST_F(SeatInputTest, IgnoresInvalidSeatAndPlaceholderButtons)
{
	SeatInput &input = SeatInput::Instance();
	input.OnButton(InputSeats::NoSeat, ControllerButton_BUTTON_A, /*up=*/false);
	EXPECT_FALSE(input.IsPressed(InputSeats::NoSeat, ControllerButton_BUTTON_A));

	input.OnButton(0, ControllerButton_NONE, /*up=*/false);
	EXPECT_FALSE(input.IsPressed(0, ControllerButton_NONE));
}

TEST_F(SeatInputTest, AxesAreTrackedPerSeat)
{
	SeatInput &input = SeatInput::Instance();
	input.OnAxis(0, SeatInput::SeatAxisLeftX, 1000);
	input.OnAxis(1, SeatInput::SeatAxisLeftX, -1000);

	EXPECT_FLOAT_EQ(input.Axis(0, SeatInput::SeatAxisLeftX), 1000);
	EXPECT_FLOAT_EQ(input.Axis(1, SeatInput::SeatAxisLeftX), -1000);
	EXPECT_FLOAT_EQ(input.Axis(0, SeatInput::SeatAxisLeftY), 0);
}

TEST_F(SeatInputTest, ResetClearsAxesToo)
{
	SeatInput &input = SeatInput::Instance();
	input.OnAxis(0, SeatInput::SeatAxisRightY, 500);
	input.Reset(0);
	EXPECT_FLOAT_EQ(input.Axis(0, SeatInput::SeatAxisRightY), 0);
}

} // namespace
} // namespace devilution
