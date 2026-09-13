#pragma once

namespace HelpMenu {
// Requires UI/HelpMenu.img and server HELP_MENU_ACTION (0x1002), version 1.
// Disabled by default; install only on a matching v83 executable.
bool Install(bool enabled);
}
