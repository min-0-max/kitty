# Soksak provider branch

The `soksak-provider-0.48.2` branch extends Kitty 0.48.2 with a headless provider ABI. Soksak
components pin an exact commit from this branch; they do not use a workspace path.

`KittyProviderSnapshot` exposes the parser-owned cursor shape and blink state together with grid,
mode, and cursor-position state. The values come from `Screen.cursor`. A consumer maps Kitty's
block, beam, underline, and hollow variants to its public terminal contract without parsing the
input stream again.

The source ownership check is:

```sh
python3 -m unittest kitty_tests.check_build.TestBuild.test_provider_snapshot_exposes_engine_cursor_state
```
