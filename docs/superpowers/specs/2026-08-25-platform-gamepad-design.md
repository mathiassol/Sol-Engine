# Gamepad (Platform #3)

Date: 25 Aug 2026
Status: implemented

## Sources

- XInput 1.4 (`XInputGetState`, four slots). Xbox layout: A/B/X/Y, bumpers,
  Back/Start, thumbs, D-pad, triggers, two sticks.
- Gameplay #4 walker + Gameplay #3 cameras as the consumer. A pad that only
  steers the fly cam is not an input system.

## Not this

- Input **actions** / remapping (Gameplay #2). Raw buttons, like keys.
- Vibration, headset, DirectInput / Raw Input HID, DualShock-only APIs.
- Requiring a physical pad in `--gates`.

## Decision

`GamepadState` lives on `InputState` (snapshot, four slots). Buttons have
the same down / pressed / released edges as keys. Sticks are −1..1 after a
circular deadzone; triggers 0..1. Helpers (`apply_stick_deadzone`,
`gamepad_begin_frame`, `gamepad_set_button`) sit on the `platform` interface
so the gate is CPU-only.

`platform-win32` polls XInput. Unfocused windows zero pads the same way they
zero keys. No XInput types in public headers.

Sandbox (walk mode only): left stick wish (analog, so `Move` uses stick
magnitude), right stick look, A jump, Start toggle walk, Y cycle camera.
Keyboard still works. Fly cam is not pad-steered.

## Gate

`Gamepad gate: deadzone=yes button=yes poll=yes connected=* (pass)`

1. Deadzone: inside the circle → 0; outside remaps toward 1.
2. Button edges: press / hold / release on a synthetic pad.
3. `IInput::update` leaves sticks in [−1, 1]. `connected` is logged, not
   required (no hardware in CI).

Prior character / camera gates still pass.

## Out of scope

Actions, rumble, more than four pads, using the pad as fly-cam WASD.
