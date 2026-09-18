/* Real fp8 GPU-kernel execution probe.
 *
 * Goes one step further than rocm_probe.c: rather than only exercising
 * the event/stream machinery, this compiles a small, original,
 * clean-room HIP kernel *from source, at runtime*, using AMD's own
 * `libamd_comgr` code-object-manager library (the same compiler
 * machinery HIP's own `hiprtc` sits on top of - `hiprtc` itself isn't
 * packaged here, comgr is, so this drives comgr directly), then loads
 * and launches it via the exact same real HIP module API
 * (`hipModuleLoadData`/`hipModuleGetFunction`/`hipModuleLaunchKernel`)
 * `daniel/hip-unixlib/native.c` already uses for danielblnc's own
 * kernels.
 *
 * This settles the open question from docs/linux-support-spec.md
 * \xc2\xa730/\xc2\xa731 directly, empirically, rather than by inference from
 * error codes: does this exact GPU + ROCm 7.1.1 combination actually
 * execute real fp8 GPU instructions, or does it reject them the same
 * way danielblnc's own (much larger, proprietary) kernels are being
 * rejected?
 *
 * No proprietary code: the kernel source below is original, minimal,
 * clean-room test code written for this diagnostic alone - it does
 * not contain, reference, or derive from any of danielblnc's actual
 * kernels.
 *
 * REQUIRES `libamd-comgr-dev` (headers only, matches the installed
 * `libamd-comgr3` runtime exactly - see daniel/investigations/README.md):
 *
 *   sudo apt install libamd-comgr-dev
 *   gcc -Wall -o fp8_kernel_probe fp8_kernel_probe.c -ldl -lamd_comgr
 *   LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu ./fp8_kernel_probe
 *
 * Written without the real header present (remote session, could not
 * install it at the time) - built against the public, stable, documented
 * AMD COMGR API from memory. The C compiler will catch any wrong enum/
 * function name at build time as a normal compile error (safe, loud,
 * easy to fix) rather than anything silently wrong at runtime - if a
 * name below doesn't match what the installed header actually calls
 * it, that's expected to need a small correction once actually built
 * against the real header for the first time.
 */
#include <amd_comgr/amd_comgr.h>

#include <stdbool.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef int hipError_t;

typedef hipError_t (*hipGetDeviceCount_t)(int *);
typedef hipError_t (*hipSetDevice_t)(int);
typedef hipError_t (*hipMalloc_t)(void **, size_t);
typedef hipError_t (*hipFree_t)(void *);
typedef hipError_t (*hipMemcpy_t)(void *, const void *, size_t, int);
typedef hipError_t (*hipModuleLoadData_t)(void **, const void *);
typedef hipError_t (*hipModuleGetFunction_t)(void **, void *, const char *);
typedef hipError_t (*hipModuleLaunchKernel_t)(void *, unsigned int, unsigned int, unsigned int,
        unsigned int, unsigned int, unsigned int, unsigned int, void *, void **, void **);
typedef hipError_t (*hipDeviceSynchronize_t)(void);
typedef const char *(*hipGetErrorString_t)(hipError_t);

#define HIP_MEMCPY_DEVICE_TO_HOST 2

static hipGetErrorString_t p_GetErrorString;

static void check_hip(const char *step, hipError_t err)
{
    printf("  %-45s -> %d (%s)\n", step, err,
            (p_GetErrorString && err != 0) ? p_GetErrorString(err) : (err == 0 ? "success" : "?"));
}

static void check_comgr(const char *step, amd_comgr_status_t status)
{
    const char *msg = NULL;
    if (status != AMD_COMGR_STATUS_SUCCESS)
        amd_comgr_status_string(status, &msg);
    printf("  %-45s -> %d%s%s\n", step, status, msg ? " " : "", msg ? msg : "");
}

/* comgr reports real compiler diagnostics (errors, warnings) as
 * AMD_COMGR_DATA_KIND_LOG entries in the *output* data set of whichever
 * action produced them - not via the status code alone. Dump them so a
 * real compile failure shows the real reason instead of just "1
 * ERROR". */
