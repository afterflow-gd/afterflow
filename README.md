# Afterflow

by prsevend

## New modification – Custom Stage Keybinds

**I made it so Afterflow now supports custom keybinds for individual tick stages!**

I've modified the mod so you can assign separate keybinds to different Afterflow tick stages. Instead of having to cycle through stages using F6, you can bind specific keys to individual stages and switch between them instantly.

This makes switching between prediction delays much faster and more convenient, especially when playing levels that require different timing adjustments.

The original F6 stage cycling functionality is still available, so you can use either method depending on your preference.

---

A predictive input cheat that fixes your early clicks and makes them accurate.

Built for **Geometry Dash 2.2** (GD 2.2081, Geode 5.11.2) on Windows, macOS, Android and iOS.

**F6** cycles stages anywhere. **F9** opens a custom in-game control panel with stage controls, a live log, copy log and a terminal shortcut. On mobile, a draggable **AF** button opens the same panel. Both desktop keys are Geode keybinds, so you can rebind them in mod settings or the keybinds popup.

## What it does

Geometry Dash runs physics at a fixed **240 ticks per second**. Afterflow keeps a hidden ghost player and for every press or release it tests the input at its current tick and at delays of 1..n ticks. If pressing now would kill you but a delayed version survives, the input is held back and delivered on the surviving tick. The same check runs for releases so early clicks and early let-gos can both be corrected.

The prediction understands cube, ball, UFO, spider, swing, wave, robot and ship movement plus orbs, pads, gravity portals, size portals, speed portals and click between frames' mid-tick clicks. It never changes the real level state since ghosts are simulated in a detached layer and every object they touch is restored afterwards.

## Controls

Afterflow uses Geode keybind settings. Open **Mod Settings → Afterflow** (or Geode's **Keybinds** popup) to change them.

| Setting | Default | Where |
| ------- | ------- | ----- |
| Cycle Stage | F6 | Anywhere |
| Control Panel | F9 | Everywhere |
| Individual Stage Keybinds | Unassigned | Mod Settings → Afterflow |

The first launch shows a one-time note with the current bindings and a shortcut to mod settings. A saved flag records that it was seen so it does not come back.

On mobile the panel key is replaced by the floating **AF** button. Drag it anywhere to move it; tap it to open the panel. The position is remembered between sessions.

## Control panel

Press **F9** (or your bound panel key) to open the panel. While it is open the game pauses so you can change settings without dying. Press **F9** again,
