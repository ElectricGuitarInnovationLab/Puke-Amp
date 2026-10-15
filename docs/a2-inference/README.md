# A2 low-latency inference investigation

For transfer to the Puke project, use the self-contained
[Codex handoff report](PUKE-CODEX-HANDOFF.md), which consolidates all test phases,
the final decision, Slim behavior and the artifact inventory.

The [real-model ARM64 decision](real-model-arm64-results.md) is the latest
conclusion: retain the existing kernel. The packing-free candidate does not
deliver a sufficiently consistent 64-frame benefit for eleven instances and
regresses at 128/256 frames with the supplied real models.

The later [native ARM64 follow-up](native-arm64-followup.md) analyzes the
user-provided benchmark CSV and the nested A2 models subsequently added under
`Models/V Amps`. The baseline inventory below predates those supplied files.

## Decision and scope

No production inference-kernel change is enabled. A packing-free Eigen candidate is
provided as `lazy-product-experiment.patch`, with repeatable comparison tools and
expanded optimized correctness tests. The available execution environment runs
x86-64 under Rosetta, not native ARM64. Its results cannot justify selecting a
kernel or block-size dispatch policy for the user's ARM64 macOS rig.

This work changes benchmarking, tests and stale source comments only. It does not
change model weights, calibration, latency, routing or the plug-in's floating-point
policy. Native ARM64 measurements with actual trained A2 models remain required.

## Baseline and source inspection

- Plug-in checkout: `46885b5` (version 0.2.15). Release 0.2.11 commit `3ca6ec7`
  and this checkout both reference core `9c7b185de346fe0725dea537bcee4bc38b5bb6d6`.
  The macOS A2 build configuration is unchanged between them. This establishes
  source equivalence, not the provenance or flags of the installed binary.
- macOS configuration defines `NAM_ENABLE_A2_FAST`, `RELEASE_OPTIMIZE = 3`,
  `DEBUG_OPTIMIZE = 0`, and standard 64-bit architectures. Xcode build settings
  use that optimization variable. A native Release binary was not built here.
- Strict A2 detection accepts only 3 or 8 channels/bottleneck, one 23-layer
  array, mono input/condition/output, the fixed dilation sequence in `a2_fast.h`,
  kernels `[6]*14 + [15]*2 + [6]*7`, LeakyReLU(0.01), ungated/ungrouped,
  no active FiLM/head1x1/post-stack head, active biased layer1x1, and a biased
  16-tap mono head. Other shapes continue through generic dispatch.
- None of the bundled models exercises this signature. In particular,
  `example_models/wavenet_a2_max.nam` has four channels and two layers and is
  not an `A2FastModel<8>` fixture. Synthetic 8-channel seeds 17/91 and 3-channel
  seed 17 exercise the supported shapes. They are not trained amp models.
- Nano already has unrolled 3x3 products and a fused final-tap, conditioning,
  activation, head accumulation and residual pass. Standard uses 156 convolution
  GEMMs plus 23 residual GEMMs per block, with separate bias/conditioning,
  activation and head passes. Both already use power-of-two mirrored histories.
  Comments incorrectly describing an eight-channel four-frame tile were corrected.
- Eigen `GeneralMatrixMatrix.h` packs both operands before its panel kernel on
  every product. `noalias()` avoids an output temporary, not operand packing.
  At 64/128/256 frames, the 8xN float RHS is 2/4/8 KiB and the 8x8 LHS is 256 B,
  below the default 128 KiB stack-allocation threshold. The eight-row destination
  disables `parallelize_gemm`'s compile-time parallel condition. The benchmark and
  focused tests additionally define `EIGEN_DONT_PARALLELIZE`; no OpenMP is linked.
  Allocation tests found no malloc/realloc/new/free/delete activity in prepared
  fast-path inference. This does not instrument arbitrary OS locks or syscalls;
  source inspection finds no such operations in the affected processing path.
- `process()` still allocates if called above its prepared maximum. Tests exercise
  sizes within that maximum, as required for realtime use; Reset/preparation is
  outside inference. This pre-existing contract is unchanged.
- The plug-in calls `disable_denormals()` around processing. The standalone tool
  deliberately does not override the environment's FP control state or enable
  fast-math. Tiny-input results are not a measurement of the plug-in's FTZ mode.

The user's stack percentages are statistical stack shares, not measured callback
fractions. Source inspection identifies repeated small products/packing as an
optimization hypothesis; these offline measurements do not attribute a percentage
of native execution time specifically to packing.

## Experiment

