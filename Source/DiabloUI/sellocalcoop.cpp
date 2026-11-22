/**
 * @file sellocalcoop.cpp
 *
 * Local co-op lobby: select seat count and a hero per seat, then start.
 */
#include "DiabloUI/sellocalcoop.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "DiabloUI/diabloui.h"
#include "DiabloUI/hero/selhero.h"
#include "DiabloUI/selok.h"
#include "DiabloUI/selyesno.h"
#include "DiabloUI/ui_flags.hpp"
#include "DiabloUI/ui_item.h"
#include "control/control.hpp"
#include "engine/assets.hpp"
#include "engine/point.hpp"
#include "multi.h"
#include "options.h"
#include "pfile.h"
#include "pfile.h"
#include "utils/language.h"
#include "utils/sdl_geometry.h"
#include "utils/ui_fwd.h"

namespace devilution {

namespace {

bool selcoop_endMenu = false;
int selcoop_seats = 2;
uint32_t selcoop_saveNumbers[4] = {};

std::vector<std::unique_ptr<UiItemBase>> vecSelCoopDialog;
std::vector<std::unique_ptr<UiListItem>> vecSelCoopItems;

enum class ModeChoice : int {
	LocalCoop = 0,
	Online = 1,
};

MultiPlayerMode selcoop_modeResult;

void SelcoopModeSelect(size_t value)
{
	selcoop_modeResult = vecSelCoopItems[value]->m_value == static_cast<int>(ModeChoice::LocalCoop)
	    ? MultiPlayerMode::LocalCoop
	    : MultiPlayerMode::Online;
	selcoop_endMenu = true;
}

void SelcoopModeEsc()
{
	selcoop_modeResult = MultiPlayerMode::Cancelled;
	selcoop_endMenu = true;
}

void SelcoopFree()
{
	ArtBackground = std::nullopt;
	vecSelCoopDialog.clear();
	vecSelCoopItems.clear();
}

void SelcoopSelect(size_t value)
{
	if (value < vecSelCoopItems.size()) {
		selcoop_seats = vecSelCoopItems[value]->m_value;
	}
	selcoop_endMenu = true;
}

void SelcoopEsc()
{
	selcoop_seats = 0; // signals cancel
	selcoop_endMenu = true;
}

bool SelcoopSelectSeats()
{
	selcoop_endMenu = false;

	LoadBackgroundArt("ui_art\\selgame");
	UiAddBackground(&vecSelCoopDialog);
	UiAddLogo(&vecSelCoopDialog);

	const Point uiPosition = GetUIRectangle().position;

	SDL_Rect rect = MakeSdlRect(uiPosition.x + 24, uiPosition.y + 161, 590, 35);
	vecSelCoopDialog.push_back(std::make_unique<UiArtText>(
	    _("Local Co-op: Number of Players").data(), rect,
	    UiFlags::AlignCenter | UiFlags::FontSize30 | UiFlags::ColorUiSilver, 3));

	vecSelCoopItems.clear();
	vecSelCoopItems.push_back(std::make_unique<UiListItem>(_("2 Players"), 2));
	vecSelCoopItems.push_back(std::make_unique<UiListItem>(_("3 Players"), 3));
	vecSelCoopItems.push_back(std::make_unique<UiListItem>(_("4 Players"), 4));

	rect = MakeSdlRect(uiPosition.x + 265, uiPosition.y + 256, 320, 26);
	vecSelCoopDialog.push_back(std::make_unique<UiList>(
	    vecSelCoopItems, 3, uiPosition.x + 265, uiPosition.y + 256,
	    320, 26, UiFlags::AlignCenter | UiFlags::FontSize24 | UiFlags::ColorUiGold));

	SDL_Rect rectOk = MakeSdlRect(uiPosition.x + 239, uiPosition.y + 390, 120, 35);
	vecSelCoopDialog.push_back(std::make_unique<UiArtTextButton>(
	    _("OK"), &UiFocusNavigationSelect, rectOk,
	    UiFlags::AlignCenter | UiFlags::FontSize30 | UiFlags::ColorUiGold));

	SDL_Rect rectCancel = MakeSdlRect(uiPosition.x + 489, uiPosition.y + 390, 144, 35);
	vecSelCoopDialog.push_back(std::make_unique<UiArtTextButton>(
	    _("Cancel"), &UiFocusNavigationEsc, rectCancel,
	    UiFlags::AlignCenter | UiFlags::FontSize30 | UiFlags::ColorUiGold));

	UiInitList(nullptr, SelcoopSelect, SelcoopEsc, vecSelCoopDialog, true);

	while (!selcoop_endMenu && !UiItemMouseEvents(nullptr, vecSelCoopDialog)) {
		UiClearScreen();
		UiPollAndRender();
	}

	SelcoopFree();
	return selcoop_seats >= 2;
}

} // namespace

MultiPlayerMode UiMultiPlayerModeDialog()
{
	selcoop_modeResult = MultiPlayerMode::Cancelled;
	selcoop_endMenu = false;

	selcoop_seats = 2;
	LoadBackgroundArt("ui_art\\selconn");
	UiAddBackground(&vecSelCoopDialog);
	UiAddLogo(&vecSelCoopDialog);

	const Point uiPosition = GetUIRectangle().position;

	SDL_Rect rect = MakeSdlRect(uiPosition.x + 24, uiPosition.y + 161, 590, 35);
	vecSelCoopDialog.push_back(std::make_unique<UiArtText>(
	    _("Multi Player").data(), rect,
	    UiFlags::AlignCenter | UiFlags::FontSize30 | UiFlags::ColorUiSilver, 3));

	vecSelCoopItems.clear();
	vecSelCoopItems.push_back(std::make_unique<UiListItem>(_("Local Co-op"), static_cast<int>(ModeChoice::LocalCoop)));
	vecSelCoopItems.push_back(std::make_unique<UiListItem>(_("Online Multiplayer"), static_cast<int>(ModeChoice::Online)));

	vecSelCoopDialog.push_back(std::make_unique<UiList>(
	    vecSelCoopItems, 2, uiPosition.x + 265, uiPosition.y + 256,
	    320, 30, UiFlags::AlignCenter | UiFlags::FontSize24 | UiFlags::ColorUiGold));

	const SDL_Rect rectOk = MakeSdlRect(uiPosition.x + 239, uiPosition.y + 429, 120, 35);
	vecSelCoopDialog.push_back(std::make_unique<UiArtTextButton>(
	    _("OK"), &UiFocusNavigationSelect, rectOk,
	    UiFlags::AlignCenter | UiFlags::FontSize30 | UiFlags::ColorUiGold));

	const SDL_Rect rectCancel = MakeSdlRect(uiPosition.x + 489, uiPosition.y + 429, 144, 35);
	vecSelCoopDialog.push_back(std::make_unique<UiArtTextButton>(
	    _("Cancel"), &UiFocusNavigationEsc, rectCancel,
	    UiFlags::AlignCenter | UiFlags::FontSize30 | UiFlags::ColorUiGold));

	UiInitList(nullptr, SelcoopModeSelect, SelcoopModeEsc, vecSelCoopDialog, true);

	while (!selcoop_endMenu) {
		UiClearScreen();
		UiPollAndRender();
	}

	SelcoopFree();
	return selcoop_modeResult;
}

bool UiLocalCoopLobbyDialog()
{
	// Step 0: check for a saved co-op session to resume
	if (pfile_load_coop_session_descriptor()) {
		bool resumable = true;
		for (uint8_t seat = 0; seat < gbLocalCoopSeats; ++seat) {
			const uint32_t saveNum = gbLocalCoopSaveSlot[seat];
			if (!pfile_save_exists(saveNum))
				resumable = false;
		}
		if (!resumable) {
			UiSelOkDialog(_("Local Co-op").data(),
			    _("A saved co-op session was found, but a hero archive is missing. Starting a new session.").data(), false);
			pfile_clear_coop_session_descriptor();
		} else {
			const bool resume = UiSelHeroYesNoDialog(
			    _("Resume Co-op Game?").data(),
			    _("A saved local co-op session was found. Resume it?").data());
			if (resume) {
				// The descriptor has restored gbLocalCoopSeats and gbLocalCoopSaveSlot
				gbLocalCoopLobbySeats = gbLocalCoopSeats;
				LoadLocalCoopSettings();
				gbLocalCoopResume = true;
				return true;
			}
			// User chose to start fresh; clear the old descriptor
			pfile_clear_coop_session_descriptor();
		}
	}

	// Step 1: pick seat count
	if (!SelcoopSelectSeats())
		return false; // cancelled

	const int seats = selcoop_seats;

	// Step 2: pick a hero for each seat. Two seats must never load the same
	// archive; that would give two players one hero.
	for (int seat = 0; seat < seats; ++seat) {
		while (true) {
			_selhero_selections heroResult = SELHERO_NEW_DUNGEON;
			uint32_t saveNumber = 0;

			UiSelHeroMultDialog(
			    pfile_ui_set_hero_infos,  // fninfo
			    pfile_ui_save_create,     // fncreate
			    pfile_delete_save,        // fnremove
			    pfile_ui_set_class_stats, // fnstats
			    &heroResult, &saveNumber);

			if (heroResult == SELHERO_PREVIOUS || saveNumber == 0) {
				// User cancelled or didn't select a hero
				return false;
			}

			bool duplicate = false;
			for (int other = 0; other < seat; ++other) {
				if (selcoop_saveNumbers[other] == saveNumber)
					duplicate = true;
			}
			if (duplicate) {
				UiSelOkDialog(_("Local Co-op").data(),
				    _("That hero is already playing in this session. Choose a different hero.").data(), false);
				continue;
			}

			selcoop_saveNumbers[seat] = saveNumber;
			break;
		}
	}

	// Step 3: configure the session. This is a one-shot override: the Settings
	// option is left alone so a later Single Player start is single player.
	gbLocalCoopLobbySeats = static_cast<uint8_t>(seats);
	LoadLocalCoopSettings();
	gbLocalCoopSeats = static_cast<uint8_t>(seats);

	// Store the per-seat save slots so InitLocalCoop loads the right heroes
	for (int seat = 0; seat < seats; ++seat) {
		gbLocalCoopSaveSlot[seat] = selcoop_saveNumbers[seat];
	}

	// Clear any stale session descriptor (fresh session)
	pfile_clear_coop_session_descriptor();

	return true;
}

} // namespace devilution