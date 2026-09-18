//! hip-bridge-daemon - native Linux side of danielblnc's runtime's real
//! HIP calls, forwarded here from hip-stub over a plain TCP loopback
//! socket (chosen over a Unix domain socket for guaranteed Winsock
//! compatibility from the Wine/Proton side - AF_UNIX support under Wine
//! is comparatively recent/less certain).
//!
//! See docs/linux-support-spec.md §12 for the staged plan this is part
//! of. This stage (device/memory forwarding) makes hipMalloc/hipFree/
//! hipMemcpy real; kernel-launch forwarding is a later stage.

mod handles;
mod hip_sys;

use std::ffi::c_void;
use std::io::Write;
use std::net::{TcpListener, TcpStream};

use hip_bridge_protocol::{Request, Response};

#[derive(Clone, Copy)]
struct DeviceHandle(*mut c_void);
// Safety: the underlying HIP device pointer is only ever dereferenced by
// libamdhip64.so itself (via the hip_sys calls), never read/written
// directly by this process - passing the handle value between threads
// serving different client connections is sound.
unsafe impl Send for DeviceHandle {}
unsafe impl Sync for DeviceHandle {}

#[derive(Clone, Copy)]
struct ModuleHandle(*mut c_void);
unsafe impl Send for ModuleHandle {}
unsafe impl Sync for ModuleHandle {}

#[derive(Clone, Copy)]
struct FunctionHandle(*mut c_void);
unsafe impl Send for FunctionHandle {}
unsafe impl Sync for FunctionHandle {}

fn handle_request(
    req: Request,
    hip: &hip_sys::Hip,
    handles: &handles::HandleTable<DeviceHandle>,
    modules: &handles::HandleTable<ModuleHandle>,
    functions: &handles::HandleTable<FunctionHandle>,
) -> Response {
    match req {
        Request::DeviceCount => match hip.device_count() {
            Ok(count) => Response::Ok(count.to_le_bytes().to_vec()),
            Err(e) => Response::Err(e),
        },
        Request::SetDevice { device } => match hip.set_device(device) {
            Ok(()) => Response::ok_empty(),
            Err(e) => Response::Err(e),
        },
        Request::Malloc { size } => match hip.malloc_raw(size) {
            Ok(ptr) => {
                let id = handles.insert(DeviceHandle(ptr));
                eprintln!("[hip-bridge-daemon] malloc({size}) -> handle {id}");
                Response::Ok(id.to_le_bytes().to_vec())
            }
            Err(e) => Response::Err(e),
        },
        Request::Free { handle } => match handles.remove(handle) {
            Some(DeviceHandle(ptr)) => match hip.free_raw(ptr) {
                Ok(()) => {
                    eprintln!("[hip-bridge-daemon] free(handle {handle})");
                    Response::ok_empty()
                }
                Err(e) => Response::Err(e),
            },
            None => Response::Err(format!("unknown handle {handle}")),
        },
        Request::MemcpyH2D { handle, offset, data } => match handles.get(handle) {
            Some(DeviceHandle(ptr)) => match hip.memcpy_h2d(ptr, offset, &data) {
                Ok(()) => Response::ok_empty(),
                Err(e) => Response::Err(e),
            },
            None => Response::Err(format!("unknown handle {handle}")),
        },
        Request::MemcpyD2H { handle, offset, len } => match handles.get(handle) {
            Some(DeviceHandle(ptr)) => match hip.memcpy_d2h(ptr, offset, len) {
                Ok(data) => Response::Ok(data),
                Err(e) => Response::Err(e),
            },
            None => Response::Err(format!("unknown handle {handle}")),
        },
        Request::GetDeviceProperties { device } => match hip.get_device_properties_r0600(device) {
            Ok(bytes) => {
                eprintln!("[hip-bridge-daemon] hipGetDevicePropertiesR0600(device={device}) -> {} bytes", bytes.len());
                Response::Ok(bytes)
            }
            Err(e) => Response::Err(e),
        },
        Request::LoadModule { data } => match hip.load_module(&data) {
            Ok(ptr) => {
                let id = modules.insert(ModuleHandle(ptr));
                eprintln!("[hip-bridge-daemon] loaded module ({} bytes) -> handle {id}", data.len());
                Response::Ok(id.to_le_bytes().to_vec())
            }
            Err(e) => Response::Err(e),
        },
        Request::GetFunction { module, name } => match modules.get(module) {
            Some(ModuleHandle(ptr)) => match hip.get_function(ptr, &name) {
                Ok(func_ptr) => {
                    let id = functions.insert(FunctionHandle(func_ptr));
                    eprintln!("[hip-bridge-daemon] resolved kernel \"{name}\" -> function handle {id}");
                    Response::Ok(id.to_le_bytes().to_vec())
                }
                Err(e) => Response::Err(e),
            },
            None => Response::Err(format!("unknown module handle {module}")),
        },
        Request::LaunchKernel { function, grid, block, shared_mem, fixups, mut args } => {
            match functions.get(function) {
                Some(FunctionHandle(func_ptr)) => {
                    // Patch in real device addresses wherever the caller's
                    // argument struct embeds one of its own fake pointer
                    // values - without this, the kernel would dereference
                    // a value that only means something on the PE side
                    // and fault the real GPU (see
                    // docs/linux-support-spec.md Â§12's pointer-fixup
                    // notes: this is exactly what caused the daemon's own
                    // HSA runtime assertion the first time this was tried
                    // without it).
                    let mut fixup_error = None;
                    for (offset, handle, intra_offset) in fixups {
                        let offset = offset as usize;
                        if offset + 8 > args.len() {
                            fixup_error = Some(format!(
                                "fixup offset {offset} out of bounds for a {}-byte argument buffer",
                                args.len()
                            ));
                            break;
                        }
                        match handles.get(handle) {
                            Some(DeviceHandle(dev_ptr)) => {
                                let real_addr = (dev_ptr as u64).wrapping_add(intra_offset);
                                args[offset..offset + 8].copy_from_slice(&real_addr.to_le_bytes());
                            }
                            None => {
                                fixup_error = Some(format!("fixup referenced unknown handle {handle}"));
                                break;
                            }
                        }
                    }
                    if let Some(e) = fixup_error {
                        return Response::Err(e);
                    }
                    match hip.launch_kernel(func_ptr, grid, block, shared_mem, &args) {
                        Ok(()) => Response::ok_empty(),
                        Err(e) => Response::Err(e),
                    }
                }
                None => Response::Err(format!("unknown function handle {function}")),
            }
        }
    }
}

