# Real-model ARM64 decision

Retain the existing Eigen GEMM implementation. Do not enable the packing-free
`lazyProduct` experiment globally or add a 64-frame dispatch based on this data.
The 11-instance improvement at 64 frames is modest and inconsistent, its p99
results are mixed, and the 128/256-frame median regressions are repeatable.
No further run is needed to reject this candidate for the current rig objective.

## Data validation

Source: repository-root `a2-real-results.csv`, supplied from the user's Apple
Silicon benchmark run. It contains 1,728 rows: twelve eight-channel trained
models, three repeats, 64/128/256 frames, one/eleven instances, four input types,
and two implementations. Each row summarizes 1,280 observations at 48 kHz.
All 864 baseline/candidate pairs are present with no duplicate pair members;
reported numeric fields are finite. All 864 printed checksums match. A checksum
match is not a per-sample numerical equivalence test.

The 24 files in `Models/a2-real-models` exactly match the nested model JSON in
the twelve original `Models/V Amps` containers, including every weight. This
CSV measures the twelve eight-channel variants; it contains no nano timing rows.
The prior optimized numerical and allocation tests remain the correctness
evidence for the candidate. This follow-up is an analysis of native performance,
not a new native allocation or numerical-equivalence test.

## Varying-input results

Microseconds per sequential processing group. Each displayed p50/p99 is the
median of 36 row statistics (twelve models times three repeats), not a percentile
pooled from raw block samples. Positive time reduction means faster.

| Frames | Instances | Existing p50 | Candidate p50 | Time reduction | Existing p99 | Candidate p99 |
|---:|---:|---:|---:|---:|---:|---:|
| 64 | 1 | 60.31 | 56.71 | 6.0% | 76.81 | 72.73 |
| 64 | 11 | 690.54 | 677.60 | 1.9% | 954.25 | 1130.96 |
| 128 | 1 | 113.33 | 118.62 | -4.7% | 136.56 | 139.46 |
| 128 | 11 | 1289.08 | 1339.40 | -3.9% | 1739.35 | 1772.62 |
| 256 | 1 | 217.44 | 235.46 | -8.3% | 264.81 | 298.42 |
| 256 | 11 | 2501.60 | 2706.27 | -8.2% | 3222.42 | 5045.46 |

The 64-frame 11-instance difference is about 13 microseconds, roughly 1% of a
64-sample period at 48 kHz. It does not materially establish more live headroom.
The large outliers in both implementations also prevent hardware-buffer
reliability claims from these offline results.

Paired analysis compares each candidate directly with its matching model,
repeat, frame size, signal and instance count. For varying input:

| Frames | Instances | Median paired p50 saving | Candidate p50 wins | Candidate p99 regressions |
|---:|---:|---:|---:|---:|
| 64 | 1 | 6.48% | 34/36 | 14/36 |
| 64 | 11 | 2.53% | 25/36 | 19/36 |
| 128 | 1 | -2.97% | 11/36 | 24/36 |
| 128 | 11 | -2.41% | 11/36 | 19/36 |
| 256 | 1 | -8.30% | 0/36 | 26/36 |
| 256 | 11 | -7.18% | 2/36 | 27/36 |

The median of paired ratios differs from the ratio of aggregate medians in the
first table. Both summaries are shown to avoid hiding pairing or workload spread.
For the 64-frame/11-instance varying case, per-pair p50 changes range from 11.7%
slower to 15.2% faster. Two models have a regression even after taking the median
of their three paired changes: V Lead Lead Layer L and V Overdrive Stereo L.
Only three models win the p50 comparison in all three repeats.

Across 64-frame/11-instance inputs, median paired p50 savings are 2.53% for
varying, 4.24% for silence, 3.88% for constant, and 0.63% for tiny input. Candidate
p99 regressions occur in 19, 9, 11 and 22 of the 36 pairs, respectively.
At 128 and 256 frames, aggregate medians regress for every input category.

## Interpretation and scope

The earlier translated and synthetic measurements overstated the benefit for
these real weights and the user's target workload. Avoiding packing saves some
small-block setup, but this implementation does not recover enough execution
time at the larger buffers. The data does not establish a specific hardware
cause for the timing tails; scheduling, thermal state and background work were
not recorded. A higher candidate p99 is reported as observed, not attributed
entirely to the kernel.

Each 11-instance observation processes eleven independent copies of one model.
It does not reproduce the exact mixture of models, twelve other effects,
Global patch, container wrapper, resampling, VST3 overhead or RME driver in Puke.
Raw per-block samples are not available, so pooled percentiles, exceedance rates
and statistical confidence intervals cannot be recovered from this CSV.

Production inference stays on the existing implementation. The experiment
patch remains available for review; benchmark/test tooling and the measurements
are the deliverables from this investigation. A future optimization should
start with a different concrete hypothesis, such as a native ARM64 fixed-shape
kernel or fewer intermediate buffer passes, and be evaluated first in a short
paired pilot with the real weights before asking for another full matrix.

Reproduce the aggregate table with:

```sh
python3 NeuralAmpModelerCore/tools/summarize_a2_matrix.py a2-real-results.csv
```
