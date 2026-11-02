use std::arch::x86_64::*;
use std::collections::{hash_map::Entry, HashMap};
use std::ffi::c_void;
use std::fs::{self, File};
use std::hash::{BuildHasherDefault, Hasher};
use std::io::{self, BufWriter, Write};
use std::os::fd::AsRawFd;
use std::sync::mpsc::sync_channel;
use std::{env, ptr, slice, thread};

extern "C" {
    fn mmap(
        addr: *mut c_void,
        len: usize,
        prot: i32,
        flags: i32,
        fd: i32,
        offset: i64,
    ) -> *mut c_void;
    fn munmap(addr: *mut c_void, len: usize) -> i32;
    fn memchr(data: *const c_void, byte: i32, len: usize) -> *const c_void;
}
struct Mapping {
    address: *mut c_void,
    length: usize,
}
impl Mapping {
    fn open(path: &str) -> io::Result<Self> {
        let file = File::open(path)?;
        let length = usize::try_from(file.metadata()?.len()).map_err(io::Error::other)?;
        let address = if length == 0 {
            ptr::null_mut()
        } else {
            // Read-only private mapping; the owning object outlives all borrowed slices.
            let address = unsafe { mmap(ptr::null_mut(), length, 1, 2, file.as_raw_fd(), 0) };
            if address as isize == -1 {
                return Err(io::Error::last_os_error());
            }
            address
        };
        Ok(Self { address, length })
    }
    fn bytes(&self) -> &[u8] {
        if self.length == 0 {
            &[]
        } else {
            unsafe { slice::from_raw_parts(self.address.cast(), self.length) }
        }
    }
}
impl Drop for Mapping {
    fn drop(&mut self) {
        if self.length != 0 {
            unsafe {
                munmap(self.address, self.length);
            }
        }
    }
}
fn find(bytes: &[u8], byte: u8) -> usize {
    // libc handles a bounded byte search without reading outside this slice.
    let found = unsafe { memchr(bytes.as_ptr().cast(), byte.into(), bytes.len()) } as *const u8;
    if found.is_null() {
        bytes.len()
    } else {
        unsafe { found.offset_from(bytes.as_ptr()) as usize }
    }
}
#[derive(Default)]
struct SampleHasher(u64);
impl Hasher for SampleHasher {
    fn finish(&self) -> u64 {
        self.0
    }
    fn write_usize(&mut self, _: usize) {}
    fn write(&mut self, bytes: &[u8]) {
        let mut a = 0u64;
        let (b, c) = if bytes.len() < 8 {
            for (i, byte) in bytes.iter().enumerate() {
                a |= u64::from(*byte) << (i * 8);
            }
            (0, 0)
        } else {
            // Every unaligned load stays inside the endpoint slice.
            unsafe {
                a = ptr::read_unaligned(bytes.as_ptr().cast::<u64>());
                (
                    ptr::read_unaligned(bytes.as_ptr().add(bytes.len() - 8).cast::<u64>()),
                    ptr::read_unaligned(bytes.as_ptr().add((bytes.len() - 8) / 2).cast::<u64>()),
                )
            }
        };
        let mut hash = a ^ b.rotate_left(21) ^ c.rotate_left(43) ^ bytes.len() as u64;
        hash ^= hash >> 33;
        hash = hash.wrapping_mul(0xff51afd7ed558ccd);
        hash ^= hash >> 33;
        self.0 = hash;
    }
}
type Groups<'a> = HashMap<&'a [u8], Group, BuildHasherDefault<SampleHasher>>;
#[derive(Default)]
struct Group {
    values: Vec<u32>,
    errors: u32,
}
#[derive(Default)]
struct Worker<'a> {
    groups: Groups<'a>,
    order: Vec<&'a [u8]>,
}
#[derive(Clone, Copy)]
struct Range {
    start: usize,
    end: usize,
}
fn hour_of(line: &[u8]) -> usize {
    let day = usize::from(line[9] - b'0') + usize::from(line[6] != b'0');
    day * 24 + usize::from(line[11] - b'0') * 10 + usize::from(line[12] - b'0') - 18
}
fn index_hours(bytes: &[u8], start: usize, end: usize) -> [Vec<Range>; 72] {
    let mut result: [Vec<Range>; 72] = std::array::from_fn(|_| Vec::new());
    let mut p = start;
    let mut first = start;
    let mut current = 72;
    while p < end {
        let hour = hour_of(&bytes[p..end]);
        if hour != current {
            if current != 72 {
                result[current].push(Range {
                    start: first,
                    end: p,
                });
            }
            first = p;
            current = hour;
        }
        p += 28 + find(&bytes[p + 28..end], b'\n') + 1;
    }
    if current != 72 {
        result[current].push(Range {
            start: first,
            end: p,
        });
    }
    result
}
fn decimal_ending(bytes: &[u8], end: usize, length: usize) -> u32 {
    // A complete valid record always has at least eight bytes before its LF.
    let mut digits = unsafe { ptr::read_unaligned(bytes.as_ptr().add(end - 8).cast::<u64>()) };
    digits &= 0x0f0f0f0f0f0f0f0f;
    if length < 8 {
        digits &= u64::MAX << ((8 - length) * 8);
    }
    digits = (digits.wrapping_mul(10) + (digits >> 8)) & 0x00ff00ff00ff00ff;
    digits = (digits.wrapping_mul(100) + (digits >> 16)) & 0x0000ffff0000ffff;
    digits = (digits.wrapping_mul(10000) + (digits >> 32)) & 0xffffffff;
    if length > 8 {
        let mut prefix = u32::from(bytes[end - length] - b'0');
        if length == 10 {
            prefix = prefix * 10 + u32::from(bytes[end - length + 1] - b'0');
        }
        prefix * 100_000_000 + digits as u32
    } else {
        digits as u32
    }
}
struct Parsed {
    start: usize,
    end: usize,
    latency: u32,
    error: bool,
    next: usize,
}
#[target_feature(enable = "avx2")]
unsafe fn parse_line(bytes: &[u8], line: usize, end: usize) -> Parsed {
    let p = line + 28;
    if end - p >= 64 {
        // Both 32-byte loads are bounded by the indexed input range.
        let a = _mm256_loadu_si256(bytes.as_ptr().add(p).cast());
        let b = _mm256_loadu_si256(bytes.as_ptr().add(p + 32).cast());
        let lf = _mm256_set1_epi8(b'\n' as i8);
        let newlines = _mm256_movemask_epi8(_mm256_cmpeq_epi8(a, lf)) as u32 as u64
            | ((_mm256_movemask_epi8(_mm256_cmpeq_epi8(b, lf)) as u32 as u64) << 32);
        if newlines != 0 {
            let sp = _mm256_set1_epi8(b' ' as i8);
            let mut spaces = _mm256_movemask_epi8(_mm256_cmpeq_epi8(a, sp)) as u32 as u64
                | ((_mm256_movemask_epi8(_mm256_cmpeq_epi8(b, sp)) as u32 as u64) << 32);
            let method = spaces.trailing_zeros() as usize;
            spaces &= spaces - 1;
            let endpoint = spaces.trailing_zeros() as usize;
            let last = newlines.trailing_zeros() as usize;
            return Parsed {
                start: p + method + 1,
                end: p + endpoint,
                latency: decimal_ending(bytes, p + last, last - endpoint - 5),
                error: bytes[p + endpoint + 1] == b'5',
                next: p + last + 1,
            };
        }
    }
    let start = p + find(&bytes[p..end], b' ') + 1;
    let name_end = start + find(&bytes[start..end], b' ');
    let error = bytes[name_end + 1] == b'5';
    let mut p = name_end + 5;
    let mut latency = 0;
    while p < end && bytes[p].is_ascii_digit() {
        latency = latency * 10 + u32::from(bytes[p] - b'0');
        p += 1;
    }
    Parsed {
        start,
        end: name_end,
        latency,
        error,
        next: p + 1,
    }
}
fn append_number(out: &mut Vec<u8>, mut value: u64) {
    const PAIRS: &[u8;200] = b"00010203040506070809101112131415161718192021222324252627282930313233343536373839404142434445464748495051525354555657585960616263646566676869707172737475767778798081828384858687888990919293949596979899";
    let mut buffer = [0; 20];
    let mut at = 20;
    while value >= 100 {
        let pair = (value % 100) as usize * 2;
        at -= 2;
        buffer[at..at + 2].copy_from_slice(&PAIRS[pair..pair + 2]);
        value /= 100;
    }
    if value < 10 {
        at -= 1;
        buffer[at] = b'0' + value as u8;
    } else {
        at -= 2;
        buffer[at..at + 2].copy_from_slice(&PAIRS[value as usize * 2..value as usize * 2 + 2]);
    }
    out.extend_from_slice(&buffer[at..]);
}
fn solve_hour<'a>(
    bytes: &'a [u8],
    ranges: &[Range],
    hour: usize,
    worker: &mut Worker<'a>,
) -> Vec<u8> {
    let mut added = false;
    for range in ranges {
        let mut p = range.start;
        while p < range.end {
            let record = unsafe { parse_line(bytes, p, range.end) };
            p = record.next;
            let name = &bytes[record.start..record.end];
            let group = match worker.groups.entry(name) {
                Entry::Occupied(entry) => entry.into_mut(),
                Entry::Vacant(entry) => {
                    added = true;
                    entry.insert(Group::default())
                }
            };
            group.values.push(record.latency);
            group.errors += u32::from(record.error);
        }
    }
    if added {
        worker.order = worker.groups.keys().copied().collect();
        worker.order.sort_unstable();
    }
    let absolute = hour + 18;
    let day = absolute / 24;
    let timestamp = format!(
        "2026-{:02}-{:02}T{:02}:00:00Z ",
        if day < 2 { 10 } else { 11 },
        if day < 2 { 30 + day } else { day - 1 },
        absolute % 24
    );
    let mut out = Vec::with_capacity(160000);
    for name in &worker.order {
        let group = worker.groups.get_mut(name).unwrap();
        let n = group.values.len();
        if n == 0 {
            continue;
        }
        let mut quantiles = [0; 3];
        if n < 32 {
            group.values.sort_unstable();
            for (i, percent) in [50usize, 95, 99].into_iter().enumerate() {
                quantiles[i] = group.values[(percent * n).div_ceil(100) - 1];
            }
        } else {
            let mut start = 0;
            for (i, percent) in [50usize, 95, 99].into_iter().enumerate() {
                let rank = (percent * n).div_ceil(100) - 1;
                let (_, value, _) = group.values[start..].select_nth_unstable(rank - start);
                quantiles[i] = *value;
                start = rank;
            }
        }
        out.extend_from_slice(timestamp.as_bytes());
        out.extend_from_slice(name);
        out.push(b' ');
        append_number(&mut out, n as u64);
        for value in quantiles {
            out.push(b' ');
            append_number(&mut out, value.into());
        }
        out.push(b' ');
        append_number(&mut out, group.errors.into());
        out.push(b' ');
        append_number(&mut out, u64::from(group.errors) * 1_000_000 / n as u64);
        out.push(b'\n');
        group.values.clear();
        group.errors = 0;
    }
    out
}
fn main() -> io::Result<()> {
    let args: Vec<_> = env::args().collect();
    let data = args.get(1).map_or("/data", String::as_str);
    let out = args.get(2).map_or("/out", String::as_str);
    let mapping = Mapping::open(&format!("{data}/requests.log"))?;
    let bytes = mapping.bytes();
    let workers = env::var("SD_THREADS")
        .ok()
        .and_then(|x| x.parse::<usize>().ok())
        .unwrap_or(if bytes.len() > 1_000_000 { 12 } else { 1 })
        .clamp(1, 12);
    let mut boundary = vec![bytes.len(); workers + 1];
    boundary[0] = 0;
    for (t, point) in boundary.iter_mut().enumerate().take(workers).skip(1) {
        let p = bytes.len() * t / workers;
        *point = (p + find(&bytes[p..], b'\n') + 1).min(bytes.len());
    }
    let mut ranges: [Vec<Range>; 72] = std::array::from_fn(|_| Vec::new());
    thread::scope(|scope| {
        let mut handles = Vec::new();
        for t in 0..workers {
            let (start, end) = (boundary[t], boundary[t + 1]);
            handles.push(scope.spawn(move || index_hours(bytes, start, end)));
        }
        for handle in handles {
            for (hour, local) in handle.join().unwrap().into_iter().enumerate() {
                ranges[hour].extend(local);
            }
        }
    });
    fs::create_dir_all(out)?;
    let mut file = BufWriter::new(File::create(format!("{out}/summary.txt"))?);
    thread::scope(|scope| -> io::Result<()> {
        let mut receivers = Vec::new();
        for t in 0..workers {
            let (sender, receiver) = sync_channel(0);
            receivers.push(receiver);
            let ranges = &ranges;
            scope.spawn(move || {
                let mut state = Worker::default();
                for hour in (t..72).step_by(workers) {
                    let output = solve_hour(bytes, &ranges[hour], hour, &mut state);
                    if sender.send(output).is_err() {
                        return;
                    }
                }
            });
        }
        for hour in 0..72 {
            let output = receivers[hour % workers].recv().map_err(io::Error::other)?;
            file.write_all(&output)?;
        }
        Ok(())
    })?;
    file.flush()
}
