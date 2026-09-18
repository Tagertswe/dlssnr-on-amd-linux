# amdhip64_7 stub

A clean-room stub of the HIP runtime import surface that
[danielblnc/DLSS-NR-on-AMD](https://github.com/danielblnc/DLSS-NR-on-AMD)'s
standalone `version.dll` proxy links against (`amdhip64_7.dll`).

Under Proton there is no real Windows HIP/ROCm driver, so without something
satisfying that import the Windows loader won't let the proxy DLL load at
all. This stub exists purely to unblock that load so we can observe how the
proxy behaves once it discovers HIP is unavailable — the actual question
being investigated (see `docs/linux-support-spec.md`, open question #5).

Every exported call is logged with its arguments to
`amdhip64_7_stub.log`, written next to the game executable.

This crate contains no proprietary code, weights, or reverse-engineered
logic — only the public HIP function *names* (as seen in the proxy's own
import table) wired to trivial "not supported" responses.

## Build

```
rustup target add x86_64-pc-windows-gnu   # one-time
cargo build --release --target x86_64-pc-windows-gnu
```

Output: `target/x86_64-pc-windows-gnu/release/amdhip64_7.dll`

## Use

Copy the built DLL next to `Cyberpunk2077.exe`, alongside danielblnc's own
`version.dll` runtime build (obtained separately — see
`docs/linux-support-spec.md` §1b for how it was extracted and verified).
Never commit that runtime build itself; it belongs in the gitignored
`windows-runtime-bridge/vendor/` directory locally, not in this repo.

## Caveat

The `hipError_t` numeric values used here are best-effort recollections of
AMD's public HIP enum, written without access to a copy of
`hip_runtime_api.h`. They should be re-verified against a real ROCm header
before being treated as anything more than a load/hook smoke test.
