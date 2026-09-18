//! HIP runtime shim consumed by danielblnc/DLSS-NR-on-AMD's standalone
//! `version.dll` proxy under Proton.
//!
//! Originally a pure stub (see git history) that let the proxy load and
//! fail gracefully with no HIP device. Per docs/linux-support-spec.md
//! §12's staged plan, device/memory functions now forward for real to
//! `hip-bridge-daemon` (a native Linux process, reachable over TCP) which
//! executes them against a real ROCm/HIP device. Kernel launch, fat-binary
//! registration, and external-memory functions remain stubbed for now -
//! later stages of the same plan.
//!
//! This crate contains no proprietary code, weights, or logic from
//! NVIDIA or danielblnc — only public HIP API *names* wired either to a
//! generic forwarding relay (this file) or, where not yet implemented,
//! trivial "unsupported" responses.
//!
//! Every export logs its call to a text file next to the game executable,
//! matching the logging convention already used elsewhere in this
//! ecosystem (danielblnc's own `_dlssnr_on_amd.log`, ReShade's
//! `ReShade.log`).

use std::ffi::{c_char, c_void, CStr};
use std::fs::OpenOptions;
use std::io::Write as _;
use std::net::TcpStream;
use std::sync::OnceLock;
use std::sync::Mutex;
use std::time::{SystemTime, UNIX_EPOCH};

use hip_bridge_protocol::{Request, Response};

// ---------------------------------------------------------------------
// Logging
// ---------------------------------------------------------------------

fn log_path() -> &'static Mutex<std::path::PathBuf> {
    static PATH: OnceLock<Mutex<std::path::PathBuf>> = OnceLock::new();
    PATH.get_or_init(|| {
        let dir = std::env::current_exe()
            .ok()
            .and_then(|p| p.parent().map(|p| p.to_path_buf()))
            .unwrap_or_else(|| std::path::PathBuf::from("."));
        Mutex::new(dir.join("amdhip64_7_stub.log"))
    })
}

fn log(msg: &str) {
    let path = log_path().lock().unwrap_or_else(|e| e.into_inner());
    let ts = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_secs())
        .unwrap_or(0);
    if let Ok(mut f) = OpenOptions::new().create(true).append(true).open(&*path) {
        let _ = writeln!(f, "[{ts}] {msg}");
    }
}

// ---------------------------------------------------------------------
// hip-bridge-daemon connection
// ---------------------------------------------------------------------

fn bridge_port() -> u16 {
    std::env::var("HIP_BRIDGE_PORT")
        .ok()
        .and_then(|s| s.parse().ok())
        .unwrap_or(47411)
}

/// Lazily connects to hip-bridge-daemon on first use and keeps the
/// connection for the process's lifetime. `None` means either it was
/// never reachable or a previous I/O error poisoned it - callers treat
/// that the same as "no device", which is the same graceful-degradation
/// contract the pure stub had when HIP was unavailable at all.
fn bridge() -> &'static Mutex<Option<TcpStream>> {
    static CONN: OnceLock<Mutex<Option<TcpStream>>> = OnceLock::new();
    CONN.get_or_init(|| {
        let addr = format!("127.0.0.1:{}", bridge_port());
        match TcpStream::connect(&addr) {
            Ok(s) => {
                log(&format!("connected to hip-bridge-daemon at {addr}"));
                Mutex::new(Some(s))
            }
            Err(e) => {
                log(&format!("hip-bridge-daemon not reachable at {addr}: {e}"));
                Mutex::new(None)
            }
        }
    })
}

/// Sends one request and waits for its response, holding the connection
/// mutex for the round-trip - the protocol is strictly one-in-flight
/// request at a time, so this also serializes concurrent callers safely.
fn call_bridge(req: Request) -> Result<Vec<u8>, String> {
    let mut guard = bridge().lock().unwrap_or_else(|e| e.into_inner());
    let stream = guard.as_mut().ok_or_else(|| "hip-bridge-daemon not connected".to_string())?;
    if let Err(e) = req.write_to(stream) {
        *guard = None; // connection is presumed dead; don't keep retrying it
        return Err(format!("write to hip-bridge-daemon failed: {e}"));
    }
    match Response::read_from(stream) {
        Ok(resp) => resp.into_result(),
        Err(e) => {
            *guard = None;
            Err(format!("read from hip-bridge-daemon failed: {e}"))
        }
    }
}

// ---------------------------------------------------------------------
// hipError_t values.
//
// Verified 2026-09-13: HIP_SUCCESS/HIP_MEMCPY_HOST_TO_DEVICE/
// HIP_MEMCPY_DEVICE_TO_HOST below were exercised for real against the
// installed ROCm 7.1 runtime (see windows-runtime-bridge/hip-bridge's native-daemon and
// docs/linux-support-spec.md §8 item 6) and are confirmed correct.
// HIP_ERROR_NO_DEVICE/HIP_ERROR_NOT_SUPPORTED remain best-effort
// recollections used only for our own stub-side error reporting, not
// values HIP itself returned in a verified run.
// ---------------------------------------------------------------------

const HIP_SUCCESS: i32 = 0;
const HIP_ERROR_NO_DEVICE: i32 = 100;
const HIP_ERROR_NOT_SUPPORTED: i32 = 801;
const HIP_MEMCPY_HOST_TO_DEVICE: i32 = 1;
const HIP_MEMCPY_DEVICE_TO_HOST: i32 = 2;

static LAST_ERROR: Mutex<i32> = Mutex::new(HIP_SUCCESS);

