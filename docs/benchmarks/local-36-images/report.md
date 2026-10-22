# ImageBatch benchmark

Version: 1.1.0; Qt: 6.8.3; compiler: 13.1.0.
OS: Windows 10 Version 22H2; architecture: x86_64; hardware threads: 6.
Dataset: 36 deterministic mixed JPEG/PNG images, 3 measured repetitions per worker count.
Each measurement uses a fresh process. One warmup is excluded; filesystem caches are not cleared.
Orders rotate 1/2/4, 2/4/1, 4/1/2. Output: JPG, max edge 1920, quality 90, identical watermark.
Qt helper pool is limited to one thread. Estimated buffer budget is 512 MiB.

| Workers | Median wall time (ms) | Speedup vs 1 |
| --- | ---: | ---: |
| 1 | 5446 | 1 |
| 2 | 3274 | 1.663 |
| 4 | 2067 | 2.635 |

All measured output SHA-256 values match the warmup output; source hashes are unchanged.
Working set is sampled every 5 ms and is not an exact instantaneous peak. Estimated reservations are separate from process memory.
Synthetic workload results apply to this machine and protocol; more workers do not guarantee a speedup.
Raw measurement JSON and results.csv are retained with this report.
