# Puke Amp A2 inference investigation — handoff to the Puke project

Prepared from the completed investigation and user-supplied Apple Silicon results, October 2026.
This report is self-contained; paths below are relative to the NeuralAmpModelerPlugin repository.

## Decision

Keep the existing Puke Amp A2 inference kernel. We tested one alternative: replacing the eight-channel Eigen convolution and residual products with `lazyProduct` to avoid GEMM operand packing. The real-model ARM64 results did not justify enabling it globally or adding a 64-frame-only dispatch.

**No new production inference optimization was enabled. This does not mean Puke Amp is as optimized as it can be.** It means this particular candidate failed the acceptance criteria for the user's low-latency, multi-instance workload. Native NEON/FMA kernels, other tiling strategies, operation fusion and reduced buffer traffic remain untested hypotheses.

There is no reason to repeat the same full benchmark matrix merely to decide this candidate. Further work should begin with a different concrete hypothesis or a host measurement that answers a new question.

## User's rig and original evidence

- Puke hosts Puke Amp on an Apple Silicon Mac with an RME UCXII interface.
- The ordinary rig patch has 11 Puke Amp instances and 12 other hosted effects, alongside a persistent Global patch.
- Several amp chains execute concurrently, including chains whose output is not selected.
- The priority is reliable live operation at 64- and 128-sample buffers; 256 samples was measured as a comparison.

A five-second native ARM64 stack sample from the original session showed hosted plug-in `processBlock` calls in approximately 72% of sampled audio-callback stacks and Puke Amp VST3 processing in approximately 56%, included within the 72%. Much of the sampled Puke Amp work was in `nam::wavenet::a2_fast::A2FastModel<8>::process` and Eigen multiplication.

These percentages are statistical stack shares, not measured callback durations. They must not be added together or treated as exact CPU-budget fractions. That original Puke host was an unoptimized Debug build. The user subsequently rebuilt and tested Release configurations, but this investigation did not collect a comparable end-to-end Release Puke callback profile.

## Baseline provenance and implementation

Repository HEAD inspected: `46885b51f7d0e5470f7dd7b71b6d7e8aea79ae39`, plug-in version 0.2.15. Core submodule revision: `9c7b185de346fe0725dea537bcee4bc38b5bb6d6`.

The originally installed plug-in was reported as 0.2.11. Its release commit, `3ca6ec7`, points to the same core revision, and its macOS A2 build configuration is unchanged relative to the inspected checkout. This establishes source-level equivalence, not independent verification of the installed binary or its compiler flags.

The plug-in already defines `NAM_ENABLE_A2_FAST`; enabling it is not a new optimization. macOS settings specify Release optimization level 3 and Debug level 0.

Relevant source:

- `NeuralAmpModelerCore/NAM/wavenet/a2_fast.cpp` and `.h`: shape detection, weights, histories and inference.
- `NeuralAmpModelerCore/NAM/container.cpp`: selection of a model inside SlimmableContainer.
- `NeuralAmpModeler/NeuralAmpModeler.cpp` and `.h`: preparation, Slim parameter, resampling wrapper and plug-in processing.

Supported A2 shapes have one 23-layer array with 3 or 8 channels/bottleneck, mono input/conditioning/output, fixed dilations and kernel sizes, LeakyReLU(0.01), no gating or active FiLM, active ungrouped layer1x1 residuals, and a biased 16-tap mono head. The strict detector excludes other configurations.

The eight-channel path uses whole-block Eigen products: 156 convolution products and 23 residual products per block. Separate passes handle bias, conditioning, activation and head accumulation. The three-channel path already uses unrolled small products and a fused final-tap/post-convolution pass. Both have power-of-two history rings with mirrored tails. Stale comments claiming the eight-channel implementation already used four-frame tiling were corrected.

Eigen's GEMM implementation packs operands on each product. `noalias()` avoids an output temporary, not this packing. At the tested sizes, the small packing buffers fit below Eigen's default stack-allocation threshold. The fixed eight-row destination disables GEMM's compile-time parallel condition. Benchmarks additionally define `EIGEN_DONT_PARALLELIZE` and do not link OpenMP.

## Candidate actually tested

Only these two eight-channel expressions changed in the experiment:

