/*
 * PROJECT:     LiberNT Runtime Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     LZNT1 decompression for RtlDecompressBuffer and RtlDecompressFragment
 * COPYRIGHT:   Copyright Sebastian Lackner
 *              Copyright Michael Müller
 *              Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 *              Copyright 2026 Ostap Brehin <osbre@protonmail.com>
 */

#![cfg_attr(not(test), no_std)]

#[allow(clippy::upper_case_acronyms)]
type NTSTATUS = i32;

const STATUS_SUCCESS: NTSTATUS = 0;
const STATUS_ACCESS_VIOLATION: NTSTATUS = 0xC0000005u32 as NTSTATUS;
const STATUS_INVALID_PARAMETER: NTSTATUS = 0xC000000Du32 as NTSTATUS;
const STATUS_BAD_COMPRESSION_BUFFER: NTSTATUS = 0xC0000242u32 as NTSTATUS;
const STATUS_UNSUPPORTED_COMPRESSION: NTSTATUS = 0xC000025Fu32 as NTSTATUS;

const COMPRESSION_FORMAT_NONE: u16 = 0x0000;
const COMPRESSION_FORMAT_DEFAULT: u16 = 0x0001;
const COMPRESSION_FORMAT_LZNT1: u16 = 0x0002;
const COMPRESSION_ENGINE_MAXIMUM: u16 = 0x0100;

const CHUNK_SIZE: usize = 0x1000;
const CHUNK_COMPRESSED: u16 = 0x8000;
const CHUNK_SIZE_MASK: u16 = 0x0FFF;
const SHORT_MATCH: usize = 16;

enum Error {
    BadCompressionBuffer,
    AccessViolation,
}

enum Match {
    Copied(usize),
    Truncated,
}

impl Error {
    fn status(self) -> NTSTATUS {
        match self {
            Error::BadCompressionBuffer => STATUS_BAD_COMPRESSION_BUFFER,
            Error::AccessViolation => STATUS_ACCESS_VIOLATION,
        }
    }
}

struct Chunk<'a> {
    compressed: bool,
    data: &'a [u8],
}

struct Chunks<'a> {
    src: &'a [u8],
}

impl<'a> Iterator for Chunks<'a> {
    type Item = Result<Chunk<'a>, Error>;

    fn next(&mut self) -> Option<Self::Item> {
        let (header, rest) = self.src.split_first_chunk::<2>()?;
        let header = u16::from_le_bytes(*header);
        if header == 0 {
            self.src = &[];
            return None;
        }

        let size = usize::from(header & CHUNK_SIZE_MASK) + 1;
        let Some((data, rest)) = rest.split_at_checked(size) else {
            self.src = &[];
            return Some(Err(Error::BadCompressionBuffer));
        };
        self.src = rest;
        Some(Ok(Chunk {
            compressed: header & CHUNK_COMPRESSED != 0,
            data,
        }))
    }
}

fn copy_prefix(dst: &mut [u8], src: &[u8]) -> usize {
    let count = dst.len().min(src.len());
    dst[..count].copy_from_slice(&src[..count]);
    count
}

fn zero_prefix(dst: &mut [u8], count: usize) -> usize {
    let count = dst.len().min(count);
    dst[..count].fill(0);
    count
}

fn copy_match(window: &mut [u8], displacement: usize) {
    let mut filled = displacement;
    while filled < window.len() {
        let (history, out) = window.split_at_mut(filled);
        filled += copy_prefix(out, history);
    }
}

fn copy_ends<const N: usize>(out: &mut [u8], history: &[u8]) {
    if let (Some(&head), Some(&tail)) = (
        history.first_chunk::<N>(),
        history.get(..out.len()).and_then(<[u8]>::last_chunk::<N>),
    ) {
        if let Some(out) = out.first_chunk_mut::<N>() {
            *out = head;
        }
        if let Some(out) = out.last_chunk_mut::<N>() {
            *out = tail;
        }
    }
}

fn copy_block<const N: usize>(window: &mut [u8], from: usize, to: usize) {
    if let Some(&block) = window.get(from..).and_then(<[u8]>::first_chunk::<N>)
        && let Some(out) = window.get_mut(to..).and_then(<[u8]>::first_chunk_mut::<N>)
    {
        *out = block;
    }
}

