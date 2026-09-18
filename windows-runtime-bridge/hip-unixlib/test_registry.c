/* Unit tests for registry.c - plain host gcc, no Wine/HIP dependency.
 * Run via `make test`. */
#include "registry.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_resolve_bundle_ptr_direct_match(void)
{
    char buf[32] = "__CLANG_OFFLOAD_BUNDLE__rest...";
    assert(resolve_bundle_ptr(buf) == buf);
}

static void test_resolve_bundle_ptr_wrapper_indirection(void)
{
    /* Simulates the compiler-generated wrapper struct
     * {magic, version, data, unused} - the real bundle lives at the
     * pointer stored at byte offset 8. */
    char bundle[32] = "__CLANG_OFFLOAD_BUNDLE__rest...";
    struct { unsigned int magic, version; const void *data; const void *unused; } wrapper;
    wrapper.data = bundle;
    assert(resolve_bundle_ptr(&wrapper) == bundle);
}

static void test_resolve_bundle_ptr_no_match(void)
{
    /* A realistic "no match" case: a wrapper-shaped struct whose data
     * field points to something else valid, just not the bundle magic -
     * not truly random bytes (which resolve_bundle_ptr's wrapper-
     * indirection path would reinterpret as a wild pointer and crash
     * dereferencing; that is a known, accepted limitation given the
     * caller only ever passes __hipRegisterFatBinary's real argument in
     * practice, not attacker-controlled data). */
    char other_data[32] = "valid memory, but not a bundle.";
    struct { unsigned int magic, version; const void *data; const void *unused; } wrapper;
    wrapper.data = other_data;
    assert(resolve_bundle_ptr(&wrapper) == NULL);
}

static void test_registry_add_then_find(void)
{
    registry_reset();
    int token_a, token_b, module_a, module_b;
    assert(registry_add_fatbin(&token_a, &module_a));
    assert(registry_add_fatbin(&token_b, &module_b));
    assert(registry_find_module(&token_a) == &module_a);
    assert(registry_find_module(&token_b) == &module_b);
}

static void test_registry_find_unknown_returns_null(void)
{
    registry_reset();
    int token;
    assert(registry_find_module(&token) == NULL);
}

static void test_registry_functions_independent_of_fatbins(void)
{
    registry_reset();
    int host_fn_a, host_fn_b, function_a, function_b;
    assert(registry_add_function(&host_fn_a, &function_a, "kernel_a"));
    assert(registry_add_function(&host_fn_b, &function_b, "kernel_b"));
    assert(registry_find_function(&host_fn_a) == &function_a);
    assert(registry_find_function(&host_fn_b) == &function_b);
    assert(registry_find_function(&host_fn_a) != registry_find_function(&host_fn_b));
}

/* docs/linux-support-spec.md \xc2\xa749: the registry started also
 * remembering each kernel's real name so every hipLaunchKernel log
 * line can be self-identifying, not just the first "resolved kernel"
 * line - this is the coverage for that. */
static void test_registry_remembers_function_name(void)
{
    registry_reset();
    int host_fn, function;
    assert(registry_add_function(&host_fn, &function, "_Z10k_swin_varILi32ELb1EEv9VarParams"));
    const char *name = registry_find_function_name(&host_fn);
    assert(name != NULL);
    assert(strcmp(name, "_Z10k_swin_varILi32ELb1EEv9VarParams") == 0);
}

static void test_registry_function_name_unknown_returns_null(void)
{
    registry_reset();
    int host_fn;
    assert(registry_find_function_name(&host_fn) == NULL);
}

static void test_registry_function_name_null_is_stored_as_empty(void)
{
    /* unix_register_function always has a real device_name in
     * practice, but the registry must not crash or misbehave if a
     * caller ever passes NULL - store it as an empty string rather
     * than a null pointer, so callers can always safely print/compare
     * what registry_find_function_name returns once a host_fn is
     * known at all. */
    registry_reset();
    int host_fn, function;
    assert(registry_add_function(&host_fn, &function, NULL));
    const char *name = registry_find_function_name(&host_fn);
    assert(name != NULL);
    assert(name[0] == '\0');
}

