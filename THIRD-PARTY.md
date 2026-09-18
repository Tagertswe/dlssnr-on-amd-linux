# Third-party notices

This repo's own code is MIT licensed (see `LICENSE`). Two small files
under `windows-runtime-bridge/investigations/` are adapted (C ports, restructured to
this project's style - not copied verbatim) from a separate MIT-licensed
project, and carry that project's license notice as required:

- `windows-runtime-bridge/investigations/snapshot_gate.h`/`.c`
- The ring-buffer/deferred-fence submission logic in
  `windows-runtime-bridge/investigations/faithful_submission_probe.c`

Adapted from **guentra/dlss5-amd-hip-linux**
(<https://github.com/guentra/dlss5-amd-hip-linux>),
`src/native_snapshot_gate.h` and `src/native_game_submission.h`:

```
MIT License

Copyright (c) 2026 Kien

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

Everything else in this repo is original work under this repo's own
MIT license, or interoperates with third-party proprietary software
(danielblnc's runtime, NVIDIA's weights) without including any of it -
see `README.md`'s Scope section and `CLAUDE.md`.