fn handle_client(
    mut stream: TcpStream,
    hip: &hip_sys::Hip,
    handles: &handles::HandleTable<DeviceHandle>,
    modules: &handles::HandleTable<ModuleHandle>,
    functions: &handles::HandleTable<FunctionHandle>,
) -> std::io::Result<()> {
    stream.set_nodelay(true).ok();
    loop {
        let req = match Request::read_from(&mut stream) {
            Ok(r) => r,
            Err(e) if e.kind() == std::io::ErrorKind::UnexpectedEof => return Ok(()),
            Err(e) => return Err(e),
        };
        let resp = handle_request(req, hip, handles, modules, functions);
        resp.write_to(&mut stream)?;
        stream.flush()?;
    }
}

fn main() {
    let port: u16 = std::env::var("HIP_BRIDGE_PORT")
        .ok()
        .and_then(|s| s.parse().ok())
        .unwrap_or(47411);

    let hip = match hip_sys::Hip::load() {
        Ok(h) => h,
        Err(e) => {
            eprintln!("[hip-bridge-daemon] failed to load HIP runtime: {e}");
            std::process::exit(1);
        }
    };

    match hip.device_count() {
        Ok(n) => eprintln!("[hip-bridge-daemon] hipGetDeviceCount() -> {n} device(s)"),
        Err(e) => {
            eprintln!("[hip-bridge-daemon] hipGetDeviceCount failed: {e}");
            std::process::exit(1);
        }
    }

    let handles: handles::HandleTable<DeviceHandle> = handles::HandleTable::new();
    let modules: handles::HandleTable<ModuleHandle> = handles::HandleTable::new();
    let functions: handles::HandleTable<FunctionHandle> = handles::HandleTable::new();

    let addr = format!("127.0.0.1:{port}");
    let listener = TcpListener::bind(&addr).unwrap_or_else(|e| {
        eprintln!("[hip-bridge-daemon] failed to bind {addr}: {e}");
        std::process::exit(1);
    });
    eprintln!("[hip-bridge-daemon] listening on {addr}");

    for stream in listener.incoming() {
        match stream {
            Ok(s) => {
                if let Err(e) = handle_client(s, &hip, &handles, &modules, &functions) {
                    eprintln!("[hip-bridge-daemon] client error: {e}");
                }
            }
            Err(e) => eprintln!("[hip-bridge-daemon] accept error: {e}"),
        }
    }
}
