#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "dvlnet/frame_queue.h"
#include "dvlnet/packet.h"
#include "dvlnet/spawned_monster_delta.hpp"

namespace devilution::net {

namespace {

buffer_t Bytes(std::initializer_list<unsigned char> bytes)
{
	return buffer_t(bytes);
}

/// \`packet_out::create\` specializations are keyed on the exact argument types,
/// so player ids must be \`plr_t\`, not \`int\`.
constexpr plr_t P(int value)
{
	return static_cast<plr_t>(value);
}

} // namespace

// ---------------------------------------------------------------------------
// packet codec
// ---------------------------------------------------------------------------

TEST(DvlNetPacket, TurnGoldenVector)
{
	packet_factory factory;
	auto pkt = factory.make_packet<PT_TURN>(P(1), PLR_BROADCAST, turn_t { 0x7F, 0x12345678 });
	ASSERT_TRUE(pkt.has_value()) << pkt.error().what();
	EXPECT_EQ((*pkt)->Type(), PT_TURN);
	EXPECT_EQ((*pkt)->Source(), 1);
	EXPECT_EQ((*pkt)->Destination(), PLR_BROADCAST);
	EXPECT_EQ((*pkt)->Data(), Bytes({ 0x02, 0x01, 0xFF, 0x7F, 0x78, 0x56, 0x34, 0x12 }));

	const auto frame = frame_queue::MakeFrame((*pkt)->Data());
	ASSERT_TRUE(frame.has_value());
	EXPECT_EQ(*frame, Bytes({ 0x08, 0x00, 0x00, 0x00, 0x02, 0x01, 0xFF, 0x7F, 0x78, 0x56, 0x34, 0x12 }));
}

TEST(DvlNetPacket, MessageRoundTrip)
{
	packet_factory factory;
	const buffer_t payload { 'h', 'e', 'l', 'l', 'o' };
	auto pkt = factory.make_packet<PT_MESSAGE>(P(2), P(3), payload);
	ASSERT_TRUE(pkt.has_value()) << pkt.error().what();

	EXPECT_EQ((*pkt)->Data(), Bytes({ 0x01, 0x02, 0x03, 'h', 'e', 'l', 'l', 'o' }));

	auto parsed = factory.make_packet((*pkt)->Data());
	ASSERT_TRUE(parsed.has_value()) << parsed.error().what();
	EXPECT_EQ((*parsed)->Type(), PT_MESSAGE);
	EXPECT_EQ((*parsed)->Source(), 2);
	EXPECT_EQ((*parsed)->Destination(), 3);
	const auto message = (*parsed)->Message();
	ASSERT_TRUE(message.has_value());
	EXPECT_EQ(**message, payload);
}

TEST(DvlNetPacket, JoinAcceptRoundTrip)
{
	packet_factory factory;
	const buffer_t info { 1, 2, 3, 4 };
	auto pkt = factory.make_packet<PT_JOIN_ACCEPT>(PLR_MASTER, PLR_BROADCAST, 0xDEADBEEF, P(2), info);
	ASSERT_TRUE(pkt.has_value()) << pkt.error().what();
	EXPECT_EQ((*pkt)->Data(), Bytes({ 0x12, 0xFE, 0xFF, 0xEF, 0xBE, 0xAD, 0xDE, 0x02, 1, 2, 3, 4 }));

	auto parsed = factory.make_packet((*pkt)->Data());
	ASSERT_TRUE(parsed.has_value()) << parsed.error().what();
	const auto cookie = (*parsed)->Cookie();
	ASSERT_TRUE(cookie.has_value());
	EXPECT_EQ(*cookie, 0xDEADBEEF);
	const auto newPlayer = (*parsed)->NewPlayer();
	ASSERT_TRUE(newPlayer.has_value());
	EXPECT_EQ(*newPlayer, 2);
	const auto parsedInfo = (*parsed)->Info();
	ASSERT_TRUE(parsedInfo.has_value());
	EXPECT_EQ(**parsedInfo, info);
}

TEST(DvlNetPacket, DisconnectRoundTrip)
{
	packet_factory factory;
	auto pkt = factory.make_packet<PT_DISCONNECT>(P(0), PLR_BROADCAST, P(1), leaveinfo_t::LEAVE_DROP);
	ASSERT_TRUE(pkt.has_value()) << pkt.error().what();

	auto parsed = factory.make_packet((*pkt)->Data());
	ASSERT_TRUE(parsed.has_value()) << parsed.error().what();
	const auto leaveInfo = (*parsed)->LeaveInfo();
	ASSERT_TRUE(leaveInfo.has_value());
	EXPECT_EQ(*leaveInfo, leaveinfo_t::LEAVE_DROP);
}

TEST(DvlNetPacket, EchoRoundTrip)
{
	packet_factory factory;
	auto pkt = factory.make_packet<PT_ECHO_REPLY>(P(1), P(0), static_cast<timestamp_t>(0x01020304));
	ASSERT_TRUE(pkt.has_value()) << pkt.error().what();
	EXPECT_EQ((*pkt)->Data(), Bytes({ 0x32, 0x01, 0x00, 0x04, 0x03, 0x02, 0x01 }));

	auto parsed = factory.make_packet((*pkt)->Data());
	ASSERT_TRUE(parsed.has_value()) << parsed.error().what();
	const auto time = (*parsed)->Time();
	ASSERT_TRUE(time.has_value());
	EXPECT_EQ(*time, 0x01020304u);
}

TEST(DvlNetPacket, RejectsTruncatedInput)
{
	packet_factory factory;
	EXPECT_FALSE(factory.make_packet(Bytes({})).has_value());
	EXPECT_FALSE(factory.make_packet(Bytes({ 0x02 })).has_value());
	EXPECT_FALSE(factory.make_packet(Bytes({ 0x02, 0x01 })).has_value());
	// Turn without its 4-byte value.
	EXPECT_FALSE(factory.make_packet(Bytes({ 0x02, 0x01, 0xFF, 0x7F })).has_value());
	// Join request without cookie.
	EXPECT_FALSE(factory.make_packet(Bytes({ 0x11, 0x00, 0xFE })).has_value());
}

TEST(DvlNetPacket, RejectsUnknownType)
{
	packet_factory factory;
	const auto error = factory.make_packet(Bytes({ 0x7F, 0x00, 0x01, 0x00 }));
	ASSERT_FALSE(error.has_value());
	EXPECT_NE(std::string(error.error().what()).find("Unknown packet type"), std::string::npos);
}

#ifdef PACKET_ENCRYPTION

TEST(DvlNetPacketEncryption, RoundTripsWithMatchingPassword)
{
	packet_factory sender("secret");
	packet_factory receiver("secret");

	auto pkt = sender.make_packet<PT_TURN>(P(1), P(2), turn_t { 5, 42 });
	ASSERT_TRUE(pkt.has_value()) << pkt.error().what();

	// Nonce + MAC + the 8 plaintext bytes
	EXPECT_EQ((*pkt)->Data().size(), crypto_secretbox_NONCEBYTES + crypto_secretbox_MACBYTES + 8);

	auto parsed = receiver.make_packet((*pkt)->Data());
	ASSERT_TRUE(parsed.has_value()) << parsed.error().what();
	const auto turn = (*parsed)->Turn();
	ASSERT_TRUE(turn.has_value());
	EXPECT_EQ(turn->SequenceNumber, 5);
	EXPECT_EQ(turn->Value, 42);
}

TEST(DvlNetPacketEncryption, RejectsWrongPassword)
{
	packet_factory sender("secret");
	packet_factory receiver("wrong");

	auto pkt = sender.make_packet<PT_TURN>(P(1), P(2), turn_t { 5, 42 });
	ASSERT_TRUE(pkt.has_value()) << pkt.error().what();

	const auto parsed = receiver.make_packet((*pkt)->Data());
	ASSERT_FALSE(parsed.has_value());
	EXPECT_EQ(parsed.error().code(), PacketError::ErrorCode::DecryptionFailed);
}

TEST(DvlNetPacketEncryption, RejectsModifiedCiphertext)
{
	packet_factory sender("secret");
	packet_factory receiver("secret");

	auto pkt = sender.make_packet<PT_TURN>(P(1), P(2), turn_t { 5, 42 });
	ASSERT_TRUE(pkt.has_value()) << pkt.error().what();

	buffer_t modified = (*pkt)->Data();
	modified[crypto_secretbox_NONCEBYTES + 1] ^= 0x01;

	const auto parsed = receiver.make_packet(modified);
	ASSERT_FALSE(parsed.has_value());
	EXPECT_EQ(parsed.error().code(), PacketError::ErrorCode::DecryptionFailed);
}

#endif // PACKET_ENCRYPTION

// ---------------------------------------------------------------------------
// frame codec
// ---------------------------------------------------------------------------

TEST(DvlNetFrame, SplitsOnByteByByteInput)
{
	const buffer_t packet { 0x02, 0x01, 0xFF, 0x7F, 0x78, 0x56, 0x34, 0x12 };
	const auto frame = frame_queue::MakeFrame(packet);
	ASSERT_TRUE(frame.has_value());

	frame_queue queue;
	for (size_t i = 0; i < frame->size(); i++) {
		queue.Write(buffer_t { (*frame)[i] });
		const auto ready = queue.PacketReady();
		ASSERT_TRUE(ready.has_value());
		if (i + 1 < frame->size()) {
			EXPECT_FALSE(*ready) << "frame reported ready after " << (i + 1) << " bytes";
		} else {
			EXPECT_TRUE(*ready);
			const auto read = queue.ReadPacket();
			ASSERT_TRUE(read.has_value());
			EXPECT_EQ(*read, packet);
		}
	}
}

TEST(DvlNetFrame, ReadsMultipleFramesFromOneBuffer)
{
	const buffer_t first { 0x02, 0x01, 0xFF, 0x7F, 0x78, 0x56, 0x34, 0x12 };
	const buffer_t second { 0x01, 0x00, 0xFF, 'a', 'b' };

	buffer_t stream;
	for (const buffer_t &packet : { first, second }) {
		const auto frame = frame_queue::MakeFrame(packet);
		ASSERT_TRUE(frame.has_value());
		stream.insert(stream.end(), frame->begin(), frame->end());
	}

	frame_queue queue;
	queue.Write(stream);

	for (const buffer_t &expected : { first, second }) {
		const auto ready = queue.PacketReady();
		ASSERT_TRUE(ready.has_value());
		ASSERT_TRUE(*ready);
		EXPECT_EQ(queue.ReadPacketFlags(), 0);
		const auto read = queue.ReadPacket();
		ASSERT_TRUE(read.has_value());
		EXPECT_EQ(*read, expected);
	}

	const auto ready = queue.PacketReady();
	ASSERT_TRUE(ready.has_value());
	EXPECT_FALSE(*ready);
}

TEST(DvlNetFrame, PreservesFlags)
{
	const buffer_t packet { 0x01 };
	const auto frame = frame_queue::MakeFrame(packet, 0x8000);
	ASSERT_TRUE(frame.has_value());
	// size (1) | flags (0x8000) << 16, little-endian
	EXPECT_EQ(*frame, Bytes({ 0x01, 0x00, 0x00, 0x80, 0x01 }));

	frame_queue queue;
	queue.Write(*frame);
	const auto ready = queue.PacketReady();
	ASSERT_TRUE(ready.has_value());
	ASSERT_TRUE(*ready);
	EXPECT_EQ(queue.ReadPacketFlags(), 0x8000);
	const auto read = queue.ReadPacket();
	ASSERT_TRUE(read.has_value());
	EXPECT_EQ(*read, packet);
}

TEST(DvlNetFrame, RejectsZeroSizedFrame)
{
	frame_queue queue;
	queue.Write(Bytes({ 0x00, 0x00, 0x00, 0x00 }));
	const auto ready = queue.PacketReady();
	ASSERT_FALSE(ready.has_value());
}

TEST(DvlNetFrame, RejectsOversizedFrame)
{
	EXPECT_FALSE(frame_queue::MakeFrame(buffer_t(frame_queue::max_frame_size + 1)).has_value());
	EXPECT_TRUE(frame_queue::MakeFrame(buffer_t(frame_queue::max_frame_size)).has_value());
}

TEST(DvlNetFrame, WaitsForCompleteFrame)
{
	const buffer_t packet(100, 0x42);
	const auto frame = frame_queue::MakeFrame(packet);
	ASSERT_TRUE(frame.has_value());

	frame_queue queue;
	queue.Write(buffer_t(frame->begin(), frame->end() - 1));
	const auto ready = queue.PacketReady();
	ASSERT_TRUE(ready.has_value());
	EXPECT_FALSE(*ready);

	queue.Write(buffer_t { frame->back() });
	const auto readyAfter = queue.PacketReady();
	ASSERT_TRUE(readyAfter.has_value());
	EXPECT_TRUE(*readyAfter);
}

// ---------------------------------------------------------------------------
// spawned-monster delta codec (the 32-bit BE vs. 64-bit LE blocker)
// ---------------------------------------------------------------------------

TEST(SpawnedMonsterDelta, GoldenVector)
{
	const std::map<size_t, SpawnedMonster> monsters {
		{ 0x0102, SpawnedMonster { 0x1234, 0xDEADBEEF, 2, 300 } },
	};

	std::byte buffer[sizeof(uint16_t) + SpawnedMonsterWireRecordSize * 4] {};
	std::byte *const end = ExportSpawnedMonsters(buffer, monsters);
	// one 16-bit count followed by the records
	ASSERT_EQ(end - buffer, static_cast<ptrdiff_t>(sizeof(uint16_t) + SpawnedMonsterWireRecordSize));

	const buffer_t expected {
		0x01, 0x00, // count
		0x02, 0x01, // monster id, LE
		0x34, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // typeIndex, LE (upper half zero)
		0xEF, 0xBE, 0xAD, 0xDE, // seed, LE
		0x02, // owner
		0x00, // reserved
		0x2C, 0x01, // spell level, LE
	};
	ASSERT_EQ(expected.size(), sizeof(uint16_t) + SpawnedMonsterWireRecordSize);
	EXPECT_EQ(std::memcmp(buffer, expected.data(), expected.size()), 0);
}

TEST(SpawnedMonsterDelta, RoundTrip)
{
	const std::map<size_t, SpawnedMonster> monsters {
		{ 0, SpawnedMonster { 0, 1, 0, 0 } },
		{ 1, SpawnedMonster { 5, 0x12345678, 1, 7 } },
		{ 127, SpawnedMonster { 999, 0x80000000, 3, -1 } },
	};

	std::byte buffer[sizeof(uint16_t) + SpawnedMonsterWireRecordSize * 4] {};
	std::byte *const end = ExportSpawnedMonsters(buffer, monsters);
	ASSERT_EQ(end - buffer, static_cast<ptrdiff_t>(sizeof(uint16_t) + SpawnedMonsterWireRecordSize * monsters.size()));

	std::map<size_t, SpawnedMonster> parsed;
	const std::byte *const consumed = ImportSpawnedMonsters(buffer, end, 128, parsed);
	ASSERT_NE(consumed, nullptr);
	EXPECT_EQ(consumed, end);
	ASSERT_EQ(parsed.size(), monsters.size());
	for (const auto &[id, monster] : monsters) {
		const auto it = parsed.find(id);
		ASSERT_NE(it, parsed.end());
		EXPECT_EQ(it->second.typeIndex, monster.typeIndex);
		EXPECT_EQ(it->second.seed, monster.seed);
		EXPECT_EQ(it->second.golemOwnerPlayerId, monster.golemOwnerPlayerId);
		EXPECT_EQ(it->second.golemSpellLevel, monster.golemSpellLevel);
	}
}

TEST(SpawnedMonsterDelta, EmptyTable)
{
	std::byte buffer[2];
	std::byte *const end = ExportSpawnedMonsters(buffer, std::map<size_t, SpawnedMonster> {});
	ASSERT_EQ(end - buffer, 2);
	EXPECT_EQ(buffer[0], std::byte { 0 });
	EXPECT_EQ(buffer[1], std::byte { 0 });

	std::map<size_t, SpawnedMonster> parsed;
	EXPECT_EQ(ImportSpawnedMonsters(buffer, end, 128, parsed), end);
	EXPECT_TRUE(parsed.empty());
}

TEST(SpawnedMonsterDelta, RejectsMalformedInput)
{
	std::map<size_t, SpawnedMonster> parsed;
	std::byte buffer[sizeof(uint16_t) + SpawnedMonsterWireRecordSize] {};
	const std::byte *const end = buffer + sizeof(buffer);

	// No room for the count.
	EXPECT_EQ(ImportSpawnedMonsters(buffer, buffer + 1, 128, parsed), nullptr);
	EXPECT_EQ(ImportSpawnedMonsters(nullptr, end, 128, parsed), nullptr);

	// Count larger than the allowed maximum.
	std::byte tooMany[2] { std::byte { 2 }, std::byte { 0 } };
	EXPECT_EQ(ImportSpawnedMonsters(tooMany, tooMany + 2, 1, parsed), nullptr);

	// Truncated record: the count says one record, but no payload follows.
	std::byte truncated[sizeof(uint16_t) + SpawnedMonsterWireRecordSize] { std::byte { 1 }, std::byte { 0 } };
	EXPECT_EQ(ImportSpawnedMonsters(truncated, truncated + 2, 128, parsed), nullptr);
	EXPECT_EQ(ImportSpawnedMonsters(truncated, truncated + 10, 128, parsed), nullptr);
	EXPECT_TRUE(parsed.empty());
}

TEST(SpawnedMonsterDelta, WireSizeIsIndependentOfTheAbi)
{
	// Regression guard: the wire record must not be sizeof(SpawnedMonster),
	// which is 16 on LP64/LE and 12 on the 32-bit big-endian Wii U ABI.
	EXPECT_EQ(SpawnedMonsterWireSize, 16u);
	EXPECT_EQ(SpawnedMonsterWireRecordSize, 18u);
}

} // namespace devilution::net