```cpp
// Existing
ztile.noalias() += W * input_block;
lin_block.noalias() += l1x1_mat * ztile;

// Candidate
ztile.noalias() += W.lazyProduct(input_block);
lin_block.noalias() += l1x1_mat.lazyProduct(ztile);
```

The candidate was portable Eigen code, not handwritten ARM NEON. It changed no weights, model architecture, calibration, latency, history layout or nano path. Its patch is retained at `docs/a2-inference/lazy-product-experiment.patch` for review. It is not applied to production inference source.

## Benchmark scope and methodology

The timing tool measures complete inner WaveNet `DSP::process` calls. It does not measure kernel-only execution, the container wrapper, resampling, complete VST3 processing or Puke's entire audio callback.

- 48 kHz; 64, 128 and 256 frames; one or eleven independently constructed model instances.
- The eleven-instance case processes eleven separate copies of one model sequentially. It does not reproduce the exact mixture of models in the rig.
- Inputs: deterministic varying envelope/harmonics/noise, silence, constant 0.125, and the varying signal scaled by 1e-38. The varying signal is not a recorded guitar performance.
- Loading, input generation, Reset/prewarming and one full cache-warm-up pass occur outside measured processing.
- Repeated passes preserve state. A steady clock surrounds the sequential processing loop.
- Timing storage is reserved. All outputs are consumed into a printed checksum outside the timed section to prevent dead-code elimination.
- Matrix runs alternate baseline/candidate process order across repeats. Within each invocation, the models run in a fixed order.
- CSV rows contain mean, p50, p99, maximum and population standard deviation in microseconds per group, plus mean ns/sample/instance and checksum.

The benchmark's previous `-Ofast` setting was changed to `-O3` before both sides of the comparison. Benchmark targets enable interprocedural optimization. No broad unsafe floating-point flags were introduced. The plug-in disables denormals during processing; the standalone benchmark does not override the environment's floating-point control state. The benchmark therefore is not a complete reproduction of the plug-in's denormal policy.

## Three measurement phases

| Phase | Execution environment / data | Main finding |
|---|---|---|
| Initial exploration | Linux x86-64 under Rosetta; Clang 12/libc++ 12; synthetic shapes | Candidate appeared faster, but results were unsuitable for choosing native ARM64 dispatch. |
| Native synthetic | User's Apple Silicon Mac; Apple LLVM 17.0.0 reported; two synthetic eight-channel seeds | Modest 64-frame benefit, marginal 128-frame benefit, small 256-frame regression. |
| Native real models | Same user workflow, twelve trained eight-channel models | Small and uneven 64-frame multi-instance gain; regressions at 128/256 frames. This is the decision-making dataset. |

The supplied native build procedure requested Release and `CMAKE_OSX_ARCHITECTURES=arm64`; the user confirmed `uname -m` returned `arm64`. Exact Apple chip model, power/thermal state, background load and per-executable build hashes were not recorded in the CSVs. The native compiler identification came from pasted benchmark output. The local Linux execution was independently identified as Rosetta from process mappings.

For eleven instances with varying input, aggregate median time reductions progressed as follows:

| Buffer | Translated synthetic | Native synthetic | Native real models |
|---:|---:|---:|---:|
| 64 | 15.1% | 4.7% | 1.9% |
| 128 | 10.6% | 2.5% | -3.9% |
| 256 | 8.7% | -1.7% | -8.2% |

Negative values mean slower. These phases differ in execution environment and/or weights and run conditions; the progression does not isolate a single cause.

## Decisive real-model results

`a2-real-results.csv` contains all 1,728 expected rows: twelve models × three repeats × three frame sizes × two instance counts × four signals × two implementations. Each row summarizes 1,280 observations. All 864 baseline/candidate pairs are present; reported numeric fields are finite and all printed checksums match.

For varying input, microseconds per processing group:

| Frames | Instances | Existing p50 | Candidate p50 | Time reduction | Existing p99 | Candidate p99 |
|---:|---:|---:|---:|---:|---:|---:|
| 64 | 1 | 60.31 | 56.71 | 6.0% | 76.81 | 72.73 |
| 64 | 11 | 690.54 | 677.60 | 1.9% | 954.25 | 1130.96 |
| 128 | 1 | 113.33 | 118.62 | -4.7% | 136.56 | 139.46 |
| 128 | 11 | 1289.08 | 1339.40 | -3.9% | 1739.35 | 1772.62 |
| 256 | 1 | 217.44 | 235.46 | -8.3% | 264.81 | 298.42 |
| 256 | 11 | 2501.60 | 2706.27 | -8.2% | 3222.42 | 5045.46 |