fn set_last_error(code: i32) -> i32 {
    if let Ok(mut e) = LAST_ERROR.lock() {
        *e = code;
    }
    code
}

const ERR_STRING: &CStr = c"amdhip64_7 shim: not implemented yet (see docs/linux-support-spec.md \xc2\xa712)";
const NO_BRIDGE_STRING: &CStr = c"amdhip64_7 shim: hip-bridge-daemon not reachable";

// ---------------------------------------------------------------------
// Device-pointer <-> daemon-handle encoding
//
// The daemon owns the real HIP device pointers; this process never
// dereferences them directly. What danielblnc's runtime treats as an
// opaque "device pointer" can just be the daemon's own u64 handle,
// reinterpreted as a pointer value - it's never touched as memory here,
// only passed back into later calls (hipMemcpy/hipFree/...), so no
// separate local handle table is needed.
// ---------------------------------------------------------------------

// Bug found empirically 2026-09-13 (docs/linux-support-spec.md's §12
// follow-up notes): danielblnc's runtime does pointer arithmetic on
// device pointers we hand it (e.g. `our_ptr + N`) before passing the
// result back into hipMemcpy - a naive "handle IS the pointer" scheme
// can't resolve that shifted value back to anything. Fix: track each
// allocation's synthetic address *range*, not just a single point value,
// so an in-bounds offset can be decoded as (handle, offset) - the wire
// protocol already had an `offset` field for exactly this, previously
// always sent as 0.
struct AllocRange {
    handle: u64,
    size: u64,
}

struct AddressSpace {
    next_base: u64,
    ranges: std::collections::BTreeMap<u64, AllocRange>,
}

impl AddressSpace {
    /// Starts comfortably away from 0 /small values so a fake address
    /// can never be mistaken for (or collide with) a real small integer
    /// or null-adjacent value the runtime might also be juggling.
    fn new() -> Self {
        AddressSpace {
            next_base: 0x0000_7000_0000_0000,
            ranges: std::collections::BTreeMap::new(),
        }
    }

    /// Reserves a fresh range for `handle`/`size` and returns its base
    /// address. Each range is padded well beyond its real size so that
    /// any plausible in-bounds offset arithmetic the caller does can
    /// never spill into the next allocation's base by coincidence.
    fn allocate(&mut self, handle: u64, size: u64) -> u64 {
        let base = self.next_base;
        let reserved = size.max(1).next_multiple_of(4096) + 4096;
        self.next_base += reserved;
        self.ranges.insert(base, AllocRange { handle, size });
        base
    }

    /// Resolves an arbitrary pointer value to (handle, offset) if it
    /// falls within some allocation's [base, base+size) range.
    fn resolve(&self, ptr: u64) -> Option<(u64, u64)> {
        let (&base, r) = self.ranges.range(..=ptr).next_back()?;
        let offset = ptr - base;
        if offset < r.size.max(1) {
            Some((r.handle, offset))
        } else {
            None
        }
    }

    /// Frees by exact base match only - matches real hipFree semantics
    /// (callers free the original allocation pointer, never an offset
    /// one).
    fn remove_exact(&mut self, ptr: u64) -> Option<u64> {
        self.ranges.remove(&ptr).map(|r| r.handle)
    }
}

fn address_space() -> &'static Mutex<AddressSpace> {
    static SPACE: OnceLock<Mutex<AddressSpace>> = OnceLock::new();
    SPACE.get_or_init(|| Mutex::new(AddressSpace::new()))
}

fn handle_to_ptr(handle: u64, size: u64) -> *mut c_void {
    address_space().lock().unwrap().allocate(handle, size) as *mut c_void
}

/// Resolves a caller-supplied pointer to (handle, offset), logging and
/// returning `None` if it doesn't fall within any known allocation -
/// exactly the failure this fix targets, now reported clearly instead of
/// silently mismatching.
fn ptr_to_handle_offset(ptr: *mut c_void, what: &str) -> Option<(u64, u64)> {
    let addr = ptr as u64;
    match address_space().lock().unwrap().resolve(addr) {
        Some((handle, offset)) => Some((handle, offset)),
        None => {
            log(&format!("{what}: pointer {ptr:p} does not resolve to any known allocation"));
            None
        }
    }
}

fn free_ptr_to_handle(ptr: *mut c_void) -> Option<u64> {
    address_space().lock().unwrap().remove_exact(ptr as u64)
}

// ---------------------------------------------------------------------
// Fat-binary bootstrap (still stubbed - stage 2+ of §12's plan)
// ---------------------------------------------------------------------

// ---------------------------------------------------------------------
// Fat-binary parsing (__CLANG_OFFLOAD_BUNDLE__ container format)
//
// Confirmed via static analysis in docs/linux-support-spec.md Â§8 item 3:
// 24-byte magic "__CLANG_OFFLOAD_BUNDLE__", then a u64 bundle count, then
// per bundle: u64 offset, u64 size, u64 triple_len, triple_len bytes of
// target-triple string. `data` here is a *live* pointer to danielblnc's
// actual compiled kernel bytes, already resident in this process's
// memory the moment his runtime calls __hipRegisterFatBinary - nothing
// is extracted from a file (see docs/linux-support-spec.md Â§12's
// engineering-plan notes on this distinction and its licensing angle).
const OFFLOAD_BUNDLE_MAGIC: &[u8] = b"__CLANG_OFFLOAD_BUNDLE__";

