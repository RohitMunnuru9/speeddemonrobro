use std::arch::x86_64::*;
use std::{
    cell::UnsafeCell,
    collections::HashMap,
    fmt::Write,
    fs::{self, File},
    os::fd::AsRawFd,
    sync::{
        atomic::{AtomicU64, Ordering::*},
        Mutex,
    },
    thread,
};
unsafe extern "C" {
    fn mmap(a: *mut u8, n: usize, p: i32, f: i32, d: i32, o: i64) -> *mut u8;
    fn munmap(a: *mut u8, n: usize) -> i32;
    fn memchr(a: *const u8, c: i32, n: usize) -> *const u8;
}
struct Mapping {
    p: *mut u8,
    n: usize,
}
unsafe impl Sync for Mapping {}
impl Mapping {
    fn new(path: &str) -> Self {
        let f = File::open(path).unwrap();
        let n = f.metadata().unwrap().len() as usize;
        let p = if n == 0 {
            std::ptr::NonNull::<u8>::dangling().as_ptr()
        } else {
            unsafe { mmap(std::ptr::null_mut(), n, 1, 2, f.as_raw_fd(), 0) }
        };
        assert_ne!(p as usize, usize::MAX);
        Self { p, n }
    }
    fn bytes(&self) -> &[u8] {
        unsafe { std::slice::from_raw_parts(self.p, self.n) }
    }
}
impl Drop for Mapping {
    fn drop(&mut self) {
        if self.n != 0 {
            unsafe {
                munmap(self.p, self.n);
            }
        }
    }
}
#[derive(Clone)]
struct Country {
    count: u64,
    ids: u64,
    name_bytes: u64,
    note_bytes: u64,
    names: u64,
    sum: i64,
    lo: i64,
    hi: i64,
}
impl Default for Country {
    fn default() -> Self {
        Self {
            count: 0,
            ids: 0,
            name_bytes: 0,
            note_bytes: 0,
            names: 0,
            sum: 0,
            lo: i64::MAX,
            hi: i64::MIN,
        }
    }
}
#[repr(C, align(16))]
struct Entry<const W: usize> {
    prefix: AtomicU64,
    tail: UnsafeCell<[u64; W]>,
}
unsafe impl<const W: usize> Sync for Entry<W> {}
#[repr(C, align(32))]
struct Mask([AtomicU64; 4]);
struct Table<const W: usize> {
    entries: Vec<Entry<W>>,
    masks: Vec<Mask>,
    overflow: Mutex<HashMap<Vec<u8>, [u64; 4]>>,
}
impl<const W: usize> Table<W> {
    fn new(n: usize) -> Self {
        Self {
            entries: (0..n)
                .map(|_| Entry {
                    prefix: AtomicU64::new(0),
                    tail: UnsafeCell::new([0; W]),
                })
                .collect(),
            masks: (0..n)
                .map(|_| Mask(std::array::from_fn(|_| AtomicU64::new(0))))
                .collect(),
            overflow: Mutex::new(HashMap::new()),
        }
    }
    fn fallback(&self, text: &[u8], c: usize) -> bool {
        let mut map = self.overflow.lock().unwrap();
        let words = map.entry(text.to_vec()).or_default();
        let bit = 1u64 << (c % 64);
        let fresh = words[c / 64] & bit == 0;
        words[c / 64] |= bit;
        fresh
    }
    #[inline(always)]
    fn mark(&self, first: u64, tail: [u64; W], hash: u64, text: &[u8], c: usize) -> bool {
        if first < 2 {
            return self.fallback(text, c);
        }
        let mut slot = hash as usize & (self.entries.len() - 1);
        for _ in 0..128 {
            let entry = &self.entries[slot];
            let mut present = entry.prefix.load(Acquire);
            if present == 0 {
                match entry.prefix.compare_exchange(0, 1, AcqRel, Acquire) {
                    Ok(_) => {
                        unsafe {
                            *entry.tail.get() = tail;
                        }
                        entry.prefix.store(first, Release);
                        present = first;
                    }
                    Err(value) => present = value,
                }
            }
            while present == 1 {
                std::hint::spin_loop();
                present = entry.prefix.load(Acquire);
            }
            if present == first && unsafe { *entry.tail.get() == tail } {
                let bit = 1u64 << (c % 64);
                return self.masks[slot].0[c / 64].fetch_or(bit, Relaxed) & bit == 0;
            }
            slot = (slot + 1) & (self.entries.len() - 1);
        }
        self.fallback(text, c)
    }
}
struct Names {
    small: Table<1>,
    large: Table<3>,
    empty: [AtomicU64; 4],
}
impl Names {
    fn prefetch(&self, text: &[u8], hash: u64) {
        if text.is_empty() || text.len() > 31 {
            return;
        }
        let h = hash as usize;
        unsafe {
            if text.len() <= 16 && (text.len() < 16 || text[15] >= 16) {
                let slot = h & (self.small.entries.len() - 1);
                _mm_prefetch(self.small.entries.as_ptr().add(slot).cast(), 3);
                _mm_prefetch(self.small.masks.as_ptr().add(slot).cast(), 3);
            } else {
                let slot = h & (self.large.entries.len() - 1);
                _mm_prefetch(self.large.entries.as_ptr().add(slot).cast(), 3);
                _mm_prefetch(self.large.masks.as_ptr().add(slot).cast(), 3);
            }
        }
    }
    fn new(n: usize) -> Self {
        Self {
            small: Table::new(n),
            large: Table::new((n / 4).max(128)),
            empty: std::array::from_fn(|_| AtomicU64::new(0)),
        }
    }
    #[inline(always)]
    fn hash(text: &[u8]) -> u64 {
        let mut first = [0u8; 8];
        let mut last = [0u8; 8];
        if text.len() < 8 {
            first[..text.len()].copy_from_slice(text);
        } else {
            first.copy_from_slice(&text[..8]);
            last.copy_from_slice(&text[text.len() - 8..]);
        }
        let mut h = u64::from_le_bytes(first)
            ^ u64::from_le_bytes(last).rotate_left(29)
            ^ text.len() as u64;
        h ^= h >> 33;
        h = h.wrapping_mul(0xff51afd7ed558ccd);
        h ^ (h >> 33)
    }
    #[inline(always)]
    fn mark(&self, text: &[u8], c: usize, hash: u64, end: *const u8) -> bool {
        if text.is_empty() {
            let bit = 1u64 << (c % 64);
            return self.empty[c / 64].fetch_or(bit, Relaxed) & bit == 0;
        }
        if text.len() > 31 {
            return self.large.fallback(text, c);
        }
        let mut key = [0u8; 32];
        unsafe {
            if end.offset_from(text.as_ptr()) >= 32 {
                let indices = _mm256_setr_epi8(
                    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21,
                    22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
                );
                let keep = _mm256_cmpgt_epi8(_mm256_set1_epi8(text.len() as i8), indices);
                let data = _mm256_loadu_si256(text.as_ptr().cast());
                _mm256_storeu_si256(key.as_mut_ptr().cast(), _mm256_and_si256(data, keep));
            } else {
                std::ptr::copy_nonoverlapping(text.as_ptr(), key.as_mut_ptr(), text.len());
            }
        }
        if text.len() <= 16 && (text.len() < 16 || text[15] >= 16) {
            if text.len() < 16 {
                key[15] = text.len() as u8;
            }
            self.small.mark(
                u64::from_le_bytes(key[..8].try_into().unwrap()),
                [u64::from_le_bytes(key[8..16].try_into().unwrap())],
                hash,
                text,
                c,
            )
        } else {
            key[31] = text.len() as u8;
            self.large.mark(
                u64::from_le_bytes(key[..8].try_into().unwrap()),
                std::array::from_fn(|i| {
                    u64::from_le_bytes(key[(i + 1) * 8..(i + 2) * 8].try_into().unwrap())
                }),
                hash,
                text,
                c,
            )
        }
    }
}
#[inline(always)]
fn number(bytes: &[u8], p: &mut usize) -> u64 {
    unsafe {
        let base = bytes.as_ptr();
        let end = base.add(bytes.len());
        let mut q = base.add(*p);
        let mut n = 0;
        while q < end && (*q).is_ascii_digit() {
            n = n * 10 + u64::from(*q - b'0');
            q = q.add(1);
        }
        *p = q.offset_from(base) as usize;
        n
    }
}
#[inline(always)]
fn find(bytes: &[u8], start: usize, c: u8) -> usize {
    let ptr = unsafe { memchr(bytes.as_ptr().add(start), i32::from(c), bytes.len() - start) };
    if ptr.is_null() {
        bytes.len()
    } else {
        ptr as usize - bytes.as_ptr() as usize
    }
}
#[inline(always)]
unsafe fn quote(mut q: *const u8, end: *const u8) -> *const u8 {
    if end.offset_from(q) >= 32 {
        let v = _mm256_loadu_si256(q.cast());
        let mask = _mm256_movemask_epi8(_mm256_cmpeq_epi8(v, _mm256_set1_epi8(b'"' as i8))) as u32;
        if mask != 0 {
            return q.add(mask.trailing_zeros() as usize);
        }
        q = q.add(32);
    }
    let found = memchr(q, i32::from(b'"'), end.offset_from(q) as usize);
    assert!(!found.is_null());
    found
}
#[inline(always)]
fn field<'a>(bytes: &'a [u8], p: &mut usize) -> (&'a [u8], usize) {
    unsafe {
        let base = bytes.as_ptr();
        let end = base.add(bytes.len());
        let mut q = base.add(*p);
        let mut start = q;
        let mut escapes = 0;
        if *q == b'"' {
            q = q.add(1);
            start = q;
            loop {
                let close = quote(q, end);
                q = close.add(1);
                if q == end || *q != b'"' {
                    *p = q.offset_from(base) as usize;
                    let len = close.offset_from(start) as usize;
                    return (std::slice::from_raw_parts(start, len), len - escapes);
                }
                q = q.add(1);
                escapes += 1;
            }
        }
        while q < end && !matches!(*q, b',' | b'\r' | b'\n') {
            q = q.add(1);
        }
        *p = q.offset_from(base) as usize;
        let len = q.offset_from(start) as usize;
        (std::slice::from_raw_parts(start, len), len)
    }
}
fn money(n: i64) -> String {
    let a = n.unsigned_abs();
    format!("{}{}.{:02}", if n < 0 { "-" } else { "" }, a / 100, a % 100)
}
fn main() {
    let args: Vec<_> = std::env::args().collect();
    let data = args.get(1).map(String::as_str).unwrap_or("/data");
    let out = args.get(2).map(String::as_str).unwrap_or("/out");
    let lookup = Mapping::new(&format!("{data}/regions.csv"));
    let input = Mapping::new(&format!("{data}/records.csv"));
    let bytes = input.bytes();
    let regions_bytes = lookup.bytes();
    let mut regions = vec![255u8; 1000000];
    let mut codes = Vec::<String>::new();
    let mut index = [-1i16; 17576];
    let mut p = find(regions_bytes, 0, b'\n') + 1;
    while p < regions_bytes.len() {
        let id = number(regions_bytes, &mut p) as usize;
        p += 1;
        let code = &regions_bytes[p..p + 3];
        let k = usize::from(code[0] - b'A') * 676
            + usize::from(code[1] - b'A') * 26
            + usize::from(code[2] - b'A');
        if index[k] < 0 {
            index[k] = codes.len() as i16;
            codes.push(String::from_utf8(code.to_vec()).unwrap());
        }
        p += 4;
        if id >= regions.len() {
            regions.resize(id + 1, 255);
        }
        regions[id] = index[k] as u8;
    }
    let unmatched = codes.len();
    codes.push("_UNMATCHED".into());
    assert!(codes.len() <= 255);
    let workers = std::env::var("SD_THREADS")
        .ok()
        .and_then(|s| s.parse::<usize>().ok())
        .unwrap_or(if bytes.len() > 1000000 { 6 } else { 1 })
        .clamp(1, 12);
    let mut boundary = vec![bytes.len(); workers + 1];
    boundary[0] = find(bytes, 0, b'\n') + 1;
    for i in 1..workers {
        let cr = find(bytes, bytes.len() * i / workers, b'\r');
        boundary[i] = (cr + 2).min(bytes.len());
    }
    let names = Names::new(if bytes.len() > 1000000 { 131072 } else { 2048 });
    let groups = thread::scope(|scope| {
        let mut handles = Vec::new();
        for t in 0..workers {
            let (names, regions, boundary, codes) = (&names, &regions, &boundary, &codes);
            handles.push(scope.spawn(move || {
                let mut groups = vec![Country::default(); codes.len()];
                let mut p = boundary[t];
                let mut pending = Vec::with_capacity(16);
                let retire = |groups: &mut Vec<Country>,
                              (id, cents, name, name_len, note_len, region, hash): (
                    u64,
                    i64,
                    &[u8],
                    usize,
                    usize,
                    usize,
                    u64,
                )| {
                    let c = regions
                        .get(region)
                        .copied()
                        .filter(|&c| c != 255)
                        .map(usize::from)
                        .unwrap_or(unmatched);
                    let g = &mut groups[c];
                    g.count += 1;
                    g.ids += id;
                    g.sum += cents;
                    g.lo = g.lo.min(cents);
                    g.hi = g.hi.max(cents);
                    g.name_bytes += name_len as u64;
                    g.note_bytes += note_len as u64;
                    g.names += u64::from(
                        names.mark(name, c, hash, unsafe { bytes.as_ptr().add(bytes.len()) }),
                    );
                };
                while p < boundary[t + 1] {
                    if bytes[p] == 0xef {
                        p += 3;
                    }
                    let id = number(bytes, &mut p);
                    p += 1;
                    let (name, name_len) = field(bytes, &mut p);
                    p += 1;
                    let (_, note_len) = field(bytes, &mut p);
                    p += 1;
                    let region = number(bytes, &mut p) as usize;
                    p += 1;
                    let negative = bytes[p] == b'-';
                    if negative {
                        p += 1;
                    }
                    let mut cents = number(bytes, &mut p) as i64 * 100;
                    p += 1;
                    cents += i64::from(bytes[p] - b'0') * 10 + i64::from(bytes[p + 1] - b'0');
                    p += 2;
                    if negative {
                        cents = -cents;
                    }
                    if p < bytes.len() && bytes[p] == b'\r' {
                        p += 1;
                    }
                    if p < bytes.len() && bytes[p] == b'\n' {
                        p += 1;
                    }
                    let hash = if name.is_empty() || name.len() > 31 {
                        0
                    } else {
                        Names::hash(name)
                    };
                    names.prefetch(name, hash);
                    if region < regions.len() {
                        unsafe {
                            _mm_prefetch(regions.as_ptr().add(region).cast(), 3);
                        }
                    }
                    pending.push((id, cents, name, name_len, note_len, region, hash));
                    if pending.len() == 16 {
                        for record in pending.drain(..) {
                            retire(&mut groups, record);
                        }
                    }
                }
                for record in pending {
                    retire(&mut groups, record);
                }
                groups
            }));
        }
        handles
            .into_iter()
            .map(|h| h.join().unwrap())
            .collect::<Vec<_>>()
    });
    let mut totals = vec![Country::default(); codes.len()];
    for groups in groups {
        for (g, r) in totals.iter_mut().zip(groups) {
            g.count += r.count;
            g.ids += r.ids;
            g.sum += r.sum;
            g.lo = g.lo.min(r.lo);
            g.hi = g.hi.max(r.hi);
            g.name_bytes += r.name_bytes;
            g.note_bytes += r.note_bytes;
            g.names += r.names;
        }
    }
    let mut order: Vec<_> = (0..codes.len()).collect();
    order.sort_unstable_by_key(|&i| &codes[i]);
    let mut output = String::new();
    for i in order {
        let g = &totals[i];
        if g.count != 0 {
            writeln!(
                &mut output,
                "{} {} {} {} {} {} {} {} {}",
                codes[i],
                g.count,
                g.ids,
                money(g.sum),
                money(g.lo),
                money(g.hi),
                g.names,
                g.name_bytes,
                g.note_bytes
            )
            .unwrap();
        }
    }
    fs::create_dir_all(out).unwrap();
    fs::write(format!("{out}/aggregates.txt"), output).unwrap();
}
