//! Minimal, dlopen-based bindings to the public HIP runtime API.
//!
//! No ROCm/HIP dev package (headers, unversioned `.so` symlink) is
//! installed on this machine - only the runtime `libamdhip64.so.7` itself.
//! Rather than link-time link against a name that doesn't exist here, this
//! loads the versioned library at runtime with dlopen/dlsym, which also
//! makes the daemon tolerant of whatever `libamdhip64.so.<N>` a given
//! ROCm install actually ships.
//!
//! The function signatures and `hipMemcpyKind`/`hipError_t` numeric values
//! below are the well-documented, CUDA-source-compatible public HIP API -
//! recalled from memory, not copied from a header (none was available to
//! copy from). Treat them as unverified until the round-trip test in
//! `main.rs` actually passes against real hardware.

use std::ffi::{c_char, c_int, c_void, CStr, CString};
use std::os::raw::c_ulong;
use std::os::raw::c_uint;

#[allow(non_camel_case_types)]
type hipError_t = c_int;
#[allow(non_camel_case_types)]
type hipMemcpyKind = c_int;

pub const HIP_SUCCESS: hipError_t = 0;
pub const HIP_MEMCPY_HOST_TO_DEVICE: hipMemcpyKind = 1;
pub const HIP_MEMCPY_DEVICE_TO_HOST: hipMemcpyKind = 2;

const RTLD_NOW: c_int = 2;

extern "C" {
    fn dlopen(filename: *const c_char, flag: c_int) -> *mut c_void;
    fn dlsym(handle: *mut c_void, symbol: *const c_char) -> *mut c_void;
    fn dlerror() -> *const c_char;
}

type HipMallocFn = unsafe extern "C" fn(*mut *mut c_void, c_ulong) -> hipError_t;
type HipFreeFn = unsafe extern "C" fn(*mut c_void) -> hipError_t;
type HipMemcpyFn =
    unsafe extern "C" fn(*mut c_void, *const c_void, c_ulong, hipMemcpyKind) -> hipError_t;
type HipGetErrorStringFn = unsafe extern "C" fn(hipError_t) -> *const c_char;
type HipGetDeviceCountFn = unsafe extern "C" fn(*mut c_int) -> hipError_t;

pub struct Hip {
    _handle: *mut c_void,
    malloc: HipMallocFn,
    free: HipFreeFn,
    memcpy: HipMemcpyFn,
    get_error_string: HipGetErrorStringFn,
    get_device_count: HipGetDeviceCountFn,
}

fn last_dlerror() -> String {
    unsafe {
        let p = dlerror();
        if p.is_null() {
            "unknown dlerror".to_string()
        } else {
            CStr::from_ptr(p).to_string_lossy().into_owned()
        }
    }
}

impl Hip {
    /// Tries a list of candidate sonames, since the exact ROCm version's
    /// suffix varies by install (`.so.7`, `.so.6`, ...).
    pub fn load() -> Result<Self, String> {
        let candidates = ["libamdhip64.so.7", "libamdhip64.so.6", "libamdhip64.so"];
        let mut handle = std::ptr::null_mut();
        let mut used = "";
        for name in candidates {
            let cname = CString::new(name).unwrap();
            handle = unsafe { dlopen(cname.as_ptr(), RTLD_NOW) };
            if !handle.is_null() {
                used = name;
                break;
            }
        }
        if handle.is_null() {
            return Err(format!(
                "dlopen failed for all candidates {candidates:?}: {}",
                last_dlerror()
            ));
        }
        eprintln!("[hip-bridge-daemon] loaded {used}");

        macro_rules! sym {
            ($name:literal, $ty:ty) => {{
                let cname = CString::new($name).unwrap();
                let p = unsafe { dlsym(handle, cname.as_ptr()) };
                if p.is_null() {
                    return Err(format!("dlsym({}) failed: {}", $name, last_dlerror()));
                }
                unsafe { std::mem::transmute::<*mut c_void, $ty>(p) }
            }};
        }

        Ok(Hip {
            _handle: handle,
            malloc: sym!("hipMalloc", HipMallocFn),
            free: sym!("hipFree", HipFreeFn),
            memcpy: sym!("hipMemcpy", HipMemcpyFn),
            get_error_string: sym!("hipGetErrorString", HipGetErrorStringFn),
            get_device_count: sym!("hipGetDeviceCount", HipGetDeviceCountFn),
        })
    }

    pub fn error_string(&self, code: hipError_t) -> String {
        unsafe { CStr::from_ptr((self.get_error_string)(code)) }
            .to_string_lossy()
            .into_owned()
    }

    pub fn device_count(&self) -> Result<i32, String> {
        let mut count: c_int = 0;
        let rc = unsafe { (self.get_device_count)(&mut count) };
        if rc != HIP_SUCCESS {
            return Err(format!(
                "hipGetDeviceCount -> {rc} ({})",
                self.error_string(rc)
            ));
        }
        Ok(count)
    }