/// Reads only the header/metadata (never the kernel payload bytes
/// themselves) to determine the whole blob's total size, so it can be
/// copied in one safe, bounded read. Returns `None` if the memory at
/// `data` doesn't look like a real bundle (defensive - this is live
/// process memory, not a file we've already validated).
unsafe fn offload_bundle_total_size(data: *const c_void) -> Option<u64> {
    let base = data as *const u8;
    let magic = std::slice::from_raw_parts(base, OFFLOAD_BUNDLE_MAGIC.len());
    if magic != OFFLOAD_BUNDLE_MAGIC {
        return None;
    }
    let mut cursor = OFFLOAD_BUNDLE_MAGIC.len();
    let read_u64 = |off: usize| -> u64 {
        let mut buf = [0u8; 8];
        std::ptr::copy_nonoverlapping(base.add(off), buf.as_mut_ptr(), 8);
        u64::from_le_bytes(buf)
    };

    let num_bundles = read_u64(cursor);
    cursor += 8;
    const SANITY_MAX_BUNDLES: u64 = 1000;
    const SANITY_MAX_SIZE: u64 = 512 * 1024 * 1024; // matches the real weights file's rough scale
    if num_bundles > SANITY_MAX_BUNDLES {
        log(&format!("offload bundle: implausible bundle count {num_bundles}, refusing to parse"));
        return None;
    }

    let mut total: u64 = cursor as u64;
    for _ in 0..num_bundles {
        let offset = read_u64(cursor);
        let size = read_u64(cursor + 8);
        let triple_len = read_u64(cursor + 16);
        if triple_len > 4096 {
            log("offload bundle: implausible triple length, refusing to parse");
            return None;
        }
        cursor += 24 + triple_len as usize;
        total = total.max(offset.saturating_add(size));
        if total > SANITY_MAX_SIZE {
            log(&format!("offload bundle: implausible total size {total}, refusing to parse"));
            return None;
        }
    }
    Some(total)
}

fn fatbin_registry() -> &'static Mutex<std::collections::HashMap<u64, u64>> {
    static REG: OnceLock<Mutex<std::collections::HashMap<u64, u64>>> = OnceLock::new();
    REG.get_or_init(|| Mutex::new(std::collections::HashMap::new()))
}

fn function_registry() -> &'static Mutex<std::collections::HashMap<u64, u64>> {
    static REG: OnceLock<Mutex<std::collections::HashMap<u64, u64>>> = OnceLock::new();
    REG.get_or_init(|| Mutex::new(std::collections::HashMap::new()))
}

static NEXT_FATBIN_ID: Mutex<u64> = Mutex::new(1);

#[no_mangle]
pub extern "C" fn __hipRegisterFatBinary(data: *const c_void) -> *mut c_void {
    log(&format!("__hipRegisterFatBinary(data={data:p})"));

    let id = {
        let mut n = NEXT_FATBIN_ID.lock().unwrap();
        let id = *n;
        *n += 1;
        id
    };

    // `data` may be the raw bundle pointer directly, or (the standard
    // compiler-generated layout) a pointer to a small wrapper struct
    // {magic: u32, version: u32, data: *const c_void, unused: *const
    // c_void} whose `data` field at byte offset 8 is the real bundle
    // pointer. Try direct first, then the one-level-indirect form, since
    // there's no reliable way to distinguish them except by which one
    // actually parses as a real bundle.
    let (bundle_ptr, size) = match unsafe { offload_bundle_total_size(data) } {
        Some(s) => (data, s),
        None => {
            let inner_ptr = unsafe {
                let mut buf = [0u8; 8];
                std::ptr::copy_nonoverlapping((data as *const u8).add(8), buf.as_mut_ptr(), 8);
                usize::from_le_bytes(buf) as *const c_void
            };
            match unsafe { offload_bundle_total_size(inner_ptr) } {
                Some(s) => {
                    log("__hipRegisterFatBinary: bundle found via wrapper-struct indirection");
                    (inner_ptr, s)
                }
                None => {
                    log("__hipRegisterFatBinary: not a recognizable __CLANG_OFFLOAD_BUNDLE__ (tried direct and wrapper-indirect) - module load skipped");
                    return id as *mut c_void;
                }
            }
        }
    };
    log(&format!("__hipRegisterFatBinary: parsed bundle, total size {size} bytes"));

    let bytes = unsafe { std::slice::from_raw_parts(bundle_ptr as *const u8, size as usize) }.to_vec();
    match call_bridge(Request::LoadModule { data: bytes }) {
        Ok(body) if body.len() == 8 => {
            let module_handle = u64::from_le_bytes(body[..8].try_into().unwrap());
            log(&format!("__hipRegisterFatBinary: loaded as module handle {module_handle} (real)"));
            fatbin_registry().lock().unwrap().insert(id, module_handle);
        }
        Ok(_) => log("__hipRegisterFatBinary: malformed LoadModule response"),
        Err(e) => log(&format!("__hipRegisterFatBinary: LoadModule failed: {e}")),
    }

    id as *mut c_void
}

