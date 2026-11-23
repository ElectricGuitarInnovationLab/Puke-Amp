# Cabinet and microphone library

`cabinet-library.json` describes the graphical cabinet selector. It is loaded from
the installed `Models` directory when the cabinet page first opens. The native
packaging scripts already copy and validate the complete `Models` tree, including
this catalog, `GUI` artwork, and `IR` recordings. Reopen the plugin editor after
changing an installed catalog.

## Using the selector

Click **Cabinet + mic...** in the main window. Choose a **Speaker**, **Microphone**,
and **Offset**, then drag the microphone or click a recorded-position dot. The
highlighted ring marks the microphone's capture point. Horizontal movement selects
distance from the speaker center; downward movement selects distance away from the
grille. These are a labeled two-axis projection, not a perspective measurement.
The readouts give the exact recorded distances in inches.

The display snaps to available recordings. Releasing the mouse loads that WAV;
dragging alone does not repeatedly rebuild the convolution engine. Arrow keys move
to the next recorded distance on the same row or column and load immediately.
Changing a menu loads the nearest available position to the previous selection.
Opening the page does not change the sound. **Close** or Escape returns to the amp.

The current library has positions at **0, 1, 2, and 5 inches** on each axis, for
each of five microphone types at **0 or 45 degrees**. Royer front and back are
separate choices sharing the supplied Royer artwork. No interpolation or synthetic
IRs are used. The capsule, cabinet, and mic art use a common display scale; the
supplied catalog maps one inch to 260 source pixels (12 logical UI pixels at the
current five-inch range). Editor resizing scales the entire page together.

**My IRs / import...** opens the existing user-library menu, including file/folder
import, refresh, and reveal. The main window's file icon still loads external WAVs.
Custom IRs remain usable even if they have no placement metadata; the page labels
them as custom rather than claiming a cabinet position. **Clear IR** removes the
current IR. The **Enabled** toggle beside **CABINET** in the main window controls cabinet bypass.
Presets and DAW sessions continue saving the WAV path in the existing format.

## Adding a cabinet or microphone

1. Place PNG graphics under `Models/GUI/cabs` or `Models/GUI/mics`. Keep the supplied
   artwork's source-pixel scale, or compensate using the optional image `scale`.
2. Add a cabinet entry and/or microphone entry to `cabinet-library.json` using the
   fields below. IDs and filename tokens must be unique within their respective
   lists. Names are the menu labels; they need not match filenames.
3. Put the WAV recordings in `Models/IR` or a subfolder using the filename convention
   below. A new recording at an existing cabinet/mic/angle needs only the WAV file.
4. Rebuild/package the application and run the catalog tests. The UI discovers
   recorded combinations from the files; unavailable microphones/angles are not
   offered for the chosen cabinet. Missing grid positions are allowed.

The catalog has `version: 1`, `omitted_grill_inches: 0`, and these two lists:

```json
{
  "cabinets": [
    {
      "id": "orange-ppc112-v30",
      "name": "Orange PPC112 / Celestion Vintage 30",
      "filename_prefix": "Orange_Celestion_V30",
      "image": {
        "path": "GUI/cabs/ppc112.png",
        "anchor": [5588, 5060]
      },
      "source_pixels_per_inch": 260
    }
  ],
  "microphones": [
    {
      "id": "shure-sm57",
      "name": "Shure SM57",
      "filename_token": "Shure_SM57",
      "images": {
        "0": {"path": "GUI/mics/sm57.png", "anchor": [1946, 55]},
        "45": {"path": "GUI/mics/sm57_45.png", "anchor": [1880, 130]}
      }
    }
  ]
}
```

Artwork paths are relative to `Models`. The two `anchor` coordinates are in source
image pixels: the speaker center for cabinets and capture point for microphones.
Transparent canvas margins are retained; anchors align the actual objects instead
of centering the whole PNG. Angled art has its own anchor so changing offset keeps
the capture point fixed. Optional `scale` defaults to `1`; for a microphone image
exported at twice the original resolution, use `0.5` and specify its anchor in that
new image's pixels. Use artwork with modest transparent margins and dimensions
when creating future assets; large source canvases consume more graphics memory.

## WAV names

```text
<cabinet filename_prefix>_<microphone filename_token>_<center>_from_center[_<grill>_from_grill][_<angle>_offset][_LL].wav
```

- Distances are nonnegative inches; decimal distances are supported.
- An omitted grill distance means `omitted_grill_inches` (currently **0 inches**).
  This is the explicit interpretation of the existing Royer and e609 short names.
- An omitted offset means **0 degrees**. Supported offsets are **0** and **45**.
- `_LL` is optional. The extension is lowercase `.wav`.
- Tokens are literal, including the parentheses in `Sennheiser_MD421II(M)`.
- Existing e609 aliases describe duplicate positions. For a new selection, a file
  with an explicit grill distance wins over an omitted distance, followed by an
  `_LL` file over an unsuffixed file, then lexical path order. Presets retain their
  exact originally selected file, including aliases.
- Unrecognized names remain available through the ordinary IR library menu but
  are not assigned speculative graphical positions.

For example, `Orange_Celestion_V30_Royer_back_121_0_from_center_1_from_grill_45_offset_LL.wav`
selects Royer back, 0 inches from center, 1 inch from grille, and `r121_45.png`.
`Orange_Celestion_V30_Shure_SM57_2_from_center_2_from_grill_LL.wav` selects the
SM57 at 2 inches on both axes, 0-degree offset, and `sm57.png`.

## Validation

```sh
cmake -S NeuralAmpModeler/tests -B /tmp/nam-library-tests -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/nam-library-tests --target cabinet_catalog_test
cd /tmp/nam-library-tests
ctest -R cabinet_catalog --output-on-failure
```

The test checks all 163 current WAVs, both example mappings, image references,
each microphone/angle's position grid, snapping, aliases, sparse sets, fractional
positions, malformed catalogs, and adding a cabinet/mic through data alone. Update
the expected shipped counts/grid in the test when intentionally expanding the
factory library. The same target is part of the existing asset-library CI job.
