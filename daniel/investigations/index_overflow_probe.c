/* 32-bit index-overflow characterization probe.
 *
 * Direct follow-up to §42's raw-pointer fault repro. That probe
 * proved a raw HIP pointer write to an already-known-bad address
 * (0x100000000, captured from a real crash) genuinely faults this
 * GPU. This probe asks a different, forward-looking question: rather
 * than starting from a known-bad address, at what *scale* does a
 * plausible, ordinary-looking tensor index computation - flattening
 * a multi-dimensional access using 32-bit int arithmetic, the kind
 * every real Swin-transformer-shaped kernel does constantly - start
 * producing an out-of-bounds address on its own, via nothing more
 * exotic than integer overflow?
 *
 * This is entirely original, clean-room test code. The kernel below
 * does not contain, reference, or derive from any of danielblnc's
 * actual kernels - it only shares the general SHAPE of computation
 * (a flattened multi-dimensional index) that any tensor kernel,
 * including his, plausibly needs. This probe does not claim to
 * reproduce his specific bug - it characterizes the general bug
 * class (docs/linux-support-spec.md \xc2\xa740c/\xc2\xa747c's standing hypothesis)
 * so that both this project and danielblnc have a concrete, testable
 * threshold to check his own kernels against, rather than a vague
 * "maybe a 32-bit overflow somewhere."
 *
 * Method: allocate one real, modest buffer (a few KB - deliberately
 * NOT itself anywhere near 4GB, so any fault can only come from the
 * index computation overflowing, not from the buffer's own real size
 * or address). Launch a kernel that computes a classic flattened
 * tensor index - idx = ((z * H + y) * W + x) * C + c, matching the
 * shape of a real 4D (batch/height/width/channel) tensor access -
 * entirely in 32-bit int arithmetic, the same type real HIP/CUDA
 * kernels overwhelmingly use for this, then writes through
 * base_ptr + idx (also compiler-promoted, but the *idx value itself*
 * has already silently overflowed/wrapped by then if the dimensions
 * were large enough). Sweep the dimension product across the 2^31
 * and 2^32 boundaries and record exactly where hipDeviceSynchronize
 * stops reporting clean success.
 *
 * REQUIRES `libamd-comgr-dev` - see daniel/investigations/README.md.
 *
 *   gcc -Wall -o index_overflow_probe index_overflow_probe.c -ldl -lamd_comgr
 *   timeout --kill-after=10s 60s env LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu ./index_overflow_probe
 *
 * Run memory-capped and wall-clock-limited (systemd-run -p MemoryMax=2G
 * ... timeout ...) - deliberately probing for the same class of real
 * GPU page fault \xc2\xa737-\xc2\xa742 found, on purpose, to characterize it.
 */
#include <amd_comgr/amd_comgr.h>

#include <stdbool.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>

typedef int hipError_t;

typedef hipError_t (*hipGetDeviceCount_t)(int *);
typedef hipError_t (*hipSetDevice_t)(int);
typedef hipError_t (*hipMalloc_t)(void **, size_t);
typedef hipError_t (*hipFree_t)(void *);
typedef hipError_t (*hipModuleLoadData_t)(void **, const void *);
typedef hipError_t (*hipModuleGetFunction_t)(void **, void *, const char *);
typedef hipError_t (*hipModuleLaunchKernel_t)(void *, unsigned int, unsigned int, unsigned int,
        unsigned int, unsigned int, unsigned int, unsigned int, void *, void **, void **);
typedef hipError_t (*hipDeviceSynchronize_t)(void);
typedef const char *(*hipGetErrorString_t)(hipError_t);

static hipGetErrorString_t p_GetErrorString;
static hipDeviceSynchronize_t p_hipDeviceSynchronize;

static void check_comgr(const char *step, amd_comgr_status_t status)
{
    const char *msg = NULL;
    if (status != AMD_COMGR_STATUS_SUCCESS)
    {
        amd_comgr_status_string(status, &msg);
        printf("  %-45s -> %d %s\n", step, status, msg ? msg : "");
        exit(1);
    }
}

/* Real, original, clean-room kernel: a classic flattened 4D tensor
 * index, exactly the shape (not the content) of what any real
 * tensor-shaped kernel needs - deliberately all 32-bit int
 * arithmetic, matching the overwhelmingly common real-world pattern
 * this whole probe is characterizing the failure mode of. */
static const char *kernel_source =
    "__kernel void kernel_flat_index_write(__global float *base_ptr,\n"
    "        int Z, int H, int W, int C, int z, int y, int x, int c) {\n"
    "    int idx = ((z * H + y) * W + x) * C + c;\n"
    "    base_ptr[idx] = 1.0f;\n"
    "}\n";