#[no_mangle]
pub extern "C" fn __hipRegisterFunction(
    fat_bin: *mut c_void,
    host_fn: *const c_void,
    device_fn: *const c_char,
    device_name: *const c_char,
    thread_limit: i32,
    tid: *mut c_void,
    bid: *mut c_void,
    b_dim: *mut c_void,
    g_dim: *mut c_void,
    w_size: *mut i32,
) {
    let name = unsafe { safe_cstr(device_name) };
    log(&format!(
        "__hipRegisterFunction(fat_bin={fat_bin:p}, host_fn={host_fn:p}, device_fn={:?}, device_name={name}, thread_limit={thread_limit}, tid={tid:p}, bid={bid:p}, b_dim={b_dim:p}, g_dim={g_dim:p}, w_size={w_size:p})",
        unsafe { safe_cstr(device_fn) }
    ));

    let fatbin_id = fat_bin as u64;
    let module_handle = match fatbin_registry().lock().unwrap().get(&fatbin_id).copied() {
        Some(h) => h,
        None => {
            log("__hipRegisterFunction: no module loaded for this fat binary - skipping");
            return;
        }
    };
    match call_bridge(Request::GetFunction { module: module_handle, name: name.into_owned() }) {
        Ok(body) if body.len() == 8 => {
            let function_handle = u64::from_le_bytes(body[..8].try_into().unwrap());
            function_registry()
                .lock()
                .unwrap()
                .insert(host_fn as u64, function_handle);
        }
        Ok(_) => log("__hipRegisterFunction: malformed GetFunction response"),
        Err(e) => log(&format!("__hipRegisterFunction: GetFunction failed: {e}")),
    }
}

#[no_mangle]
pub extern "C" fn __hipRegisterVar(
    fat_bin: *mut c_void,
    host_var: *const c_void,
    device_name: *const c_char,
) {
    log(&format!(
        "__hipRegisterVar(fat_bin={fat_bin:p}, host_var={host_var:p}, device_name={})",
        unsafe { safe_cstr(device_name) }
    ));
}

#[no_mangle]
pub extern "C" fn __hipUnregisterFatBinary(fat_bin: *mut c_void) {
    log(&format!("__hipUnregisterFatBinary(fat_bin={fat_bin:p})"));
    if !fat_bin.is_null() {
        unsafe {
            drop(Box::from_raw(fat_bin as *mut u8));
        }
    }
}

#[repr(C)]
pub struct Dim3 {
    pub x: u32,
    pub y: u32,
    pub z: u32,
}

// Bug found empirically 2026-09-13: this pair always returning an error
// meant the compiler-generated <<<>>> kernel-launch stub bailed out
// silently before ever calling hipLaunchKernel at all - every single
// kernel call site was a no-op, not because hipLaunchKernel itself was
// stubbed (it also is, for now), but because it was never even reached.
// Real semantics: push stores the configuration for the *current thread*
// (kernel launches from a <<<>>> expression push, then the generated
// stub immediately pops the same configuration before calling
// hipLaunchKernel with it) - a thread-local stack, since nested/recursive
// launches are technically possible.
#[derive(Clone, Copy)]
struct CallConfig {
    grid: Dim3Value,
    block: Dim3Value,
    shared_mem: usize,
    stream: *mut c_void,
}
#[derive(Clone, Copy)]
struct Dim3Value {
    x: u32,
    y: u32,
    z: u32,
}

thread_local! {
    static CALL_CONFIG_STACK: std::cell::RefCell<Vec<CallConfig>> = const { std::cell::RefCell::new(Vec::new()) };
}

#[no_mangle]
pub extern "C" fn __hipPushCallConfiguration(
    grid_dim: *const Dim3,
    block_dim: *const Dim3,
    shared_mem: usize,
    stream: *mut c_void,
) -> i32 {
    log(&format!(
        "__hipPushCallConfiguration(shared_mem={shared_mem}, stream={stream:p}) grid_dim={grid_dim:p} block_dim={block_dim:p}"
    ));
    if grid_dim.is_null() || block_dim.is_null() {
        log("__hipPushCallConfiguration: null grid/block pointer");
        return set_last_error(HIP_ERROR_NOT_SUPPORTED);
    }
    let (grid, block) = unsafe {
        let g = &*grid_dim;
        let b = &*block_dim;
        (
            Dim3Value { x: g.x, y: g.y, z: g.z },
            Dim3Value { x: b.x, y: b.y, z: b.z },
        )
    };
    CALL_CONFIG_STACK.with(|stack| {
        stack.borrow_mut().push(CallConfig { grid, block, shared_mem, stream });
    });
    HIP_SUCCESS
}

#[no_mangle]
pub extern "C" fn __hipPopCallConfiguration(
    grid_dim: *mut Dim3,
    block_dim: *mut Dim3,
    shared_mem: *mut usize,
    stream: *mut c_void,
) -> i32 {
    log(&format!(
        "__hipPopCallConfiguration(grid_dim={grid_dim:p}, block_dim={block_dim:p}, shared_mem={shared_mem:p}, stream={stream:p})"
    ));
    let popped = CALL_CONFIG_STACK.with(|stack| stack.borrow_mut().pop());
    match popped {
        Some(cfg) => {
            unsafe {
                if !grid_dim.is_null() {
                    (*grid_dim).x = cfg.grid.x;
                    (*grid_dim).y = cfg.grid.y;
                    (*grid_dim).z = cfg.grid.z;
                }
                if !block_dim.is_null() {
                    (*block_dim).x = cfg.block.x;
                    (*block_dim).y = cfg.block.y;
                    (*block_dim).z = cfg.block.z;
                }
                if !shared_mem.is_null() {
                    *shared_mem = cfg.shared_mem;
                }
                if !stream.is_null() {
                    *(stream as *mut *mut c_void) = cfg.stream;
                }
            }
            HIP_SUCCESS
        }
        None => {
            log("__hipPopCallConfiguration: stack empty - push/pop mismatch");
            set_last_error(HIP_ERROR_NOT_SUPPORTED)
        }
    }
}

// ---------------------------------------------------------------------
// Device / runtime queries - real, forwarded (§12 stage 1)
// ---------------------------------------------------------------------