fn copy_blocks<const N: usize>(window: &mut [u8], displacement: usize) {
    let mut to = displacement;
    while to + N < window.len() {
        copy_block::<N>(window, to - displacement, to);
        to += N;
    }
    if let Some(to) = window.len().checked_sub(N) {
        copy_block::<N>(window, to - displacement, to);
    }
}

fn copy_short_match(window: &mut [u8], displacement: usize) {
    let Some((history, out)) = window.split_at_mut_checked(displacement) else {
        return;
    };
    match out.len() {
        8.. => copy_ends::<8>(out, history),
        4.. => copy_ends::<4>(out, history),
        _ => copy_ends::<2>(out, history),
    }
}

#[inline(never)]
fn copy_match_code(
    dst: &mut [u8],
    dst_cur: usize,
    code: u16,
    displacement_bits: &mut u32,
) -> Result<Match, Error> {
    while *displacement_bits < 12 && (1 << *displacement_bits) < dst_cur {
        *displacement_bits += 1;
    }
    let length_bits = 16 - *displacement_bits;
    let length = usize::from(code & ((1 << length_bits) - 1)) + 3;
    let displacement = usize::from(code >> length_bits) + 1;

    let start = dst_cur
        .checked_sub(displacement)
        .ok_or(Error::BadCompressionBuffer)?;
    let end = dst.len().min(dst_cur + length);
    let Some(window) = dst.get_mut(start..end) else {
        return Err(Error::BadCompressionBuffer);
    };
    if end - dst_cur == length && displacement >= length && length <= SHORT_MATCH {
        copy_short_match(window, displacement);
    } else if displacement >= SHORT_MATCH && window.len() >= displacement + SHORT_MATCH {
        copy_blocks::<SHORT_MATCH>(window, displacement);
    } else {
        copy_match(window, displacement);
    }
    Ok(if end - dst_cur == length {
        Match::Copied(end)
    } else {
        Match::Truncated
    })
}

fn decompress_chunk(dst: &mut [u8], src: &[u8]) -> Result<usize, Error> {
    let mut src_cur = 0usize;
    let mut dst_cur = 0usize;
    let mut displacement_bits = 4u32;

    while let Some((&flags, rest)) = src.get(src_cur..).and_then(<[u8]>::split_first)
        && dst_cur < dst.len()
    {
        src_cur += 1;
        let group = rest.get(..16).unwrap_or(rest);

        if flags == 0
            && let Some(literals) = group.first_chunk::<8>()
            && let Some(out) = dst
                .get_mut(dst_cur..)
                .and_then(<[u8]>::first_chunk_mut::<8>)
        {
            *out = *literals;
            src_cur += 8;
            dst_cur += 8;
            continue;
        }

        let mut at = 0usize;
        for bit in 0..8 {
            if at >= group.len() {
                break;
            }
            if flags & (1 << bit) != 0 {
                let code = group
                    .get(at..)
                    .and_then(<[u8]>::first_chunk::<2>)
                    .ok_or(Error::BadCompressionBuffer)?;
                at += 2;
                match copy_match_code(
                    dst,
                    dst_cur,
                    u16::from_le_bytes(*code),
                    &mut displacement_bits,
                )? {
                    Match::Copied(end) => dst_cur = end,
                    Match::Truncated => return Ok(dst.len()),
                }
            } else {
                let (Some(out), Some(&byte)) = (dst.get_mut(dst_cur), group.get(at)) else {
                    return Ok(dst_cur);
                };
                *out = byte;
                dst_cur += 1;
                at += 1;
            }
        }
        src_cur += at;
    }

    Ok(dst_cur)
}

