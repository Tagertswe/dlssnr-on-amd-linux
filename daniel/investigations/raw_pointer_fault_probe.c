/* Raw HIP pointer fault probe.
 *
 * Direct follow-up to the standalone D3D12 test built in
 * ~/repos/vkd3d-proton (tests/d3d12_external_memory_fd.c,
 * test_external_memory_fd_shared_buffer_far_offset_access - see
 * docs/linux-support-spec.md \xc2\xa741). That test proved a genuinely
 * out-of-bounds access at the real captured fault address
 * (0x100000000, from \xc2\xa738's devcoredump) does NOT crash the GPU when
 * it goes through a real, descriptor-bounded D3D12 UAV - the
 * driver/hardware safely clamps it.
 *
 * HIP/ROCm compute kernels are different: they use flat, raw GPU
 * pointers with no descriptor-level bounds checking at all (unlike
 * D3D12's UAV/SRV views). This probe tests that directly: compile a
 * real, original, clean-room kernel that writes through a raw pointer
 * argument, and launch it with that argument set to the literal
 * address 0x100000000 - the exact real fault address - to see whether
 * *this* kind of access (no descriptor to clamp against) is what
 * actually faults the GPU, matching the real ring-timeout/page-fault
 * signature from \xc2\xa737/\xc2\xa738, unlike the D3D12 UAV case.
 *
 * Real safety note, not theoretical: this is deliberately trying to
 * reproduce the same class of GPU page fault/ring-timeout this whole
 * investigation has been chasing. Expect this to potentially trigger
 * a real `ring gfx_0.0.0 timeout` / kernel-driver ring reset, the same
 * as every real crash this session - which the kernel has reset
 * through cleanly every single time so far (see \xc2\xa737's
 * "device wedged, but recovered through reset" journal lines). Run
 * with a hard wall-clock timeout and under a memory cap regardless.
 *
 * No proprietary code: this kernel is original, minimal, clean-room
 * test code written for this diagnostic alone. It does not contain,
 * reference, or derive from any of danielblnc's actual kernels.
 *
 * REQUIRES `libamd-comgr-dev` (headers only, matches the installed
 * `libamd-comgr3` runtime exactly - see daniel/investigations/README.md,
 * including the no-root `apt-get download`/`dpkg-deb -x` alternative):
 *
 *   sudo apt install libamd-comgr-dev
 *   gcc -Wall -o raw_pointer_fault_probe raw_pointer_fault_probe.c -ldl -lamd_comgr
 *   timeout --kill-after=10s 30s env LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu ./raw_pointer_fault_probe
 */
#include <amd_comgr/amd_comgr.h>

#include <stdbool.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

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
    fflush(stdout);
}

