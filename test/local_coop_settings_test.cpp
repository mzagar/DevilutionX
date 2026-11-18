#include <cstdio>
#include <filesystem>
#include <string>

#include <gtest/gtest.h>

#include "multi.h"
#include "options.h"
#include "utils/paths.h"

namespace devilution {
namespace {

class LocalCoopSettings : public ::testing::Test {
protected:
	void SetUp() override
	{
		dir_ = std::filesystem::temp_directory_path() / "devilutionx-local-coop-settings";
		std::filesystem::remove_all(dir_);
		std::filesystem::create_directories(dir_);
		paths::SetPrefPath(dir_.string() + "/");
	}

	void TearDown() override
	{
		gbLocalCoopSeats = 0;
		gbLocalCoopAutoStart = false;
		GetOptions().Gameplay.localCoopPlayers.SetValue(LocalCoopPlayers::One);
		std::filesystem::remove_all(dir_);
	}

	void WriteIni(const char *contents)
	{
		FILE *file = std::fopen((dir_ / "local_coop.ini").string().c_str(), "wb");
		ASSERT_NE(file, nullptr);
		std::fputs(contents, file);
		std::fclose(file);
	}

	std::filesystem::path dir_;
};

TEST_F(LocalCoopSettings, MenuOptionSelectsTwoSeats)
{
	GetOptions().Gameplay.localCoopPlayers.SetValue(LocalCoopPlayers::Two);
	LoadLocalCoopSettings();
	EXPECT_EQ(gbLocalCoopSeats, 2);
	GetOptions().Gameplay.localCoopPlayers.SetValue(LocalCoopPlayers::One);
}

TEST_F(LocalCoopSettings, MissingFileKeepsOneSeat)
{
	LoadLocalCoopSettings();
	EXPECT_EQ(gbLocalCoopSeats, 0);
}

TEST_F(LocalCoopSettings, AutoStartRequiresASession)
{
	WriteIni("[LocalCoop]\nSeats=2\nAutoStart=1\n");
	LoadLocalCoopSettings();
	EXPECT_EQ(gbLocalCoopSeats, 2);
	EXPECT_TRUE(gbLocalCoopAutoStart);
}

TEST_F(LocalCoopSettings, AutoStartIsIgnoredWithoutSeats)
{
	WriteIni("[LocalCoop]\nAutoStart=1\n");
	LoadLocalCoopSettings();
	EXPECT_EQ(gbLocalCoopSeats, 0);
	EXPECT_FALSE(gbLocalCoopAutoStart);
}

TEST_F(LocalCoopSettings, ReadsTwoSeats)
{
	WriteIni("[LocalCoop]\nSeats=2\n");
	LoadLocalCoopSettings();
	EXPECT_EQ(gbLocalCoopSeats, 2);
}

TEST_F(LocalCoopSettings, ReadsFourSeats)
{
	WriteIni("[LocalCoop]\nSeats=4\n");
	LoadLocalCoopSettings();
	EXPECT_EQ(gbLocalCoopSeats, 4);
}

TEST_F(LocalCoopSettings, RejectsOutOfRangeValues)
{
	WriteIni("[LocalCoop]\nSeats=1\n");
	LoadLocalCoopSettings();
	EXPECT_EQ(gbLocalCoopSeats, 0);

	WriteIni("[LocalCoop]\nSeats=5\n");
	LoadLocalCoopSettings();
	EXPECT_EQ(gbLocalCoopSeats, 0);

	WriteIni("[LocalCoop]\nSeats=banana\n");
	LoadLocalCoopSettings();
	EXPECT_EQ(gbLocalCoopSeats, 0);
}

} // namespace
} // namespace devilution