int main(void)
{
    /* Piped/systemd-run output is fully buffered by default, not
     * line-buffered - if the process aborts (exactly what this probe
     * deliberately risks), any unflushed output is silently lost,
     * making it impossible to tell which sweep case actually faulted.
     * Force line-buffering unconditionally so every printf lands
     * before the next one, abort or not - found live, the hard way,
     * on this exact file's first real run (empty output on a real
     * crash) before this fix. */
    setvbuf(stdout, NULL, _IOLBF, 0);

    printf("=== 32-bit index-overflow characterization probe ===\n\n");
    printf("Kernel: idx = ((z*H+y)*W+x)*C+c, all int32 - the classic flattened\n");
    printf("tensor-index shape. Sweeping dimension products across the 2^31/2^32\n");
    printf("boundaries to find exactly where this stops being safe on real\n");
    printf("hardware, using a real but small (16KB) backing buffer throughout -\n");
    printf("any fault can only come from the index arithmetic itself overflowing,\n");
    printf("never from the buffer's own size or address.\n\n");

    void *h = dlopen("libamdhip64.so.7", RTLD_NOW);
    if (!h) h = dlopen("libamdhip64.so", RTLD_NOW);
    if (!h) { printf("dlopen(libamdhip64) failed: %s\n", dlerror()); return 1; }

    hipGetDeviceCount_t p_hipGetDeviceCount = (hipGetDeviceCount_t)dlsym(h, "hipGetDeviceCount");
    hipSetDevice_t p_hipSetDevice = (hipSetDevice_t)dlsym(h, "hipSetDevice");
    hipMalloc_t p_hipMalloc = (hipMalloc_t)dlsym(h, "hipMalloc");
    hipFree_t p_hipFree = (hipFree_t)dlsym(h, "hipFree");
    hipModuleLoadData_t p_hipModuleLoadData = (hipModuleLoadData_t)dlsym(h, "hipModuleLoadData");
    hipModuleGetFunction_t p_hipModuleGetFunction = (hipModuleGetFunction_t)dlsym(h, "hipModuleGetFunction");
    hipModuleLaunchKernel_t p_hipModuleLaunchKernel = (hipModuleLaunchKernel_t)dlsym(h, "hipModuleLaunchKernel");
    p_hipDeviceSynchronize = (hipDeviceSynchronize_t)dlsym(h, "hipDeviceSynchronize");
    p_GetErrorString = (hipGetErrorString_t)dlsym(h, "hipGetErrorString");

    if (!p_hipGetDeviceCount || !p_hipModuleLoadData || !p_hipModuleGetFunction || !p_hipModuleLaunchKernel)
    {
        printf("Required HIP symbols missing, aborting.\n");
        return 1;
    }

    int count = 0;
    p_hipGetDeviceCount(&count);
    if (count < 1) { printf("No device, aborting.\n"); return 1; }
    p_hipSetDevice(0);

    printf("--- compiling kernel_flat_index_write via comgr, target gfx1201 ---\n");
    amd_comgr_data_t src_data;
    check_comgr("create_data", amd_comgr_create_data(AMD_COMGR_DATA_KIND_SOURCE, &src_data));
    check_comgr("set_data", amd_comgr_set_data(src_data, strlen(kernel_source), kernel_source));
    check_comgr("set_data_name", amd_comgr_set_data_name(src_data, "probe.cl"));

    amd_comgr_data_set_t input_set, bc_set, linked_bc_set, reloc_set, exec_set;
    check_comgr("create_data_set(input)", amd_comgr_create_data_set(&input_set));
    check_comgr("data_set_add", amd_comgr_data_set_add(input_set, src_data));

    amd_comgr_action_info_t action_info;
    check_comgr("create_action_info", amd_comgr_create_action_info(&action_info));
    check_comgr("set_language", amd_comgr_action_info_set_language(action_info, AMD_COMGR_LANGUAGE_OPENCL_1_2));
    check_comgr("set_isa_name", amd_comgr_action_info_set_isa_name(action_info, "amdgcn-amd-amdhsa--gfx1201"));

    check_comgr("create_data_set(bc)", amd_comgr_create_data_set(&bc_set));
    check_comgr("COMPILE_SOURCE_WITH_DEVICE_LIBS_TO_BC",
            amd_comgr_do_action(AMD_COMGR_ACTION_COMPILE_SOURCE_WITH_DEVICE_LIBS_TO_BC, action_info, input_set, bc_set));
    check_comgr("create_data_set(linked_bc)", amd_comgr_create_data_set(&linked_bc_set));
    check_comgr("LINK_BC_TO_BC", amd_comgr_do_action(AMD_COMGR_ACTION_LINK_BC_TO_BC, action_info, bc_set, linked_bc_set));
    check_comgr("create_data_set(reloc)", amd_comgr_create_data_set(&reloc_set));
    check_comgr("CODEGEN_BC_TO_RELOCATABLE",
            amd_comgr_do_action(AMD_COMGR_ACTION_CODEGEN_BC_TO_RELOCATABLE, action_info, linked_bc_set, reloc_set));
    check_comgr("create_data_set(exec)", amd_comgr_create_data_set(&exec_set));
    check_comgr("LINK_RELOCATABLE_TO_EXECUTABLE",
            amd_comgr_do_action(AMD_COMGR_ACTION_LINK_RELOCATABLE_TO_EXECUTABLE, action_info, reloc_set, exec_set));

    size_t exec_count = 0;
    check_comgr("action_data_count", amd_comgr_action_data_count(exec_set, AMD_COMGR_DATA_KIND_EXECUTABLE, &exec_count));
    if (exec_count < 1) { printf("Compilation produced no executable, aborting.\n"); return 1; }
    amd_comgr_data_t exec_data;
    check_comgr("action_data_get_data", amd_comgr_action_data_get_data(exec_set, AMD_COMGR_DATA_KIND_EXECUTABLE, 0, &exec_data));
    size_t exec_size = 0;
    check_comgr("get_data(size)", amd_comgr_get_data(exec_data, &exec_size, NULL));
    void *exec_bytes = malloc(exec_size);
    check_comgr("get_data(bytes)", amd_comgr_get_data(exec_data, &exec_size, exec_bytes));

    void *module = NULL;
    if (p_hipModuleLoadData(&module, exec_bytes) != 0 || !module)
    {
        printf("Module load failed, aborting.\n");
        return 1;
    }
    void *fn = NULL;
    if (p_hipModuleGetFunction(&fn, module, "kernel_flat_index_write") != 0 || !fn)
    {
        printf("Kernel not found, aborting.\n");
        return 1;
    }

    /* Real, small, modest backing buffer - 16KB. Never itself the
     * source of any fault; only the index arithmetic can produce one. */
    void *buf = NULL;
    p_hipMalloc(&buf, 16384);
    printf("real buffer: %p (16384 bytes)\n\n", buf);

    /* Sweep dimension products across 2^31 and 2^32. Coordinates are
     * set to the LAST valid element of each dimension (Z-1, H-1, W-1,
     * C-1) - not zero - since idx=((z*H+y)*W+x)*C+c only actually
     * reaches a value near the full dimension product when the
     * coordinates themselves are near the top of their range (an
     * all-zero coordinate makes every multiplication term 0 regardless
     * of how large the dimensions are, which would test nothing - a
     * real bug found and fixed in this exact file before the first
     * real run, see docs/linux-support-spec.md \xc2\xa748a). This matches a
     * real, ordinary access pattern: touching the last element of a
     * large tensor is exactly where a flattened index approaches the
     * total element count. */
    struct { int Z, H, W, C; const char *label; } cases[] = {
        {   1,    1,    1,       1000, "trivial - control, expect success"},
        {1000, 1000, 1000,          2, "~2e9 product, under INT32_MAX (~2.1e9)"},
        {1024, 1024, 1024,          2, "2^31 exactly - the int32 signed-overflow edge"},
        {1024, 1024, 1024,          4, "2^32 - the full unsigned-wrap edge, this project's real fault address scale"},
        {2048, 2048, 1024,          4, "~1.7e10 product, well past both boundaries"},
    };

    for (size_t i = 0; i < sizeof(cases)/sizeof(cases[0]); i++)
    {
        int64_t product = (int64_t)cases[i].Z * cases[i].H * cases[i].W * cases[i].C;
        int z = cases[i].Z - 1, y = cases[i].H - 1, x = cases[i].W - 1, c = cases[i].C - 1;
        void *args[] = { &buf, &cases[i].Z, &cases[i].H, &cases[i].W, &cases[i].C, &z, &y, &x, &c };
        printf("--- Z=%d H=%d W=%d C=%d (dimension product ~%.3e, real int32 range is +/-2.1e9) - %s ---\n",
                cases[i].Z, cases[i].H, cases[i].W, cases[i].C, (double)product, cases[i].label);
        int launch_ret = p_hipModuleLaunchKernel(fn, 1, 1, 1, 1, 1, 1, 0, NULL, args, NULL);
        int sync_ret = p_hipDeviceSynchronize();
        printf("  hipModuleLaunchKernel -> %d, hipDeviceSynchronize -> %d (%s)\n\n",
                launch_ret, sync_ret, p_GetErrorString ? p_GetErrorString(sync_ret) : "?");
        if (sync_ret != 0)
        {
            printf("FAULT at this dimension product - stopping sweep here (further\n");
            printf("launches after a real device fault are not meaningful).\n");
            break;
        }
    }

    if (buf) p_hipFree(buf);
    free(exec_bytes);

    printf("\n=== probe complete ===\n");
    printf("Whatever dimension product first produced a real error above is the\n");
    printf("concrete threshold where an ordinary int32 flattened-index computation\n");
    printf("- ordinary code, not a bug in this probe - starts producing invalid\n");
    printf("addresses on this exact hardware/ROCm combination. See\n");
    printf("docs/linux-support-spec.md \xc2\xa7" "48 for the real result.\n");
    return 0;
}