#[no_mangle]
pub extern "C" fn hipGetDeviceCount(count: *mut i32) -> i32 {
    log("hipGetDeviceCount()");
    match call_bridge(Request::DeviceCount) {
        Ok(body) if body.len() == 4 => {
            let n = i32::from_le_bytes(body[..4].try_into().unwrap());
            log(&format!("hipGetDeviceCount() -> {n} (real)"));
            if !count.is_null() {
                unsafe { *count = n };
            }
            HIP_SUCCESS
        }
        Ok(_) => {
            log("hipGetDeviceCount(): malformed response body");
            if !count.is_null() {
                unsafe { *count = 0 };
            }
            set_last_error(HIP_ERROR_NO_DEVICE)
        }
        Err(e) => {
            log(&format!("hipGetDeviceCount(): {e}"));
            if !count.is_null() {
                unsafe { *count = 0 };
            }
            set_last_error(HIP_ERROR_NO_DEVICE)
        }
    }
}

/// Exact byte size of the "R0600" hipDeviceProp_t ABI, confirmed
/// 2026-09-13 via a real compiler against the public ROCm/HIP +
/// ROCm/clr headers (see native-daemon's hip_sys.rs for how). AMD
/// deliberately freezes this layout forever under the R0600 name, which
/// is what makes a pure byte-for-byte forward - without this shim ever
/// needing to understand the struct's contents - safe.
const HIP_DEVICE_PROP_R0600_SIZE: usize = 1472;

#[no_mangle]
pub extern "C" fn hipGetDevicePropertiesR0600(props: *mut c_void, device: i32) -> i32 {
    log(&format!("hipGetDevicePropertiesR0600(device={device})"));
    match call_bridge(Request::GetDeviceProperties { device }) {
        Ok(body) if body.len() == HIP_DEVICE_PROP_R0600_SIZE => {
            if !props.is_null() {
                unsafe {
                    std::ptr::copy_nonoverlapping(body.as_ptr(), props as *mut u8, body.len());
                }
            }
            log("hipGetDevicePropertiesR0600(): ok (real)");
            HIP_SUCCESS
        }
        Ok(body) => {
            log(&format!(
                "hipGetDevicePropertiesR0600(): daemon returned {} bytes, expected {HIP_DEVICE_PROP_R0600_SIZE} - refusing to write a mismatched size into the caller's buffer",
                body.len()
            ));
            set_last_error(HIP_ERROR_NOT_SUPPORTED)
        }
        Err(e) => {
            log(&format!("hipGetDevicePropertiesR0600(): {e}"));
            set_last_error(HIP_ERROR_NO_DEVICE)
        }
    }
}

#[no_mangle]
pub extern "C" fn hipSetDevice(device: i32) -> i32 {
    log(&format!("hipSetDevice(device={device})"));
    match call_bridge(Request::SetDevice { device }) {
        Ok(_) => {
            log("hipSetDevice(): ok (real)");
            HIP_SUCCESS
        }
        Err(e) => {
            log(&format!("hipSetDevice(): {e}"));
            set_last_error(HIP_ERROR_NO_DEVICE)
        }
    }
}

#[no_mangle]
pub extern "C" fn hipDeviceSynchronize() -> i32 {
    log("hipDeviceSynchronize()");
    // hipMemcpy is synchronous end-to-end over the bridge already (no
    // queued async work exists yet to wait on), so this is a correct
    // no-op success for now rather than a stub failure.
    HIP_SUCCESS
}

#[no_mangle]
pub extern "C" fn hipDriverGetVersion(version: *mut i32) -> i32 {
    log("hipDriverGetVersion()");
    if !version.is_null() {
        unsafe { *version = 0 };
    }
    HIP_SUCCESS
}

#[no_mangle]
pub extern "C" fn hipRuntimeGetVersion(version: *mut i32) -> i32 {
    log("hipRuntimeGetVersion()");
    if !version.is_null() {
        unsafe { *version = 0 };
    }
    HIP_SUCCESS
}

#[no_mangle]
pub extern "C" fn hipGetLastError() -> i32 {
    let e = LAST_ERROR.lock().map(|g| *g).unwrap_or(HIP_ERROR_NOT_SUPPORTED);
    log(&format!("hipGetLastError() -> {e}"));
    e
}

#[no_mangle]
pub extern "C" fn hipGetErrorString(error: i32) -> *const c_char {
    log(&format!("hipGetErrorString(error={error})"));
    if error == HIP_ERROR_NO_DEVICE {
        NO_BRIDGE_STRING.as_ptr()
    } else {
        ERR_STRING.as_ptr()
    }
}

// ---------------------------------------------------------------------
// Memory - real, forwarded (§12 stage 1)
// ---------------------------------------------------------------------

#[no_mangle]
pub extern "C" fn hipMalloc(ptr: *mut *mut c_void, size: usize) -> i32 {
    log(&format!("hipMalloc(size={size})"));
    match call_bridge(Request::Malloc { size: size as u64 }) {
        Ok(body) if body.len() == 8 => {
            let handle = u64::from_le_bytes(body[..8].try_into().unwrap());
            log(&format!("hipMalloc(size={size}) -> handle {handle} (real)"));
            if !ptr.is_null() {
                unsafe { *ptr = handle_to_ptr(handle, size as u64) };
            }
            HIP_SUCCESS
        }
        Ok(_) => {
            log("hipMalloc(): malformed response body");
            if !ptr.is_null() {
                unsafe { *ptr = std::ptr::null_mut() };
            }
            set_last_error(HIP_ERROR_NO_DEVICE)
        }
        Err(e) => {
            log(&format!("hipMalloc(size={size}): {e}"));
            if !ptr.is_null() {
                unsafe { *ptr = std::ptr::null_mut() };
            }
            set_last_error(HIP_ERROR_NO_DEVICE)
        }
    }
}