The two-line candidate replaces convolution and residual products with
`lazyProduct`. Fixed eight-element reductions then avoid GEMM packing, leaving
history, bias, conditioning, activation and head semantics intact. It adds no
runtime dispatch, buffers, allocation, threads or architecture dependency. Nano
is untouched and serves as a control. This is a portable small-product experiment,
not an ARM NEON implementation. It is retained only as a patch, not enabled code.

Handwritten NEON/FMA, tile-size selection, further fusion and conditional mirror
refresh should be evaluated on native hardware after this simpler comparison.
Writing an unvalidated NEON path in the translated environment would not establish
its performance or correctness. No speculative production path was added.

## Measured result (translated x86-64 only)

Eight-channel varying input, median of per-run/model medians, microseconds per
sequential group (two seeds, three process runs):

| Frames | Instances | Existing | Candidate | Time reduction |
|---:|---:|---:|---:|---:|
| 64 | 1 | 97.44 | 82.10 | 15.7% |
| 64 | 11 | 1071.48 | 910.21 | 15.1% |
| 128 | 1 | 183.29 | 164.27 | 10.4% |
| 128 | 11 | 2071.00 | 1850.54 | 10.6% |
| 256 | 1 | 359.31 | 327.48 | 8.9% |
| 256 | 11 | 4042.85 | 3692.58 | 8.7% |

These median gains repeat across all paired varying-input runs, but tail latency
is not uniformly better. At 128 frames/11 instances, the median of row p99s
regresses from 2569 to 3785 us; the worst observation grows from 16.7 to 49.3 ms.
One constant-input 256-frame/single-instance pair regresses 3.2% in median.
The unchanged nano control is approximately flat in overall medians (varying
11-instance 128-frame median regresses 0.8%), but individual control pairs vary
substantially. These outliers reinforce the need for native, quiet-machine and
live-host measurements, rather than justifying a block-size dispatch threshold.

[All gains, regressions and tails](results.md), [raw CSV](translated-results.csv).
Raw rows also contain mean ns/sample/instance and standard deviation. Regenerate:

```sh
python3 NeuralAmpModelerCore/tools/summarize_a2_matrix.py \
  docs/a2-inference/translated-results.csv
```

## Method and environment

Full model `DSP::process` timing only, not kernel-only or complete VST3 timing.
Each observation sequentially processes 1 or 11 separately constructed model
instances with independent weights/history storage. All instances receive the
same input. This approximates concurrent active amp chains but not Puke's graph,
other effects, Global patch, interface driver or audio thread scheduling.

- Linux 6.12.76-linuxkit container, eight advertised CPUs; CPU model unavailable.
  Compiler/executables are x86-64; `/proc/self/maps` identifies `/run/rosetta/rosetta`.
  ARM feature flags from `lscpu` do not make these native ARM measurements.
- Clang 12.0.0, libc++ 12, bundled Eigen. Release `-O3 -DNDEBUG`, C++20,
  benchmark LTO (`-flto=thin`), no `-Ofast`, fast-math, OpenMP or CPU-specific flags.
  The old benchmark used `-Ofast`; it was changed to `-O3` before both builds.
  GCC 9 initially failed on core C++20 `atomic<shared_ptr>`; Clang/libc++ uses the
  core's existing libc++ implementation without modifying core compatibility code.
- 48 kHz fixtures; 64, 128, 256 frames; 1 and 11 instances; varying, silence,
  constant 0.125 and tiny signals. Varying is deterministic harmonics, decaying
  envelopes and noise, not a recorded guitar. Tiny scales it by 1e-38.
- Model loading, signal generation, Reset/prewarm and one full 128-block cache
  warm-up pass precede measurement. Three measured 128-block passes retain state.
  Three independent process runs alternate baseline/candidate order, giving
  384 observations per row and 1,152 per model/scenario/variant overall.
- A monotonic clock surrounds only the sequential instance processing loop.
  Timing storage is reserved ahead of time. Every output is consumed into a
  printed checksum outside the timed section, including all eleven instances.
  This also touches output memory between observations. Clock overhead is not
  subtracted. Partial final blocks are excluded from timing distributions.
- CSV reports mean, median, p99, maximum, population standard deviation in
  microseconds per complete sequential group, and mean ns/sample/instance.
  Process state continues across repeated input passes. Fixture weight generation
  uses `uniform_real_distribution`; use the same standard library or saved .nam
  files for bit-identical fixtures on another platform.

Unpinned container timing includes translation, scheduler and host noise. Even a
p99 below an audio deadline would not establish hardware-buffer reliability.

## Memory/cache considerations

Estimated float storage, including weights, histories and working buffers but not
object/allocator overhead:

