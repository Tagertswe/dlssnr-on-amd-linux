/* WMMA (matrix) fp8 GPU-kernel execution probe.
 *
 * Direct follow-up to fp8_kernel_probe.c. That probe proved a simple
 * *scalar* fp8 decode instruction (__builtin_amdgcn_cvt_f32_fp8)
 * executes correctly on this GPU/ROCm/OS combination - see
 * docs/linux-support-spec.md \xc2\xa733. But danielblnc's actual failing
 * kernel, identified in \xc2\xa735b by demangling its symbol
 * (_Z10k_swin_varILi32ELb1EEv9VarParams -> void k_swin_var<32,
 * true>(VarParams), a Swin-transformer variance kernel), is
 * transformer-shaped and very likely uses a real matrix-multiply-
 * accumulate fp8 instruction (WMMA), not a scalar decode. This probe
 * tests that specific, narrower instruction family directly, exactly
 * the way fp8_kernel_probe.c tested the scalar one: compile a real,
 * original, clean-room kernel from source via libamd_comgr, load and
 * launch it via the same hipModule* API daniel/hip-unixlib/native.c
 * uses, and see whether hipDeviceSynchronize/hipGetLastError reports
 * a real error - the same "operation not supported" signature seen
 * live in the game's crash log.
 *
 * No proprietary code: this kernel is original, minimal, clean-room
 * test code written for this diagnostic alone. It does not contain,
 * reference, or derive from any of danielblnc's actual kernels -
 * only the public, documented LLVM/AMDGPU WMMA intrinsic family
 * (llvm.amdgcn.wmma.f32.16x16x16.fp8.fp8, confirmed to exist in this
 * system's own installed
 * /usr/include/llvm-21/llvm/IR/IntrinsicsAMDGPU.td - real hardware
 * support for it on RDNA4/gfx1201 was already documented research in
 * \xc2\xa730c).
 *
 * REQUIRES `libamd-comgr-dev` (headers only, matches the installed
 * `libamd-comgr3` runtime exactly - see daniel/investigations/README.md,
 * including the no-root `apt-get download`/`dpkg-deb -x` alternative):
 *
 *   sudo apt install libamd-comgr-dev
 *   gcc -Wall -o wmma_fp8_probe wmma_fp8_probe.c -ldl -lamd_comgr
 *   LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu ./wmma_fp8_probe
 *
 * The real clang builtin name was NOT guessed from the LLVM IR
 * intrinsic name alone (that guess,
 * __builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8, failed to compile -
 * "use of undeclared identifier"). Found instead by grepping real
 * builtin-name strings directly out of the installed
 * libamd_comgr.so.3.0.0 binary (`strings -a ... | grep wmma_f32`),
 * which is compiled with its own bundled clang and exposes a
 * gfx12-specific, wave-width-suffixed family not visible in Ubuntu's
 * separate system llvm-21 package's .td file:
 * __builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12 (also a
 * _w64_gfx12 variant, and non-"_gfx12" w32/w64 variants for older
 * gfx11 WMMA without the fp8/bf8 operand types). RDNA4/gfx1201 is
 * wave32 by default, hence the _w32_gfx12 variant used below. Vector
 * operand types (int2_t/float8_t) also could not use OpenCL's
 * built-in int2/float8 sugar (undeclared identifier under this
 * comgr/OPENCL_1_2 action, for reasons not further investigated) -
 * worked around with Clang's own portable
 * __attribute__((ext_vector_type(N))) instead, which does not depend
 * on OpenCL's builtin-type registration.
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

/* kernel_touch: same fp32-only control as fp8_kernel_probe.c.
 *
 * kernel_wmma_fp8_touch: the actual question. Builds two all-zero
 * packed-fp8 operands (int2, i.e. <2 x i32> - 8 fp8 lanes each,
 * matching the intrinsic's documented packing) and an all-zero
 * <8 x float> accumulator, calls the real WMMA fp8x fp8->f32
 * matrix-multiply-accumulate builtin once, and writes the first of
 * the 8 result lanes out. Deliberately zero operands: this test is
 * about whether the instruction *executes* at all on this hardware
 * (matching the real failure mode - hipLaunchKernel/hipGetLastError
 * reporting "operation not supported" - not about validating a real
 * numeric product, which would need reasoning about this specific
 * wave32 WMMA layout's exact per-lane operand assignment that isn't
 * available without AMD's own internal documentation). Zero times
 * zero, accumulated, is unambiguously 0.0f if the instruction runs
 * at all - any non-zero/garbage result or a real hipError_t here is
 * equally informative either way. */
static const char *kernel_source =
    "typedef int int2_t __attribute__((ext_vector_type(2)));\n"
    "typedef float float8_t __attribute__((ext_vector_type(8)));\n"
    "\n"
    "__kernel void kernel_touch(__global float *out) {\n"
    "    out[0] = 3.5f + 1.5f;\n"
    "}\n"
    "\n"
    "__kernel void kernel_wmma_fp8_touch(__global float *out) {\n"
    "    int2_t a = (int2_t)(0, 0);\n"
    "    int2_t b = (int2_t)(0, 0);\n"
    "    float8_t c = (float8_t)(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);\n"
    "    float8_t r = __builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12(a, b, c);\n"
    "    out[0] = r[0];\n"
    "}\n";

int main(void)
{
    printf("=== WMMA fp8 matrix kernel execution probe ===\n\n");

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

    printf("\n--- compiling kernel_touch + kernel_wmma_fp8_touch via comgr, target gfx1201 ---\n");

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

    printf("\n--- kernel_touch (control, no fp8/WMMA) ---\n");
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

    printf("\n--- kernel_wmma_fp8_touch (the actual question) ---\n");
    void *fn_wmma = NULL;
    check_hip("hipModuleGetFunction(kernel_wmma_fp8_touch)",
            p_hipModuleGetFunction(&fn_wmma, module, "kernel_wmma_fp8_touch"));
    if (fn_wmma)
    {
        void *args2[] = { &dev_out };
        check_hip("hipModuleLaunchKernel(kernel_wmma_fp8_touch)",
                p_hipModuleLaunchKernel(fn_wmma, 1, 1, 1, 1, 1, 1, 0, NULL, args2, NULL));
        check_hip("hipDeviceSynchronize [THE ANSWER IS HERE]", p_hipDeviceSynchronize());
        float result = -1.0f;
        check_hip("hipMemcpy(result back)", p_hipMemcpy(&result, dev_out, sizeof(float), HIP_MEMCPY_DEVICE_TO_HOST));
        printf("  result: %f (0*0 accumulated - expect 0.0 if the instruction ran at all)\n", result);
    }

    if (dev_out) p_hipFree(dev_out);
    free(exec_bytes);

    printf("\n=== probe complete ===\n");
    printf("If kernel_touch succeeded but kernel_wmma_fp8_touch's hipDeviceSynchronize\n");
    printf("reported a real error (not 0/success), that is direct, empirical confirmation\n");
    printf("that this GPU/ROCm combination cannot execute this WMMA fp8 instruction -\n");
    printf("matching the real crash's failure mode (hipLaunchKernel succeeds, the real\n");
    printf("error only surfaces at hipGetLastError/hipDeviceSynchronize) far more closely\n");
    printf("than the scalar decode test in fp8_kernel_probe.c did. See\n");
    printf("docs/linux-support-spec.md \xc2\xa7" "35b/\xc2\xa7" "35c.\n");
    return 0;
}
