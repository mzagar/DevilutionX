#include <cstdint>
#include <cstring>

#include <gtest/gtest.h>

#include "dvlnet/loopback.h"
#include "dvlnet/packet.h"
#include "multi.h"
#include "player.h"

namespace devilution::net {
namespace {

class LoopbackTransport : public ::testing::Test {
protected:
	void SetUp() override { Players.resize(2); }
	void TearDown() override { Players.clear(); }
};

TEST_F(LoopbackTransport, DeliversWithExplicitSender)
{
	loopback provider;
	ASSERT_EQ(provider.create("local"), 0);

	char fromSeat0[] = "seat0";
	char fromSeat1[] = "seat1";
	ASSERT_TRUE(provider.SNetSendMessageAs(0, 1, fromSeat0, sizeof(fromSeat0)));
	ASSERT_TRUE(provider.SNetSendMessageAs(1, 0, fromSeat1, sizeof(fromSeat1)));

	uint8_t sender = PLR_BROADCAST;
	void *data = nullptr;
	size_t size = 0;
	ASSERT_TRUE(provider.SNetReceiveMessage(&sender, &data, &size));
	EXPECT_EQ(sender, 0);
	EXPECT_EQ(size, sizeof(fromSeat0));
	EXPECT_EQ(std::memcmp(data, fromSeat0, sizeof(fromSeat0)), 0);

	ASSERT_TRUE(provider.SNetReceiveMessage(&sender, &data, &size));
	EXPECT_EQ(sender, 1);
	EXPECT_EQ(size, sizeof(fromSeat1));
	EXPECT_EQ(std::memcmp(data, fromSeat1, sizeof(fromSeat1)), 0);
}

TEST_F(LoopbackTransport, DefaultSendKeepsTheSinglePlayerIdentity)
{
	loopback provider;
	ASSERT_EQ(provider.create("local"), 0);

	char payload[] = "x";
	ASSERT_TRUE(provider.SNetSendMessage(0, payload, sizeof(payload)));

	uint8_t sender = PLR_BROADCAST;
	void *data = nullptr;
	size_t size = 0;
	ASSERT_TRUE(provider.SNetReceiveMessage(&sender, &data, &size));
	EXPECT_EQ(sender, 0);
	EXPECT_EQ(size, sizeof(payload));
}

TEST_F(LoopbackTransport, SinglePlayerSendStillDropsOtherDestinations)
{
	loopback provider;
	ASSERT_EQ(provider.create("local"), 0);

	char payload[] = "x";
	ASSERT_TRUE(provider.SNetSendMessage(1, payload, sizeof(payload)));

	uint8_t sender = PLR_BROADCAST;
	void *data = nullptr;
	size_t size = 0;
	EXPECT_FALSE(provider.SNetReceiveMessage(&sender, &data, &size));
}

} // namespace
} // namespace devilution::net
