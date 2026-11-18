# Slow Time Pitch Fix

An SKSE plugin for Skyrim Special Edition / Anniversary Edition. It fixes a bug in the base game: while time is slowed, the gamepad right stick looks up and down extremely slowly. Left and right turning is not affected. The plugin can also set how fast the player moves during Slow Time.

## The problem

Any Slow Time effect triggers the bug. That includes the Slow Time shout and mods that stop or slow time, such as rings or powers.

- Right stick left/right: normal speed.
- Right stick up/down: slowed by the same factor as the world. At 5% world speed, vertical look runs at 5% speed. With "time stop" effects it is effectively frozen.
- Mouse look is not affected.

## Cause

This was found by reverse engineering `SkyrimSE.exe` 1.7.104.

- The gamepad look routine (Address Library ID `42354`) time-normalizes **only the pitch (Y) value**:

  ```
  y *= min(1 / fLookTimeNormalizingFloorFramerate, frameDelta) * fLookTimeNormalizingTargetFramerate
  ```

  This happens when `bGamepadLookTimeNormalizeInputs` is on, which is the default.
- `frameDelta` is the **game-time** frame delta (ID `410199`), which is scaled by the global time multiplier. Slowed time therefore slows the pitch as well. Yaw is not normalized this way, so it keeps normal speed.

## The fix

The plugin changes that one instruction (`minss xmm0, [rip+disp32]`) so it reads the **real-time** frame delta instead (ID `410200`, the variable right next to it).

- In normal play both values are identical, so nothing changes.
- While time is slowed, vertical look now matches horizontal look.
- The patch is applied only if exactly one matching instruction is found inside that function. Otherwise nothing is changed and the reason is logged.

## Optional: player speed during Slow Time

Slow Time effects slow the player down as well. The player speed is the game setting `fVATSPlayerMagicTimeSlowdownMult`. `Skyrim.esm` sets it to `0.6`, and it does not depend on how strongly the world is slowed.

You can override this value in the INI. It is off by default (`-1` means the game value is not touched).

- The INI is re-read after every save load and new game, so changes apply without restarting the game.
- Perks that set their own value through the "Mod Player Magic Slowdown" entry point keep doing so. Vanilla Quick Reflexes is one of them.

## Configuration

The settings are in `Data\SKSE\Plugins\SlowTimePitchFix.ini`:

```ini
[General]
bFixGamepadPitch=1      ; 1 = apply the gamepad pitch fix (recommended), 0 = vanilla behaviour

[SlowTime]
fPlayerSpeedMult=-1     ; -1 = keep game value (0.6), 1.0 = normal speed, 0.05 - 1.0 = custom
```

## Requirements

- [SKSE64](https://skse.silverlock.org/) for your game version
- [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444) (All in one, Anniversary Edition)

## Compatibility

| Game version | Status |
|---|---|
| 1.7.104 | Tested: pitch fix and player speed both work |
| 1.7.x | Supported through Address Library (format 5) |
| 1.6.x (AE, 1.6.317+) | Address Library (format 2) and the player speed setting are verified. The pitch fix is applied only if the same code is found at runtime; check the log |
| 1.5.97, VR | Not supported |

- No ESP, no scripts, and nothing is written to your save. You can install or remove it at any time.
- Works with any source of slowed time: the vanilla Slow Time shout and any mod that slows or stops time (rings, powers, hotkeys, ...).
- If another plugin has already modified the same instruction, this plugin leaves it alone and says so in the log.

## Log

`Documents\My Games\Skyrim Special Edition\SKSE\SlowTimePitchFix.log`

## Building

You need Visual Studio 2022 or Build Tools 2022 with the C++ x64 toolset. Run `build.bat`. The DLL is written to `build\` and copied to `package\SKSE\Plugins\`.

`tools\altest.cpp` is a small test that resolves the plugin's IDs from `versionlib-*.bin` files given on the command line.

## Credits

- The SKSE team, for SKSE64 and its plugin API.
- meh321, for Address Library for SKSE Plugins.

## License

MIT. See `LICENSE`.