| Channels | Frames | KiB/instance | MiB for 11 |
|---|---:|---:|---:|
| 8 | 64 | 513.95 | 5.52 |
| 8 | 128 | 620.45 | 6.66 |
| 8 | 256 | 841.45 | 9.04 |
| 3 | 64 | 182.56 | 1.96 |
| 3 | 128 | 222.81 | 2.39 |
| 3 | 256 | 306.31 | 3.29 |

The candidate leaves these buffers unchanged and removes per-product stack
packing work. The 11-instance measurements include a much larger history working
set; they cannot be inferred by multiplying the one-instance minimum. Cache-miss
hardware counters were not collected. History mirror refresh still copies the
entire prepared block size for every layer, an independent future hypothesis.

## Reproduction

From the repository root, on native macOS with Xcode tools (check `uname -m` is
`arm64` and the resulting executable with `file`):

```sh
cmake -S NeuralAmpModelerCore -B /tmp/a2-native -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build /tmp/a2-native --target bench_a2_fast test_a2 -j2
/tmp/a2-native/tools/test_a2 /tmp/a2-baseline-output.bin
cp /tmp/a2-native/tools/bench_a2_fast /tmp/a2-baseline
# Apply only to a clean copy of this change; the patch paths are relative to core.
git -C NeuralAmpModelerCore apply ../docs/a2-inference/lazy-product-experiment.patch
cmake --build /tmp/a2-native --target bench_a2_fast test_a2 -j2
/tmp/a2-native/tools/test_a2 /tmp/a2-candidate-output.bin
cp /tmp/a2-native/tools/bench_a2_fast /tmp/a2-candidate
python3 NeuralAmpModelerCore/tools/compare_a2_outputs.py \
  /tmp/a2-baseline-output.bin /tmp/a2-candidate-output.bin
git -C NeuralAmpModelerCore apply -R ../docs/a2-inference/lazy-product-experiment.patch
python3 NeuralAmpModelerCore/tools/benchmark_a2_matrix.py \
  /tmp/a2-baseline /tmp/a2-candidate --blocks 128 --iters 3 --output a2-results.csv
# Prefer actual trained models, once available:
python3 NeuralAmpModelerCore/tools/benchmark_a2_matrix.py \
  /tmp/a2-baseline /tmp/a2-candidate --models /path/to/standard.nam /path/to/nano.nam
# Explicit generic reference timing, separate from the existing fast baseline:
/tmp/a2-baseline --path generic --buffer 128 --instances 11 /path/to/standard.nam
```

For this Linux environment the configure command instead used
`-DCMAKE_CXX_COMPILER=clang++-12 -DNAM_USE_LIBCXX_LINUX=ON`, without the macOS flag.
The matrix runner exits on failed commands; unsupported models and invalid
arguments fail rather than silently contributing empty results.

The focused test's optional output contains native-endian NAM_SAMPLE values
(default double), only written outside tracked processing. Compare same-platform
baseline and candidate captures for a direct cross-build numerical check.

## Correctness and realtime validation

Both baseline and candidate run against explicit generic WaveNet creation,
bypassing A2 dispatch. Assertions remain enabled with optimized `-O3` code.
Checks cover both shapes, three ordinary weight seeds per shape, two additional
bias-free subnormal fixtures, fixed blocks including 1/3/63/64/128/255/256,
32768-sample mixed streams with cycling lengths
1/3/7/31/63/64/65/127/128/129/255/256, repeated 48/44.1/48 kHz preparation,
ring wraps, persistent state, and reset equality with a newly constructed model.

All outputs must be finite. The ordinary reference tolerance is absolute 5e-5,
retaining the existing tolerance to allow float32 reduction-order differences
through 23 nonlinear layers; it is not a perceptual error claim or a guarantee
for arbitrary trained weights. Bias-free tiny streams use absolute 1e-41.
Reset versus fresh preparation requires exact equality. Allocation tracking
surrounds fast processing, including variable lengths, after preparation.

See accompanying test logs and measurement tables for observed results. Native
ARM64 trained-model coverage and a Release Puke session with the RME UCXII at
64/128 samples remain the final validation steps before enabling any candidate.

Observed checks: baseline and candidate both passed all focused tests. Maximum
ordinary error against generic WaveNet was 6.19888e-6; the tiny-stream reference
error was zero, with 8191 nonzero float-subnormal outputs per shape in this
environment. The direct cross-build comparison used zero tolerance and passed
(see `direct-comparison.txt`). All 216 paired benchmark rows also have matching
printed output checksums. No prepared fast-path allocation/deallocation was
observed. Logs: [baseline](baseline-tests.txt), [candidate](candidate-tests.txt).
Only the focused A2 tests were run; the unrelated full core/plug-in suite and
native macOS plug-in build were not run.