static void check_comgr(const char *step, amd_comgr_status_t status)
{
    const char *msg = NULL;
    if (status != AMD_COMGR_STATUS_SUCCESS)
        amd_comgr_status_string(status, &msg);
    printf("  %-45s -> %d%s%s\n", step, status, msg ? " " : "", msg ? msg : "");
    fflush(stdout);
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

/* kernel_touch: fp32-only control, real allocated output buffer -
 * proves the module loaded and basic execution works at all.
 *
 * kernel_write_raw_ptr: the actual question. Writes through whatever
 * raw pointer it's given, no bounds checking possible - this is
 * exactly how HIP/ROCm kernels normally address memory (unlike
 * D3D12's descriptor-bounded UAVs, tested separately in \xc2\xa741). Called
 * with the literal address 0x100000000 - \xc2\xa738's real captured fault
 * address - not a real allocation. */
static const char *kernel_source =
    "__kernel void kernel_touch(__global float *out) {\n"
    "    out[0] = 3.5f + 1.5f;\n"
    "}\n"
    "\n"
    "__kernel void kernel_write_raw_ptr(__global float *raw_ptr) {\n"
    "    *raw_ptr = 42.0f;\n"
    "}\n";

int main(void)
{
    printf("=== raw HIP pointer fault probe ===\n\n");
    printf("Target address: 0x100000000 (the real fault address captured in\n");
    printf("docs/linux-support-spec.md \xc2\xa7" "38's devcoredump). This is deliberately\n");
    printf("NOT a real allocation - the whole point is to see what a raw,\n");
    printf("unchecked HIP pointer write to exactly this address does.\n\n");

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

    printf("\n--- compiling kernel_touch + kernel_write_raw_ptr via comgr, target gfx1201 ---\n");

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

    check_comgr("amd_comgr_create_data_set(reloc)", amd_comgr_create_data_set(&reloc_set));
    check_comgr("do_action(CODEGEN_BC_TO_RELOCATABLE)",
            amd_comgr_do_action(AMD_COMGR_ACTION_CODEGEN_BC_TO_RELOCATABLE,
                    action_info, linked_bc_set, reloc_set));

    check_comgr("amd_comgr_create_data_set(exec)", amd_comgr_create_data_set(&exec_set));
    check_comgr("do_action(LINK_RELOCATABLE_TO_EXECUTABLE)",
            amd_comgr_do_action(AMD_COMGR_ACTION_LINK_RELOCATABLE_TO_EXECUTABLE,
                    action_info, reloc_set, exec_set));

    size_t exec_count = 0;
    check_comgr("amd_comgr_action_data_count(exec)",
            amd_comgr_action_data_count(exec_set, AMD_COMGR_DATA_KIND_EXECUTABLE, &exec_count));
    if (exec_count < 1) { printf("Compilation produced no executable, aborting.\n"); return 1; }

    amd_comgr_data_t exec_data;
    check_comgr("amd_comgr_action_data_get_data(exec, 0)",
            amd_comgr_action_data_get_data(exec_set, AMD_COMGR_DATA_KIND_EXECUTABLE, 0, &exec_data));

    size_t exec_size = 0;
    check_comgr("amd_comgr_get_data(size query)", amd_comgr_get_data(exec_data, &exec_size, NULL));

    void *exec_bytes = malloc(exec_size);
    check_comgr("amd_comgr_get_data(bytes)", amd_comgr_get_data(exec_data, &exec_size, exec_bytes));

    printf("\n--- loading real compiled code object via hipModule*, same API native.c uses ---\n");

    void *module = NULL;
    check_hip("hipModuleLoadData(compiled code object)", p_hipModuleLoadData(&module, exec_bytes));
    if (!module) { printf("Module load failed, aborting.\n"); return 1; }

    void *dev_out = NULL;
    check_hip("hipMalloc(dev_out, 4 bytes)", p_hipMalloc(&dev_out, sizeof(float)));

    printf("\n--- kernel_touch (control, real allocated buffer) ---\n");
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

    printf("\n--- kernel_write_raw_ptr(0x100000000) - THE ACTUAL QUESTION ---\n");
    void *fn_raw = NULL;
    check_hip("hipModuleGetFunction(kernel_write_raw_ptr)",
            p_hipModuleGetFunction(&fn_raw, module, "kernel_write_raw_ptr"));
    if (fn_raw)
    {
        void *bad_ptr = (void *)(uintptr_t)0x100000000ull;
        void *args2[] = { &bad_ptr };
        check_hip("hipModuleLaunchKernel(kernel_write_raw_ptr)",
                p_hipModuleLaunchKernel(fn_raw, 1, 1, 1, 1, 1, 1, 0, NULL, args2, NULL));
        printf("  calling hipDeviceSynchronize now - this is the real test moment...\n");
        fflush(stdout);
        check_hip("hipDeviceSynchronize [THE ANSWER IS HERE]", p_hipDeviceSynchronize());
        printf("  (if you see this line, the process did not hang)\n");
    }

    if (dev_out) p_hipFree(dev_out);
    free(exec_bytes);

    printf("\n=== probe complete ===\n");
    printf("If hipDeviceSynchronize after kernel_write_raw_ptr reported a real\n");
    printf("error (not 0/success), or the process hung/never reached this line,\n");
    printf("that is direct, empirical confirmation that a raw, unchecked HIP\n");
    printf("pointer write to this exact address is what actually faults the GPU -\n");
    printf("unlike the descriptor-bounded D3D12 UAV case in \xc2\xa7" "41, which was safely\n");
    printf("clamped. See docs/linux-support-spec.md \xc2\xa7" "42.\n");
    return 0;
}
