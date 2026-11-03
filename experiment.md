# Snapdragon 8 Gen 3 TG Performance Investigation

## Status

This document records an observed performance difference and proposes a controlled experiment to identify its cause. The hypotheses below are **not conclusions**; each must be validated with measurements.

## Observed result

- SoC: Qualcomm Snapdragon 8 Gen 3 (Hexagon v75)
- Model: Gemma 4 E2B
- Workload: token generation (TG / decode)
- Observation reported by the repository author: the dspqueue backend generates tokens at **more than 2x** the speed of the FastRPC/ION-mempool backend on the same device.

The exact tok/s values, build revisions, model quantization, context length, generation settings, thermal state, and backend configuration have not yet been recorded here. Add them before treating this as a reproducible benchmark.

## Main hypotheses

### H1. DSP worker/thread budget on v75

The FastRPC path may have a smaller effective worker budget than dspqueue on v75. FastRPC can require threads for the RPC entry/main path and auxiliary work (including HMX queue handling), in addition to compute workers. If the FastRPC implementation clamps its worker count to fit the six hardware DSP threads, it may leave fewer workers for graph execution.

**Test**
1. Log the actual number of compute workers, HMX queue threads, and other QuRT threads used by each backend at runtime.
2. Sweep the FastRPC compute worker count over every supported value (for example 2, 3, and 4), without changing other settings.
3. If possible, run dspqueue with the same effective compute-worker count.
4. Record TG tok/s and per-token DSP busy/idle time.

**Interpretation:** If matching the effective worker budget substantially narrows the gap, thread allocation is a major contributor. Do not infer this from configured thread counts alone; record the threads actually created and scheduled.

### H2. Synchronous FastRPC invocation versus asynchronous dspqueue submission

Decode has a small amount of work per token compared with prompt processing. Host-side descriptor preparation, submission, wake-up, and completion waits can therefore become a large fraction of each token's latency. An asynchronous queue may overlap AP-side preparation with DSP execution, while a synchronous invoke can introduce a serialization point.

**Test**
Instrument both backends with monotonic timestamps around:
- graph/batch preparation on AP;
- queue or RPC submission;
- time blocked waiting for completion;
- DSP execution (where independently measurable);
- result handling and synchronization.

Report counts and totals **per generated token**, not just averages per invocation:
- number of submissions/invokes;
- total AP preparation time;
- total submit-to-completion wait time;
- total DSP execution time;
- p50/p95 submission and completion latency.

**Interpretation:** A large FastRPC wait/dispatch component with similar DSP execution time points to control-path overhead or lost overlap. Avoid counting DSP execution twice if the completion interval already includes it.

### H3. Cache maintenance and memory synchronization

The two backends may differ in cache ownership, clean/invalidate policy, synchronization granularity, and whether immutable weights are repeatedly invalidated. TG repeatedly reads model weights, so unnecessary cache maintenance can be costly.

**Test**
1. Count cache clean/invalidate calls by buffer role (weights, activations, KV cache, scratch, output).
2. Measure bytes and elapsed time per operation, where the platform exposes reliable timing.
3. Check whether immutable weights are invalidated on every token or only when first made visible to DSP.
4. Verify that any experimental reduction in cache maintenance preserves correctness; do not remove required coherency operations.

**Interpretation:** A high per-token cache-maintenance cost in FastRPC, absent in dspqueue, would support this hypothesis. Record both call count and bytes; call count alone can mislead.

### H4. Decode graph shape, kernel selection, and fallback paths

The backends may not execute identical graphs or kernels even when given the same model. Small-batch decode can expose inefficient kernels, layout conversions, CPU fallbacks, or different fusion decisions.

**Test**
- Capture the operation/backend assignment for one representative decode token.
- Compare kernel names, tensor shapes/strides, quantization formats, and CPU fallback operations.
- Record time by operation class: attention, FFN/matmul, normalization, RoPE, KV-cache update, sampling, and layout/copy operations.
- Confirm that both backends use the same model file, quantization, context length, batch/ubatch, and offload layers.

**Interpretation:** If DSP execution time differs materially for the same operation and shape, investigate kernel selection, parallelization, and memory layout before changing the transport layer.

### H5. LM head and output-path differences

If one backend offloads the LM head while the other executes it elsewhere, or if they use different output layouts/repacking, the per-token cost can differ significantly.

**Test**
- Log where the LM head executes in each backend (DSP, CPU, or another backend).
- Measure LM-head execution, logits transfer, repacking/layout conversion, and sampling separately.
- Verify that both paths produce equivalent logits within the expected numerical tolerance.

## Controlled benchmark protocol

For each run, record:

| Field | Value |
|---|---|
| Device / SoC | Snapdragon 8 Gen 3 |
| Firmware / Android build | |
| Repository commit | |
| Backend and build flags | |
| DSP worker configuration (actual) | |
| Model file and quantization | Gemma 4 E2B / |
| Context length at decode start | |
| Prompt tokens / generated tokens | |
| Batch / ubatch | |
| KV-cache type and placement | |
| CPU affinity / thread count | |
| Performance mode / clock policy | |
| Temperature at start and end | |
| Warm-up iterations | |
| Repetitions | |
| TG tok/s (median, min, max) | |
| Per-token latency p50 / p95 | |

Recommended procedure:

1. Use the exact same model file, prompt, context length, generation parameters, and number of generated tokens.
2. Warm up both backends before measuring. Exclude model load and prompt processing from TG timing.
3. Alternate backend order (A/B, then B/A) to reduce thermal and clock-order bias.
4. Run at least five measured repetitions per configuration; report median and spread.
5. Keep device performance mode, CPU affinity, background load, and cooling conditions constant.
6. Check output correctness and token count. Do not compare runs with different stopping conditions.
7. Report PP and TG separately. This investigation is specifically about TG.

## Suggested result table

| Backend | Effective DSP workers | Submissions/token | Wait ms/token | Cache sync ms/token | DSP compute ms/token | TG tok/s |
|---|---:|---:|---:|---:|---:|---:|
| dspqueue | | | | | | |
| FastRPC/mempool | | | | | | |

## Recommended investigation order

1. Establish a reproducible baseline and fill in the benchmark metadata.
2. Log actual DSP thread/worker creation and scheduling on both backends.
3. Add per-token submit/wait/DSP timing to isolate synchronous dispatch overhead.
4. Count and time cache maintenance by buffer role.
5. Compare operation placement and kernel selection, especially attention, FFN, KV-cache update, and LM head.
6. Change one factor at a time and rerun the same benchmark.

## Important caveats

- A twofold TG gap does not, by itself, prove that FastRPC, mempool, cache maintenance, or thread clamping is the cause.
- Fewer workers alone would not normally be expected to explain a greater-than-2x gap; it may combine with dispatch overhead, cache behavior, or different kernels.
- Do not compare results across different commits or model quantizations without calling that out.
- Keep correctness and cache coherency intact while instrumenting or optimizing.