fn decompress<'w>(
    dst: &mut [u8],
    src: &[u8],
    offset: u32,
    workspace: impl FnOnce() -> Option<&'w mut [u8; CHUNK_SIZE]>,
    fragment: bool,
) -> Result<usize, Error> {
    if src.len() < 2 {
        return Err(Error::BadCompressionBuffer);
    }

    let mut chunks = Chunks { src };
    let mut offset = offset as usize;

    while offset >= CHUNK_SIZE {
        let Some(chunk) = chunks.next() else {
            return Ok(0);
        };
        chunk?;
        offset -= CHUNK_SIZE;
    }

    let mut dst_cur = 0usize;

    if offset != 0 {
        let Some(chunk) = chunks.next().transpose()? else {
            return Ok(0);
        };
        if fragment && !chunk.compressed {
            return Err(Error::BadCompressionBuffer);
        }
        if dst.is_empty() {
            return Ok(0);
        }

        dst_cur = if chunk.compressed {
            let workspace = workspace().ok_or(Error::AccessViolation)?;
            let written = decompress_chunk(workspace, chunk.data)?;
            if written < offset {
                zero_prefix(dst, CHUNK_SIZE - offset)
            } else {
                let tail = workspace
                    .get(offset..written)
                    .ok_or(Error::BadCompressionBuffer)?;
                copy_prefix(dst, tail)
            }
        } else {
            chunk
                .data
                .get(offset..)
                .map_or(0, |tail| copy_prefix(dst, tail))
        };
    }

    for chunk in chunks {
        let chunk = chunk?;
        if fragment && !chunk.compressed {
            return Err(Error::BadCompressionBuffer);
        }

        let padding = (dst_cur + offset) % CHUNK_SIZE;
        if padding != 0 {
            let block = CHUNK_SIZE - padding;
            let Some(out) = dst.get_mut(dst_cur..).filter(|out| block < out.len()) else {
                return Ok(dst_cur);
            };
            dst_cur += zero_prefix(out, block);
        }

        let Some(out) = dst.get_mut(dst_cur..).filter(|out| !out.is_empty()) else {
            return Ok(dst_cur);
        };
        dst_cur += if chunk.compressed {
            decompress_chunk(out, chunk.data)?
        } else {
            copy_prefix(out, chunk.data)
        };
    }

    Ok(dst_cur)
}

unsafe fn buffer_mut<'a>(ptr: *mut u8, size: u32) -> &'a mut [u8] {
    if ptr.is_null() || size == 0 {
        &mut []
    } else {
        unsafe { core::slice::from_raw_parts_mut(ptr, size as usize) }
    }
}

unsafe fn buffer<'a>(ptr: *const u8, size: u32) -> &'a [u8] {
    if ptr.is_null() || size == 0 {
        &[]
    } else {
        unsafe { core::slice::from_raw_parts(ptr, size as usize) }
    }
}

unsafe fn decompress_lznt1(
    compression_format: u16,
    final_size: *mut u32,
    run: impl FnOnce() -> Result<usize, Error>,
) -> NTSTATUS {
    match compression_format & !COMPRESSION_ENGINE_MAXIMUM {
        COMPRESSION_FORMAT_LZNT1 => match run() {
            Ok(written) => {
                if let Some(final_size) = unsafe { final_size.as_mut() } {
                    *final_size = written as u32;
                }
                STATUS_SUCCESS
            }
            Err(error) => error.status(),
        },
        COMPRESSION_FORMAT_NONE | COMPRESSION_FORMAT_DEFAULT => STATUS_INVALID_PARAMETER,
        _ => STATUS_UNSUPPORTED_COMPRESSION,
    }
}

#[allow(clippy::missing_safety_doc)]
#[unsafe(no_mangle)]
pub unsafe extern "system-unwind" fn RtlDecompressBuffer(
    compression_format: u16,
    uncompressed_buffer: *mut u8,
    uncompressed_buffer_size: u32,
    compressed_buffer: *mut u8,
    compressed_buffer_size: u32,
    final_uncompressed_size: *mut u32,
) -> NTSTATUS {
    unsafe {
        decompress_lznt1(compression_format, final_uncompressed_size, || {
            decompress(
                buffer_mut(uncompressed_buffer, uncompressed_buffer_size),
                buffer(compressed_buffer, compressed_buffer_size),
                0,
                || None,
                false,
            )
        })
    }
}

#[allow(clippy::missing_safety_doc)]
#[unsafe(no_mangle)]
pub unsafe extern "system-unwind" fn RtlDecompressFragment(
    compression_format: u16,
    uncompressed_buffer: *mut u8,
    uncompressed_buffer_size: u32,
    compressed_buffer: *mut u8,
    compressed_buffer_size: u32,
    offset: u32,
    final_uncompressed_size: *mut u32,
    workspace: *mut core::ffi::c_void,
) -> NTSTATUS {
    unsafe {
        decompress_lznt1(compression_format, final_uncompressed_size, || {
            decompress(
                buffer_mut(uncompressed_buffer, uncompressed_buffer_size),
                buffer(compressed_buffer, compressed_buffer_size),
                offset,
                || workspace.cast::<[u8; CHUNK_SIZE]>().as_mut(),
                true,
            )
        })
    }
}

