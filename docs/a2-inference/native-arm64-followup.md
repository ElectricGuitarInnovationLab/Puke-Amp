# Native ARM64 follow-up

The subsequent [real-model ARM64 analysis](real-model-arm64-results.md) completes
the comparison and rejects enabling this candidate for the current rig objective.
The synthetic results and extraction procedure below are retained for reference.

The supplied `a2-native-results.csv` has 288 complete rows: three repeats,
two synthetic eight-channel seeds, three frame sizes, one or eleven independent
instances, four signals, and both baseline and packing-free candidate. All
144 baseline/candidate pairs have identical printed output checksums. These
checksums are a smoke check; the optimized correctness tests remain the
numerical reference.

For varying input, the median of six per-run/model block medians was:

| Frames | Instances | Baseline (µs) | Candidate (µs) | Median change |
|---:|---:|---:|---:|---:|
| 64 | 1 | 53.15 | 48.94 | 7.9% faster |
| 64 | 11 | 590.48 | 562.56 | 4.7% faster |
| 128 | 1 | 99.69 | 98.40 | 1.3% faster |
| 128 | 11 | 1121.94 | 1094.10 | 2.5% faster |
| 256 | 1 | 191.35 | 193.79 | 1.3% slower |
| 256 | 11 | 2130.52 | 2166.81 | 1.7% slower |

These are full inner WaveNet DSP processing times, not kernel-only or VST3
processing. At 64 frames with eleven instances, three of six paired
varying-input p99 measurements regressed; the median of row p99s rose from
683 to 966 µs. At 128 frames with eleven instances, the median of row p99s
rose from 1428 to 1542 µs. Large maximum-time outliers occurred for both
variants. Offline timing cannot establish 64-sample live reliability, and
the 256-frame regression argues against enabling this kernel at every size.
The original `a2-native-results.csv` remains the raw measurement record.

The twelve supplied files in `Models/V Amps` are SlimmableContainer files.
Each holds one three-channel A2 nano and one eight-channel A2 standard WaveNet
with unchanged 48 kHz sample rate. All 24 nested models passed the benchmark's
strict A2 detector after extraction. The plug-in Slim parameter defaults to
1.0 and the container's `max_value` thresholds are 0.5 and 1.0, so the
eight-channel model is selected at default and at any Slim value >= 0.5.
The three-channel model is selected below 0.5. A saved Puke patch may override
the plug-in default.

The benchmark accepts standalone WaveNet files. To measure the real weights
without changing the source containers, extract their nested model specs:

```sh
python3 NeuralAmpModelerCore/tools/extract_a2_submodels.py \
  "Models/V Amps" /tmp/a2-real-models
python3 NeuralAmpModelerCore/tools/benchmark_a2_matrix.py \
  /tmp/a2-baseline /tmp/a2-candidate \
  --models /tmp/a2-real-models/*__a2_8.nam \
  --output a2-real-results.csv
python3 NeuralAmpModelerCore/tools/summarize_a2_matrix.py a2-real-results.csv
```

The generated files are benchmark fixtures that preserve the complete nested
model JSON, including weights. They do not alter the rig files. Use
`*__a2_3.nam` for a separate nano run if the patch uses Slim below 0.5.
The matrix runner retains 64/128/256 frames, one/eleven instances, varying,
silence, constant and tiny signals, and alternated variant order.

Keep the production A2 kernel unchanged until real-weight ARM64 results and
live Puke testing show a useful 64/128-sample gain without unacceptable tail
behavior. A size-specific candidate can be considered if that evidence holds.
