# Soksak provider branch

The `soksak-provider-0.48.2` branch extends Kitty 0.48.2 with a headless provider ABI. Soksak
components pin an exact commit from this branch; they do not use a workspace path.

`KittyProviderSnapshot` exposes the parser-owned cursor shape and blink state together with grid,
mode, and cursor-position state. The values come from `Screen.cursor`. A consumer maps Kitty's
block, beam, underline, and hollow variants to its public terminal contract without parsing the
input stream again.

Provider input applies OSC 4/10/11/12 and their reset forms to the same `ColorProfile` state used
by Kitty rendering. `KittyProviderThemeOverrides` exposes only explicitly active foreground,
background, cursor, and 256-color palette overrides. An explicit color remains present even when
its RGB value equals the configured base color. OSC reset and terminal reset clear presence as
well as restoring the effective engine color. Consumers combine this raw engine state with their
host-owned base theme; they do not parse the input stream a second time.

The source ownership check is:

```sh
python3 -m unittest kitty_tests.check_build.TestBuild.test_provider_snapshot_exposes_engine_cursor_state
python3 -m unittest kitty_tests.check_build.TestBuild.test_provider_feed_exposes_raw_osc_color_overrides
```