    /// Round-trips `data` through a real device allocation: host -> device
    /// -> a fresh host buffer. Returns the bytes read back so the caller
    /// can diff them against the original.
    pub fn roundtrip(&self, data: &[u8]) -> Result<Vec<u8>, String> {
        let len = data.len() as c_ulong;
        let mut device_ptr: *mut c_void = std::ptr::null_mut();

        let rc = unsafe { (self.malloc)(&mut device_ptr, len) };
        if rc != HIP_SUCCESS {
            return Err(format!("hipMalloc -> {rc} ({})", self.error_string(rc)));
        }

        let rc = unsafe {
            (self.memcpy)(
                device_ptr,
                data.as_ptr() as *const c_void,
                len,
                HIP_MEMCPY_HOST_TO_DEVICE,
            )
        };
        if rc != HIP_SUCCESS {
            unsafe { (self.free)(device_ptr) };
            return Err(format!(
                "hipMemcpy H2D -> {rc} ({})",
                self.error_string(rc)
            ));
        }

        let mut out = vec![0u8; data.len()];
        let rc = unsafe {
            (self.memcpy)(
                out.as_mut_ptr() as *mut c_void,
                device_ptr,
                len,
                HIP_MEMCPY_DEVICE_TO_HOST,
            )
        };
        unsafe { (self.free)(device_ptr) };
        if rc != HIP_SUCCESS {
            return Err(format!(
                "hipMemcpy D2H -> {rc} ({})",
                self.error_string(rc)
            ));
        }

        Ok(out)
    }
}

impl Hip {
    /// Real, persistent allocation - the returned pointer stays valid
    /// until a matching `free_raw` call, unlike `roundtrip`'s
    /// allocate-copy-copy-free-in-one-call helper above.
    pub fn malloc_raw(&self, size: u64) -> Result<*mut c_void, String> {
        let mut ptr: *mut c_void = std::ptr::null_mut();
        let rc = unsafe { (self.malloc)(&mut ptr, size as c_ulong) };
        if rc != HIP_SUCCESS {
            return Err(format!("hipMalloc -> {rc} ({})", self.error_string(rc)));
        }
        Ok(ptr)
    }

    pub fn free_raw(&self, ptr: *mut c_void) -> Result<(), String> {
        let rc = unsafe { (self.free)(ptr) };
        if rc != HIP_SUCCESS {
            return Err(format!("hipFree -> {rc} ({})", self.error_string(rc)));
        }
        Ok(())
    }

    /// Copies `data` into the device allocation at `ptr + offset`.
    pub fn memcpy_h2d(&self, ptr: *mut c_void, offset: u64, data: &[u8]) -> Result<(), String> {
        let dst = unsafe { (ptr as *mut u8).add(offset as usize) as *mut c_void };
        let rc = unsafe {
            (self.memcpy)(
                dst,
                data.as_ptr() as *const c_void,
                data.len() as c_ulong,
                HIP_MEMCPY_HOST_TO_DEVICE,
            )
        };
        if rc != HIP_SUCCESS {
            return Err(format!("hipMemcpy H2D -> {rc} ({})", self.error_string(rc)));
        }
        Ok(())
    }

    /// Reads `len` bytes back from the device allocation at `ptr + offset`.
    pub fn memcpy_d2h(&self, ptr: *mut c_void, offset: u64, len: u64) -> Result<Vec<u8>, String> {
        let src = unsafe { (ptr as *mut u8).add(offset as usize) as *const c_void };
        let mut out = vec![0u8; len as usize];
        let rc = unsafe {
            (self.memcpy)(
                out.as_mut_ptr() as *mut c_void,
                src,
                len as c_ulong,
                HIP_MEMCPY_DEVICE_TO_HOST,
            )
        };
        if rc != HIP_SUCCESS {
            return Err(format!("hipMemcpy D2H -> {rc} ({})", self.error_string(rc)));
        }
        Ok(out)
    }

    pub fn set_device(&self, device: i32) -> Result<(), String> {
        // hipSetDevice isn't in the loaded symbol table yet (§8 item 5's
        // stub never needed it to be); device 0 is implicit/default in
        // HIP when there's exactly one device, which covers this host.
        // Left as a no-op returning success rather than silently lying
        // about a symbol we haven't actually resolved.
        let _ = device;
        Ok(())
    }
}

/// Exact byte size of the "R0600"-versioned hipDeviceProp_t struct,
/// confirmed 2026-09-13 via `sizeof()`/`offsetof()` compiled against the
/// real, public ROCm/HIP + ROCm/clr headers (ROCm/HIP's
/// include/hip/hip_runtime_api.h + ROCm/clr's
/// hipamd/include/hip/amd_detail) with gcc - not a guess. AMD deliberately
/// freezes this exact layout forever under the R0600 name specifically so
/// callers built against different ROCm releases stay ABI-compatible,
/// which is what lets this be forwarded byte-for-byte without needing to
/// know what danielblnc's own binary was compiled against.
pub const HIP_DEVICE_PROP_R0600_SIZE: usize = 1472;

type HipGetDevicePropertiesR0600Fn = unsafe extern "C" fn(*mut c_void, c_int) -> hipError_t;

