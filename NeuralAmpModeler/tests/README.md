Run the library and real-loader tests with a C++20 compiler and standard library:

```sh
bash NeuralAmpModeler/scripts/apply-iplug2-patches.sh
cmake -S NeuralAmpModeler/tests -B /tmp/nam-library-tests -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/nam-library-tests --parallel 2
ctest --test-dir /tmp/nam-library-tests --output-on-failure
```

The editor resize test also compiles an override against the real iPlug2 base
class, ensuring a clean checkout has the required virtual resize hook. Both
native build scripts apply `patches/iplug2-editor-resize.patch` before compiling;
apply it before direct IDE builds too (the Bash command above works in Git Bash).
Removing `override` alone would leave host resize calls using the base class's
rectangular limits instead of the plugin's aspect-ratio constraint.

The cabinet catalog tests cover all bundled WAVs and graphics references, both
documented example mappings, snapping, omitted distances, duplicate captures,
sparse/fractional positions, and extending the catalog with new equipment.

The filesystem tests cover nested imports, invalid files, extension filtering,
concurrent name collisions, unchanged originals, UTF-8 names, category isolation,
reference persistence/relocation, and path traversal rejection. The loader tests
validate bundled amp, pedal, and IR examples and reject malformed imports using the
same validation functions as the UI.

Native UI smoke test (macOS/Windows): import a file and a nested folder from each of
the three menus; check the completion report and select the new items. Open another
plugin instance and the standalone app, then reopen the menus to see the same files.
Save a DAW session and an FXP, close/reopen the editor and host, and verify recall.
Verify the file-open icon still auditions external files, that Included/My Library
navigation works with the amp/pedal arrow buttons, and that failed imports leave the current
sound unchanged. Reveal the library, add a subfolder, and verify Refresh. While a
folder import is running, keep a menu open and verify completion waits until it closes.

Shared appearance and control smoke test (macOS/Windows):

- Resize from horizontal, vertical, and corner edges in a macOS VST3 host.
  Verify the window stays at 3:2 between 300×200 and 2400×1600, with no gray
  margins, and that controls and microphone placement remain aligned. Repeat
  with each page open, then close/reopen the editor and move between Retina and
  standard displays. In other formats, verify the corner grip scales all pages
  proportionally within the same limits.
- Open **Pedals**, **Settings**, and **Cabinet + mic...**, then close each page.
  Verify header buttons do not overlap page controls or intercept **Close**.
- Check knob dragging, mouse wheel, double-click reset, and numeric value entry
  on the amplifier and pedal pages. Check Gate, EQ, cabinet, and pedal toggles
  and confirm host automation still updates the controls.
- Load and select multiple pedal slots; verify loaded icons are purple and the
  selected slot has a purple outline. Move, bypass, and remove pedals.
- Check input calibration editing and output-mode radio buttons in **Settings**,
  including unavailable options for models without calibration metadata.
- Open CPU/Quality for a slimmable model, adjust its value, and dismiss by clicking
  outside the card. Check input/output meter levels and clipping indicators.
- Check popup submenus, scrolling in long libraries, disabled states, and hover
  contrast. Resize the editor and check text, icons, and controls at high DPI.

Cabinet page smoke test (macOS/Windows):

- Open **Cabinet + mic...** with no IR loaded; verify opening alone does not load
  a sound. Check all five microphones and both offsets use the corresponding PNG.
- Click and drag to 0, 1, 2, and 5 inches on both axes. Check exact readouts, capsule
  anchoring when changing angle, snapping between dots, and that only mouse release
  changes the sound. Try the two filename examples in `Models/CABINET_LIBRARY.md`.
- Use arrow keys along each axis. Escape during dragging must cancel the change.
  Close/reopen and resize the editor; the art should retain its proportions.
- Save/recall a preset and a DAW session, reopen the editor, and check all three
  menus and distances. Recall a preset with no IR and confirm the previous IR is
  cleared from both sound and display. Clear then immediately choose another IR.
- Load a custom WAV via the file icon and via **My IRs / import...**. Verify the
  custom indication and that it does not claim a recorded microphone position.
- Remove a selected WAV after opening the page, try loading it, and verify the
  prior sound/path/position survive the failure. Restore the WAV afterwards.
- Temporarily remove the catalog or one referenced graphic, reopen the editor,
  and verify the unavailable-library screen and working custom IR menu. Restore
  the resources afterwards. Verify installed VST3 and standalone resource lookup.
