#include <string_view>

#ifdef __WIIU__
#include <coreinit/debug.h>
#endif

#define ErrAsio(message) devilution::ErrDlg("ASIO Error", message, __FILE__, __LINE__)

namespace devilution {

extern void ErrDlg(const char* title, std::string_view error, std::string_view logFilePath, int logLineNr);

} // namespace devilution

namespace asio::detail {

void fatal_exception(const char* message)
{
#ifdef __WIIU__
	// ErrDlg needs the UI, which does not exist yet while wiiu_init() is
	// bringing the network up. Mirror the message to OSReport so the reason is
	// visible in Cemu's log even when the dialog itself cannot be shown.
	OSReport("ASIOFATAL: %s\n", message);
#endif
	ErrAsio(message);
}

} // namespace asio::detail