#[cfg(not(test))]
#[panic_handler]
fn panic(_info: &core::panic::PanicInfo) -> ! {
    #[cfg(any(target_arch = "x86_64", target_arch = "x86"))]
    unsafe {
        core::arch::asm!("ud2", options(noreturn))
    }
    #[cfg(target_arch = "aarch64")]
    unsafe {
        core::arch::asm!("brk #0", options(noreturn))
    }
    #[cfg(target_arch = "riscv64")]
    unsafe {
        core::arch::asm!("ebreak", options(noreturn))
    }
    #[cfg(not(any(
        target_arch = "x86_64",
        target_arch = "x86",
        target_arch = "aarch64",
        target_arch = "riscv64"
    )))]
    loop {}
}

#[cfg(test)]
mod tests {
    use super::*;

    fn decompress_buffer(src: &[u8], capacity: usize) -> (NTSTATUS, Vec<u8>, u32) {
        decompress_format(COMPRESSION_FORMAT_LZNT1, src, capacity)
    }

    fn decompress_format(format: u16, src: &[u8], capacity: usize) -> (NTSTATUS, Vec<u8>, u32) {
        let mut out = vec![0u8; capacity];
        let mut final_size = 0u32;
        let status = unsafe {
            RtlDecompressBuffer(
                format,
                out.as_mut_ptr(),
                out.len() as u32,
                src.as_ptr() as *mut u8,
                src.len() as u32,
                &mut final_size,
            )
        };
        (status, out, final_size)
    }

    fn decompress_fragment(src: &[u8], capacity: usize, offset: u32) -> (NTSTATUS, Vec<u8>, u32) {
        let mut out = vec![0u8; capacity];
        let mut workspace = vec![0u8; CHUNK_SIZE];
        let mut final_size = 0u32;
        let status = unsafe {
            RtlDecompressFragment(
                COMPRESSION_FORMAT_LZNT1,
                out.as_mut_ptr(),
                out.len() as u32,
                src.as_ptr() as *mut u8,
                src.len() as u32,
                offset,
                &mut final_size,
                workspace.as_mut_ptr() as *mut core::ffi::c_void,
            )
        };
        (status, out, final_size)
    }

    #[test]
    fn uncompressed_chunk() {
        let src = [0x03, 0x00, b'a', b'b', b'c', b'd'];
        let (status, out, final_size) = decompress_buffer(&src, 8);
        assert_eq!(status, STATUS_SUCCESS);
        assert_eq!(final_size, 4);
        assert_eq!(&out[..4], b"abcd");
    }

    #[test]
    fn compressed_literals() {
        let mut src = vec![0x08, 0x80, 0x00];
        src.extend_from_slice(b"abcdefgh");
        let (status, out, final_size) = decompress_buffer(&src, 8);
        assert_eq!(status, STATUS_SUCCESS);
        assert_eq!(final_size, 8);
        assert_eq!(&out[..8], b"abcdefgh");
    }

    #[test]
    fn compressed_back_reference() {
        let mut src = vec![0x0B, 0x80, 0x00];
        src.extend_from_slice(b"abcdefgh");
        src.extend_from_slice(&[0x01, 0x00, 0x30]);
        let (status, out, final_size) = decompress_buffer(&src, 16);
        assert_eq!(status, STATUS_SUCCESS);
        assert_eq!(final_size, 11);
        assert_eq!(&out[..11], b"abcdefghefg");
    }

    fn compressed_chunk(data: &[u8]) -> Vec<u8> {
        let header = 0x8000 | (data.len() as u16 - 1);
        let mut chunk = header.to_le_bytes().to_vec();
        chunk.extend_from_slice(data);
        chunk
    }

    #[test]
    fn overlapping_back_reference() {
        let src = compressed_chunk(&[0x02, b'a', 0x61, 0x00]);
        let (status, out, final_size) = decompress_buffer(&src, 128);
        assert_eq!(status, STATUS_SUCCESS);
        assert_eq!(final_size, 101);
        assert!(out[..101].iter().all(|&b| b == b'a'));
        assert_eq!(out[101], 0);
    }