Each displayed p50 or p99 is the median of 36 row statistics (twelve models × three repeats), not a percentile pooled from raw block samples. The time-reduction column compares these aggregate p50 values.

Pairing each model/repeat directly gives further evidence:

- At 64 frames/11 instances, the candidate wins 25 of 36 p50 comparisons, but p99 worsens in 19 of 36. Median paired p50 saving is 2.53%; individual pairs range from 11.7% slower to 15.2% faster. Only three models win p50 in all three repeats.
- At 128 frames/11 instances, the candidate wins only 11 of 36 p50 comparisons; p99 worsens in 19 of 36.
- At 256 frames/11 instances, it wins only 2 of 36 p50 comparisons; p99 worsens in 27 of 36.
- At 64 frames/11 instances, median paired p50 savings are 4.24% for silence, 3.88% for constant input and 0.63% for tiny input. Aggregate p50 regresses at 128 and 256 across every input category.

The approximately 13 µs aggregate saving at 64 frames/11 instances is roughly 1% of a 64-sample period at 48 kHz. It does not establish reliable additional live headroom. Large outliers occur in both variants. Scheduling and thermal behavior were not recorded, so higher candidate tails cannot be attributed entirely to its kernel.

## Numerical and realtime validation

Focused optimized tests ran locally with Clang/libc++ under translated x86-64, with assertions enabled. Both baseline and candidate passed comparison with explicit generic WaveNet creation across both supported shapes and several synthetic weight seeds.

- Long 32,768-sample streams, ring wraps and persistent state.
- Fixed and cycling irregular block lengths, including 1, 3, 7, 31, 63, 64, 65, 127, 128, 129, 255 and 256.
- Repeated 48/44.1/48 kHz preparation and exact reset-versus-fresh-state comparison.
- Silence, tiny input, and bias-free fixtures that actually produced float-subnormal outputs.
- No tracked allocation/deallocation during prepared fast-path processing.

Ordinary reference tolerance was absolute `5e-5`; the largest observed difference was `6.19888e-6`. Bias-free subnormal fixtures used `1e-41` and had zero observed error. A direct baseline/candidate capture comparison covered 438,272 samples with maximum absolute difference zero.

These detailed numerical/allocation results were not independently repeated and captured on native ARM64 with all trained models. The native CSV checksums agree, but checksum equality is not per-sample equivalence. No full plug-in regression suite or live RME dropout/overrun measurement was performed as part of this investigation.

Allocation safety assumes processing within the prepared maximum block size. The existing A2 path reallocates if that maximum is exceeded. Allocation tracking does not prove absence of every OS-level lock or syscall. Container model changes perform Reset/prewarming under a mutex; that control operation is distinct from steady-state inference.

## Actual models and Slim behavior

The twelve files under `Models/V Amps` are SlimmableContainer models, each containing a complete three-channel A2 nano and eight-channel A2 standard model at 48 kHz. All 24 extracted specs under `Models/a2-real-models` exactly match the nested JSON, including weights, and passed the strict A2 detector. The native real-model CSV benchmarks only the twelve eight-channel versions.

For these particular containers:

| Slim value | Selected model | Consequence |
|---|---|---|
| 0.50 through 1.00 | Eight-channel standard | Same inference workload; 0.8 saves no CPU relative to 1.0. |
| 0.00 through 0.49 | Three-channel nano | Smaller trained model; no further savings by moving within this range. |

The threshold comparison is strict `<`; exactly 0.50 still selects standard. Slim defaults to 1.0, although a saved patch can override it. Zero does not mute the amp. This is model selection, not continuous width interpolation. Nano may sound different; changing to it is a tonal tradeoff, not a transparent inference optimization. Native real-model nano CPU savings have not been measured in the supplied CSVs. Across instances, the approximate saving would be the sum of each switched instance's standard-minus-nano processing time; total host CPU also includes all other work.

## Implications and next work for Puke

The existing inference implementation is already specialized, but no performance ceiling has been established. Enabling A2 fast dispatch again, enabling the rejected patch, or treating a Slim value of 0.8 as a CPU saving would not address the demonstrated problem.