static void test_registry_function_name_truncates_safely(void)
{
    /* A name far longer than the fixed buffer must not overflow it -
     * it should be truncated, but still real, still null-terminated,
     * still usable. */
    registry_reset();
    int host_fn, function;
    char long_name[500];
    memset(long_name, 'x', sizeof(long_name) - 1);
    long_name[sizeof(long_name) - 1] = '\0';
    assert(registry_add_function(&host_fn, &function, long_name));
    const char *name = registry_find_function_name(&host_fn);
    assert(name != NULL);
    assert(strlen(name) < sizeof(long_name) - 1);
    assert(strlen(name) > 0);
}

static void test_registry_reset_clears_everything(void)
{
    registry_reset();
    int token, module;
    registry_add_fatbin(&token, &module);
    assert(registry_find_module(&token) == &module);
    registry_reset();
    assert(registry_find_module(&token) == NULL);
}

static void test_registry_capacity_limit_is_respected(void)
{
    registry_reset();
    static int tokens[300];
    static int modules[300];
    int added = 0;
    for (int i = 0; i < 300; i++)
        if (registry_add_fatbin(&tokens[i], &modules[i]))
            added++;
    /* MAX_FATBINS is 16 - adding beyond capacity must fail cleanly
     * (return 0), not overflow the fixed array. */
    assert(added == 16);
}

/* docs/linux-support-spec.md's g_e4m3_lut finding: __hipRegisterVar used
 * to be a pure no-op, so hipMemcpyToSymbol had nothing to resolve a
 * host_var pointer against - this is the coverage for the fix. */
static void test_registry_add_var_then_find(void)
{
    registry_reset();
    int host_var_a, host_var_b, device_ptr_a, device_ptr_b;
    assert(registry_add_var(&host_var_a, &device_ptr_a));
    assert(registry_add_var(&host_var_b, &device_ptr_b));
    assert(registry_find_var(&host_var_a) == &device_ptr_a);
    assert(registry_find_var(&host_var_b) == &device_ptr_b);
}

static void test_registry_find_var_unknown_returns_null(void)
{
    registry_reset();
    int host_var;
    assert(registry_find_var(&host_var) == NULL);
}

/* A host_var and a host_fn table entry sharing the same numeric address
 * must never be confused with each other - separate tables, separate
 * lookups. */
static void test_registry_vars_independent_of_functions(void)
{
    registry_reset();
    int shared_addr, function, device_ptr;
    assert(registry_add_function(&shared_addr, &function, "some_kernel"));
    assert(registry_add_var(&shared_addr, &device_ptr));
    assert(registry_find_function(&shared_addr) == &function);
    assert(registry_find_var(&shared_addr) == &device_ptr);
}

static void test_registry_var_capacity_limit_is_respected(void)
{
    registry_reset();
    static int hosts[60];
    static int devices[60];
    int added = 0;
    for (int i = 0; i < 60; i++)
        if (registry_add_var(&hosts[i], &devices[i]))
            added++;
    /* MAX_VARS is 32 - adding beyond capacity must fail cleanly
     * (return 0), not overflow the fixed array. */
    assert(added == 32);
}

int main(void)
{
    test_resolve_bundle_ptr_direct_match();
    test_resolve_bundle_ptr_wrapper_indirection();
    test_resolve_bundle_ptr_no_match();
    test_registry_add_then_find();
    test_registry_find_unknown_returns_null();
    test_registry_functions_independent_of_fatbins();
    test_registry_remembers_function_name();
    test_registry_function_name_unknown_returns_null();
    test_registry_function_name_null_is_stored_as_empty();
    test_registry_function_name_truncates_safely();
    test_registry_reset_clears_everything();
    test_registry_capacity_limit_is_respected();
    test_registry_add_var_then_find();
    test_registry_find_var_unknown_returns_null();
    test_registry_vars_independent_of_functions();
    test_registry_var_capacity_limit_is_respected();
    printf("all tests passed\n");
    return 0;
}
