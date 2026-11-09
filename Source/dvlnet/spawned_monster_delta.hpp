#pragma once

#include <cstddef>
#include <cstdint>

#include "utils/endian_read.hpp"
#include "utils/endian_write.hpp"

namespace devilution::net {

/**
 * @brief Delta state for a monster that was spawned during gameplay (golems, revived skeletons, ...).
 *
 * This is engine state, but its wire representation must not depend on the
 * local ABI. See `SpawnedMonsterWireSize` for the contract.
 */
struct SpawnedMonster {
	size_t typeIndex;
	uint32_t seed;
	uint8_t golemOwnerPlayerId;
	int16_t golemSpellLevel;
};

/** Size of a spawned-monster record on the wire (little-endian, fixed layout). */
constexpr size_t SpawnedMonsterWireSize = 16;
/** Size of one record as it appears in a delta: 16-bit monster id + payload. */
constexpr size_t SpawnedMonsterWireRecordSize = sizeof(uint16_t) + SpawnedMonsterWireSize;

/**
 * @brief Serializes the spawned-monster delta table.
 *
 * The record layout is defined explicitly here instead of being `memcpy`'d
 * from the native struct, because that struct used to be copied with
 * `sizeof(DSpawnedMonster)`: 16 bytes with `size_t typeIndex` on a 64-bit
 * little-endian host, but 12 bytes on a 32-bit big-endian target such as the
 * Wii U. Peers then disagreed about the record size and the byte order, which
 * corrupted late-join/resync data whenever a spawned monster was present.
 *
 * The layout below is byte-identical to what 64-bit little-endian builds
 * produced natively, so those peers keep interoperating:
 *
 *   offset  size  field
 *   0       8     typeIndex (LE; the upper 4 bytes are zero for real indices)
 *   8       4     seed (LE)
 *   12      1     golemOwnerPlayerId
 *   13      1     reserved, written as 0 and ignored when reading
 *   14      2     golemSpellLevel (LE)
 *
 * @param dst destination, must have room for `SpawnedMonsterWireRecordSize * monsters.size()`
 * @return pointer past the written data
 */
template <typename Map>
std::byte *ExportSpawnedMonsters(std::byte *dst, const Map &monsters)
{
	WriteLE16(dst, static_cast<uint16_t>(monsters.size()));
	dst += sizeof(uint16_t);

	for (const auto &[monsterId, monster] : monsters) {
		WriteLE16(dst, static_cast<uint16_t>(monsterId));
		dst += sizeof(uint16_t);
		WriteLE64(dst, static_cast<uint64_t>(monster.typeIndex));
		dst += 8;
		WriteLE32(dst, monster.seed);
		dst += 4;
		*dst++ = static_cast<std::byte>(monster.golemOwnerPlayerId);
		*dst++ = std::byte { 0 };
		WriteLE16(dst, static_cast<uint16_t>(monster.golemSpellLevel));
		dst += 2;
	}

	return dst;
}

/**
 * @brief Parses the spawned-monster delta table written by #ExportSpawnedMonsters.
 *
 * @param src start of the table
 * @param end end of the delta buffer
 * @param maxMonsters upper bound for the record count (untrusted input)
 * @param monsters output map, only appended to
 * @return pointer past the parsed data, or `nullptr` if the input is malformed
 */
template <typename Map>
const std::byte *ImportSpawnedMonsters(const std::byte *src, const std::byte *end, size_t maxMonsters, Map &monsters)
{
	if (src == nullptr || static_cast<size_t>(end - src) < sizeof(uint16_t))
		return nullptr;

	const uint16_t count = LoadLE16(src);
	src += sizeof(uint16_t);
	if (count > maxMonsters)
		return nullptr;

	if (static_cast<size_t>(end - src) < count * SpawnedMonsterWireRecordSize)
		return nullptr;

	for (uint16_t i = 0; i < count; i++) {
		const uint16_t monsterId = LoadLE16(src);
		src += sizeof(uint16_t);
		const uint64_t typeIndex = LoadLE64(src);
		src += 8;
		const uint32_t seed = LoadLE32(src);
		src += 4;
		const uint8_t golemOwnerPlayerId = static_cast<uint8_t>(*src++);
		src++; // reserved
		const int16_t golemSpellLevel = static_cast<int16_t>(LoadLE16(src));
		src += 2;

		// The target may be 32-bit; a peer must not be able to overflow size_t.
		if (typeIndex > static_cast<uint64_t>(SIZE_MAX))
			return nullptr;

		monsters.emplace(monsterId, SpawnedMonster { static_cast<size_t>(typeIndex), seed, golemOwnerPlayerId, golemSpellLevel });
	}

	return src;
}

} // namespace devilution::net
