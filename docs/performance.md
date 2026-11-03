# Performance

Measurements against the targets in [PLAN.md §6](PLAN.md#6-performance-targets) and §2a
(old hardware). Reproduce them with the developer tools (`-DSC_BUILD_TOOLS=ON`):

```bash
build/rel/tools/sc_make_bench_model bench.stp 1000 2   # 1,000 distinct parts, 2,000 instances
build/rel/tools/sc_bench_import bench.stp               # read / transfer / mesh / memory
build/rel/tools/sc_bench_import bench.stp cache-dir     # run twice: store, then load from cache
```

The benchmark model is synthetic: filleted plates with 2–6 holes each, every part distinct (no
instancing shortcut for the reader), one color per part. CI runs the same benchmark on a 500-part
model with OCCT 8.0.1 and prints it in the Linux job summary ("Import benchmark").

## Import (91 MB STEP, 1,000 parts, 2,000 instances)

Release build, OCCT 7.6.3, a 4-vCPU cloud container (slower per core than a current laptop).

| Stage | 1 core | 4 cores |
|---|---|---|
| Read (STEP parser) | 13.8 s | 13.1 s |
| Transfer (to XCAF) | 16.4 s | 15.6 s |
| Mesh | 12.4 s | **4.8 s** |
| Assembly index + PMI | 0.07 s | 0.06 s |
| **Total** | 47.1 s | **38.0 s** |
| Re-open from cache | | **1.3 s** |

- **Meshing is parallel** across parts (OCCT's thread pool), or across faces when a model has only
  a few large parts. It was serial before M5.
- **Read and transfer are single-threaded inside OCCT** and dominate. They scale with file size,
  so a 50 MB file of the same kind takes about 16 s here. The UI stays responsive (import runs on a
  worker thread with progress and cancel). The §6 target of ~10 s on a modern laptop is met only by
  machines with clearly faster single-core speed than this container; the cache makes every
  re-open near-instant regardless.
- **Open-file cache:** the XCAF document with triangulation is stored as BinXCAF (plus a sidecar
  for cameras, units and import messages) under the OS cache folder, keyed by size, mtime, sampled
  content hash, app/OCCT versions and mesh parameters. Storing takes 0.7 s for this model (108 MB on
  disk) and happens on the import thread. The cache is capped at 1 GB (least recently used entries
  are removed) and can be turned off or cleared in Preferences.

## Memory

| Scenario | Resident memory |
|---|---|
| Idle, empty window (Mesa llvmpipe) | 246 MB, of which ~135 MB is llvmpipe's software framebuffers and ~70 MB is libLLVM/gallium code; the app's own heap is ~25 MB |
| Peak while importing the 91 MB model | 895 MB |
| After import, model shown | 325 MB (was 851 MB before releasing freed memory) |
| After re-open from cache | 256 MB |

- The STEP reader's model is freed after transfer, but glibc kept the pages mapped: the app now
  returns freed memory to the OS after each import and when a model is closed
  (`core::releaseFreedMemory`, a no-op outside glibc).
- The §2a "under 2 GB peak, under 1 GB after load" budget for a 50 MB file is met with a wide
  margin. The idle target (< 150 MB) cannot be measured on llvmpipe because the software renderer's
  buffers live in our process; with a hardware driver they are in video memory, which leaves an
  estimated ~100 MB. Not yet verified on a real GPU.

## Startup

The main window is visible ~0.5 s after launch (warm disk cache, Release build). Cold start on an
HDD is not measured yet.

## Navigation

- Parts whose bounding box projects to under 4 px are skipped while the camera moves (orbit, pan,
  zoom) and drawn again as soon as it stops (OCCT Z-layer culling size). Frustum culling is on.
- Redraws happen only on demand; hover highlighting redraws only the immediate layer.
- Frame rates on the 5M-triangle target have not been measured on real GPUs; CI renders with Mesa
  llvmpipe, which is not representative.

## Not done yet

- A coarse first mesh followed by refinement (the plan's "coarse first pass").
- Lazy loading of the assembly tree for very large assemblies (the tree is built eagerly; it takes
  milliseconds for 2,000 nodes).
- Measurements on the 2016 reference machine and on real GPUs (Windows/macOS).