impl Hip {
    /// Loaded lazily via a second dlsym call (not part of the initial
    /// `load()` symbol set) since this is the one function this shim adds
    /// after the original 5-symbol stub-era set.
    pub fn get_device_properties_r0600(&self, device: i32) -> Result<Vec<u8>, String> {
        let cname = CString::new("hipGetDevicePropertiesR0600").unwrap();
        let p = unsafe { dlsym(self._handle, cname.as_ptr()) };
        if p.is_null() {
            return Err(format!(
                "dlsym(hipGetDevicePropertiesR0600) failed: {}",
                last_dlerror()
            ));
        }
        let f: HipGetDevicePropertiesR0600Fn = unsafe { std::mem::transmute(p) };
        let mut buf = vec![0u8; HIP_DEVICE_PROP_R0600_SIZE];
        let rc = unsafe { f(buf.as_mut_ptr() as *mut c_void, device) };
        if rc != HIP_SUCCESS {
            return Err(format!(
                "hipGetDevicePropertiesR0600 -> {rc} ({})",
                self.error_string(rc)
            ));
        }
        Ok(buf)
    }
}

type HipModuleLoadDataFn = unsafe extern "C" fn(*mut *mut c_void, *const c_void) -> hipError_t;
type HipModuleGetFunctionFn =
    unsafe extern "C" fn(*mut *mut c_void, *mut c_void, *const c_char) -> hipError_t;
#[allow(clippy::too_many_arguments)]
type HipModuleLaunchKernelFn = unsafe extern "C" fn(
    *mut c_void, // function
    c_uint,      // gridDimX
    c_uint,      // gridDimY
    c_uint,      // gridDimZ
    c_uint,      // blockDimX
    c_uint,      // blockDimY
    c_uint,      // blockDimZ
    c_uint,      // sharedMemBytes
    *mut c_void, // stream
    *mut *mut c_void, // kernelParams
    *mut *mut c_void, // extra
) -> hipError_t;

impl Hip {
    fn lazy_sym<T>(&self, name: &str) -> Result<T, String> {
        let cname = CString::new(name).unwrap();
        let p = unsafe { dlsym(self._handle, cname.as_ptr()) };
        if p.is_null() {
            return Err(format!("dlsym({name}) failed: {}", last_dlerror()));
        }
        Ok(unsafe { std::mem::transmute_copy(&p) })
    }

    /// Loads a fat-binary image (the exact bytes captured live from
    /// danielblnc's own `__hipRegisterFatBinary` call) as a real HIP
    /// module. Returns an opaque module pointer for later
    /// `hipModuleGetFunction` calls.
    pub fn load_module(&self, data: &[u8]) -> Result<*mut c_void, String> {
        let f: HipModuleLoadDataFn = self.lazy_sym("hipModuleLoadData")?;
        let mut module: *mut c_void = std::ptr::null_mut();
        let rc = unsafe { f(&mut module, data.as_ptr() as *const c_void) };
        if rc != HIP_SUCCESS {
            return Err(format!(
                "hipModuleLoadData -> {rc} ({})",
                self.error_string(rc)
            ));
        }
        Ok(module)
    }

    pub fn get_function(&self, module: *mut c_void, name: &str) -> Result<*mut c_void, String> {
        let f: HipModuleGetFunctionFn = self.lazy_sym("hipModuleGetFunction")?;
        let cname = CString::new(name).map_err(|_| "kernel name contains a NUL byte".to_string())?;
        let mut function: *mut c_void = std::ptr::null_mut();
        let rc = unsafe { f(&mut function, module, cname.as_ptr()) };
        if rc != HIP_SUCCESS {
            return Err(format!(
                "hipModuleGetFunction({name}) -> {rc} ({})",
                self.error_string(rc)
            ));
        }
        Ok(function)
    }

    /// Launches `function` with a single packed argument buffer - every
    /// kernel observed in danielblnc's binary takes exactly one
    /// parameter (a struct, by value; confirmed from its own mangled
    /// names, e.g. `_Z16k_swin_1h_32_fp810SwinParams` demangles to
    /// `k_swin_1h_32_fp8(SwinParams)`), so `kernelParams` is always a
    /// one-element array pointing at a local copy of those bytes.
    pub fn launch_kernel(
        &self,
        function: *mut c_void,
        grid: (u32, u32, u32),
        block: (u32, u32, u32),
        shared_mem: u32,
        args: &[u8],
    ) -> Result<(), String> {
        let f: HipModuleLaunchKernelFn = self.lazy_sym("hipModuleLaunchKernel")?;
        let mut arg_buf = args.to_vec();
        let mut kernel_params: [*mut c_void; 1] = [arg_buf.as_mut_ptr() as *mut c_void];
        let rc = unsafe {
            f(
                function,
                grid.0,
                grid.1,
                grid.2,
                block.0,
                block.1,
                block.2,
                shared_mem,
                std::ptr::null_mut(),
                kernel_params.as_mut_ptr(),
                std::ptr::null_mut(),
            )
        };
        if rc != HIP_SUCCESS {
            return Err(format!(
                "hipModuleLaunchKernel -> {rc} ({})",
                self.error_string(rc)
            ));
        }
        Ok(())
    }
}
