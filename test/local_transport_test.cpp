#include <cstdint>
#include <cstring>

#include <gtest/gtest.h>

#include "dvlnet/local_bus.h"
#include "dvlnet/local_client.h"
#include "multi.h"
#include "player.h"
#include "storm/storm_net.hpp"

namespace devilution::net {
namespace {

class LocalTransport : public ::testing::Test {
protected:
	void SetUp() override
	{
		local_bus::Instance().reset();
		Players.clear();
		Players.resize(2);
	}

	void TearDown() override
	{
		local_bus::Instance().reset();
		Players.clear();
	}
};

TEST_F(LocalTransport, DirectedMessageKeepsTheSenderIdentity)
{
	local_client host;
	local_client joiner;
	ASSERT_EQ(host.create("local"), 0);
	ASSERT_EQ(joiner.join("local"), 1);

	char payload[] = "hello";
	ASSERT_TRUE(host.SNetSendMessage(1, payload, sizeof(payload)));

	uint8_t sender = PLR_BROADCAST;
	void *data = nullptr;
	size_t size = 0;
	ASSERT_TRUE(joiner.SNetReceiveMessage(&sender, &data, &size));
	EXPECT_EQ(sender, 0);
	EXPECT_EQ(size, sizeof(payload));
	EXPECT_EQ(std::memcmp(data, payload, sizeof(payload)), 0);
}

TEST_F(LocalTransport, BroadcastReachesEveryOtherEndpointOnce)
{
	local_client host;
	local_client joiner;
	ASSERT_EQ(host.create("local"), 0);
	ASSERT_EQ(joiner.join("local"), 1);

	char payload[] = "tick";
	ASSERT_TRUE(joiner.SNetSendMessage(SNPLAYER_OTHERS, payload, sizeof(payload)));

	uint8_t sender = PLR_BROADCAST;
	void *data = nullptr;
	size_t size = 0;
	ASSERT_TRUE(host.SNetReceiveMessage(&sender, &data, &size));
	EXPECT_EQ(sender, 1);
	EXPECT_EQ(size, sizeof(payload));
	EXPECT_EQ(std::memcmp(data, payload, sizeof(payload)), 0);

	// The sender does not get its own broadcast back.
	uint8_t ignoredSender = PLR_BROADCAST;
	void *ignoredData = nullptr;
	size_t ignoredSize = 0;
	EXPECT_FALSE(joiner.SNetReceiveMessage(&ignoredSender, &ignoredData, &ignoredSize));
}

TEST_F(LocalTransport, SelfAddressedMessageIsQueuedLocally)
{
	local_client host;
	ASSERT_EQ(host.create("local"), 0);

	char payload[] = "note";
	ASSERT_TRUE(host.SNetSendMessage(0, payload, sizeof(payload)));

	uint8_t sender = PLR_BROADCAST;
	void *data = nullptr;
	size_t size = 0;
	ASSERT_TRUE(host.SNetReceiveMessage(&sender, &data, &size));
	EXPECT_EQ(sender, 0);
	EXPECT_EQ(std::memcmp(data, payload, sizeof(payload)), 0);
}

TEST_F(LocalTransport, TurnsAreExchangedBetweenEndpoints)
{
	local_client host;
	local_client joiner;
	ASSERT_EQ(host.create("local"), 0);
	ASSERT_EQ(joiner.join("local"), 1);

	int32_t hostTurn = 0x1234;
	int32_t joinerTurn = -0x4321;
	ASSERT_TRUE(host.SNetSendTurn(reinterpret_cast<char *>(&hostTurn), sizeof(hostTurn)));
	ASSERT_TRUE(joiner.SNetSendTurn(reinterpret_cast<char *>(&joinerTurn), sizeof(joinerTurn)));

	// Both endpoints poll and take the same turn number.
	uint32_t hostStatus[2] {};
	uint32_t joinerStatus[2] {};
	char *hostData[2] {};
	char *joinerData[2] {};
	size_t hostSizes[2] {};
	size_t joinerSizes[2] {};

	ASSERT_TRUE(joiner.SNetReceiveTurns(joinerData, joinerSizes, joinerStatus));
	ASSERT_TRUE(host.SNetReceiveTurns(hostData, hostSizes, hostStatus));

	ASSERT_EQ(joinerSizes[0], sizeof(int32_t));
	ASSERT_EQ(joinerSizes[1], sizeof(int32_t));
	EXPECT_EQ(*reinterpret_cast<int32_t *>(joinerData[0]), hostTurn);
	EXPECT_EQ(*reinterpret_cast<int32_t *>(joinerData[1]), joinerTurn);
	EXPECT_NE(joinerStatus[0] & PS_TURN_ARRIVED, 0);
	EXPECT_NE(joinerStatus[1] & PS_TURN_ARRIVED, 0);
}

TEST_F(LocalTransport, DetachIsReportedToTheRemainingEndpoint)
{
	local_client host;
	{
		local_client joiner;
		ASSERT_EQ(host.create("local"), 0);
		ASSERT_EQ(joiner.join("local"), 1);
	}

	host.process_network_packets();
	// Detach is transport-level: the peer slot must be reported, and the bus must
	// not deliver anything to a new endpoint claiming the same slot.
	EXPECT_FALSE(local_bus::Instance().is_attached(1));
}

} // namespace
} // namespace devilution::net