#[no_mangle]
pub extern "C" fn hipFree(ptr: *mut c_void) -> i32 {
    log(&format!("hipFree(ptr={ptr:p})"));
    if ptr.is_null() {
        return HIP_SUCCESS; // freeing null is a defined no-op, matching real HIP/CUDA convention
    }
    let handle = match free_ptr_to_handle(ptr) {
        Some(h) => h,
        None => {
            log("hipFree(): pointer does not match any known allocation base");
            return set_last_error(HIP_ERROR_NOT_SUPPORTED);
        }
    };
    match call_bridge(Request::Free { handle }) {
        Ok(_) => {
            log("hipFree(): ok (real)");
            HIP_SUCCESS
        }
        Err(e) => {
            log(&format!("hipFree(): {e}"));
            set_last_error(HIP_ERROR_NOT_SUPPORTED)
        }
    }
}

fn memcpy_forward(dst: *mut c_void, src: *const c_void, size: usize, kind: i32) -> i32 {
    match kind {
        HIP_MEMCPY_HOST_TO_DEVICE => {
            let Some((handle, offset)) = ptr_to_handle_offset(dst, "hipMemcpy H2D") else {
                return set_last_error(HIP_ERROR_NOT_SUPPORTED);
            };
            // src is a plain pointer into our own process's memory - safe
            // to read directly.
            let data = unsafe { std::slice::from_raw_parts(src as *const u8, size) }.to_vec();
            match call_bridge(Request::MemcpyH2D { handle, offset, data }) {
                Ok(_) => HIP_SUCCESS,
                Err(e) => {
                    log(&format!("hipMemcpy H2D: {e}"));
                    set_last_error(HIP_ERROR_NOT_SUPPORTED)
                }
            }
        }
        HIP_MEMCPY_DEVICE_TO_HOST => {
            let Some((handle, offset)) = ptr_to_handle_offset(src as *mut c_void, "hipMemcpy D2H") else {
                return set_last_error(HIP_ERROR_NOT_SUPPORTED);
            };
            match call_bridge(Request::MemcpyD2H {
                handle,
                offset,
                len: size as u64,
            }) {
                Ok(data) if data.len() == size => {
                    unsafe { std::ptr::copy_nonoverlapping(data.as_ptr(), dst as *mut u8, size) };
                    HIP_SUCCESS
                }
                Ok(_) => {
                    log("hipMemcpy D2H: daemon returned the wrong number of bytes");
                    set_last_error(HIP_ERROR_NOT_SUPPORTED)
                }
                Err(e) => {
                    log(&format!("hipMemcpy D2H: {e}"));
                    set_last_error(HIP_ERROR_NOT_SUPPORTED)
                }
            }
        }
        other => {
            // Host-to-host, device-to-device, and "default" (inferred)
            // kinds aren't implemented yet - honest failure rather than
            // silently doing the wrong thing.
            log(&format!("hipMemcpy: unsupported kind {other}"));
            set_last_error(HIP_ERROR_NOT_SUPPORTED)
        }
    }
}

#[no_mangle]
pub extern "C" fn hipMemcpy(dst: *mut c_void, src: *const c_void, size: usize, kind: i32) -> i32 {
    log(&format!("hipMemcpy(dst={dst:p}, src={src:p}, size={size}, kind={kind})"));
    memcpy_forward(dst, src, size, kind)
}

#[no_mangle]
pub extern "C" fn hipMemcpyAsync(
    dst: *mut c_void,
    src: *const c_void,
    size: usize,
    kind: i32,
    stream: *mut c_void,
) -> i32 {
    log(&format!(
        "hipMemcpyAsync(dst={dst:p}, src={src:p}, size={size}, kind={kind}, stream={stream:p}) - executed synchronously, stream ignored"
    ));
    memcpy_forward(dst, src, size, kind)
}

#[no_mangle]
pub extern "C" fn hipMemcpyToSymbol(
    symbol: *const c_void,
    src: *const c_void,
    size: usize,
    offset: usize,
    kind: i32,
) -> i32 {
    log(&format!(
        "hipMemcpyToSymbol(symbol={symbol:p}, src={src:p}, size={size}, offset={offset}, kind={kind})"
    ));
    // Still stubbed: symbols come from the fat binary (stage 2+ of §12's
    // plan, not yet implemented), so there's no real device address to
    // resolve `symbol` to yet.
    set_last_error(HIP_ERROR_NOT_SUPPORTED)
}

#[no_mangle]
pub extern "C" fn hipMemset(ptr: *mut c_void, value: i32, size: usize) -> i32 {
    log(&format!("hipMemset(ptr={ptr:p}, value={value}, size={size})"));
    set_last_error(HIP_ERROR_NOT_SUPPORTED)
}

#[no_mangle]
pub extern "C" fn hipMemsetAsync(
    ptr: *mut c_void,
    value: i32,
    size: usize,
    stream: *mut c_void,
) -> i32 {
    log(&format!(
        "hipMemsetAsync(ptr={ptr:p}, value={value}, size={size}, stream={stream:p})"
    ));
    set_last_error(HIP_ERROR_NOT_SUPPORTED)
}

// ---------------------------------------------------------------------
// Kernel execution (still stubbed - stage 2+ of §12's plan)
// ---------------------------------------------------------------------