static void dump_logs(const char *label, amd_comgr_data_set_t set)
{
    size_t count = 0;
    if (amd_comgr_action_data_count(set, AMD_COMGR_DATA_KIND_LOG, &count) != AMD_COMGR_STATUS_SUCCESS || count == 0)
        return;
    printf("  --- %s: %zu log entr%s ---\n", label, count, count == 1 ? "y" : "ies");
    for (size_t i = 0; i < count; i++)
    {
        amd_comgr_data_t log_data;
        if (amd_comgr_action_data_get_data(set, AMD_COMGR_DATA_KIND_LOG, i, &log_data) != AMD_COMGR_STATUS_SUCCESS)
            continue;
        size_t size = 0;
        amd_comgr_get_data(log_data, &size, NULL);
        if (size == 0) continue;
        char *buf = malloc(size + 1);
        amd_comgr_get_data(log_data, &size, buf);
        buf[size] = '\0';
        printf("%s\n", buf);
        free(buf);
    }
}

/* Two deliberately tiny, original, clean-room kernels:
 *
 *  - kernel_touch: writes a fixed value with plain fp32 arithmetic.
 *    No fp8 involved at all - this is the control. If even this
 *    fails, the problem is general kernel execution, not fp8
 *    specifically.
 *
 *  - kernel_fp8_decode: decodes a real, hand-constructed fp8 (e4m3)
 *    bit pattern back to fp32 using Clang's real AMDGPU fp8 decode
 *    builtin. This is the actual fp8 instruction path under suspicion
 *    - if this one specifically fails with "operation not supported"
 *    while kernel_touch succeeds, that's a direct, empirical
 *    confirmation of the fp8 theory from docs/linux-support-spec.md
 *    \xc2\xa730c, not an inference from someone else's proprietary binary's
 *    behavior.
 *
 * Written as plain OpenCL C, not HIP: an initial attempt used
 * AMD_COMGR_LANGUAGE_HIP with a raw `__attribute__((amdgpu_kernel))`
 * (how comgr's own device-only examples are often written), but HIP
 * mode does a real two-target (host x86_64 + device gfx1201) split
 * compile, and the attribute was being dropped on whichever pass
 * actually got linked into the final module - `hipModuleGetFunction`
 * could never find either kernel even once compilation itself
 * succeeded with no errors (see docs/linux-support-spec.md \xc2\xa732c for
 * the full diagnosis). OpenCL C's `__kernel` is comgr's standard,
 * single-target-only device path - no host stub, no split - and
 * reaches exactly the same AMDGPU backend and the same Clang builtins.
 *
 * `__builtin_amdgcn_cvt_f32_fp8(int packed, int byte_index)` is the
 * real, live-confirmed signature on this exact system (found by
 * reading comgr's own real compiler diagnostic after an initial
 * 3-argument guess was rejected - see docs/linux-support-spec.md
 * \xc2\xa732c) - it decodes byte `byte_index` of `packed` as an fp8 (e4m3)
 * value back to fp32. `0x38` is the well-known, documented e4m3 bit
 * pattern for exactly 1.0 (sign 0, exponent 0111 = bias 7, mantissa
 * 000) - a real, verifiable fp8 value, not an arbitrary one. */
static const char *kernel_source =
    "__kernel void kernel_touch(__global float *out) {\n"
    "    out[0] = 3.5f + 1.5f;\n"
    "}\n"
    "\n"
    "__kernel void kernel_fp8_decode(__global float *out) {\n"
    "    out[0] = __builtin_amdgcn_cvt_f32_fp8(0x38, 0);\n"
    "}\n";

