# Draft GitHub issue for guentra/dlss5-amd-hip-linux

**Status: draft only, not sent.** Review and edit before posting.

---

**Title:** Collaboration / community space? (not a bug report)

**Body:**

Hey — not a bug report, just a question about collaboration.

I've been working on a separate Linux/Proton project bringing DLSS-NR
to AMD GPUs (a HIP compatibility shim for danielblnc's runtime, tested
on Cyberpunk 2077 / RX 9070 XT), and ran into the same general problem
space your submission-boundary work deals with: getting a D3D12↔HIP
handoff to behave correctly under Wine/Proton on AMD.

I read through `native_game_submission.h` and `native_snapshot_gate.h`
properly, not just the README - real, careful engineering (the ring-
buffer fence-wait discipline, the producer-ordering check, the
"a timeout is not cancellation, don't free GPU-referenced storage"
handling). I ended up adapting the ring-buffer submission logic and
the snapshot-gate check into a standalone test probe on my own side
(MIT-attributed, obviously) to verify realistic-cadence submission
against vkd3d-proton on my hardware - it passed cleanly, for what it's
worth.

One finding that might be useful to you specifically, separate from
that: I found that a GPU-side spin-wait pattern (used for D3D12↔compute
synchronization in the runtime I've been bridging) can trigger a real,
silent AMDGPU kernel-level ring timeout + reset under this stack -
`dmesg` shows `ring gfx_0.0.0 timeout` -> `Starting gfx_0.0.0 ring
reset` -> `device wedged, but recovered through reset`, completely
invisible to userspace/Vulkan (no `VK_ERROR_DEVICE_LOST`, no error
in-game). It happened on every single GPU-side-spin-wait session I
tested, self-recovering via the kernel's own reset path, but with no
reliable resumption of real work afterward. If your own submission
path does anything spin-wait-shaped at the D3D12/HIP boundary, this
might be worth checking for on your end too - happy to share more
detail if useful.

Given your repo doesn't have Discussions enabled and I couldn't find
another public contact channel - would you be open to a Discord (or
enabling GitHub Discussions) for this kind of back-and-forth? Also
happy to just keep using issues if you'd rather not stand up another
channel. Either way, I'd be interested in contributing where it makes
sense, particularly around Linux/Proton-specific quirks (ROCm library
path resolution under pressure-vessel sandboxing, prefix/DLL-override
gotchas, that kind of thing) - it sounds like your project has moved
further on the actual neural-network side than I have.

---

## Notes for review before sending

- No proprietary code, weights, or danielblnc-specific implementation
  detail is included - only the general AMDGPU/kernel-level mechanism
  (real, host-level `dmesg` output), which isn't tied to any one
  runtime's proprietary internals.
- The ring-hang description is deliberately general (not tied to
  "danielblnc's shader" specifically) so it reads as a genuine,
  transferable finding rather than a claim about his code.
- The "I read your code and adapted part of it" paragraph is genuine -
  this actually happened (`daniel/investigations/snapshot_gate.*`,
  `faithful_submission_probe.c`, properly MIT-attributed in
  `THIRD-PARTY.md`), not flattery. Only mentions the clean synthetic
  probe result (real and true) - deliberately does **not** mention the
  separate, inconclusive ReShade-backend-hookup test from later the
  same session, since that result is unverified and may well be a
  mistake on this project's own side, not a finding about his project.
  Don't add it in without confirming what actually happened there
  first.
- Doesn't commit to anything (no promise of a specific contribution,
  no assumption he wants help) - just opens the door.
- Consider trimming if it reads as too long for a first contact -
  the ring-hang paragraph is still the real hook; the code-reading
  paragraph could be shortened to a single sentence if this feels
  long.