/// Every kernel observed in danielblnc's binary takes exactly one
/// parameter, a struct passed by value (confirmed from mangled names,
/// e.g. `_Z16k_swin_1h_32_fp810SwinParams` demangles to
/// `k_swin_1h_32_fp8(SwinParams)`) - so `args[0]` is always a pointer to
/// that one struct's bytes in this process's own memory. Its exact size
/// isn't known generically (would require the real struct definition,
/// which isn't public), so this copies a generously-sized bounded chunk
/// rather than the unknowable exact size - reading some bytes past the
/// real struct from a valid host allocation is accepted as a pragmatic
/// risk for this research phase (over-reading is safe; over-writing
/// would not be, and this code never writes here).
const MAX_KERNEL_ARG_BYTES: usize = 4096;

#[no_mangle]
pub extern "C" fn hipLaunchKernel(
    function: *const c_void,
    grid_dim: *const Dim3,
    block_dim: *const Dim3,
    args: *mut *mut c_void,
    shared_mem: usize,
    stream: *mut c_void,
) -> i32 {
    log(&format!(
        "hipLaunchKernel(function={function:p}, grid_dim={grid_dim:p}, block_dim={block_dim:p}, args={args:p}, shared_mem={shared_mem}, stream={stream:p})"
    ));

    let function_handle = match function_registry().lock().unwrap().get(&(function as u64)).copied() {
        Some(h) => h,
        None => {
            log("hipLaunchKernel: function was never registered via __hipRegisterFunction");
            return set_last_error(HIP_ERROR_NOT_SUPPORTED);
        }
    };

    if grid_dim.is_null() || block_dim.is_null() || args.is_null() {
        log("hipLaunchKernel: null grid/block/args pointer");
        return set_last_error(HIP_ERROR_NOT_SUPPORTED);
    }

    let (grid, block) = unsafe {
        let g = &*grid_dim;
        let b = &*block_dim;
        ((g.x, g.y, g.z), (b.x, b.y, b.z))
    };

    let arg_ptr = unsafe { *args };
    if arg_ptr.is_null() {
        log("hipLaunchKernel: args[0] is null");
        return set_last_error(HIP_ERROR_NOT_SUPPORTED);
    }
    let arg_bytes =
        unsafe { std::slice::from_raw_parts(arg_ptr as *const u8, MAX_KERNEL_ARG_BYTES) }.to_vec();

    // Scan for our own fake device-pointer values embedded in the
    // argument struct (input/output/weight buffer pointers are near-
    // certain in any real compute kernel's params) and record where the
    // daemon needs to patch in the real device address before launching
    // - see docs/linux-support-spec.md Â§12's pointer-fixup notes for
    // why this is necessary (the daemon's HSA runtime hard-crashed with
    // a real GPU memory fault the first time this wasn't done).
    let fixups = {
        let space = address_space().lock().unwrap();
        let mut found = Vec::new();
        let mut offset = 0usize;
        while offset + 8 <= arg_bytes.len() {
            let candidate = u64::from_le_bytes(arg_bytes[offset..offset + 8].try_into().unwrap());
            if let Some((handle, intra_offset)) = space.resolve(candidate) {
                found.push((offset as u32, handle, intra_offset));
            }
            offset += 8; // pointer fields are always 8-byte aligned in practice
        }
        found
    };
    if !fixups.is_empty() {
        log(&format!("hipLaunchKernel: found {} embedded device-pointer field(s) to fix up", fixups.len()));
    }

    match call_bridge(Request::LaunchKernel {
        function: function_handle,
        grid,
        block,
        shared_mem: shared_mem as u32,
        fixups,
        args: arg_bytes,
    }) {
        Ok(_) => {
            log("hipLaunchKernel: ok (real)");
            HIP_SUCCESS
        }
        Err(e) => {
            log(&format!("hipLaunchKernel: {e}"));
            set_last_error(HIP_ERROR_NOT_SUPPORTED)
        }
    }
}

// ---------------------------------------------------------------------
// Events (still stubbed)
// ---------------------------------------------------------------------

#[no_mangle]
pub extern "C" fn hipEventCreate(event: *mut *mut c_void) -> i32 {
    log("hipEventCreate()");
    if !event.is_null() {
        unsafe { *event = std::ptr::null_mut() };
    }
    set_last_error(HIP_ERROR_NOT_SUPPORTED)
}

#[no_mangle]
pub extern "C" fn hipEventRecord(event: *mut c_void, stream: *mut c_void) -> i32 {
    log(&format!("hipEventRecord(event={event:p}, stream={stream:p})"));
    set_last_error(HIP_ERROR_NOT_SUPPORTED)
}

#[no_mangle]
pub extern "C" fn hipEventSynchronize(event: *mut c_void) -> i32 {
    log(&format!("hipEventSynchronize(event={event:p})"));
    set_last_error(HIP_ERROR_NOT_SUPPORTED)
}

#[no_mangle]
pub extern "C" fn hipEventElapsedTime(
    ms: *mut f32,
    start: *mut c_void,
    stop: *mut c_void,
) -> i32 {
    log(&format!("hipEventElapsedTime(start={start:p}, stop={stop:p})"));
    if !ms.is_null() {
        unsafe { *ms = 0.0 };
    }
    set_last_error(HIP_ERROR_NOT_SUPPORTED)
}

// ---------------------------------------------------------------------
// D3D12 external-memory interop (still stubbed - the transport spike in
// windows-runtime-bridge/hip-bridge/pe-client proves this path works via a *separate*
// Vulkan-side import, not through these HIP-side entry points; wiring
// that into the real shim is a later stage)
// ---------------------------------------------------------------------

