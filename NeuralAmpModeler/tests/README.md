Run the library and real-loader tests with a C++20 compiler and standard library:

```sh
cmake -S NeuralAmpModeler/tests -B /tmp/nam-library-tests -DCMAKE_BUILD_TYPE=Debug
cmake --build /tmp/nam-library-tests --parallel 2
ctest --test-dir /tmp/nam-library-tests --output-on-failure
```

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
navigation works with the arrow buttons, and that failed imports leave the current
sound unchanged. Reveal the library, add a subfolder, and verify Refresh. While a
folder import is running, keep a menu open and verify completion waits until it closes.