int main(void)
{
    printf("=== fp8 kernel execution probe ===\n\n");

    void *h = dlopen("libamdhip64.so.7", RTLD_NOW);
    if (!h) h = dlopen("libamdhip64.so", RTLD_NOW);
    if (!h) { printf("dlopen(libamdhip64) failed: %s\n", dlerror()); return 1; }

    hipGetDeviceCount_t p_hipGetDeviceCount = (hipGetDeviceCount_t)dlsym(h, "hipGetDeviceCount");
    hipSetDevice_t p_hipSetDevice = (hipSetDevice_t)dlsym(h, "hipSetDevice");
    hipMalloc_t p_hipMalloc = (hipMalloc_t)dlsym(h, "hipMalloc");
    hipFree_t p_hipFree = (hipFree_t)dlsym(h, "hipFree");
    hipMemcpy_t p_hipMemcpy = (hipMemcpy_t)dlsym(h, "hipMemcpy");
    hipModuleLoadData_t p_hipModuleLoadData = (hipModuleLoadData_t)dlsym(h, "hipModuleLoadData");
    hipModuleGetFunction_t p_hipModuleGetFunction = (hipModuleGetFunction_t)dlsym(h, "hipModuleGetFunction");
    hipModuleLaunchKernel_t p_hipModuleLaunchKernel = (hipModuleLaunchKernel_t)dlsym(h, "hipModuleLaunchKernel");
    hipDeviceSynchronize_t p_hipDeviceSynchronize = (hipDeviceSynchronize_t)dlsym(h, "hipDeviceSynchronize");
    p_GetErrorString = (hipGetErrorString_t)dlsym(h, "hipGetErrorString");

    if (!p_hipGetDeviceCount || !p_hipModuleLoadData || !p_hipModuleGetFunction || !p_hipModuleLaunchKernel)
    {
        printf("Required HIP symbols missing, aborting.\n");
        return 1;
    }

    int count = 0;
    check_hip("hipGetDeviceCount", p_hipGetDeviceCount(&count));
    if (count < 1) { printf("No device, aborting.\n"); return 1; }
    check_hip("hipSetDevice(0)", p_hipSetDevice(0));

    printf("\n--- compiling kernel_touch + kernel_fp8_decode via comgr, target gfx1201 ---\n");

    amd_comgr_data_t src_data;
    check_comgr("amd_comgr_create_data(SOURCE)",
            amd_comgr_create_data(AMD_COMGR_DATA_KIND_SOURCE, &src_data));
    check_comgr("amd_comgr_set_data",
            amd_comgr_set_data(src_data, strlen(kernel_source), kernel_source));
    check_comgr("amd_comgr_set_data_name",
            amd_comgr_set_data_name(src_data, "probe.cl"));

    amd_comgr_data_set_t input_set, bc_set, linked_bc_set, reloc_set, exec_set;
    check_comgr("amd_comgr_create_data_set(input)", amd_comgr_create_data_set(&input_set));
    check_comgr("amd_comgr_data_set_add(src)", amd_comgr_data_set_add(input_set, src_data));

    amd_comgr_action_info_t action_info;
    check_comgr("amd_comgr_create_action_info", amd_comgr_create_action_info(&action_info));
    check_comgr("amd_comgr_action_info_set_language",
            amd_comgr_action_info_set_language(action_info, AMD_COMGR_LANGUAGE_OPENCL_1_2));
    check_comgr("amd_comgr_action_info_set_isa_name",
            amd_comgr_action_info_set_isa_name(action_info, "amdgcn-amd-amdhsa--gfx1201"));
    check_comgr("amd_comgr_action_info_set_logging",
            amd_comgr_action_info_set_logging(action_info, true));

    check_comgr("amd_comgr_create_data_set(bc)", amd_comgr_create_data_set(&bc_set));
    check_comgr("do_action(COMPILE_SOURCE_WITH_DEVICE_LIBS_TO_BC)",
            amd_comgr_do_action(AMD_COMGR_ACTION_COMPILE_SOURCE_WITH_DEVICE_LIBS_TO_BC,
                    action_info, input_set, bc_set));
    dump_logs("COMPILE_SOURCE_WITH_DEVICE_LIBS_TO_BC", bc_set);

    check_comgr("amd_comgr_create_data_set(linked_bc)", amd_comgr_create_data_set(&linked_bc_set));
    check_comgr("do_action(LINK_BC_TO_BC)",
            amd_comgr_do_action(AMD_COMGR_ACTION_LINK_BC_TO_BC, action_info, bc_set, linked_bc_set));
    dump_logs("LINK_BC_TO_BC", linked_bc_set);

    check_comgr("amd_comgr_create_data_set(reloc)", amd_comgr_create_data_set(&reloc_set));
    check_comgr("do_action(CODEGEN_BC_TO_RELOCATABLE)",
            amd_comgr_do_action(AMD_COMGR_ACTION_CODEGEN_BC_TO_RELOCATABLE,
                    action_info, linked_bc_set, reloc_set));
    dump_logs("CODEGEN_BC_TO_RELOCATABLE", reloc_set);

    check_comgr("amd_comgr_create_data_set(exec)", amd_comgr_create_data_set(&exec_set));
    check_comgr("do_action(LINK_RELOCATABLE_TO_EXECUTABLE)",
            amd_comgr_do_action(AMD_COMGR_ACTION_LINK_RELOCATABLE_TO_EXECUTABLE,
                    action_info, reloc_set, exec_set));

    size_t exec_count = 0;
    check_comgr("amd_comgr_action_data_count(exec)",
            amd_comgr_action_data_count(exec_set, AMD_COMGR_DATA_KIND_EXECUTABLE, &exec_count));
    printf("  executable data objects produced: %zu\n", exec_count);
    if (exec_count < 1) { printf("Compilation produced no executable, aborting.\n"); return 1; }

    amd_comgr_data_t exec_data;
    check_comgr("amd_comgr_action_data_get_data(exec, 0)",
            amd_comgr_action_data_get_data(exec_set, AMD_COMGR_DATA_KIND_EXECUTABLE, 0, &exec_data));

    size_t exec_size = 0;
    check_comgr("amd_comgr_get_data(size query)", amd_comgr_get_data(exec_data, &exec_size, NULL));
    printf("  code object size: %zu bytes\n", exec_size);

    void *exec_bytes = malloc(exec_size);
    check_comgr("amd_comgr_get_data(bytes)", amd_comgr_get_data(exec_data, &exec_size, exec_bytes));

    printf("\n--- loading real compiled code object via hipModule*, same API native.c uses ---\n");

    void *module = NULL;
    check_hip("hipModuleLoadData(compiled code object)", p_hipModuleLoadData(&module, exec_bytes));
    if (!module) { printf("Module load failed, aborting.\n"); return 1; }

    void *dev_out = NULL;
    check_hip("hipMalloc(dev_out, 4 bytes)", p_hipMalloc(&dev_out, sizeof(float)));

    printf("\n--- kernel_touch (control, no fp8) ---\n");
    void *fn_touch = NULL;
    check_hip("hipModuleGetFunction(kernel_touch)", p_hipModuleGetFunction(&fn_touch, module, "kernel_touch"));
    if (fn_touch)
    {
        void *args1[] = { &dev_out };
        check_hip("hipModuleLaunchKernel(kernel_touch)",
                p_hipModuleLaunchKernel(fn_touch, 1, 1, 1, 1, 1, 1, 0, NULL, args1, NULL));
        check_hip("hipDeviceSynchronize", p_hipDeviceSynchronize());
        float result = -1.0f;
        check_hip("hipMemcpy(result back)", p_hipMemcpy(&result, dev_out, sizeof(float), HIP_MEMCPY_DEVICE_TO_HOST));
        printf("  result: %f (expect 5.0)\n", result);
    }

    printf("\n--- kernel_fp8_decode (the actual question) ---\n");
    void *fn_fp8 = NULL;
    check_hip("hipModuleGetFunction(kernel_fp8_decode)",
            p_hipModuleGetFunction(&fn_fp8, module, "kernel_fp8_decode"));
    if (fn_fp8)
    {
        void *args2[] = { &dev_out };
        check_hip("hipModuleLaunchKernel(kernel_fp8_decode)",
                p_hipModuleLaunchKernel(fn_fp8, 1, 1, 1, 1, 1, 1, 0, NULL, args2, NULL));
        check_hip("hipDeviceSynchronize [THE ANSWER IS HERE]", p_hipDeviceSynchronize());
        float result = -1.0f;
        check_hip("hipMemcpy(result back)", p_hipMemcpy(&result, dev_out, sizeof(float), HIP_MEMCPY_DEVICE_TO_HOST));
        printf("  result: %f (0x38 is the documented e4m3 bit pattern for exactly 1.0 - expect 1.0 exactly)\n", result);
    }

    if (dev_out) p_hipFree(dev_out);
    free(exec_bytes);

    printf("\n=== probe complete ===\n");
    printf("If kernel_touch succeeded but kernel_fp8_decode's hipDeviceSynchronize\n");
    printf("reported a real error (not 0/success), that is direct, empirical confirmation\n");
    printf("of the fp8 theory in docs/linux-support-spec.md \xc2\xa7" "30c - not an inference.\n");
    return 0;
}
