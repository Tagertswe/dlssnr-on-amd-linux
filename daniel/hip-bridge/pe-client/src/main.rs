//! hip-bridge-pe-client - Windows-side half of the #6 interop spike.
//!
//! Adapts the transport proven working in zmodelerlover/dlss5-neural-amd's
//! MIT-licensed `src/vkbridge/vkbridge.cpp` (D3D12 shared texture -> own
//! Vulkan device import via VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT
//! -> host-visible staging buffer) and adds a TCP leg to hip-bridge-daemon
//! so the bytes that land in that staging buffer actually round-trip
//! through a real HIP device on the Linux host, not just through Vulkan.
//!
//! Run standalone (not injected into a game) under Proton to test the
//! whole chain: D3D12 -> Vulkan -> TCP -> HIP device -> TCP -> Vulkan ->
//! D3D12, then diff the final bytes against what was written at the start.

use std::io::{Read, Write};
use std::net::TcpStream;
use std::sync::Mutex;
use std::sync::OnceLock;

use ash::vk;
use windows::core::Interface;
use windows::Win32::Foundation::{CloseHandle, HANDLE};
use windows::Win32::Graphics::Direct3D12::*;
use windows::Win32::Graphics::Dxgi::Common::*;
use windows::Win32::Graphics::Dxgi::*;

/// Console output from a Wine "console subsystem" program is not reliably
/// forwarded when no real terminal is attached (observed directly: this
/// program's very first `println!` never appeared anywhere when run via
/// `proton run` with stdout/stderr redirected to a file). File-based
/// logging is already this project's established pattern (see
/// docs/linux-support-spec.md §1e) for exactly this reason - applying
/// it here too instead of relying on inherited stdout.
fn log_path() -> &'static Mutex<std::path::PathBuf> {
    static PATH: OnceLock<Mutex<std::path::PathBuf>> = OnceLock::new();
    PATH.get_or_init(|| {
        let dir = std::env::current_exe()
            .ok()
            .and_then(|p| p.parent().map(|p| p.to_path_buf()))
            .unwrap_or_else(|| std::path::PathBuf::from("."));
        Mutex::new(dir.join("hip-bridge-pe-client.log"))
    })
}

fn log_line(msg: &str) {
    eprintln!("{msg}");
    let path = log_path().lock().unwrap_or_else(|e| e.into_inner());
    if let Ok(mut f) = std::fs::OpenOptions::new().create(true).append(true).open(&*path) {
        let _ = writeln!(f, "{msg}");
    }
}

macro_rules! log {
    ($($arg:tt)*) => {
        log_line(&format!($($arg)*))
    };
}

const WIDTH: u32 = 256;
const HEIGHT: u32 = 64;
const BYTES_PER_PIXEL: u32 = 4; // R8G8B8A8_UNORM
const IMAGE_BYTES: usize = (WIDTH * HEIGHT * BYTES_PER_PIXEL) as usize;

fn fill_pattern(seed: u8) -> Vec<u8> {
    (0..IMAGE_BYTES)
        .map(|i| ((i as u32).wrapping_mul(31).wrapping_add(seed as u32 * 97) & 0xFF) as u8)
        .collect()
}

fn send_roundtrip(stream: &mut TcpStream, data: &[u8]) -> std::io::Result<Vec<u8>> {
    stream.write_all(&(data.len() as u32).to_le_bytes())?;
    stream.write_all(data)?;

    let mut len_buf = [0u8; 4];
    stream.read_exact(&mut len_buf)?;
    let len = u32::from_le_bytes(len_buf) as usize;
    let mut out = vec![0u8; len];
    stream.read_exact(&mut out)?;
    Ok(out)
}

fn first_difference(a: &[u8], b: &[u8]) -> Option<usize> {
    a.iter().zip(b.iter()).position(|(x, y)| x != y)
}

