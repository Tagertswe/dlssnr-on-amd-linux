/* Standalone ROCm runtime capability probe.
 *
 * Does NOT need hipcc, HIP headers, or a device compiler - none are
 * installed on this system (matching daniel/hip-unixlib/native.c's own
 * "no ROCm dev package" constraint). dlopen's the real
 * libamdhip64.so.7 directly, the same way native.c does, and calls the
 * real runtime API with hand-declared prototypes matching HIP's public,
 * stable C ABI (int-sized hipError_t, opaque void* handles).
 *
 * Purpose: isolate whether the *event/stream completion-signaling
 * machinery itself* works on this exact GPU + ROCm 7.1.1 + Ubuntu 26.04
 * combination, independent of danielblnc's proprietary fp8 kernels and
 * independent of this project's own Wine/Proton code entirely. Uses a
 * real hipMemcpyAsync as the "work" between two events, since that goes
 * through the same GPU command-queue / completion-signal path a kernel
 * launch would, without needing to compile anything.
 *
 * See docs/linux-support-spec.md \xc2\xa730/\xc2\xa731 for the investigation this
 * feeds into.
 */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef int hipError_t;

typedef hipError_t (*hipGetDeviceCount_t)(int *);
typedef hipError_t (*hipSetDevice_t)(int);
typedef hipError_t (*hipDeviceSynchronize_t)(void);
typedef hipError_t (*hipMalloc_t)(void **, size_t);
typedef hipError_t (*hipFree_t)(void *);
typedef hipError_t (*hipMemcpy_t)(void *, const void *, size_t, int);
typedef hipError_t (*hipMemcpyAsync_t)(void *, const void *, size_t, int, void *);
typedef hipError_t (*hipGetDevicePropertiesR0600_t)(void *, int);
typedef hipError_t (*hipStreamCreateWithFlags_t)(void **, unsigned int);
typedef hipError_t (*hipStreamSynchronize_t)(void *);
typedef hipError_t (*hipStreamDestroy_t)(void *);
typedef hipError_t (*hipEventCreateWithFlags_t)(void **, unsigned int);
typedef hipError_t (*hipEventRecord_t)(void *, void *);
typedef hipError_t (*hipEventSynchronize_t)(void *);
typedef hipError_t (*hipEventQuery_t)(void *);
typedef hipError_t (*hipEventElapsedTime_t)(float *, void *, void *);
typedef hipError_t (*hipEventDestroy_t)(void *);
typedef const char *(*hipGetErrorString_t)(hipError_t);
typedef hipError_t (*hipDriverGetVersion_t)(int *);
typedef hipError_t (*hipRuntimeGetVersion_t)(int *);

#define HIP_MEMCPY_HOST_TO_DEVICE 1
#define HIP_MEMCPY_DEVICE_TO_HOST 2

/* Exact byte size of the "R0600" hipDeviceProp_t ABI, matching
 * daniel/hip-unixlib/native.c's own confirmed value (see
 * docs/linux-support-spec.md \xc2\xa78 item 8 / \xc2\xa712) - this test reuses that
 * already-validated size rather than re-deriving it. */
#define HIP_DEVICE_PROP_R0600_SIZE 1472

static void *h;
static hipGetErrorString_t p_GetErrorString;

