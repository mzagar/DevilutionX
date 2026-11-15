#include <cstdint>

#include <gtest/gtest.h>

#include "msg.h"
#include "pack.h"

namespace devilution {
namespace {

// Bits in both halves, so a 16-bit decode keeps only 0x5678.
constexpr uint32_t FlagsWithBothHalvesSet = 0x12345678;

// The wire structs are packed, so their members are not necessarily aligned:
// write the little-endian bytes instead of binding a uint32_t reference.
template <typename T>
void WriteWireFlags(T &wireItem, uint32_t value)
{
	auto *field = reinterpret_cast<uint8_t *>(&wireItem.dwBuff);
	field[0] = static_cast<uint8_t>(value);
	field[1] = static_cast<uint8_t>(value >> 8);
	field[2] = static_cast<uint8_t>(value >> 16);
	field[3] = static_cast<uint8_t>(value >> 24);
}

} // namespace

TEST(ItemFlagsWire, DecodesTItemFlags)
{
	// The wire representation is little-endian: the reader must decode all 32 bits
	// regardless of the host byte order (this is the Wii U / PPC regression).
	TItem wireItem {};
	WriteWireFlags(wireItem, FlagsWithBothHalvesSet);

	EXPECT_EQ(ReadNetworkItemFlags(wireItem), FlagsWithBothHalvesSet);
}

TEST(ItemFlagsWire, DecodesItemPackFlags)
{
	ItemPack wireItem {};
	WriteWireFlags(wireItem, FlagsWithBothHalvesSet);

	EXPECT_EQ(ReadNetworkItemFlags(wireItem), FlagsWithBothHalvesSet);
}

TEST(ItemFlagsWire, PreservesHellfireFlagInTheHighHalf)
{
	// CF_HELLFIRE is a gameplay-relevant flag; the high half must not be dropped.
	constexpr uint32_t HellfireInHighHalf = static_cast<uint32_t>(CF_HELLFIRE) << 16;

	TItem wireItem {};
	WriteWireFlags(wireItem, HellfireInHighHalf);

	const uint32_t decoded = ReadNetworkItemFlags(wireItem);
	EXPECT_EQ(decoded, HellfireInHighHalf);
	EXPECT_EQ(decoded >> 16, static_cast<uint32_t>(CF_HELLFIRE));
}

} // namespace devilution