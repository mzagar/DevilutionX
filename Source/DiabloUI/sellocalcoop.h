#pragma once

#include <cstdint>

namespace devilution {

bool UiLocalCoopLobbyDialog();

/// What "Multi Player" should do next.
enum class MultiPlayerMode : uint8_t {
	Cancelled,
	LocalCoop,
	Online,
};

/// "Multi Player" submenu: local co-op or the online transports.
MultiPlayerMode UiMultiPlayerModeDialog();

} // namespace devilution