//! Wire protocol for the hip-stub <-> hip-bridge-daemon TCP connection.
//!
//! Deliberately has no OS-specific dependencies so it compiles and its
//! tests run natively on Linux, even though `hip-stub` (one of its two
//! consumers) only ever gets cross-compiled to `x86_64-pc-windows-gnu` -
//! that target can't run its own test binaries in this environment, but
//! this crate's logic is exactly the part worth testing in isolation
//! anyway: framing/encoding bugs here would be silent and hard to
//! diagnose from either side alone.
//!
//! Wire format:
//!   request:  [u8 opcode][u32 LE body_len][body_len bytes]
//!   response: [u8 status (0=ok, 1=err)][u32 LE body_len][body_len bytes]
//! An err response's body is a UTF-8 error message.

use std::io::{self, Read, Write};

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Request {
    DeviceCount,
    SetDevice { device: i32 },
    Malloc { size: u64 },
    Free { handle: u64 },
    MemcpyH2D { handle: u64, offset: u64, data: Vec<u8> },
    MemcpyD2H { handle: u64, offset: u64, len: u64 },
    GetDeviceProperties { device: i32 },
    LoadModule { data: Vec<u8> },
    GetFunction { module: u64, name: String },
    LaunchKernel {
        function: u64,
        grid: (u32, u32, u32),
        block: (u32, u32, u32),
        shared_mem: u32,
        /// Byte offsets within `args` that contain one of the caller's
        /// own fake device-pointer values, needing rewriting to the
        /// real device address before the kernel runs. Each is
        /// (byte_offset_in_args, handle, offset_within_that_allocation).
        /// See docs/linux-support-spec.md Â§12's pointer-fixup notes:
        /// without this, argument structs embedding device pointers
        /// (virtually all real compute kernels) cause a real GPU memory
        /// fault when the kernel dereferences a value that only means
        /// something on the PE side.
        fixups: Vec<(u32, u64, u64)>,
        args: Vec<u8>,
    },
}

impl Request {
    fn opcode(&self) -> u8 {
        match self {
            Request::DeviceCount => 1,
            Request::SetDevice { .. } => 2,
            Request::Malloc { .. } => 3,
            Request::Free { .. } => 4,
            Request::MemcpyH2D { .. } => 5,
            Request::MemcpyD2H { .. } => 6,
            Request::GetDeviceProperties { .. } => 7,
            Request::LoadModule { .. } => 8,
            Request::GetFunction { .. } => 9,
            Request::LaunchKernel { .. } => 10,
        }
    }

    fn encode_body(&self) -> Vec<u8> {
        match self {
            Request::DeviceCount => Vec::new(),
            Request::SetDevice { device } => device.to_le_bytes().to_vec(),
            Request::Malloc { size } => size.to_le_bytes().to_vec(),
            Request::Free { handle } => handle.to_le_bytes().to_vec(),
            Request::MemcpyH2D { handle, offset, data } => {
                let mut buf = Vec::with_capacity(24 + data.len());
                buf.extend_from_slice(&handle.to_le_bytes());
                buf.extend_from_slice(&offset.to_le_bytes());
                buf.extend_from_slice(&(data.len() as u64).to_le_bytes());
                buf.extend_from_slice(data);
                buf
            }
            Request::MemcpyD2H { handle, offset, len } => {
                let mut buf = Vec::with_capacity(24);
                buf.extend_from_slice(&handle.to_le_bytes());
                buf.extend_from_slice(&offset.to_le_bytes());
                buf.extend_from_slice(&len.to_le_bytes());
                buf
            }
            Request::GetDeviceProperties { device } => device.to_le_bytes().to_vec(),
            Request::LoadModule { data } => data.clone(),
            Request::GetFunction { module, name } => {
                let name_bytes = name.as_bytes();
                let mut buf = Vec::with_capacity(8 + name_bytes.len());
                buf.extend_from_slice(&module.to_le_bytes());
                buf.extend_from_slice(name_bytes);
                buf
            }
            Request::LaunchKernel { function, grid, block, shared_mem, fixups, args } => {
                let mut buf = Vec::with_capacity(8 + 24 + 4 + 4 + fixups.len() * 20 + args.len());
                buf.extend_from_slice(&function.to_le_bytes());
                buf.extend_from_slice(&grid.0.to_le_bytes());
                buf.extend_from_slice(&grid.1.to_le_bytes());
                buf.extend_from_slice(&grid.2.to_le_bytes());
                buf.extend_from_slice(&block.0.to_le_bytes());
                buf.extend_from_slice(&block.1.to_le_bytes());
                buf.extend_from_slice(&block.2.to_le_bytes());
                buf.extend_from_slice(&shared_mem.to_le_bytes());
                buf.extend_from_slice(&(fixups.len() as u32).to_le_bytes());
                for &(off, handle, intra) in fixups {
                    buf.extend_from_slice(&off.to_le_bytes());
                    buf.extend_from_slice(&handle.to_le_bytes());
                    buf.extend_from_slice(&intra.to_le_bytes());
                }
                buf.extend_from_slice(args);
                buf
            }
        }
    }

