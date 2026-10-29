# Afterflow

by prsevend

a predictive input cheat that fixes your early clicks and makes them accurate

built for **geometry dash 2.2** (gd 2.2081 geode 5.11.2 windows). no menus and no in-game overlay: **f6** cycles stages anywhere and **f9** toggles a terminal with a live log (kitty on linux/proton or a powershell log viewer on native windows). both keys are geode keybind settings so you can rebind them in mod settings or the keybinds popup

## what it does

geometry dash runs physics at a fixed **240 ticks per second**. afterflow keeps a hidden ghost player and for every press or release it tests the input at its current tick and at delays of 1..n ticks. if pressing now would kill you but a delayed version survives the input is held back and delivered on the surviving tick. the same check runs for releases so early clicks and early let-gos can both be corrected

the prediction understands cube, ball, ufo, spider, swing, wave, robot and ship movement plus orbs, pads, gravity portals, size portals, speed portals and click between frames' mid tick clicks. it never changes the real level state since ghosts are simulated in a detached layer and every object they touch is restored afterwards

## keybinds

afterflow uses geode keybind settings. open **mod settings → afterflow** (or geode's **keybinds** popup) to change them

| Setting | Default | Where |
| ------- | ------- | ----- |
| Cycle Stage | F6 | anywhere |
| Terminal | F9 | everywhere |

the first launch shows a one time note with the current bindings and a shortcut to mod settings. a saved flag records that it was seen so it does not come back

## stages
press **f6** (or your bound cycle stage key) anywhere to cycle. the stage is remembered across sessions. outside a level a notification shows the selected stage. the key also works while the terminal is focused

| Stage | All modes | Wave |
| ----- | --------- | ---- |
| 1 | 2 ticks | 2 ticks |
| 2 | 4 ticks | 4 ticks |
| 3 | 8 ticks | 8 ticks |
| 4 | 16 ticks | 8 ticks |
| 5 | 24 ticks | 12 ticks |
| 6 | 32 ticks | 16 ticks |
| 7 | 40 ticks | 20 ticks |
| 8 | off - 100% raw input | off |

at 60 fps one frame is 4 ticks so 4 ticks is `1.0f @ 60fps` and 16 ticks is `4.0f @ 60fps`
## terminal (f9)

the game runs as a windows process under proton so it cannot start a linux program directly. afterflow writes a live log to `/dev/shm/afterflow-<user>/terminal.log` (where `<user>` is your linux username) and drops a trigger file at `/dev/shm/afterflow-<user>/open-kitty`. `/dev/shm` is memory-backed, so the log and runtime files disappear on reboot; if `/dev/shm` is unavailable afterflow falls back to `/tmp/afterflow-<user>`. a small systemd user service watches the trigger and toggles the real **kitty** window on the host while tailing that log. the terminal key (default **f9**) opens the terminal when it is closed, raises it when it is open but not focused, and closes it when it is focused. the live messages are not duplicated to the geode console; only warnings and errors are logged there

the service is included as `afterflow-terminal-watch.sh` + `afterflow-terminal.service`:

```sh
install -Dm755 afterflow-terminal-watch.sh ~/.local/bin/afterflow-terminal-watch.sh
mkdir -p ~/.config/systemd/user
cp afterflow-terminal.service ~/.config/systemd/user/
systemctl --user daemon-reload
systemctl --user enable --now afterflow-terminal.service
```

the service starts with your user session and reads the running compositor environment itself (hyprland, sway, niri, kde, gnome, or x11), so you do not need to import wayland variables manually. after updating, re-copy both the script and the unit, then run `systemctl --user daemon-reload`, `systemctl --user enable afterflow-terminal.service`, and `systemctl --user restart afterflow-terminal.service`. if f9 still does nothing, check `/dev/shm/afterflow-<user>/watch.log`, `/dev/shm/afterflow-<user>/kitty.log`, and `journalctl --user -u afterflow-terminal.service`; `watch.log` records the detected wayland and display values

requires `kitty` on linux/proton. native windows uses powershell and needs no service. on hyprland the service also applies a floating centered window rule and raises the window above the game. on other compositors the log window still opens but placement is left to the window manager

## build

you need geode sdk 5.11.2 plus cmake 3.29+ plus clang plus the 2.2081 bindings

```sh
# linux cross compiling a windows mod
export GEODE_SDK=/path/to/geode-sdk
export GEODE_BINDINGS_REPO_PATH=/path/to/geode-bindings   # only needed for the 2.2081 binding fixes
geode build -p windows

# windows
set GEODE_SDK=C:\path\to\geode-sdk
geode build

# macos (native, builds arm64 and x86_64 by default)
export GEODE_SDK=/path/to/geode-sdk
geode build
```

the packaged mod ends up in `build-win/` when cross compiling from linux (`build/` on windows and macos) and it also gets copied into the game's `geode/mods` folder automatically

## installation

copy the resulting .geode file into geometry dash's geode/mods folder and restart the game. enable **afterflow** in the geode mod list. the defaults work out of the box so you can rebind the two keys in mod settings if you want different ones

## credits

dynamic mod
whitechocolate
click before frames

lucide.dev for icon

## warnings

this is a cheat. it changes input timing so your runs are not legit. dont use it on rated levels or leaderboard submissions or anything you want to claim as legit. some levels and lists have input buffer or macro detectors so you can get flagged.