    #[test]
    fn back_reference_truncated_by_buffer() {
        let src = compressed_chunk(&[0x02, b'a', 0x61, 0x00]);
        let (status, out, final_size) = decompress_buffer(&src, 50);
        assert_eq!(status, STATUS_SUCCESS);
        assert_eq!(final_size, 50);
        assert!(out.iter().all(|&b| b == b'a'));
    }

    #[test]
    fn displacement_width_grows_with_position() {
        let mut data = vec![0x00];
        data.extend_from_slice(b"01234567");
        data.push(0x00);
        data.extend_from_slice(b"89abcdef");
        data.extend_from_slice(&[0x01, 0x00, 0xF0]);
        let (status, out, final_size) = decompress_buffer(&compressed_chunk(&data), 32);
        assert_eq!(status, STATUS_SUCCESS);
        assert_eq!(final_size, 19);
        assert_eq!(&out[..19], b"0123456789abcdef012");

        data.truncate(data.len() - 3);
        data.extend_from_slice(&[0x02, b'g', 0x02, 0x80]);
        let (status, out, final_size) = decompress_buffer(&compressed_chunk(&data), 32);
        assert_eq!(status, STATUS_SUCCESS);
        assert_eq!(final_size, 22);
        assert_eq!(&out[..22], b"0123456789abcdefg01234");
    }

    #[test]
    fn back_reference_before_start() {
        let src = [0x02, 0x80, 0x01, 0x00, 0x00];
        let (status, _, _) = decompress_buffer(&src, 16);
        assert_eq!(status, STATUS_BAD_COMPRESSION_BUFFER);
    }

    #[test]
    fn truncated_streams() {
        assert_eq!(decompress_buffer(&[], 16).0, STATUS_BAD_COMPRESSION_BUFFER);
        assert_eq!(
            decompress_buffer(&[0x00], 16).0,
            STATUS_BAD_COMPRESSION_BUFFER
        );
        assert_eq!(
            decompress_buffer(&[0x08, 0x80], 16).0,
            STATUS_BAD_COMPRESSION_BUFFER
        );
        assert_eq!(
            decompress_buffer(&[0x08, 0x80, 0x00, b'a'], 16).0,
            STATUS_BAD_COMPRESSION_BUFFER
        );
    }

    #[test]
    fn unsupported_formats() {
        let src = [0x03, 0x00, b'a', b'b', b'c', b'd'];
        for (format, status) in [
            (COMPRESSION_FORMAT_NONE, STATUS_INVALID_PARAMETER),
            (COMPRESSION_FORMAT_DEFAULT, STATUS_INVALID_PARAMETER),
            (0xFF, STATUS_UNSUPPORTED_COMPRESSION),
        ] {
            assert_eq!(decompress_format(format, &src, 16).0, status);
        }
    }

    #[test]
    fn null_final_size() {
        let src = [0x03, 0x00, b'a', b'b', b'c', b'd'];
        let mut out = [0u8; 8];
        let status = unsafe {
            RtlDecompressBuffer(
                COMPRESSION_FORMAT_LZNT1,
                out.as_mut_ptr(),
                out.len() as u32,
                src.as_ptr() as *mut u8,
                src.len() as u32,
                core::ptr::null_mut(),
            )
        };
        assert_eq!(status, STATUS_SUCCESS);
        assert_eq!(&out[..4], b"abcd");
    }

    #[test]
    fn padding_between_chunks() {
        let mut src = vec![0x08, 0x80, 0x00];
        src.extend_from_slice(b"abcdefgh");
        src.extend_from_slice(&[0x08, 0x80, 0x00]);
        src.extend_from_slice(b"ijklmnop");
        let (status, out, final_size) = decompress_buffer(&src, CHUNK_SIZE + 8);
        assert_eq!(status, STATUS_SUCCESS);
        assert_eq!(final_size, CHUNK_SIZE as u32 + 8);
        assert_eq!(&out[..8], b"abcdefgh");
        assert!(out[8..CHUNK_SIZE].iter().all(|&b| b == 0));
        assert_eq!(&out[CHUNK_SIZE..CHUNK_SIZE + 8], b"ijklmnop");
    }

