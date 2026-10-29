# v2.1.6

- f6 now cycles stages from the windows powershell terminal and the linux kitty terminal. windows handles f6 directly in the log viewer; kitty maps f6 to a cycle helper and keeps the raw-key fallback.

# v2.1.5

- stage cycle feedback and terminal errors now go to the terminal log only. afterflow no longer shows geode notifications; open the windows powershell terminal or kitty terminal to see the selected stage.

# v2.1.4

- f6 now cycles stages outside a level too. a notification shows the selected stage while in a menu or editor.
- [fix] linux/proton terminal not opening after v2.1.2 on some compositors such as cachyos/hyprland. the watcher now finds the wayland or x11 session itself instead of depending on a process named hyprland.
- [fix] macos package now includes the native dylib, so geode no longer reports `unable to find platform binary under the name "afterflow.prsevend.dylib"`.
- linux users must update `afterflow-terminal-watch.sh` and `afterflow-terminal.service` along with the mod.

# v2.1.3

- [fix] windows terminal not showing.

# v2.1.2

macos support

# v2.1.1

- [fix] terminal runtime files are now per-user (`/tmp/afterflow-<user>`), so switching Linux accounts no longer leaves stale triggers, close files or logs that stop the terminal from opening or make kitty open and close in a loop.
- the terminal watcher ignores a trigger that existed before it started, so a stale trigger from a previous session cant reopen kitty on login
- [fix] terminal not showing up when clicked on custom set userkeybind or f9.

# v2.1.0

- f6 and f9 are now real geode keybinds instead of hard coded non-changable set configs (i realized that people with 60% kbs dont have function keys so i "fixed" this.) 
- first install comes with a tutorial that tells them how to change the keybinds for the terminal and the cycling
- the terminal toggle is no longer a hardcoded keyboard hook; it listens for the keybind setting press.

# v2.0.5

- fixed ghost predictions and death effects running while paused

# v2.0.4

- close terminal via F9 (Linux & Windows)
- [afterflow] terminal

# v2.0.3

- fix visual (dash) orbs bugging
- added 10f / 5f @ 60fps (stage 7)

# v2.0.2

- added the version check. reads `version.txt` on launch and when a level starts, at most every 10 minutes.
- if it differs from the installed version, the console and terminal say `out of date` with the release link, and the menu shows a `dismiss` / `update` popup. `update` opens your default browser.

# v2.0.1

- new purple diamond icon.
- terminal, window class and log paths renamed.
- f9 opens kitty on linux/proton or cmd on windows.
- f6 works while the terminal is focused.

# v2.0.0

- whole undetected revamp for afterflow (was previously afterglow/moonsky)

# v1.0.0

inital release