use std::arch::x86_64::*;
use std::cell::UnsafeCell;
use std::collections::HashMap;
use std::ffi::c_void;
use std::fs::{self, File};
use std::hash::{BuildHasherDefault, Hasher};
use std::io::{self, BufWriter, Write};
use std::os::fd::AsRawFd;
use std::sync::atomic::{AtomicBool, AtomicPtr, AtomicUsize, Ordering};
use std::sync::{mpsc::channel, Arc, Condvar, Mutex};
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
fn hour_of(line: &[u8]) -> usize {
    let day = usize::from(line[9] - b'0') + usize::from(line[6] != b'0');
    day * 24 + usize::from(line[11] - b'0') * 10 + usize::from(line[12] - b'0') - 18
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

type Dictionary<K, V> = HashMap<K, V, BuildHasherDefault<SampleHasher>>;

const NAME_SLOTS: usize = 4096;
#[repr(C, align(128))]
struct Name {
    bytes: [u8; 125],
    len: u8,
}
struct Overflow {
    ids: Dictionary<Arc<[u8]>, u32>,
    names: Vec<Arc<[u8]>>,
}
struct Catalog {
    tags: Box<[std::sync::atomic::AtomicU64]>,
    data: Box<[UnsafeCell<Name>]>,
    overflow: Mutex<Overflow>,
    published: AtomicUsize,
    sorted: Mutex<(usize, Arc<Vec<u32>>)>,
}
// A successful 0->BUSY claim gives one writer exclusive access to a Name.
// Its release tag publishes the complete immutable payload; readers acquire
// that tag before comparing bytes. Payloads are never overwritten or removed.
unsafe impl Sync for Catalog {}
impl Catalog {
    fn new() -> Self {
        Self {
            tags: (0..NAME_SLOTS)
                .map(|_| std::sync::atomic::AtomicU64::new(0))
                .collect(),
            data: (0..NAME_SLOTS)
                .map(|_| {
                    UnsafeCell::new(Name {
                        bytes: [0; 125],
                        len: 0,
                    })
                })
                .collect(),
            overflow: Mutex::new(Overflow {
                ids: Dictionary::default(),
                names: Vec::new(),
            }),
            published: AtomicUsize::new(0),
            sorted: Mutex::new((0, Arc::new(Vec::new()))),
        }
    }
    #[cold]
    #[inline(never)]
    fn fallback(&self, name: &[u8]) -> u32 {
        let mut map = self.overflow.lock().unwrap();
        if let Some(&id) = map.ids.get(name) {
            return id;
        }
        let id = u32::try_from(NAME_SLOTS + map.names.len()).expect("endpoint index overflow");
        assert!(id < 0x80000000);
        let key: Arc<[u8]> = Arc::from(name);
        map.names.push(key.clone());
        map.ids.insert(key, id);
        self.published.fetch_add(1, Ordering::Release);
        id
    }
    #[inline(always)]
    fn intern(&self, name: &[u8]) -> u32 {
        if name.len() > 125 {
            return self.fallback(name);
        }
        let mut hasher = SampleHasher::default();
        hasher.write(name);
        let mut hash = hasher.finish();
        if hash < 2 {
            hash += 2;
        }
        let mut slot = hash as usize & (NAME_SLOTS - 1);
        for _ in 0..128 {
            let tag = &self.tags[slot];
            let mut value = tag.load(Ordering::Acquire);
            if value == 0 {
                match tag.compare_exchange(0, 1, Ordering::AcqRel, Ordering::Acquire) {
                    Ok(_) => {
                        unsafe {
                            let data = &mut *self.data[slot].get();
                            data.bytes[..name.len()].copy_from_slice(name);
                            data.len = name.len() as u8;
                        }
                        self.published.fetch_add(1, Ordering::Release);
                        tag.store(hash, Ordering::Release);
                        return slot as u32;
                    }
                    Err(current) => value = current,
                }
            }
            while value == 1 {
                std::hint::spin_loop();
                value = tag.load(Ordering::Acquire);
            }
            if value == hash {
                let data = unsafe { &*self.data[slot].get() };
                if data.len as usize == name.len() && &data.bytes[..name.len()] == name {
                    return slot as u32;
                }
            }
            slot = (slot + 1) & (NAME_SLOTS - 1);
        }
        self.fallback(name)
    }
    // Caller obtained this ID from a published slot or the locked overflow map.
    // Boxed/Arc payload allocations stay stable until all scoped workers join.
    unsafe fn name_ready(&self, id: u32) -> &[u8] {
        let id = id as usize;
        if id < NAME_SLOTS {
            let data = &*self.data[id].get();
            return &data.bytes[..usize::from(data.len)];
        }
        let map = self.overflow.lock().unwrap();
        let name = &map.names[id - NAME_SLOTS];
        let p = name.as_ptr();
        let n = name.len();
        drop(map);
        slice::from_raw_parts(p, n)
    }
    fn order_snapshot(&self) -> Arc<Vec<u32>> {
        let mut cache = self.sorted.lock().unwrap();
        let version = self.published.load(Ordering::Acquire);
        if cache.0 != version || cache.1.len() != version {
            let mut ids: Vec<u32> = (0..NAME_SLOTS)
                .filter(|&id| self.tags[id].load(Ordering::Acquire) > 1)
                .map(|id| id as u32)
                .collect();
            let map = self.overflow.lock().unwrap();
            ids.extend((0..map.names.len()).map(|i| (NAME_SLOTS + i) as u32));
            drop(map);
            ids.sort_unstable_by(|&a, &b| unsafe { self.name_ready(a).cmp(self.name_ready(b)) });
            *cache = (version, Arc::new(ids));
        }
        cache.1.clone()
    }
}
const CHUNK: usize = 256;
const SLOTS: usize = 512;
const REGION_BYTES: usize = CHUNK * SLOTS * 8;
struct Region {
    next: AtomicPtr<Region>,
    counts: [std::sync::atomic::AtomicU16; SLOTS],
    data: *mut u64,
}
impl Region {
    fn new() -> Box<Self> {
        unsafe {
            let data = mmap(ptr::null_mut(), REGION_BYTES, 3, 0x22, -1, 0);
            assert_ne!(data as isize, -1, "record mapping failed");
            Box::new(Self {
                next: AtomicPtr::new(ptr::null_mut()),
                counts: std::array::from_fn(|_| std::sync::atomic::AtomicU16::new(0)),
                data: data.cast(),
            })
        }
    }
    fn ensure(link: &AtomicPtr<Region>) -> *mut Region {
        let old = link.load(Ordering::Acquire);
        if !old.is_null() {
            return old;
        }
        let mut node = Self::new();
        let raw = &mut *node as *mut Region;
        match link.compare_exchange(ptr::null_mut(), raw, Ordering::AcqRel, Ordering::Acquire) {
            Ok(_) => Box::into_raw(node),
            Err(other) => other,
        }
    }
}
impl Drop for Region {
    fn drop(&mut self) {
        unsafe {
            munmap(self.data.cast(), REGION_BYTES);
        }
    }
}
struct Batch {
    region: *mut Region,
    slot: usize,
    count: usize,
    rows: *mut u64,
}
struct Hour {
    state: AtomicUsize,
    writers: AtomicUsize,
    next: AtomicUsize,
    head: AtomicPtr<Region>,
}
impl Hour {
    fn new() -> Self {
        Self {
            state: AtomicUsize::new(0),
            writers: AtomicUsize::new(0),
            next: AtomicUsize::new(0),
            head: AtomicPtr::new(ptr::null_mut()),
        }
    }
    fn reserve(&self) -> Box<Batch> {
        let index = self.next.fetch_add(1, Ordering::Relaxed);
        let mut node = Region::ensure(&self.head);
        for _ in 0..index / SLOTS {
            node = Region::ensure(unsafe { &(*node).next });
        }
        let slot = index % SLOTS;
        Box::new(Batch {
            region: node,
            slot,
            count: 0,
            rows: unsafe { (*node).data.add(slot * CHUNK) },
        })
    }
    fn push(&self, batch: Box<Batch>) {
        unsafe {
            (*batch.region).counts[batch.slot].store(batch.count as u16, Ordering::Release);
        }
    }
}
impl Drop for Hour {
    fn drop(&mut self) {
        let mut at = *self.head.get_mut();
        while !at.is_null() {
            unsafe {
                let node = Box::from_raw(at);
                at = node.next.load(Ordering::Relaxed);
            }
        }
    }
}
#[derive(Clone, Copy)]
struct Piece {
    start: usize,
    end: usize,
    hour: usize,
}
struct Progress {
    epoch: Mutex<usize>,
    changed: Condvar,
}
impl Progress {
    fn signal(&self) {
        *self.epoch.lock().unwrap() += 1;
        self.changed.notify_all();
    }
}
fn finish_hour(hour: usize, state: &Hour, catalog: &Catalog) -> Vec<u8> {
    // CLOSED excludes new writers, and the caller observed zero active writers.
    // Acquire publishes every initialized node. This thread owns the detached
    // list; neither other workers nor Hour::drop can access its nodes afterwards.
    let head = state.head.swap(ptr::null_mut(), Ordering::Acquire);
    if head.is_null() {
        return Vec::new();
    }

    let mut regions = Vec::new();
    let mut at = head;
    while !at.is_null() {
        unsafe {
            let node = Box::from_raw(at);
            at = node.next.load(Ordering::Relaxed);
            regions.push(node);
        }
    }

    let order = catalog.order_snapshot();
    let name_count = order.iter().copied().max().map_or(0, |id| id as usize + 1);
    let mut starts = vec![0usize; name_count + 1];
    let mut errors = vec![0u32; name_count];
    for region in &regions {
        for slot in 0..SLOTS {
            let count = region.counts[slot].load(Ordering::Acquire) as usize;
            let rows = unsafe { slice::from_raw_parts(region.data.add(slot * CHUNK), count) };
            for &row in rows {
                let id = ((row >> 32) & 0x7fffffff) as usize;
                starts[id + 1] += 1;
                errors[id] += (row >> 63) as u32;
            }
        }
    }
    for id in 0..name_count {
        starts[id + 1] += starts[id];
    }
    let mut cursor = starts.clone();
    let mut values = vec![0u32; starts[name_count]];
    for region in &regions {
        for slot in 0..SLOTS {
            let count = region.counts[slot].load(Ordering::Acquire) as usize;
            let rows = unsafe { slice::from_raw_parts(region.data.add(slot * CHUNK), count) };
            for &row in rows {
                let id = ((row >> 32) & 0x7fffffff) as usize;
                values[cursor[id]] = row as u32;
                cursor[id] += 1;
            }
        }
    }
    drop(regions);
    let absolute = hour + 18;
    let day = absolute / 24;
    let timestamp = format!(
        "2026-{:02}-{:02}T{:02}:00:00Z ",
        if day < 2 { 10 } else { 11 },
        if day < 2 { 30 + day } else { day - 1 },
        absolute % 24
    );
    let mut out = Vec::with_capacity(160000);
    for &id in order.iter() {
        let id = id as usize;
        if starts[id] == starts[id + 1] {
            continue;
        }
        let latencies = &mut values[starts[id]..starts[id + 1]];
        let n = latencies.len();
        let mut quantiles = [0; 3];
        if n < 32 {
            latencies.sort_unstable();
            for (i, p) in [50usize, 95, 99].into_iter().enumerate() {
                quantiles[i] = latencies[(p * n).div_ceil(100) - 1];
            }
        } else {
            let mut start = 0;
            for (i, p) in [50usize, 95, 99].into_iter().enumerate() {
                let rank = (p * n).div_ceil(100) - 1;
                let (_, value, _) = latencies[start..].select_nth_unstable(rank - start);
                quantiles[i] = *value;
                start = rank;
            }
        }
        out.extend_from_slice(timestamp.as_bytes());
        out.extend_from_slice(unsafe { catalog.name_ready(id as u32) });
        out.push(b' ');
        append_number(&mut out, n as u64);
        for value in quantiles {
            out.push(b' ');
            append_number(&mut out, value.into());
        }
        out.push(b' ');
        append_number(&mut out, errors[id].into());
        out.push(b' ');
        append_number(&mut out, u64::from(errors[id]) * 1000000 / n as u64);
        out.push(b'\n');
    }
    out
}
fn run_pass(data: &str, out: &str, workers: usize, early: bool) -> io::Result<bool> {
    let mapping = Mapping::open(&format!("{data}/requests.log"))?;
    let length = mapping.length;
    let base = mapping.address as usize;
    // The full-file slice is confined to this setup block. Workers create slices
    // for their uniquely claimed pieces only, so releasing a piece's interior
    // pages cannot invalidate a slice held by another worker.
    let pieces = {
        let bytes = mapping.bytes();
        let count = (workers * 32).min((length / 65536).max(1));
        let mut bounds = Vec::with_capacity(count + 1);
        bounds.push(0);
        for i in 1..count {
            let p = length * i / count;
            bounds.push((p + find(&bytes[p..], b'\n') + 1).min(length));
        }
        bounds.push(length);
        bounds
            .windows(2)
            .filter(|b| b[0] < b[1])
            .map(|b| Piece {
                start: b[0],
                end: b[1],
                hour: hour_of(&bytes[b[0]..b[1]]),
            })
            .collect::<Vec<_>>()
    };
    let hours: Vec<Hour> = (0..72).map(|_| Hour::new()).collect();
    let catalog = Catalog::new();
    let redo = AtomicBool::new(false);
    let next_piece = AtomicUsize::new(0);
    let front = AtomicUsize::new(0);
    let next_hour = AtomicUsize::new(0);
    let done: Vec<AtomicBool> = (0..pieces.len()).map(|_| AtomicBool::new(false)).collect();
    let progress = Progress {
        epoch: Mutex::new(0),
        changed: Condvar::new(),
    };
    fs::create_dir_all(out)?;
    let mut file = BufWriter::new(File::create(format!("{out}/summary.txt"))?);
    thread::scope(|scope| -> io::Result<()> {
        let (tx, rx) = channel();
        for _ in 0..workers {
            let (tx, pieces, hours, catalog, redo, next_piece, front, next_hour, done, progress) = (
                tx.clone(),
                &pieces,
                &hours,
                &catalog,
                &redo,
                &next_piece,
                &front,
                &next_hour,
                &done,
                &progress,
            );
            scope.spawn(move || {
                loop {
                    if redo.load(Ordering::Relaxed) {
                        break;
                    }
                    let current = front.load(Ordering::Acquire);
                    let cutoff = if current == pieces.len() {
                        72
                    } else if early {
                        pieces[current].hour.saturating_sub(1)
                    } else {
                        0
                    };
                    let h = next_hour.load(Ordering::Relaxed);
                    if h < cutoff
                        && next_hour
                            .compare_exchange(h, h + 1, Ordering::Relaxed, Ordering::Relaxed)
                            .is_ok()
                    {
                        hours[h].state.store(1, Ordering::SeqCst);
                        while hours[h].writers.load(Ordering::SeqCst) != 0
                            && !redo.load(Ordering::Relaxed)
                        {
                            thread::yield_now();
                        }
                        if redo.load(Ordering::Relaxed) {
                            break;
                        }
                        let output = finish_hour(h, &hours[h], catalog);
                        hours[h].state.store(2, Ordering::Release);
                        if tx.send((h, output)).is_err() {
                            redo.store(true, Ordering::Relaxed);
                            progress.signal();
                            break;
                        }
                        continue;
                    }
                    let i = next_piece.fetch_add(1, Ordering::Relaxed);
                    if i >= pieces.len() {
                        if current == pieces.len() {
                            if h >= 72 {
                                break;
                            }
                            continue;
                        }
                        let epoch = progress.epoch.lock().unwrap();
                        let before = *epoch;
                        if front.load(Ordering::Acquire) < pieces.len()
                            && !redo.load(Ordering::Relaxed)
                        {
                            let _guard = progress
                                .changed
                                .wait_while(epoch, |value| {
                                    *value == before && !redo.load(Ordering::Relaxed)
                                })
                                .unwrap();
                        }
                        continue;
                    }
                    let piece = pieces[i];
                    {
                        // No reference into this slice escapes the piece. Interned keys own their
                        // bytes; published records contain numbers only.
                        let bytes = unsafe {
                            slice::from_raw_parts(
                                (base + piece.start) as *const u8,
                                piece.end - piece.start,
                            )
                        };
                        let mut active = [false; 72];
                        let mut pending: [Option<Box<Batch>>; 72] = std::array::from_fn(|_| None);
                        let mut p = 0;
                        while p < bytes.len() {
                            let h = hour_of(&bytes[p..]);
                            if !active[h] {
                                hours[h].writers.fetch_add(1, Ordering::SeqCst);
                                if hours[h].state.load(Ordering::SeqCst) != 0 {
                                    hours[h].writers.fetch_sub(1, Ordering::SeqCst);
                                    redo.store(true, Ordering::Relaxed);
                                    progress.signal();
                                    break;
                                }
                                active[h] = true;
                            }
                            let record = unsafe { parse_line(bytes, p, bytes.len()) };
                            p = record.next;
                            let name = &bytes[record.start..record.end];
                            let id = catalog.intern(name);
                            let batch = pending[h].get_or_insert_with(|| hours[h].reserve());
                            unsafe {
                                *batch.rows.add(batch.count) = u64::from(record.latency)
                                    | (u64::from(id) << 32)
                                    | (u64::from(record.error) << 63);
                            }
                            batch.count += 1;
                            if batch.count == CHUNK {
                                hours[h].push(pending[h].take().unwrap());
                            }
                        }
                        for h in 0..72 {
                            if active[h] {
                                if let Some(batch) = pending[h].take() {
                                    if !redo.load(Ordering::Relaxed) {
                                        hours[h].push(batch);
                                    }
                                }
                                hours[h].writers.fetch_sub(1, Ordering::SeqCst);
                            }
                        }
                    }
                    if RELEASE_INPUT {
                        let first = piece.start.div_ceil(4096) * 4096;
                        let last = piece.end / 4096 * 4096;
                        if first < last {
                            unsafe {
                                munmap((base + first) as *mut c_void, last - first);
                            }
                        }
                    }
                    done[i].store(true, Ordering::Release);
                    let mut f = front.load(Ordering::Acquire);
                    while f < done.len() && done[f].load(Ordering::Acquire) {
                        match front.compare_exchange_weak(
                            f,
                            f + 1,
                            Ordering::AcqRel,
                            Ordering::Acquire,
                        ) {
                            Ok(_) => f += 1,
                            Err(new) => f = new,
                        }
                    }
                    progress.signal();
                }
            });
        }
        drop(tx);
        let mut ready: Vec<Option<Vec<u8>>> = (0..72).map(|_| None).collect();
        let mut next = 0;
        for (h, output) in rx {
            ready[h] = Some(output);
            while next < 72 && ready[next].is_some() {
                let bytes = ready[next].take().unwrap();
                if let Err(error) = file.write_all(&bytes) {
                    redo.store(true, Ordering::Relaxed);
                    progress.signal();
                    return Err(error);
                }
                next += 1;
            }
        }
        if !redo.load(Ordering::Relaxed) && next != 72 {
            return Err(io::Error::other("incomplete hour output"));
        }
        Ok(())
    })?;
    file.flush()?;
    Ok(redo.load(Ordering::Relaxed))
}
const RELEASE_INPUT: bool = false;
fn main() -> io::Result<()> {
    let args: Vec<_> = env::args().collect();
    let data = args.get(1).map_or("/data", String::as_str);
    let out = args.get(2).map_or("/out", String::as_str);
    let workers = env::var("SD_THREADS")
        .ok()
        .and_then(|s| s.parse::<usize>().ok())
        .unwrap_or(12)
        .clamp(1, 12);
    if run_pass(data, out, workers, true)? {
        if run_pass(data, out, workers, false)? {
            return Err(io::Error::other("safe pass rejected a record"));
        }
    }
    Ok(())
}