For host-side investigation, useful next measurements are a native Release callback profile of the actual saved rig, per-node processing durations, the true mixture of model/Slim settings, callback tails and real driver underruns at 64/128 samples. Record telemetry into preallocated storage and inspect it outside the callback. The 48 kHz buffer periods are 1.333 ms and 2.667 ms, respectively; include the Global patch, other effects, resampling and plug-in overhead when comparing whole-callback time with these periods.

Verify how Puke handles unselected chains. Their cost may remain present, but suspending processing changes model history, effect tails and later reactivation behavior. No host routing/suspension optimization was implemented or validated here.

For further plug-in work, plausible new hypotheses are fixed eight-channel ARM64 NEON/FMA kernels, frame tiling, fusion of post-convolution passes and less history-mirror copying. Start with a short native pilot on real weights before another full matrix. Improve measurement provenance by recording executable hashes, actual compiler flags, architecture and raw block timings; pair or randomize variants per model to reduce ordering effects. Preserve portability, realtime constraints, state semantics and numerical validation.

## Handoff artifacts and reproduction

The current working tree contains uncommitted core-submodule edits and untracked tools/reports/data. Checking out the listed HEAD revisions alone will not reproduce this work. Preserve these files explicitly when handing them to the Puke agent. The existing dirty `iPlug2` submodule is unrelated to this investigation.

| Artifact | Location |
|---|---|
| This consolidated report | `docs/a2-inference/PUKE-CODEX-HANDOFF.md` |
| Decisive raw native real-model measurements | `a2-real-results.csv` |
| Native synthetic measurements | `a2-native-results.csv` |
| Initial translated measurements | `docs/a2-inference/translated-results.csv` |
| Rejected experiment patch | `docs/a2-inference/lazy-product-experiment.patch` |
| Detailed latest analysis | `docs/a2-inference/real-model-arm64-results.md` |
| Test evidence | `docs/a2-inference/baseline-tests.txt`, `docs/a2-inference/candidate-tests.txt`, `docs/a2-inference/direct-comparison.txt` |
| Benchmark and matrix runner | `NeuralAmpModelerCore/tools/bench_a2_fast.cpp`, `NeuralAmpModelerCore/tools/benchmark_a2_matrix.py` |
| Analysis and output comparison | `NeuralAmpModelerCore/tools/summarize_a2_matrix.py`, `NeuralAmpModelerCore/tools/compare_a2_outputs.py` |
| Container fixture extraction | `NeuralAmpModelerCore/tools/extract_a2_submodels.py` |
| Focused tests and fixtures | `NeuralAmpModelerCore/tools/test_a2.cpp`, `NeuralAmpModelerCore/tools/test/test_a2_fast.cpp`, `NeuralAmpModelerCore/tools/test/a2_fixture.h` |
| Build changes | `NeuralAmpModelerCore/tools/CMakeLists.txt` |
| Actual model files | `Models/V Amps`, `Models/a2-real-models` |

The source currently contains `W * input_block` and `l1x1_mat * ztile`, not the candidate's `lazyProduct` expressions. Benchmark/test additions and comment corrections are retained; production model architecture, weights, calibration and routing were not changed.

To regenerate the real-model summary from the plug-in repository root:

```sh
python3 NeuralAmpModelerCore/tools/summarize_a2_matrix.py a2-real-results.csv
```

To build the existing benchmark and focused tests on native macOS, after transferring the working-tree files:

```sh
cmake -S NeuralAmpModelerCore -B /tmp/a2-handoff \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build /tmp/a2-handoff --target bench_a2_fast test_a2 -j2
file /tmp/a2-handoff/tools/bench_a2_fast
/tmp/a2-handoff/tools/test_a2
```

The experiment patch is evidence, not a recommended production change. If it is ever revisited, its path from `git -C NeuralAmpModelerCore` is `../docs/a2-inference/lazy-product-experiment.patch`. Verify whether it is already applied before applying or reversing it; copied benchmark executables retain the implementation they were compiled with even after the source is restored.

SHA-256 of the decisive raw CSV:

```text
53cfd13d6986db9d6a192c7677f5b6719b16c6dff827ebc156c0035870174978  a2-real-results.csv
```