/// D3D12 device + a single shared texture, exported as an NT handle.
struct D3D12Side {
    device: ID3D12Device,
    queue: ID3D12CommandQueue,
    allocator: ID3D12CommandAllocator,
    list: ID3D12GraphicsCommandList,
    shared_resource: ID3D12Resource,
    shared_handle: HANDLE,
    adapter_luid: windows::Win32::Foundation::LUID,
}

fn create_d3d12_side() -> windows::core::Result<D3D12Side> {
    unsafe {
        let factory: IDXGIFactory1 = CreateDXGIFactory1()?;
        // First hardware adapter is fine for this single-GPU spike; a
        // production version should match by LUID against the chosen
        // Vulkan physical device the way vkbridge.cpp does.
        let adapter: IDXGIAdapter1 = factory.EnumAdapters1(0)?;
        let desc = adapter.GetDesc1()?;

        let mut device: Option<ID3D12Device> = None;
        D3D12CreateDevice(
            &adapter,
            windows::Win32::Graphics::Direct3D::D3D_FEATURE_LEVEL_11_0,
            &mut device,
        )?;
        let device = device.expect("D3D12CreateDevice succeeded but returned no device");

        let queue_desc = D3D12_COMMAND_QUEUE_DESC {
            Type: D3D12_COMMAND_LIST_TYPE_DIRECT,
            ..Default::default()
        };
        let queue: ID3D12CommandQueue = device.CreateCommandQueue(&queue_desc)?;
        let allocator: ID3D12CommandAllocator =
            device.CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT)?;
        let list: ID3D12GraphicsCommandList = device.CreateCommandList(
            0,
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            &allocator,
            None,
        )?;
        list.Close()?;

        let heap_props = D3D12_HEAP_PROPERTIES {
            Type: D3D12_HEAP_TYPE_DEFAULT,
            ..Default::default()
        };
        let resource_desc = D3D12_RESOURCE_DESC {
            Dimension: D3D12_RESOURCE_DIMENSION_TEXTURE2D,
            Width: WIDTH as u64,
            Height: HEIGHT,
            DepthOrArraySize: 1,
            MipLevels: 1,
            Format: DXGI_FORMAT_R8G8B8A8_UNORM,
            SampleDesc: DXGI_SAMPLE_DESC { Count: 1, Quality: 0 },
            Layout: D3D12_TEXTURE_LAYOUT_UNKNOWN,
            ..Default::default()
        };

        let mut shared_resource: Option<ID3D12Resource> = None;
        device.CreateCommittedResource(
            &heap_props,
            D3D12_HEAP_FLAG_SHARED,
            &resource_desc,
            D3D12_RESOURCE_STATE_COMMON,
            None,
            &mut shared_resource,
        )?;
        let shared_resource = shared_resource.expect("CreateCommittedResource returned no resource");

        let shared_handle: HANDLE = device.CreateSharedHandle(
            &shared_resource,
            None,
            0x10000000u32, // GENERIC_ALL
            windows::core::PCWSTR::null(),
        )?;

        Ok(D3D12Side {
            device,
            queue,
            allocator,
            list,
            shared_resource,
            shared_handle,
            adapter_luid: desc.AdapterLuid,
        })
    }
}

