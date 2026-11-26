# Cabinet and microphone library

`cabinet-library.json` describes the graphical cabinet selector. It is loaded from
the installed `Models` directory when the cabinet page first opens. The native
packaging scripts already copy and validate the complete `Models` tree, including
this catalog, `GUI` artwork, and `IR` recordings. Reopen the plugin editor after
changing an installed catalog.

## Using the selector

Click **Cabinet + mics...** in the main window. The speaker selector above the grid
sets the cabinet for both recorded microphones. Choose a microphone in the **Mic A**
or **Mic B** row, then choose its angle. Both microphones receive the same amp output.
Mic B starts **Off**. When adding a second mic, its placement starts at the nearest
available capture to the first mic.

Click a row to edit that microphone, including when the capsules overlap. The A/B badges
identify each capsule. Drag the selected microphone or click a recorded dot and release
to load that WAV. Arrow keys move to the next recorded distance on the same row or
column and load immediately. Right moves away from the center, and down moves away
from the grille. Opening the page or dragging a preview leaves playback alone until
a selection is committed. **Close** or Escape returns to the amp.

Each row has a level control (−60 to +12 dB) and pan. Both default to **0 dB, center**.
Center sends full level to each stereo channel. Panning attenuates the opposite channel.
On a mono output both mic levels contribute and the saved pan settings take effect again
when using stereo. **Invert polarity** reverses Mic B. Two identical recordings at
unity and center add about 6 dB, or cancel when B is inverted. Use the stereo output
meter to check peaks while blending.

The Orange PPC112 has positions at **0, 1, 2, and 5 inches** on each axis for five
microphone choices at **0 or 45 degrees**. Royer front and back share the supplied
Royer artwork. Both mics snap to recorded distances on the same scale.

The Mesa Boogie Thiele 1x12 / Celestion C90 has two room captures each for the
Neumann TLM 103 and Shure SM57. Select a mic row, then choose **Inside booth**
or **Outside booth** under **Atmosphere**. Inside was recorded in the isolation
booth. Outside was recorded from the room with the booth door open. The two dots
below the distance grid correspond to these buttons. Left and right arrow keys
switch between booth positions. When a mic has both kinds of capture, the up
arrow returns to the measured grid and the down arrow leaves its bottom edge.

The page's **IR library / import...** button targets the selected row. Imported
recordings without placement metadata appear as **Custom IR** and have level, pan,
and (for B) polarity controls. **Clear Mic A/B** or **Off** removes just that slot.
On the main window, opening a WAV directly selects it for Mic A. Choosing a different
recorded cabinet moves the other recorded mic to its nearest available position. The
main clear button clears both slots. **Enabled** bypasses the whole cabinet while
retaining both selections. After a short fade, bypass skips convolution. Selecting
the same WAV twice shares a single convolution result.

Presets and DAW sessions save both WAV references, levels, pans, and B polarity.
Earlier presets open with their original WAV in Mic A and Mic B off.

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

Room captures use a named placement in the same filename slot as the measured
distances. The shipped suffixes are `AtmosphericInside_G11B_LL` and
`AtmosphericOutside_G11B_LL`. The parser also accepts the prefix `Atmosphere`
and permits omission of `_G11B` and `_LL`. A cabinet/mic combination can have
either booth position, both, or a mix of booth and measured positions. Each
booth position uses the straight-on mic artwork.

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

The test checks all 167 current WAVs, both example mappings, image references,
booth locations and the Orange position grids, snapping, aliases, sparse sets, fractional
positions, malformed catalogs, and adding a cabinet/mic through data alone. Update
the expected shipped counts/grid in the test when intentionally expanding the
factory library. The same target is part of the existing asset-library CI job.