    pub fn write_to<W: Write>(&self, w: &mut W) -> io::Result<()> {
        let body = self.encode_body();
        w.write_all(&[self.opcode()])?;
        w.write_all(&(body.len() as u32).to_le_bytes())?;
        w.write_all(&body)?;
        Ok(())
    }

    pub fn read_from<R: Read>(r: &mut R) -> io::Result<Self> {
        let mut opcode_buf = [0u8; 1];
        r.read_exact(&mut opcode_buf)?;
        let body = read_framed_body(r)?;

        let bad = |msg: &str| io::Error::new(io::ErrorKind::InvalidData, msg.to_string());
        match opcode_buf[0] {
            1 => Ok(Request::DeviceCount),
            2 => {
                if body.len() != 4 {
                    return Err(bad("SetDevice body must be 4 bytes"));
                }
                Ok(Request::SetDevice { device: i32::from_le_bytes(body[..4].try_into().unwrap()) })
            }
            3 => {
                if body.len() != 8 {
                    return Err(bad("Malloc body must be 8 bytes"));
                }
                Ok(Request::Malloc { size: u64::from_le_bytes(body[..8].try_into().unwrap()) })
            }
            4 => {
                if body.len() != 8 {
                    return Err(bad("Free body must be 8 bytes"));
                }
                Ok(Request::Free { handle: u64::from_le_bytes(body[..8].try_into().unwrap()) })
            }
            5 => {
                if body.len() < 24 {
                    return Err(bad("MemcpyH2D body too short"));
                }
                let handle = u64::from_le_bytes(body[0..8].try_into().unwrap());
                let offset = u64::from_le_bytes(body[8..16].try_into().unwrap());
                let data_len = u64::from_le_bytes(body[16..24].try_into().unwrap()) as usize;
                if body.len() != 24 + data_len {
                    return Err(bad("MemcpyH2D declared length does not match body size"));
                }
                Ok(Request::MemcpyH2D { handle, offset, data: body[24..].to_vec() })
            }
            6 => {
                if body.len() != 24 {
                    return Err(bad("MemcpyD2H body must be 24 bytes"));
                }
                let handle = u64::from_le_bytes(body[0..8].try_into().unwrap());
                let offset = u64::from_le_bytes(body[8..16].try_into().unwrap());
                let len = u64::from_le_bytes(body[16..24].try_into().unwrap());
                Ok(Request::MemcpyD2H { handle, offset, len })
            }
            7 => {
                if body.len() != 4 {
                    return Err(bad("GetDeviceProperties body must be 4 bytes"));
                }
                Ok(Request::GetDeviceProperties {
                    device: i32::from_le_bytes(body[..4].try_into().unwrap()),
                })
            }
            8 => Ok(Request::LoadModule { data: body }),
            9 => {
                if body.len() < 8 {
                    return Err(bad("GetFunction body too short"));
                }
                let module = u64::from_le_bytes(body[0..8].try_into().unwrap());
                let name = String::from_utf8(body[8..].to_vec())
                    .map_err(|_| bad("GetFunction name is not valid UTF-8"))?;
                Ok(Request::GetFunction { module, name })
            }
            10 => {
                if body.len() < 40 {
                    return Err(bad("LaunchKernel body too short"));
                }
                let function = u64::from_le_bytes(body[0..8].try_into().unwrap());
                let gx = u32::from_le_bytes(body[8..12].try_into().unwrap());
                let gy = u32::from_le_bytes(body[12..16].try_into().unwrap());
                let gz = u32::from_le_bytes(body[16..20].try_into().unwrap());
                let bx = u32::from_le_bytes(body[20..24].try_into().unwrap());
                let by = u32::from_le_bytes(body[24..28].try_into().unwrap());
                let bz = u32::from_le_bytes(body[28..32].try_into().unwrap());
                let shared_mem = u32::from_le_bytes(body[32..36].try_into().unwrap());
                let num_fixups = u32::from_le_bytes(body[36..40].try_into().unwrap()) as usize;
                let fixups_end = 40usize
                    .checked_add(num_fixups.checked_mul(20).ok_or_else(|| bad("fixup count overflow"))?)
                    .ok_or_else(|| bad("fixup count overflow"))?;
                if body.len() < fixups_end {
                    return Err(bad("LaunchKernel body too short for declared fixup count"));
                }
                let mut fixups = Vec::with_capacity(num_fixups);
                let mut cursor = 40usize;
                for _ in 0..num_fixups {
                    let off = u32::from_le_bytes(body[cursor..cursor + 4].try_into().unwrap());
                    let handle = u64::from_le_bytes(body[cursor + 4..cursor + 12].try_into().unwrap());
                    let intra = u64::from_le_bytes(body[cursor + 12..cursor + 20].try_into().unwrap());
                    fixups.push((off, handle, intra));
                    cursor += 20;
                }
                let args = body[fixups_end..].to_vec();
                Ok(Request::LaunchKernel {
                    function,
                    grid: (gx, gy, gz),
                    block: (bx, by, bz),
                    shared_mem,
                    fixups,
                    args,
                })
            }
            other => Err(bad(&format!("unknown opcode {other}"))),
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Response {
    Ok(Vec<u8>),
    Err(String),
}

impl Response {
    pub fn ok_empty() -> Self {
        Response::Ok(Vec::new())
    }

    pub fn write_to<W: Write>(&self, w: &mut W) -> io::Result<()> {
        match self {
            Response::Ok(body) => {
                w.write_all(&[0u8])?;
                w.write_all(&(body.len() as u32).to_le_bytes())?;
                w.write_all(body)?;
            }
            Response::Err(msg) => {
                let bytes = msg.as_bytes();
                w.write_all(&[1u8])?;
                w.write_all(&(bytes.len() as u32).to_le_bytes())?;
                w.write_all(bytes)?;
            }
        }
        Ok(())
    }

    pub fn read_from<R: Read>(r: &mut R) -> io::Result<Self> {
        let mut status = [0u8; 1];
        r.read_exact(&mut status)?;
        let body = read_framed_body(r)?;
        match status[0] {
            0 => Ok(Response::Ok(body)),
            1 => Ok(Response::Err(String::from_utf8_lossy(&body).into_owned())),
            other => Err(io::Error::new(
                io::ErrorKind::InvalidData,
                format!("unknown response status byte {other}"),
            )),
        }
    }

    /// Convenience for the client side: turns Err into a real Result,
    /// and Ok's body into a plain byte vector.
    pub fn into_result(self) -> Result<Vec<u8>, String> {
        match self {
            Response::Ok(body) => Ok(body),
            Response::Err(msg) => Err(msg),
        }
    }
}

const MAX_BODY_LEN: u32 = 256 * 1024 * 1024;

fn read_framed_body<R: Read>(r: &mut R) -> io::Result<Vec<u8>> {
    let mut len_buf = [0u8; 4];
    r.read_exact(&mut len_buf)?;
    let len = u32::from_le_bytes(len_buf);
    if len > MAX_BODY_LEN {
        return Err(io::Error::new(
            io::ErrorKind::InvalidData,
            format!("frame body length {len} exceeds sanity cap {MAX_BODY_LEN}"),
        ));
    }
    let mut body = vec![0u8; len as usize];
    r.read_exact(&mut body)?;
    Ok(body)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Cursor;

    fn roundtrip_request(req: Request) {
        let mut buf = Vec::new();
        req.write_to(&mut buf).unwrap();
        let mut cursor = Cursor::new(buf);
        let decoded = Request::read_from(&mut cursor).unwrap();
        assert_eq!(req, decoded);
    }

    #[test]
    fn device_count_roundtrips() {
        roundtrip_request(Request::DeviceCount);
    }

    #[test]
    fn set_device_roundtrips() {
        roundtrip_request(Request::SetDevice { device: 0 });
        roundtrip_request(Request::SetDevice { device: -1 });
    }

    #[test]
    fn malloc_roundtrips() {
        roundtrip_request(Request::Malloc { size: 0 });
        roundtrip_request(Request::Malloc { size: 65536 });
        roundtrip_request(Request::Malloc { size: u64::MAX });
    }

    #[test]
    fn free_roundtrips() {
        roundtrip_request(Request::Free { handle: 42 });
    }

    #[test]
    fn memcpy_h2d_roundtrips_including_empty_data() {
        roundtrip_request(Request::MemcpyH2D { handle: 7, offset: 0, data: vec![] });
        roundtrip_request(Request::MemcpyH2D {
            handle: 7,
            offset: 128,
            data: (0..=255u8).collect(),
        });
    }

    #[test]
    fn memcpy_d2h_roundtrips() {
        roundtrip_request(Request::MemcpyD2H { handle: 7, offset: 0, len: 1024 });
    }

    #[test]
    fn get_device_properties_roundtrips() {
        roundtrip_request(Request::GetDeviceProperties { device: 0 });
    }

    #[test]
    fn load_module_roundtrips_including_empty_data() {
        roundtrip_request(Request::LoadModule { data: vec![] });
        roundtrip_request(Request::LoadModule { data: (0..=255u8).collect() });
    }

    #[test]
    fn get_function_roundtrips() {
        roundtrip_request(Request::GetFunction { module: 1, name: "k_swin_1h_32_fp8".to_string() });
        roundtrip_request(Request::GetFunction { module: 1, name: String::new() });
    }

    #[test]
    fn launch_kernel_roundtrips() {
        roundtrip_request(Request::LaunchKernel {
            function: 42,
            grid: (16, 1, 1),
            block: (256, 1, 1),
            shared_mem: 0,
            fixups: vec![],
            args: vec![1, 2, 3, 4],
        });
        roundtrip_request(Request::LaunchKernel {
            function: 42,
            grid: (1, 1, 1),
            block: (1, 1, 1),
            shared_mem: 0,
            fixups: vec![],
            args: vec![],
        });
    }

    #[test]
    fn launch_kernel_with_fixups_roundtrips() {
        roundtrip_request(Request::LaunchKernel {
            function: 42,
            grid: (1, 1, 1),
            block: (1, 1, 1),
            shared_mem: 0,
            fixups: vec![(0, 7, 0), (16, 8, 256)],
            args: vec![0u8; 32],
        });
    }

    #[test]
    fn launch_kernel_rejects_truncated_fixup_table() {
        // Declares 5 fixups but the body is far too short to hold them.
        let mut body = Vec::new();
        body.extend_from_slice(&1u64.to_le_bytes()); // function
        body.extend_from_slice(&[0u8; 24]); // grid+block
        body.extend_from_slice(&0u32.to_le_bytes()); // shared_mem
        body.extend_from_slice(&5u32.to_le_bytes()); // num_fixups = 5, but no data follows
        let mut buf = vec![10u8];
        buf.extend_from_slice(&(body.len() as u32).to_le_bytes());
        buf.extend_from_slice(&body);
        let err = Request::read_from(&mut Cursor::new(buf)).unwrap_err();
        assert_eq!(err.kind(), io::ErrorKind::InvalidData);
    }

    #[test]
    fn get_function_with_non_utf8_name_is_rejected() {
        let mut body = 1u64.to_le_bytes().to_vec();
        body.push(0xFF); // invalid UTF-8 byte
        let mut buf = vec![9u8];
        buf.extend_from_slice(&(body.len() as u32).to_le_bytes());
        buf.extend_from_slice(&body);
        let err = Request::read_from(&mut Cursor::new(buf)).unwrap_err();
        assert_eq!(err.kind(), io::ErrorKind::InvalidData);
    }

    #[test]
    fn response_ok_roundtrips() {
        let resp = Response::Ok(vec![1, 2, 3]);
        let mut buf = Vec::new();
        resp.write_to(&mut buf).unwrap();
        let decoded = Response::read_from(&mut Cursor::new(buf)).unwrap();
        assert_eq!(resp, decoded);
    }

    #[test]
    fn response_err_roundtrips_and_converts() {
        let resp = Response::Err("hipMalloc failed".to_string());
        let mut buf = Vec::new();
        resp.write_to(&mut buf).unwrap();
        let decoded = Response::read_from(&mut Cursor::new(buf)).unwrap();
        assert_eq!(decoded, resp);
        assert_eq!(decoded.into_result(), Err("hipMalloc failed".to_string()));
    }

    #[test]
    fn response_ok_into_result() {
        let resp = Response::Ok(vec![9, 9]);
        assert_eq!(resp.into_result(), Ok(vec![9, 9]));
    }

    #[test]
    fn malformed_set_device_body_is_rejected() {
        // opcode 2 (SetDevice) with a 1-byte body instead of the required 4.
        let mut buf = vec![2u8];
        buf.extend_from_slice(&1u32.to_le_bytes());
        buf.push(0xFF);
        let err = Request::read_from(&mut Cursor::new(buf)).unwrap_err();
        assert_eq!(err.kind(), io::ErrorKind::InvalidData);
    }

    #[test]
    fn unknown_opcode_is_rejected() {
        let mut buf = vec![250u8];
        buf.extend_from_slice(&0u32.to_le_bytes());
        let err = Request::read_from(&mut Cursor::new(buf)).unwrap_err();
        assert_eq!(err.kind(), io::ErrorKind::InvalidData);
    }

    #[test]
    fn memcpy_h2d_length_mismatch_is_rejected() {
        // Declares data_len=10 but only supplies 3 bytes of data.
        let mut body = Vec::new();
        body.extend_from_slice(&7u64.to_le_bytes());
        body.extend_from_slice(&0u64.to_le_bytes());
        body.extend_from_slice(&10u64.to_le_bytes());
        body.extend_from_slice(&[1, 2, 3]);
        let mut buf = vec![5u8];
        buf.extend_from_slice(&(body.len() as u32).to_le_bytes());
        buf.extend_from_slice(&body);
        let err = Request::read_from(&mut Cursor::new(buf)).unwrap_err();
        assert_eq!(err.kind(), io::ErrorKind::InvalidData);
    }

    #[test]
    fn oversized_frame_is_rejected_before_allocating() {
        let mut buf = vec![1u8];
        buf.extend_from_slice(&(MAX_BODY_LEN + 1).to_le_bytes());
        let err = Request::read_from(&mut Cursor::new(buf)).unwrap_err();
        assert_eq!(err.kind(), io::ErrorKind::InvalidData);
    }
}
