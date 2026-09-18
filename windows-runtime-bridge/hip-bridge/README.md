# hip-bridge

The #6 interop spike from `docs/linux-support-spec.md`: does a D3D12
resource's memory survive a real trip through a HIP device on this Linux
host, end to end, under Proton?

## Architecture

Two small programs instead of one complex Wine-internals patch — see the
spec's §8 item 6 for why this design was chosen over the originally
proposed Wine-unixlib/native-module approach (that approach is still
possible later if the extra IPC hop here turns out to matter for
latency/production use, but this is far cheaper to build and verify first).

```
 Windows/Proton side (pe-client)          Linux native side (native-daemon)
 ┌─────────────────────────────┐          ┌──────────────────────────────┐
 │ D3D12 shared texture         │          │                              │
 │   -> own Vulkan device       │          │  dlopen("libamdhip64.so.7")  │
 │   -> imported as VkImage     │  TCP     │  hipMalloc / hipMemcpy       │
 │   -> host-visible staging    │ ───────► │  (host -> device -> host)    │
 │      buffer, CPU-mapped      │ ◄─────── │                              │
 │   -> diff bytes               │          │                              │
 └─────────────────────────────┘          └──────────────────────────────┘
```

The D3D12↔Vulkan half reuses the transport pattern already proven working
by zmodelerlover's MIT-licensed `src/vkbridge/vkbridge.cpp`
(`VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT`, imported straight
from a Win32 `HANDLE` — no raw Linux fd needed anywhere on that side).

## Status (2026-09-13)

- **`native-daemon`: built, and independently verified against real
  hardware.** Round-tripped a 1MB random payload through `hipMalloc` /
  `hipMemcpy` (host→device→host) on the RX 9070 XT via
  `dlopen("libamdhip64.so.7")` — no dev headers or link-time `-lamdhip64`
  needed. Bytes came back identical. This also empirically confirms the
  `hipMemcpyKind`/`hipError_t` numeric values used here and in the
  `amdhip64_7` stub, since they worked against the real runtime.
- **`pe-client`: builds cleanly for `x86_64-pc-windows-gnu`**, imports
  `d3d12.dll`/`dxgi.dll` as expected, loads `vulkan-1.dll` dynamically at
  runtime via `ash::Entry::load()`. **Not yet run under Proton** — that's
  the next step, and the actual point of this spike (does `winevulkan`
  expose `VK_KHR_external_memory_win32` to guest apps at all? Does the
  import actually succeed against RADV?).
- The `pe-client`'s D3D12 write path is currently simplified: it writes
  the test pattern directly into the mapped Vulkan staging buffer rather
  than through a D3D12 upload-heap copy (which is what `vkbridge.cpp` does
  and which is the next piece to wire in). This first run is about
  proving the Vulkan↔HIP leg of the chain; the D3D12 write leg reuses an
  already-verified pattern and is lower-risk to add once the harder leg is
  confirmed.

## Run it

1. On the Linux host:
   ```
   cd native-daemon && cargo build --release
   ./target/release/hip-bridge-daemon
   ```
2. Under Proton (e.g. via a Steam non-Steam-game entry, or `wine`/`proton run`
   pointed at the built `.exe`), on the same machine so `127.0.0.1` reaches
   the daemon above:
   ```
   cd pe-client && cargo build --release --target x86_64-pc-windows-gnu
   # then run target/x86_64-pc-windows-gnu/release/hip-bridge-pe-client.exe
   # under Proton
   ```
3. Watch both sides' stderr. Success looks like:
   `SUCCESS: N bytes identical after D3D12 -> Vulkan -> TCP -> HIP device -> TCP -> Vulkan`
