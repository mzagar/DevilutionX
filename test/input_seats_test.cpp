#include <gtest/gtest.h>

#include "controls/input_seats.hpp"

namespace devilution {
namespace {

class InputSeatsTest : public ::testing::Test {
protected:
	void SetUp() override { InputSeats::Instance().Reset(); }
	void TearDown() override { InputSeats::Instance().Reset(); }
};

TEST_F(InputSeatsTest, AssignsLowestFreeSeat)
{
	EXPECT_EQ(InputSeats::Instance().Assign(100), 0);
	EXPECT_EQ(InputSeats::Instance().Assign(200), 1);
	EXPECT_EQ(InputSeats::Instance().SeatFor(200), 1);
	EXPECT_EQ(InputSeats::Instance().SeatCount(), 2);
}

TEST_F(InputSeatsTest, ReassigningTheSameDeviceIsIdempotent)
{
	EXPECT_EQ(InputSeats::Instance().Assign(100), 0);
	EXPECT_EQ(InputSeats::Instance().Assign(100), 0);
	EXPECT_EQ(InputSeats::Instance().SeatCount(), 1);
}

TEST_F(InputSeatsTest, ReleaseBumpsGenerationAndFreesTheSeat)
{
	ASSERT_EQ(InputSeats::Instance().Assign(100), 0);
	const uint32_t generation = InputSeats::Instance().Generation(0);
	InputSeats::Instance().Release(100);

	EXPECT_EQ(InputSeats::Instance().Generation(0), generation + 1);
	EXPECT_EQ(InputSeats::Instance().SeatFor(100), InputSeats::NoSeat);
	EXPECT_FALSE(InputSeats::Instance().DeviceFor(0).has_value());

	// A new device takes the lowest free seat.
	EXPECT_EQ(InputSeats::Instance().Assign(300), 0);
	EXPECT_EQ(InputSeats::Instance().DeviceFor(0).value(), 300u);
}

TEST_F(InputSeatsTest, RefusesMoreThanFourDevices)
{
	for (uint32_t i = 0; i < InputSeats::MaxSeats; i++)
		EXPECT_EQ(InputSeats::Instance().Assign(1000 + i), i);
	EXPECT_EQ(InputSeats::Instance().Assign(9999), InputSeats::NoSeat);
}

} // namespace
} // namespace devilution
