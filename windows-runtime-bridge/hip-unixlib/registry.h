/* Pure, Wine/HIP-independent logic factored out of native.c so it can be
 * unit tested with a plain host compiler - no Wine headers, no ROCm
 * runtime, no cross-compilation needed. */
#ifndef HIP_UNIXLIB_REGISTRY_H
#define HIP_UNIXLIB_REGISTRY_H

/* Bundle format: 24-byte magic "__CLANG_OFFLOAD_BUNDLE__", confirmed via
 * static analysis (docs/linux-support-spec.md \xc2\xa78 item 3). The pointer
 * __hipRegisterFatBinary receives may be the raw bundle pointer, or (the
 * standard compiler-generated layout) a small wrapper struct
 * {magic,version,data,unused} whose `data` field at byte offset 8 is the
 * real bundle pointer - try both. Returns NULL if neither matches.
 * Unlike the earlier socket-IPC port, no total size needs computing:
 * hipModuleLoadData parses the bundle's own embedded metadata itself.
 *
 * Known limitation, deliberately accepted: if `data` is neither a real
 * bundle nor a real wrapper struct, the wrapper-indirection check reads
 * 8 bytes at offset +8 and treats them as a pointer to dereference -
 * with truly arbitrary/attacker-controlled input this could crash on an
 * invalid address. Acceptable here because the only real caller
 * (__hipRegisterFatBinary) only ever receives this pointer from
 * danielblnc's own legitimate compiler-generated registration code, not
 * untrusted external input. */
const void *resolve_bundle_ptr(const void *data);

/* Fat-binary token -> loaded hipModule_t, and host_fn -> hipFunction_t.
 * Fixed-size linear-scan tables; the real workload is small (a couple of
 * fat binaries, a few dozen kernels), so this is plenty. registry_reset()
 * exists only for test isolation - production code never needs it. */
void registry_reset(void);
int registry_add_fatbin(const void *token, void *module);
void *registry_find_module(const void *token);

/* `name` is the real mangled kernel symbol (danielblnc's own
 * device_name string, e.g. "_Z10k_swin_varILi32ELb1EEv9VarParams") -
 * copied into the registry's own storage (truncated, always
 * null-terminated, if longer than the fixed buffer), not just
 * referenced by pointer. This was previously logged once at
 * registration time and then discarded - meaning every later
 * hipLaunchKernel log line only ever had the bare host_fn pointer to
 * go by, and finding out which kernel actually failed in a real crash
 * log meant manually cross-referencing that pointer against an
 * earlier "resolved kernel" line by hand (done repeatedly, by hand,
 * throughout this project's own investigation - see
 * docs/linux-support-spec.md \xc2\xa749). Storing it here lets
 * unix_launch_kernel log the real kernel name on every single launch
 * instead. */
int registry_add_function(const void *host_fn, void *function, const char *name);
void *registry_find_function(const void *host_fn);

/* Returns the real kernel name for a previously-registered host_fn,
 * or NULL if never registered. The returned pointer is owned by the
 * registry and stays valid until registry_reset() - callers must not
 * free it. */
const char *registry_find_function_name(const void *host_fn);

/* host_var -> resolved real device global address, the same shape as
 * host_fn -> hipFunction_t above but kept in its own table rather than
 * reused, since the two address spaces (PE global-variable addresses
 * vs PE function addresses) have no reason to ever be compared against
 * each other - keeping them separate avoids a host_var and a host_fn
 * that happened to share a numeric value ever being confused.
 *
 * docs/linux-support-spec.md's g_e4m3_lut finding: __hipRegisterVar
 * used to be a pure no-op, so hipMemcpyToSymbol had no way to find
 * which real device global a given host_var pointer corresponded to -
 * every real write silently failed. This is the fix: remember what
 * unix_register_var resolved via hipModuleGetGlobal, so
 * unix_memcpy_to_symbol can look it up by the same host_var pointer
 * hipMemcpyToSymbol's `symbol` argument passes through unchanged. */
int registry_add_var(const void *host_var, void *device_ptr);
void *registry_find_var(const void *host_var);

#endif
