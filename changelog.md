# v2.2.4

- **fix: ios crash at the end of the loading bar.** the panel was being built from `CCDirector::willSwitchToScene`, i.e. in the middle of the loading screen -> menu transition, and allocating fonts and sprites there is what kills ios. the overlay is now built on the next main thread tick, against whichever scene is running by then.
- [fix] the stage strip no longer dereferences a label that failed to create, and it is bounds checked against the stage label array.
- [fix] the version check no longer runs inside `$on_mod(Loaded)`; it fires once the menu layer is up. `setMouseEnabled` is desktop only now.
- added startup breadcrumbs to the geode log (`load start`, `load complete`, `building overlay`, `overlay attached`, `menu ready`) so a crash on a platform we cannot debug locally can be pinned to a stage.
- added `tools/check-ios-linkage.py`, which checks a packaged `.geode`'s ios slice against a geode ios release.
- [build] the combined package is forced back to deflate. the `combine` action started defaulting to `--zstd` and the geode ios loader has no zstd, so the package extracted to nothing and the game said "no binary could be found for current platform". the action is pinned and a check step now fails the build if a package is not deflate.
- you can drag the pause menu af button anywhere on mobile now, and the button glow follows the logo instead of drawing a circle around it.

# v2.4.1

- **mobile pause menu button**: android and ios now get an AF button in the pause menu, directly under the settings button and the same size as it. tap it to open the control panel.
- the floating draggable af button is now off by default on mobile; turn it back on with the new "floating af button" setting.
- keybind and terminal settings are now windows/mac only.
- [build] workflow now builds android32, android64 and ios, and still packages one combined `.geode` if ios fails.

# v2.4.0

- **auto-update in game**: the out-of-date popup now downloads, verifies and installs the new `.geode` itself, then restarts the game. no more browser round-trip.
- the update download shows a live progress panel with percentage, MB downloaded / MB total and current speed in MB/s.
- downloads always resolve `releases/latest/download/afterflow.prsevend.geode`, so the updater no longer depends on the release tag naming scheme.
- update installs use a safe write (temp file + rename), so an interrupted download can no longer leave a corrupt `.geode` behind.
- added a 120s transfer timeout to the update download so a dead connection cannot strand the progress panel.
- **config import / export**: the settings tab can copy the current gameplay tuning to the clipboard as JSON and paste it back, with clamping and a status readout.
- **game mode icons**: the settings tab shows the cube, ship, ball, ufo, wave, robot, spider and swing icons in your own colours, centred in their own row with a rounded backdrop. the icon for the mode you are currently in is highlighted, the rest dim.
- [fix] game mode icons no longer render inside the theme colour swatches.
- [fix] the newest log line is marked with a full-width accent row instead of a 3px vertical bar that looked like a text cursor.
- [fix] panel glow reduced from 7 rings at 8 alpha to 5 rings at 3 alpha, and the drop shadow softened from 120 to 72 alpha.
- [fix] panel shell is no longer crushed to near-black: the tint multipliers moved from `0.62/0.70` to `0.88/0.92`.
- [fix] smoother rounded corners everywhere (`SEGMENTS` 10 -> 24).
- [fix] `CMakeLists.txt` version was stuck at `2.3.0` while `mod.json` and `version.txt` said `2.4.0`.
- [build] a post-build step now clears geode's extracted-mod cache so a rebuild can never silently load a stale dll again. disable with `-DAFTERFLOW_PURGE_UNZIPPED=OFF`.

# v2.3.0

- redesigned the in-game panel: compact header, stage pills, all-mode/wave stat readouts, striped live log, translucent cards and softer pill buttons.
- new **ui customization** settings: primary color, secondary color, panel scale and font scale. changes apply live while the panel is open.
- [fix] windows builds with clang-cl and ninja again; msvc-only flags and apple architectures are no longer applied to every platform.
- [fix] the packaged `.geode` now includes the bundled fonts and their license.

# v2.2.0

- new custom in-game control panel on **f9** with stage arrows, stage dots, a live log, copy log, close and a terminal shortcut.
- the panel pauses the game while open and resumes when it is closed.
- added android and ios targets. mobile gets a draggable **af** button that opens the same panel, and its position is saved between sessions.
- the panel uses bundled sora fonts, with `bigFont.fnt` as a fallback.
- f9 now opens the control panel instead of the terminal directly. use the **terminal** button in the panel to open the log window.
- the live log keeps recent messages so the panel can show them and copy them to the clipboard.

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