#define LOAD(name) \
    do { \
        p_##name = (name##_t)dlsym(h, #name); \
        printf("  dlsym(%-28s) -> %s\n", #name, p_##name ? "OK" : "MISSING"); \
    } while (0)

static void report(const char *step, hipError_t err)
{
    const char *s = (p_GetErrorString && err != 0) ? p_GetErrorString(err) : (err == 0 ? "success" : "?");
    printf("  %-40s -> %d (%s)\n", step, err, s);
}

int main(void)
{
    printf("=== ROCm runtime capability probe ===\n");

    h = dlopen("libamdhip64.so.7", RTLD_NOW);
    if (!h) h = dlopen("libamdhip64.so", RTLD_NOW);
    if (!h)
    {
        printf("dlopen failed: %s\n", dlerror());
        return 1;
    }
    printf("dlopen: OK\n\n");

    printf("Resolving symbols:\n");
    hipGetDeviceCount_t p_hipGetDeviceCount;
    hipSetDevice_t p_hipSetDevice;
    hipDeviceSynchronize_t p_hipDeviceSynchronize;
    hipMalloc_t p_hipMalloc;
    hipFree_t p_hipFree;
    hipMemcpy_t p_hipMemcpy;
    hipMemcpyAsync_t p_hipMemcpyAsync;
    hipGetDevicePropertiesR0600_t p_hipGetDevicePropertiesR0600;
    hipStreamCreateWithFlags_t p_hipStreamCreateWithFlags;
    hipStreamSynchronize_t p_hipStreamSynchronize;
    hipStreamDestroy_t p_hipStreamDestroy;
    hipEventCreateWithFlags_t p_hipEventCreateWithFlags;
    hipEventRecord_t p_hipEventRecord;
    hipEventSynchronize_t p_hipEventSynchronize;
    hipEventQuery_t p_hipEventQuery;
    hipEventElapsedTime_t p_hipEventElapsedTime;
    hipEventDestroy_t p_hipEventDestroy;
    hipDriverGetVersion_t p_hipDriverGetVersion;
    hipRuntimeGetVersion_t p_hipRuntimeGetVersion;

    p_hipGetDeviceCount = (hipGetDeviceCount_t)dlsym(h, "hipGetDeviceCount");
    printf("  dlsym(hipGetDeviceCount)            -> %s\n", p_hipGetDeviceCount ? "OK" : "MISSING");
    p_hipSetDevice = (hipSetDevice_t)dlsym(h, "hipSetDevice");
    printf("  dlsym(hipSetDevice)                 -> %s\n", p_hipSetDevice ? "OK" : "MISSING");
    p_hipDeviceSynchronize = (hipDeviceSynchronize_t)dlsym(h, "hipDeviceSynchronize");
    printf("  dlsym(hipDeviceSynchronize)         -> %s\n", p_hipDeviceSynchronize ? "OK" : "MISSING");
    p_hipMalloc = (hipMalloc_t)dlsym(h, "hipMalloc");
    printf("  dlsym(hipMalloc)                    -> %s\n", p_hipMalloc ? "OK" : "MISSING");
    p_hipFree = (hipFree_t)dlsym(h, "hipFree");
    printf("  dlsym(hipFree)                      -> %s\n", p_hipFree ? "OK" : "MISSING");
    p_hipMemcpy = (hipMemcpy_t)dlsym(h, "hipMemcpy");
    printf("  dlsym(hipMemcpy)                    -> %s\n", p_hipMemcpy ? "OK" : "MISSING");
    p_hipGetDevicePropertiesR0600 = (hipGetDevicePropertiesR0600_t)dlsym(h, "hipGetDevicePropertiesR0600");
    printf("  dlsym(hipGetDevicePropertiesR0600)  -> %s\n", p_hipGetDevicePropertiesR0600 ? "OK" : "MISSING");
    p_hipMemcpyAsync = (hipMemcpyAsync_t)dlsym(h, "hipMemcpyAsync");
    printf("  dlsym(hipMemcpyAsync)               -> %s\n", p_hipMemcpyAsync ? "OK" : "MISSING");
    p_hipStreamCreateWithFlags = (hipStreamCreateWithFlags_t)dlsym(h, "hipStreamCreateWithFlags");
    printf("  dlsym(hipStreamCreateWithFlags)     -> %s\n", p_hipStreamCreateWithFlags ? "OK" : "MISSING");
    p_hipStreamSynchronize = (hipStreamSynchronize_t)dlsym(h, "hipStreamSynchronize");
    printf("  dlsym(hipStreamSynchronize)         -> %s\n", p_hipStreamSynchronize ? "OK" : "MISSING");
    p_hipStreamDestroy = (hipStreamDestroy_t)dlsym(h, "hipStreamDestroy");
    printf("  dlsym(hipStreamDestroy)             -> %s\n", p_hipStreamDestroy ? "OK" : "MISSING");
    p_hipEventCreateWithFlags = (hipEventCreateWithFlags_t)dlsym(h, "hipEventCreateWithFlags");
    printf("  dlsym(hipEventCreateWithFlags)      -> %s\n", p_hipEventCreateWithFlags ? "OK" : "MISSING");
    p_hipEventRecord = (hipEventRecord_t)dlsym(h, "hipEventRecord");
    printf("  dlsym(hipEventRecord)               -> %s\n", p_hipEventRecord ? "OK" : "MISSING");
    p_hipEventSynchronize = (hipEventSynchronize_t)dlsym(h, "hipEventSynchronize");
    printf("  dlsym(hipEventSynchronize)          -> %s\n", p_hipEventSynchronize ? "OK" : "MISSING");
    p_hipEventQuery = (hipEventQuery_t)dlsym(h, "hipEventQuery");
    printf("  dlsym(hipEventQuery)                -> %s\n", p_hipEventQuery ? "OK" : "MISSING");
    p_hipEventElapsedTime = (hipEventElapsedTime_t)dlsym(h, "hipEventElapsedTime");
    printf("  dlsym(hipEventElapsedTime)          -> %s\n", p_hipEventElapsedTime ? "OK" : "MISSING");
    p_hipEventDestroy = (hipEventDestroy_t)dlsym(h, "hipEventDestroy");
    printf("  dlsym(hipEventDestroy)              -> %s\n", p_hipEventDestroy ? "OK" : "MISSING");
    p_GetErrorString = (hipGetErrorString_t)dlsym(h, "hipGetErrorString");
    printf("  dlsym(hipGetErrorString)            -> %s\n", p_GetErrorString ? "OK" : "MISSING");
    p_hipDriverGetVersion = (hipDriverGetVersion_t)dlsym(h, "hipDriverGetVersion");
    printf("  dlsym(hipDriverGetVersion)          -> %s\n", p_hipDriverGetVersion ? "OK" : "MISSING");
    p_hipRuntimeGetVersion = (hipRuntimeGetVersion_t)dlsym(h, "hipRuntimeGetVersion");
    printf("  dlsym(hipRuntimeGetVersion)         -> %s\n\n", p_hipRuntimeGetVersion ? "OK" : "MISSING");

    if (!p_hipGetDeviceCount || !p_hipMalloc || !p_hipStreamCreateWithFlags || !p_hipEventCreateWithFlags)
    {
        printf("Required symbols missing, aborting.\n");
        return 1;
    }

    int driver_version = -1, runtime_version = -1;
    if (p_hipDriverGetVersion) p_hipDriverGetVersion(&driver_version);
    if (p_hipRuntimeGetVersion) p_hipRuntimeGetVersion(&runtime_version);
    printf("hipDriverGetVersion: %d, hipRuntimeGetVersion: %d\n\n", driver_version, runtime_version);

    int count = -1;
    hipError_t err = p_hipGetDeviceCount(&count);
    report("hipGetDeviceCount", err);
    printf("  device count: %d\n", count);
    if (err != 0 || count < 1) { printf("No usable device, aborting.\n"); return 1; }

    err = p_hipSetDevice(0);
    report("hipSetDevice(0)", err);

    if (p_hipGetDevicePropertiesR0600)
    {
        printf("\n--- device properties ABI sanity (validates native.c's 1472-byte assumption) ---\n");
        /* Exactly the size daniel/hip-unixlib/native.c already assumes - if
         * the real library actually writes more than this into the buffer,
         * that's a real heap overflow this test would be the first to
         * surface, rather than finding out via a crash under Wine. */
        char props[HIP_DEVICE_PROP_R0600_SIZE];
        memset(props, 0xAA, sizeof(props)); /* poison, so unwritten tail bytes are obvious */
        err = p_hipGetDevicePropertiesR0600(props, 0);
        report("hipGetDevicePropertiesR0600(device 0)", err);
        if (err == 0)
        {
            /* hipDeviceProp_t's very first field is char name[256] - a
             * long-stable, documented part of the public ABI, safe to
             * read without the rest of the struct's exact layout. */
            char name[257];
            memcpy(name, props, 256);
            name[256] = '\0';
            printf("  name field (first 256 bytes): \"%s\"\n", name);
        }
    }

    printf("\n--- basic memory sanity ---\n");
    void *dev_a = NULL, *dev_b = NULL;
    const size_t bytes = 4 * 1024 * 1024; /* 4 MiB, deliberately not tiny */
    err = p_hipMalloc(&dev_a, bytes);
    report("hipMalloc(dev_a, 4MiB)", err);
    err = p_hipMalloc(&dev_b, bytes);
    report("hipMalloc(dev_b, 4MiB)", err);

    if (p_hipMemcpy)
    {
        char host_pattern[64], host_readback[64];
        memset(host_pattern, 0x5a, sizeof(host_pattern));
        memset(host_readback, 0, sizeof(host_readback));
        err = p_hipMemcpy(dev_a, host_pattern, sizeof(host_pattern), HIP_MEMCPY_HOST_TO_DEVICE);
        report("hipMemcpy(H2D, sync)", err);
        err = p_hipMemcpy(host_readback, dev_a, sizeof(host_readback), HIP_MEMCPY_DEVICE_TO_HOST);
        report("hipMemcpy(D2H, sync)", err);
        printf("  round-trip matches: %s\n", memcmp(host_pattern, host_readback, sizeof(host_pattern)) == 0 ? "yes" : "NO");
    }

    printf("\n--- stream + event machinery (the actual thing under test) ---\n");
    void *stream = NULL;
    err = p_hipStreamCreateWithFlags(&stream, 1 /* hipStreamNonBlocking */);
    report("hipStreamCreateWithFlags(nonblocking)", err);

    void *ev_start = NULL, *ev_stop = NULL;
    err = p_hipEventCreateWithFlags(&ev_start, 0);
    report("hipEventCreateWithFlags(start)", err);
    err = p_hipEventCreateWithFlags(&ev_stop, 0);
    report("hipEventCreateWithFlags(stop)", err);

    err = p_hipEventRecord(ev_start, stream);
    report("hipEventRecord(start, stream)", err);

    /* Real GPU DMA-engine work on the stream, between the two events -
     * the same completion-signaling path a kernel launch would use,
     * without needing anything compiled. */
    if (dev_a && dev_b && p_hipMemcpyAsync)
    {
        err = p_hipMemcpyAsync(dev_b, dev_a, bytes, 3 /* hipMemcpyDeviceToDevice */, stream);
        report("hipMemcpyAsync(dev_b<-dev_a, on stream)", err);
    }

    err = p_hipEventRecord(ev_stop, stream);
    report("hipEventRecord(stop, stream)", err);

    err = p_hipStreamSynchronize(stream);
    report("hipStreamSynchronize(stream)", err);

    err = p_hipEventSynchronize(ev_stop);
    report("hipEventSynchronize(stop)", err);

    err = p_hipEventQuery(ev_stop);
    report("hipEventQuery(stop) [expect success=0]", err);

    float ms = -1.0f;
    err = p_hipEventElapsedTime(&ms, ev_start, ev_stop);
    report("hipEventElapsedTime(start, stop)", err);
    printf("  elapsed: %.4f ms\n", ms);

    printf("\n--- cleanup ---\n");
    if (p_hipEventDestroy) { p_hipEventDestroy(ev_start); p_hipEventDestroy(ev_stop); }
    if (p_hipStreamDestroy) p_hipStreamDestroy(stream);
    if (dev_a) p_hipFree(dev_a);
    if (dev_b) p_hipFree(dev_b);

    printf("\n=== probe complete ===\n");
    return 0;
}
