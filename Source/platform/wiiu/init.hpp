#pragma once

/// Sets up the Wii U environment (paths, volumes). Called from main().
void wiiu_init();

/// Logs the resolved devilution paths. Called once the game has started.
void wiiu_log_paths();
