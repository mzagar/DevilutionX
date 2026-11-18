/**
 * @file pfile.h
 *
 * Interface of the save game encoding functionality.
 */
#pragma once

#include <cstdint>
#include <expected>
#include <string>

#include "DiabloUI/diabloui.h"
#include "player.h"

#ifdef UNPACKED_SAVES
#include "utils/file_util.h"
#else
#include "mpq/mpq_reader.hpp"
#include "mpq/mpq_writer.hpp"
#endif

namespace devilution {

#define MAX_CHARACTERS 99

extern bool gbValidSaveFile;

#ifdef UNPACKED_SAVES
struct SaveReader {
	explicit SaveReader(std::string &&dir)
	    : dir_(std::move(dir))
	{
	}

	const std::string &dir() const
	{
		return dir_;
	}

	std::unique_ptr<std::byte[]> ReadFile(const char *filename, std::size_t &fileSize, int32_t &error);

	bool HasFile(const char *path)
	{
		return ::devilution::FileExists((dir_ + path).c_str());
	}

private:
	std::string dir_;
};

struct SaveWriter {
	explicit SaveWriter(std::string &&dir)
	    : dir_(std::move(dir))
	{
	}

	bool WriteFile(const char *filename, const std::byte *data, size_t size);

	bool HasFile(const char *path)
	{
		return ::devilution::FileExists((dir_ + path).c_str());
	}

	void RenameFile(const char *from, const char *to)
	{
		::devilution::RenameFile((dir_ + from).c_str(), (dir_ + to).c_str());
	}

	void RemoveHashEntry(const char *path)
	{
		RemoveFile((dir_ + path).c_str());
	}

	void RemoveHashEntries(bool (*fnGetName)(uint8_t, char *));

private:
	std::string dir_;
};

#else
using SaveReader = MpqArchive;
using SaveWriter = MpqWriter;
#endif

/**
 * @brief Comparison result of pfile_compare_hero_demo
 */
struct HeroCompareResult {
	enum Status : uint8_t {
		ReferenceNotFound,
		Same,
		Difference,
	};
	Status status;
	std::string message;
};

std::optional<SaveReader> OpenSaveArchive(uint32_t saveNum);
std::optional<SaveReader> OpenStashArchive();
const char *pfile_get_password();
std::unique_ptr<std::byte[]> ReadArchive(SaveReader &archive, const char *pszName, size_t *pdwLen = nullptr);
void pfile_write_hero(bool writeGameData = false);
// Saves every active local co-op seat's hero to its own save slot.
// Game data (world) is saved once with seat 0's save when writeGameData is true.
void pfile_write_all_heroes(bool writeGameData = false);

// Co-op session descriptor: persists the seat-to-save mapping across sessions.
// Written on save, read by the lobby to offer resume.
bool pfile_save_coop_session_descriptor();
bool pfile_load_coop_session_descriptor();
void pfile_clear_coop_session_descriptor();

#ifndef DISABLE_DEMOMODE
/**
 * @brief Save a reference game-state (save game) for the demo recording
 * @param demo that is recorded
 */
void pfile_write_hero_demo(int demo);
/**
 * @brief Compares the actual game-state (savegame) with a reference game-state (save game from demo recording)
 * @param demo for the comparison
 * @param logDetails in case of a difference log details
 * @return The comparison result.
 */
HeroCompareResult pfile_compare_hero_demo(int demo, bool logDetails);
#endif

void sfile_write_stash();
bool pfile_ui_set_hero_infos(bool (*uiAddHeroInfo)(_uiheroinfo *));
void pfile_ui_set_class_stats(HeroClass playerClass, _uidefaultstats *classStats);
uint32_t pfile_ui_get_first_unused_save_num();
bool pfile_ui_save_create(_uiheroinfo *heroinfo);
bool pfile_delete_save(_uiheroinfo *heroInfo);
void pfile_read_player_from_save(uint32_t saveNum, Player &player);
void pfile_save_level();
std::expected<void, std::string> pfile_convert_levels();
void pfile_remove_temp_files();
std::unique_ptr<std::byte[]> pfile_read(const char *pszName, size_t *pdwLen);
void pfile_update(bool forceSave);

} // namespace devilution