    #[test]
    fn fragment_skips_and_partially_copies() {
        let mut src = vec![0x03, 0x00];
        src.extend_from_slice(b"AAAA");
        src.extend_from_slice(&[0x08, 0x80, 0x00]);
        src.extend_from_slice(b"abcdefgh");
        let (status, out, final_size) = decompress_fragment(&src, 4, CHUNK_SIZE as u32 + 2);
        assert_eq!(status, STATUS_SUCCESS);
        assert_eq!(final_size, 4);
        assert_eq!(&out[..4], b"cdef");
    }

    #[test]
    fn fragment_rejects_uncompressed_chunks() {
        let src = [0x03, 0x00, b'a', b'b', b'c', b'd'];
        let (status, _, _) = decompress_fragment(&src, 8, 0);
        assert_eq!(status, STATUS_BAD_COMPRESSION_BUFFER);
    }

    #[test]
    fn fragment_requires_workspace() {
        let mut src = vec![0x08, 0x80, 0x00];
        src.extend_from_slice(b"abcdefgh");
        let mut out = [0u8; 8];
        let mut final_size = 0u32;
        let status = unsafe {
            RtlDecompressFragment(
                COMPRESSION_FORMAT_LZNT1,
                out.as_mut_ptr(),
                out.len() as u32,
                src.as_ptr() as *mut u8,
                src.len() as u32,
                2,
                &mut final_size,
                core::ptr::null_mut(),
            )
        };
        assert_eq!(status, STATUS_ACCESS_VIOLATION);
    }

    #[test]
    fn fragment_ignores_short_workspace_at_chunk_start() {
        let mut src = vec![0x08, 0x80, 0x00];
        src.extend_from_slice(b"abcdefgh");
        let mut out = [0u8; 8];
        let mut workspace = [0u8; 1];
        let mut final_size = 0u32;
        let status = unsafe {
            RtlDecompressFragment(
                COMPRESSION_FORMAT_LZNT1,
                out.as_mut_ptr(),
                out.len() as u32,
                src.as_ptr() as *mut u8,
                src.len() as u32,
                0,
                &mut final_size,
                workspace.as_mut_ptr() as *mut core::ffi::c_void,
            )
        };
        assert_eq!(status, STATUS_SUCCESS);
        assert_eq!(&out, b"abcdefgh");
        assert_eq!(final_size, 8);
    }

    fn match_code(position: usize, displacement: usize, length: usize) -> [u8; 2] {
        let mut bits = 4;
        while bits < 12 && (1 << bits) < position {
            bits += 1;
        }
        ((((displacement - 1) << (16 - bits)) | (length - 3)) as u16).to_le_bytes()
    }

    #[test]
    fn grouped_matches_of_every_copy_size() {
        let mut data = vec![];
        let mut expected = b"0123456789abcdefghijklmnopqrstuv".to_vec();
        for literals in expected.chunks(8) {
            data.push(0x00);
            data.extend_from_slice(literals);
        }
        data.push(0xFF);
        for (displacement, length) in [
            (32, 3),
            (20, 5),
            (30, 12),
            (17, 16),
            (40, 40),
            (3, 10),
            (1, 30),
            (100, 18),
        ] {
            data.extend_from_slice(&match_code(expected.len(), displacement, length));
            for _ in 0..length {
                expected.push(expected[expected.len() - displacement]);
            }
        }
        data.extend_from_slice(&[0x00; 9]);
        expected.extend_from_slice(&[0x00; 8]);

        let src = compressed_chunk(&data);
        let (status, out, final_size) = decompress_buffer(&src, expected.len() + 8);
        assert_eq!(status, STATUS_SUCCESS);
        assert_eq!(final_size as usize, expected.len());
        assert_eq!(&out[..expected.len()], &expected[..]);
        assert!(out[expected.len()..].iter().all(|&b| b == 0));

        for capacity in [36, 40, 50, 70, 100, 140] {
            let (status, out, final_size) = decompress_buffer(&src, capacity);
            assert_eq!(status, STATUS_SUCCESS);
            assert_eq!(final_size as usize, capacity);
            assert_eq!(out, expected[..capacity]);
        }
    }
}