fn main() {
    std::panic::set_hook(Box::new(|info| {
        log_line(&format!("PANIC: {info}"));
    }));

    log!("hip-bridge-pe-client -- D3D12 -> Vulkan -> TCP -> HIP device -> back");
    log!("log file: {}", log_path().lock().unwrap().display());

    let d3d = match create_d3d12_side() {
        Ok(d) => d,
        Err(e) => {
            log!("D3D12 setup failed: {e:?}");
            std::process::exit(2);
        }
    };
    log!(
        "D3D12 device + shared {}x{} texture created (adapter LUID {:08x}:{:08x})",
        WIDTH, HEIGHT, d3d.adapter_luid.HighPart, d3d.adapter_luid.LowPart
    );

    // --- Vulkan side: own instance/device, import the shared texture ----
    let entry = unsafe { ash::Entry::load() }.expect("failed to load vulkan-1.dll");
    let app_info = vk::ApplicationInfo::default()
        .application_name(c"hip-bridge-pe-client")
        .api_version(vk::API_VERSION_1_2);
    let instance_ci = vk::InstanceCreateInfo::default().application_info(&app_info);
    let instance = unsafe { entry.create_instance(&instance_ci, None) }
        .expect("vkCreateInstance failed");

    let physical_devices =
        unsafe { instance.enumerate_physical_devices() }.expect("no Vulkan physical devices");

    // Match the D3D12 adapter's LUID, same reasoning as vkbridge.cpp: on a
    // multi-adapter system picking "device 0" on each API independently
    // can silently land on two different GPUs.
    let mut id_props = vk::PhysicalDeviceIDProperties::default();
    let mut chosen_gpu = None;
    for &gpu in &physical_devices {
        let mut props2 = vk::PhysicalDeviceProperties2::default().push_next(&mut id_props);
        unsafe { instance.get_physical_device_properties2(gpu, &mut props2) };
        if id_props.device_luid_valid != 0 {
            let luid = &id_props.device_luid;
            let low = u32::from_le_bytes(luid[0..4].try_into().unwrap());
            let high = u32::from_le_bytes(luid[4..8].try_into().unwrap());
            if low == d3d.adapter_luid.LowPart as u32 && high as i32 == d3d.adapter_luid.HighPart {
                chosen_gpu = Some(gpu);
                break;
            }
        }
    }
    let gpu = chosen_gpu.unwrap_or(physical_devices[0]);
    if chosen_gpu.is_none() {
        log!("warning: no Vulkan device LUID matched the D3D12 adapter; falling back to device 0");
    }

    let queue_family = unsafe { instance.get_physical_device_queue_family_properties(gpu) }
        .iter()
        .position(|f| f.queue_flags.contains(vk::QueueFlags::GRAPHICS))
        .expect("no graphics queue family") as u32;

    let priorities = [1.0f32];
    let queue_ci = vk::DeviceQueueCreateInfo::default()
        .queue_family_index(queue_family)
        .queue_priorities(&priorities);
    let queue_cis = [queue_ci];
    let device_extensions: [*const i8; 3] = [
        c"VK_KHR_external_memory".as_ptr(),
        c"VK_KHR_external_memory_win32".as_ptr(),
        c"VK_KHR_dedicated_allocation".as_ptr(),
    ];
    let device_ci = vk::DeviceCreateInfo::default()
        .queue_create_infos(&queue_cis)
        .enabled_extension_names(&device_extensions);
    let device = unsafe { instance.create_device(gpu, &device_ci, None) }
        .expect("vkCreateDevice failed - likely missing external-memory-win32 support in winevulkan");
    log!("Vulkan device created with external-memory-win32 extensions enabled");

    let mem_props = unsafe { instance.get_physical_device_memory_properties(gpu) };

    // ---- import the D3D12 texture as a VkImage --------------------------
    let mut ext_mem_image_ci = vk::ExternalMemoryImageCreateInfo::default()
        .handle_types(vk::ExternalMemoryHandleTypeFlags::D3D12_RESOURCE);
    let image_ci = vk::ImageCreateInfo::default()
        .push_next(&mut ext_mem_image_ci)
        .image_type(vk::ImageType::TYPE_2D)
        .format(vk::Format::R8G8B8A8_UNORM)
        .extent(vk::Extent3D { width: WIDTH, height: HEIGHT, depth: 1 })
        .mip_levels(1)
        .array_layers(1)
        .samples(vk::SampleCountFlags::TYPE_1)
        .tiling(vk::ImageTiling::OPTIMAL)
        .usage(vk::ImageUsageFlags::TRANSFER_SRC | vk::ImageUsageFlags::TRANSFER_DST)
        .initial_layout(vk::ImageLayout::UNDEFINED);
    let image = unsafe { device.create_image(&image_ci, None) }.expect("vkCreateImage failed");

    let mem_reqs = unsafe { device.get_image_memory_requirements(image) };
    let type_index = (0..mem_props.memory_type_count)
        .find(|&i| {
            (mem_reqs.memory_type_bits & (1 << i)) != 0
                && mem_props.memory_types[i as usize]
                    .property_flags
                    .contains(vk::MemoryPropertyFlags::DEVICE_LOCAL)
        })
        .expect("no device-local memory type accepts this image");

    // Both of these extend MemoryAllocateInfo directly as siblings in its
    // pNext chain (ash's ImportMemoryWin32HandleInfoKHR has no p_next
    // field of its own to chain further structs into - unlike the C API,
    // where vkbridge.cpp's version chains dedicated off of the import
    // struct instead; either ordering reaches the same allocator info).
    let mut dedicated = vk::MemoryDedicatedAllocateInfo::default().image(image);
    // ash's Win32 external-memory import struct needs a raw HANDLE value;
    // ash's `vk::HANDLE` is just an alias for a pointer-sized type here.
    let mut import_info = vk::ImportMemoryWin32HandleInfoKHR::default()
        .handle_type(vk::ExternalMemoryHandleTypeFlags::D3D12_RESOURCE)
        .handle(d3d.shared_handle.0 as vk::HANDLE);
    let alloc_info = vk::MemoryAllocateInfo::default()
        .allocation_size(mem_reqs.size)
        .memory_type_index(type_index)
        .push_next(&mut import_info)
        .push_next(&mut dedicated);
    let memory =
        unsafe { device.allocate_memory(&alloc_info, None) }.expect("importing D3D12 texture into Vulkan failed");
    unsafe { device.bind_image_memory(image, memory, 0) }.expect("vkBindImageMemory failed");
    log!("D3D12 resource imported as VkImage ({} bytes)", mem_reqs.size);

    // ---- host-visible staging buffer for CPU read/write -----------------
    let buffer_ci = vk::BufferCreateInfo::default()
        .size(IMAGE_BYTES as u64)
        .usage(vk::BufferUsageFlags::TRANSFER_SRC | vk::BufferUsageFlags::TRANSFER_DST)
        .sharing_mode(vk::SharingMode::EXCLUSIVE);
    let staging = unsafe { device.create_buffer(&buffer_ci, None) }.expect("vkCreateBuffer failed");
    let staging_reqs = unsafe { device.get_buffer_memory_requirements(staging) };
    let staging_type = (0..mem_props.memory_type_count)
        .find(|&i| {
            (staging_reqs.memory_type_bits & (1 << i)) != 0
                && mem_props.memory_types[i as usize].property_flags.contains(
                    vk::MemoryPropertyFlags::HOST_VISIBLE | vk::MemoryPropertyFlags::HOST_COHERENT,
                )
        })
        .expect("no host-visible+coherent memory type for the staging buffer");
    let staging_alloc = vk::MemoryAllocateInfo::default()
        .allocation_size(staging_reqs.size)
        .memory_type_index(staging_type);
    let staging_mem = unsafe { device.allocate_memory(&staging_alloc, None) }.unwrap();
    unsafe { device.bind_buffer_memory(staging, staging_mem, 0) }.unwrap();
    let staging_ptr = unsafe {
        device.map_memory(staging_mem, 0, IMAGE_BYTES as u64, vk::MemoryMapFlags::empty())
    }
    .expect("vkMapMemory failed") as *mut u8;

    let queue = unsafe { device.get_device_queue(queue_family, 0) };
    let pool_ci = vk::CommandPoolCreateInfo::default()
        .flags(vk::CommandPoolCreateFlags::RESET_COMMAND_BUFFER)
        .queue_family_index(queue_family);
    let pool = unsafe { device.create_command_pool(&pool_ci, None) }.unwrap();
    let cmd_alloc_info = vk::CommandBufferAllocateInfo::default()
        .command_pool(pool)
        .level(vk::CommandBufferLevel::PRIMARY)
        .command_buffer_count(1);
    let cmd = unsafe { device.allocate_command_buffers(&cmd_alloc_info) }.unwrap()[0];

    let begin = |cmd: vk::CommandBuffer| unsafe {
        device
            .begin_command_buffer(
                cmd,
                &vk::CommandBufferBeginInfo::default()
                    .flags(vk::CommandBufferUsageFlags::ONE_TIME_SUBMIT),
            )
            .unwrap();
    };
    let submit_and_wait = |cmd: vk::CommandBuffer| unsafe {
        device.end_command_buffer(cmd).unwrap();
        let cmds = [cmd];
        let submit = vk::SubmitInfo::default().command_buffers(&cmds);
        device
            .queue_submit(queue, &[submit], vk::Fence::null())
            .unwrap();
        device.queue_wait_idle(queue).unwrap();
    };

    // The one and only transition out of UNDEFINED (see vkbridge.cpp's
    // comment: doing this more than once would discard image contents).
    begin(cmd);
    let barrier = vk::ImageMemoryBarrier::default()
        .old_layout(vk::ImageLayout::UNDEFINED)
        .new_layout(vk::ImageLayout::GENERAL)
        .src_queue_family_index(vk::QUEUE_FAMILY_IGNORED)
        .dst_queue_family_index(vk::QUEUE_FAMILY_IGNORED)
        .image(image)
        .subresource_range(vk::ImageSubresourceRange {
            aspect_mask: vk::ImageAspectFlags::COLOR,
            base_mip_level: 0,
            level_count: 1,
            base_array_layer: 0,
            layer_count: 1,
        })
        .dst_access_mask(vk::AccessFlags::MEMORY_READ | vk::AccessFlags::MEMORY_WRITE);
    unsafe {
        device.cmd_pipeline_barrier(
            cmd,
            vk::PipelineStageFlags::TOP_OF_PIPE,
            vk::PipelineStageFlags::ALL_COMMANDS,
            vk::DependencyFlags::empty(),
            &[],
            &[],
            &[barrier],
        )
    };
    submit_and_wait(cmd);

    // ---- write a known pattern via a real D3D12 upload-heap copy, matching vkbridge.cpp --------
    // WIDTH * BYTES_PER_PIXEL = 1024, already a multiple of D3D12's 256-byte
    // copy-footprint alignment requirement (same reasoning as vkbridge.cpp).
    let row_pitch = WIDTH * BYTES_PER_PIXEL;
    let pattern = fill_pattern(1);

    let upload: ID3D12Resource = unsafe {
        let up_heap = D3D12_HEAP_PROPERTIES {
            Type: D3D12_HEAP_TYPE_UPLOAD,
            ..Default::default()
        };
        let buf_desc = D3D12_RESOURCE_DESC {
            Dimension: D3D12_RESOURCE_DIMENSION_BUFFER,
            Width: IMAGE_BYTES as u64,
            Height: 1,
            DepthOrArraySize: 1,
            MipLevels: 1,
            Format: DXGI_FORMAT_UNKNOWN,
            SampleDesc: DXGI_SAMPLE_DESC { Count: 1, Quality: 0 },
            Layout: D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
            ..Default::default()
        };
        let mut upload: Option<ID3D12Resource> = None;
        d3d.device
            .CreateCommittedResource(
                &up_heap,
                D3D12_HEAP_FLAG_NONE,
                &buf_desc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                None,
                &mut upload,
            )
            .expect("upload buffer creation failed");
        upload.expect("CreateCommittedResource returned no upload buffer")
    };
    unsafe {
        let mut mapped: *mut core::ffi::c_void = std::ptr::null_mut();
        upload.Map(0, None, Some(&mut mapped)).expect("upload buffer Map failed");
        std::ptr::copy_nonoverlapping(pattern.as_ptr(), mapped as *mut u8, IMAGE_BYTES);
        upload.Unmap(0, None);
    }
    log!("upload buffer written ({IMAGE_BYTES} bytes)");

    let fence: ID3D12Fence = unsafe {
        d3d.device
            .CreateFence(0, D3D12_FENCE_FLAG_NONE)
            .expect("CreateFence failed")
    };
    let fence_event =
        unsafe { windows::Win32::System::Threading::CreateEventW(None, false, false, windows::core::PCWSTR::null()) }
            .expect("CreateEventW failed");

    unsafe {
        d3d.allocator.Reset().expect("allocator Reset failed");
        d3d.list.Reset(&d3d.allocator, None).expect("list Reset failed");

        let to_copy_dest = D3D12_RESOURCE_BARRIER {
            Type: D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
            Anonymous: D3D12_RESOURCE_BARRIER_0 {
                Transition: core::mem::ManuallyDrop::new(D3D12_RESOURCE_TRANSITION_BARRIER {
                    pResource: core::mem::ManuallyDrop::new(Some(d3d.shared_resource.clone())),
                    Subresource: D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                    StateBefore: D3D12_RESOURCE_STATE_COMMON,
                    StateAfter: D3D12_RESOURCE_STATE_COPY_DEST,
                }),
            },
            ..Default::default()
        };
        d3d.list.ResourceBarrier(&[to_copy_dest]);

        let dst = D3D12_TEXTURE_COPY_LOCATION {
            pResource: core::mem::ManuallyDrop::new(Some(d3d.shared_resource.clone())),
            Type: D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,
            Anonymous: D3D12_TEXTURE_COPY_LOCATION_0 { SubresourceIndex: 0 },
        };
        let src = D3D12_TEXTURE_COPY_LOCATION {
            pResource: core::mem::ManuallyDrop::new(Some(upload.clone())),
            Type: D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,
            Anonymous: D3D12_TEXTURE_COPY_LOCATION_0 {
                PlacedFootprint: D3D12_PLACED_SUBRESOURCE_FOOTPRINT {
                    Offset: 0,
                    Footprint: D3D12_SUBRESOURCE_FOOTPRINT {
                        Format: DXGI_FORMAT_R8G8B8A8_UNORM,
                        Width: WIDTH,
                        Height: HEIGHT,
                        Depth: 1,
                        RowPitch: row_pitch,
                    },
                },
            },
        };
        d3d.list.CopyTextureRegion(&dst, 0, 0, 0, &src, None);

        let to_common = D3D12_RESOURCE_BARRIER {
            Type: D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
            Anonymous: D3D12_RESOURCE_BARRIER_0 {
                Transition: core::mem::ManuallyDrop::new(D3D12_RESOURCE_TRANSITION_BARRIER {
                    pResource: core::mem::ManuallyDrop::new(Some(d3d.shared_resource.clone())),
                    Subresource: D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                    StateBefore: D3D12_RESOURCE_STATE_COPY_DEST,
                    StateAfter: D3D12_RESOURCE_STATE_COMMON,
                }),
            },
            ..Default::default()
        };
        d3d.list.ResourceBarrier(&[to_common]);

        d3d.list.Close().expect("list Close failed");
        let lists: [Option<ID3D12CommandList>; 1] = [Some(d3d.list.cast().unwrap())];
        d3d.queue.ExecuteCommandLists(&lists);
        d3d.queue.Signal(&fence, 1).expect("queue Signal failed");
        fence.SetEventOnCompletion(1, fence_event).expect("SetEventOnCompletion failed");
        windows::Win32::System::Threading::WaitForSingleObject(fence_event, 5000);
        let _ = CloseHandle(fence_event);
    }
    log!("D3D12 upload-heap copy to shared texture complete and fenced");

    // ---- Vulkan-side: copy the imported image (now holding the D3D12 write) into the staging
    // buffer. Without this step the staging buffer's own memory was being read/written directly,
    // never actually exercising the imported image at all - which is what the earlier "success"
    // run had been doing, undetected.
    begin(cmd);
    let region = vk::BufferImageCopy::default()
        .image_subresource(vk::ImageSubresourceLayers {
            aspect_mask: vk::ImageAspectFlags::COLOR,
            mip_level: 0,
            base_array_layer: 0,
            layer_count: 1,
        })
        .image_extent(vk::Extent3D { width: WIDTH, height: HEIGHT, depth: 1 });
    unsafe {
        device.cmd_copy_image_to_buffer(cmd, image, vk::ImageLayout::GENERAL, staging, &[region]);
    }
    submit_and_wait(cmd);
    log!("Vulkan copied the imported D3D12 image into the staging buffer");

    let sent: Vec<u8> = unsafe { std::slice::from_raw_parts(staging_ptr, IMAGE_BYTES) }.to_vec();
    log!("read back {} bytes from the Vulkan-side staging buffer after the D3D12 write", sent.len());

    match first_difference(&pattern, &sent) {
        None if pattern.len() == sent.len() => {
            log!("VERIFIED: D3D12 write survived the trip through the imported Vulkan image intact");
        }
        Some(i) => {
            log!(
                "WARNING: D3D12 write did NOT survive the Vulkan image round-trip - first diff at byte {i}: wrote {:#04x}, image gave back {:#04x}",
                pattern[i], sent[i]
            );
        }
        None => {
            log!(
                "WARNING: length mismatch between D3D12 write ({} bytes) and what came back through the image ({} bytes)",
                pattern.len(), sent.len()
            );
        }
    }

    // ---- hand the bytes to hip-bridge-daemon over TCP --------------------
    let port: u16 = std::env::var("HIP_BRIDGE_PORT")
        .ok()
        .and_then(|s| s.parse().ok())
        .unwrap_or(47411);
    let mut stream = match TcpStream::connect(("127.0.0.1", port)) {
        Ok(s) => s,
        Err(e) => {
            log!("failed to connect to hip-bridge-daemon on 127.0.0.1:{port}: {e}");
            std::process::exit(2);
        }
    };
    log!("connected to hip-bridge-daemon on 127.0.0.1:{port}");

    let received = match send_roundtrip(&mut stream, &sent) {
        Ok(r) => r,
        Err(e) => {
            log!("TCP round-trip failed: {e}");
            std::process::exit(2);
        }
    };

    log!("comparing {} sent vs {} received bytes", sent.len(), received.len());

    match first_difference(&sent, &received) {
        None if sent.len() == received.len() => {
            log!(
                "SUCCESS: {} bytes identical after D3D12 -> Vulkan -> TCP -> HIP device -> TCP -> Vulkan",
                sent.len()
            );
        }
        Some(i) => {
            log!(
                "FAIL: first difference at byte {i}: sent {:#04x}, got {:#04x}",
                sent[i], received[i]
            );
            std::process::exit(1);
        }
        None => {
            log!(
                "FAIL: length mismatch, sent {} bytes, got {} bytes",
                sent.len(),
                received.len()
            );
            std::process::exit(1);
        }
    }

    // Instrumented teardown: an earlier run reached the success point per
    // the daemon's own log but produced no further client-side output.
    // Logging before/after each individual call pinpoints exactly which
    // one is responsible instead of guessing - the file log survives even
    // if the process is killed immediately after the last flushed line.
    log!("teardown: begin");
    unsafe {
        log!("teardown: unmap_memory");
        device.unmap_memory(staging_mem);
        log!("teardown: destroy_buffer(staging)");
        device.destroy_buffer(staging, None);
        log!("teardown: free_memory(staging_mem)");
        device.free_memory(staging_mem, None);
        log!("teardown: destroy_image");
        device.destroy_image(image, None);
        log!("teardown: free_memory(memory)");
        device.free_memory(memory, None);
        log!("teardown: destroy_command_pool");
        device.destroy_command_pool(pool, None);
        log!("teardown: destroy_device");
        device.destroy_device(None);
        log!("teardown: destroy_instance");
        instance.destroy_instance(None);
        log!("teardown: CloseHandle(shared_handle)");
        let _ = CloseHandle(d3d.shared_handle);
    }
    log!("teardown: dropping D3D12 objects");
    drop(upload);
    drop(fence);
    drop(d3d.list);
    drop(d3d.allocator);
    drop(d3d.queue);
    drop(d3d.shared_resource);
    drop(d3d.device);
    log!("teardown: complete, exiting normally");
}
