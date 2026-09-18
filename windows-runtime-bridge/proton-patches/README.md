# Proton/Wine patches

**No PRs are open against `ValveSoftware/wine` or `HansKristian-Work/vkd3d-proton` yet.**
There is no prebuilt patched Proton to download, and no upstream branch
to point at. What exists is exactly what's in this directory: two raw,
reviewable patch files, plus the instructions below to build a real,
byte-compatible Proton yourself from them using Valve's own official
build pipeline — the same one Valve uses to produce the Proton builds
Steam ships.

Both patches are **opt-in only** (gated behind `VKD3D_CONFIG=external_memory_fd`)
— building and running a patched Proton has zero effect on any other
game unless that flag is set.

## What each patch does, and why it's needed

D3D12↔HIP interop (what `windows-runtime-bridge/hip-unixlib/` needs to share GPU memory
between Daniel's runtime and the real ROCm driver) requires exporting a
D3D12 shared resource's backing memory as a real Linux file descriptor.
Neither patch invents new capability — Vulkan's `VK_KHR_external_memory_fd`
already does exactly this — they wire an existing extension through two
places that currently block it:

- **`wine-winevulkan-external-memory-fd.patch`** (against [ValveSoftware/wine](https://github.com/ValveSoftware/wine),
  `dlls/winevulkan/make_vulkan`) — a one-line removal from Wine's own
  `UNEXPOSED_EXTENSIONS` allowlist. `VK_KHR_external_memory_fd` is
  already implemented by Wine's Vulkan thunk generator, just
  deliberately hidden from every Windows-side Vulkan client
  (confirmed by reading `dlls/winevulkan/vulkan_thunks.c` directly —
  the generated code exists, `vkGetDeviceProcAddr` just returns NULL
  for it). This patch un-hides it.
- **`vkd3d-proton-external-memory-fd.patch`** (against [HansKristian-Work/vkd3d-proton](https://github.com/HansKristian-Work/vkd3d-proton))
  — adds the `VKD3D_CONFIG=external_memory_fd` flag, enables the
  now-unhidden Vulkan extension when set, and makes `CreateSharedHandle`
  hand back a real fd instead of the Win32/`D3DKMTShareObjects` path
  (which reports success under Wine without producing anything usable).
  Deliberately scoped to shared *buffers* only, never textures — see
  the in-patch comments for why. Includes a real test suite
  (`tests/d3d12_external_memory_fd.c`, 7 tests) covering both allocation
  routes, the opt-in-off regression case, and a live diagnostic probe
  for the separate GPU-fault investigation in `docs/linux-support-spec.md`.

Full narrative of how these were found, built, and debugged (including
two real bugs caught and fixed along the way — a null function-pointer
crash and a missing dimension check) is in `docs/linux-support-spec.md`,
§15–§27.

## Base commits these patches were generated against

- `vkd3d-proton-external-memory-fd.patch`: [HansKristian-Work/vkd3d-proton](https://github.com/HansKristian-Work/vkd3d-proton) `2a38e7991f851e85151c104504d5e343530d1523`
- `wine-winevulkan-external-memory-fd.patch`: [ValveSoftware/wine](https://github.com/ValveSoftware/wine) `dc26e61847081a1b5cb0733dc30feba6ee575482`

**If you're building against `ValveSoftware/Proton`'s pinned submodule
commits instead (recommended — see below), these may not apply
cleanly as-is.** In particular, older pinned vkd3d-proton commits (this
was hit against Proton `proton-11.0-2c`'s pinned commit, `212991f`)
use an older config-flag mechanism (`#define VKD3D_CONFIG_FLAG_*` in
`include/vkd3d.h` + a name table in `device.c`, instead of the current
`VKD3D_DECL_CONFIG` X-macro in `config_flag_decl.h`). Porting means
reusing a vacant bit in the older bitmask instead of adding a new
`VKD3D_DECL_CONFIG` line — everything else (the buffer allocation
changes, `CreateSharedHandle`, all 7 tests) applies unchanged, since
none of it touches the refactored area. The Wine patch applied with
zero changes against the pinned tag tested. See `docs/linux-support-spec.md`
§27b for the full account.

## How to build and test a patched Proton, via Valve's own pipeline

This mirrors exactly how this project itself validated the patches —
building through host-installed dependencies produced a subtly
different binary (missing ~15 `--without-X` configure flags Proton's
real build recipe never needs, because it never touches host
libraries) that caused confusing, hard-to-diagnose crashes unrelated
to the patch itself. Building through Valve's pinned SDK container
avoids that class of problem entirely.

1. **Clone Proton with submodules**, at whatever tag you're targeting
   (this was validated against `proton-11.0-2c`):

   ```sh
   git clone --recursive --branch proton-11.0-2c https://github.com/ValveSoftware/Proton.git
   cd Proton
   git submodule update --init --recursive
   ```

   (`--init --recursive` matters — a non-recursive submodule update
   misses nested submodules like `dxvk-nvapi`'s own `Vulkan-Headers`,
   which fails the build at configure time with a missing-include-dir
   error.)

2. **Apply the patches** against the pinned submodules:

   ```sh
   cd wine && git apply /path/to/windows-runtime-bridge/proton-patches/wine-winevulkan-external-memory-fd.patch && cd ..
   cd vkd3d-proton && git apply /path/to/windows-runtime-bridge/proton-patches/vkd3d-proton-external-memory-fd.patch && cd ..
   ```

   If `git apply` fails on the vkd3d-proton patch due to the config-flag
   mechanism mismatch above, port it by hand per the note above — the
   patch is small and well-commented, and the conflict will be isolated
   to `config_flag_decl.h`/`config_flags.h`.

3. **Build inside Valve's official Proton SDK container** — do not
   build against host libraries; Proton's build recipe assumes the
   container's exact dependency versions. Valve publishes the pinned
   container image and build tooling in the `Proton` repo itself
   (`docker/` directory) — follow that repo's own `README.md` /
   `docs/CONTRIBUTING.md` for the current `docker run`/`podman run`
   invocation and image tag for the tag you checked out, then run the
   actual build from inside it:

   ```sh
   ./configure.sh --container-engine docker   # or podman; see Proton's own docs for current flags
   make redist
   ```

   `make redist` output lands under `build/redist`. (If you need debug
   symbols/trace-level logging for further investigation — not required
   to just test the patch — pass `UNSTRIPPED_BUILD=1` to `make redist`;
   note Make's dependency tracking doesn't always notice this variable
   changed, so delete the `.vkd3d-proton-x86_64-*` stamp files under
   the build directory to force a clean reconfigure if a previous build
   used a different value.)

4. **Install as a custom Steam compatibility tool** and select it for
   testing, rather than replacing your normal Proton:

   ```sh
   mkdir -p ~/.steam/root/compatibilitytools.d/proton-fdtest
   cp -r build/redist/* ~/.steam/root/compatibilitytools.d/proton-fdtest/
   ```

   Restart Steam, then pick "proton-fdtest" (or whatever you named it)
   under the game's Properties → Compatibility tab.

5. **Run the vkd3d-proton test suite** (fast, no game required) to
   confirm the patch itself works before testing against any real game:

   ```sh
   VKD3D_CONFIG=external_memory_fd WINEPREFIX=/path/to/a/test/prefix \
       wine build/redist/lib/wine/x86_64-windows/.../tests/d3d12  # exact test binary path depends on the build layout
   ```

   All 7 `test_external_memory_fd_*` tests should pass with the flag
   set; the two `_default_off_*` tests specifically verify nothing
   changes when it's unset.

6. **Test against the real game** by setting the Proton version above
   in Steam and adding `VKD3D_CONFIG=external_memory_fd` to the game's
   launch options (see the root `README.md` and `docs/linux-support-spec.md`
   for the rest of the launch-option string this project actually uses).