#[no_mangle]
pub extern "C" fn hipImportExternalMemory(
    ext_mem: *mut *mut c_void,
    handle_desc: *const c_void,
) -> i32 {
    log(&format!(
        "hipImportExternalMemory(handle_desc={handle_desc:p})"
    ));
    if !ext_mem.is_null() {
        unsafe { *ext_mem = std::ptr::null_mut() };
    }
    set_last_error(HIP_ERROR_NOT_SUPPORTED)
}

#[no_mangle]
pub extern "C" fn hipExternalMemoryGetMappedBuffer(
    dev_ptr: *mut *mut c_void,
    ext_mem: *mut c_void,
    buffer_desc: *const c_void,
) -> i32 {
    log(&format!(
        "hipExternalMemoryGetMappedBuffer(ext_mem={ext_mem:p}, buffer_desc={buffer_desc:p})"
    ));
    if !dev_ptr.is_null() {
        unsafe { *dev_ptr = std::ptr::null_mut() };
    }
    set_last_error(HIP_ERROR_NOT_SUPPORTED)
}

#[no_mangle]
pub extern "C" fn hipDestroyExternalMemory(ext_mem: *mut c_void) -> i32 {
    log(&format!("hipDestroyExternalMemory(ext_mem={ext_mem:p})"));
    set_last_error(HIP_ERROR_NOT_SUPPORTED)
}

// ---------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------

unsafe fn safe_cstr<'a>(ptr: *const c_char) -> std::borrow::Cow<'a, str> {
    if ptr.is_null() {
        "<null>".into()
    } else {
        CStr::from_ptr(ptr).to_string_lossy()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn allocate_then_resolve_at_base_returns_zero_offset() {
        let mut space = AddressSpace::new();
        let base = space.allocate(7, 1024);
        assert_eq!(space.resolve(base), Some((7, 0)));
    }

    #[test]
    fn resolve_mid_allocation_returns_correct_offset() {
        let mut space = AddressSpace::new();
        let base = space.allocate(7, 1024);
        assert_eq!(space.resolve(base + 100), Some((7, 100)));
    }

    #[test]
    fn resolve_one_past_the_end_fails() {
        let mut space = AddressSpace::new();
        let base = space.allocate(7, 1024);
        assert_eq!(space.resolve(base + 1024), None);
    }

    #[test]
    fn resolve_before_any_allocation_fails() {
        let space = AddressSpace::new();
        assert_eq!(space.resolve(0), None);
    }

    #[test]
    fn two_allocations_never_overlap_even_with_offset_arithmetic() {
        let mut space = AddressSpace::new();
        let a = space.allocate(1, 100);
        let b = space.allocate(2, 100);
        assert_ne!(a, b);
        // An offset comfortably inside `a`'s declared size still resolves
        // to `a`, never spilling into `b`'s range.
        assert_eq!(space.resolve(a + 99), Some((1, 99)));
        assert_eq!(space.resolve(b), Some((2, 0)));
    }

    #[test]
    fn zero_sized_allocation_still_resolves_at_its_base() {
        let mut space = AddressSpace::new();
        let base = space.allocate(3, 0);
        assert_eq!(space.resolve(base), Some((3, 0)));
    }

    fn build_offload_bundle(entries: &[(u64, u64, &str)]) -> Vec<u8> {
        let mut buf = OFFLOAD_BUNDLE_MAGIC.to_vec();
        buf.extend_from_slice(&(entries.len() as u64).to_le_bytes());
        for &(offset, size, triple) in entries {
            buf.extend_from_slice(&offset.to_le_bytes());
            buf.extend_from_slice(&size.to_le_bytes());
            buf.extend_from_slice(&(triple.len() as u64).to_le_bytes());
            buf.extend_from_slice(triple.as_bytes());
        }
        buf
    }

    #[test]
    fn offload_bundle_size_matches_the_largest_entry_end() {
        let buf = build_offload_bundle(&[
            (100, 50, "host-x86_64-unknown-linux-gnu-"),
            (5_681_152, 424_840, "hipv4-amdgcn-amd-amdhsa--gfx1201"),
        ]);
        let size = unsafe { offload_bundle_total_size(buf.as_ptr() as *const c_void) };
        assert_eq!(size, Some(5_681_152 + 424_840));
    }

    #[test]
    fn offload_bundle_with_no_entries_still_covers_the_header() {
        let buf = build_offload_bundle(&[]);
        let size = unsafe { offload_bundle_total_size(buf.as_ptr() as *const c_void) };
        assert_eq!(size, Some(buf.len() as u64));
    }

    #[test]
    fn offload_bundle_rejects_bad_magic() {
        let buf = b"not a bundle at all, just plain bytes probably".to_vec();
        let size = unsafe { offload_bundle_total_size(buf.as_ptr() as *const c_void) };
        assert_eq!(size, None);
    }

    #[test]
    fn offload_bundle_rejects_implausible_bundle_count() {
        let mut buf = OFFLOAD_BUNDLE_MAGIC.to_vec();
        buf.extend_from_slice(&u64::MAX.to_le_bytes()); // absurd count
        let size = unsafe { offload_bundle_total_size(buf.as_ptr() as *const c_void) };
        assert_eq!(size, None);
    }

    #[test]
    fn remove_exact_only_matches_the_original_base() {
        let mut space = AddressSpace::new();
        let base = space.allocate(9, 64);
        assert_eq!(space.remove_exact(base + 1), None); // offset pointer must not free
        assert_eq!(space.remove_exact(base), Some(9));
        assert_eq!(space.resolve(base), None); // gone after removal
    }
}
