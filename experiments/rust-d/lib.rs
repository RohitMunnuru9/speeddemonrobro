use std::{
    slice,
    sync::atomic::{AtomicI64, AtomicUsize, Ordering},
    thread,
};
type Item = (u64, u32);
struct Heap {
    buckets: [Vec<Item>; 65],
    last: u64,
    count: usize,
    active: u64,
}
impl Default for Heap {
    fn default() -> Self {
        Self {
            buckets: std::array::from_fn(|_| Vec::new()),
            last: 0,
            count: 0,
            active: 0,
        }
    }
}
impl Heap {
    #[inline(always)]
    fn bucket(value: u64) -> usize {
        (64 - value.leading_zeros()) as usize
    }
    #[inline(always)]
    fn push(&mut self, key: u64, node: u32) {
        let b = Self::bucket(self.last ^ key);
        self.buckets[b].push((key, node));
        if b != 0 {
            self.active |= 1u64 << (b - 1);
        }
        self.count += 1;
    }
    #[inline(always)]
    fn minimum_with(&mut self, mut prefetch: impl FnMut(u32)) -> u64 {
        if self.buckets[0].is_empty() {
            let b = self.active.trailing_zeros() as usize + 1;
            let mut values = std::mem::take(&mut self.buckets[b]);
            self.last = values.iter().map(|v| v.0).min().unwrap();
            for &item in &values {
                if item.0 == self.last {
                    prefetch(item.1);
                }
                let dest = Self::bucket(self.last ^ item.0);
                self.buckets[dest].push(item);
                if dest != 0 {
                    self.active |= 1u64 << (dest - 1);
                }
            }
            values.clear();
            self.buckets[b] = values;
            self.active &= !(1u64 << (b - 1));
        }
        self.last
    }
    #[inline(always)]
    fn minimum(&mut self) -> u64 {
        self.minimum_with(|_| {})
    }
    #[inline(always)]
    fn pop(&mut self) -> Item {
        self.minimum();
        self.count -= 1;
        self.buckets[0].pop().unwrap()
    }
    fn clear(&mut self) {
        for bucket in &mut self.buckets {
            bucket.clear();
        }
        self.last = 0;
        self.count = 0;
        self.active = 0;
    }
}
struct State {
    entries: Vec<u64>,
    wide: Vec<u64>,
    filter: [u64; 1024],
    used: usize,
    bits: u32,
    node_mask: u32,
    epoch_limit: u32,
    epoch: u32,
    epoch_mask: u32,
    tag: u32,
}
impl State {
    fn new(n: u32) -> Self {
        let bits = (32 - (n - 1).leading_zeros()).max(1);
        assert!(bits <= 30);
        let node_mask = (1u32 << bits) - 1;
        Self {
            entries: vec![0; 1024],
            wide: Vec::new(),
            filter: [0; 1024],
            used: 0,
            bits,
            node_mask,
            epoch_limit: (1u32 << (31 - bits)) - 1,
            epoch: 0,
            epoch_mask: 0x7fffffff & !node_mask,
            tag: 0,
        }
    }
    #[inline(always)]
    fn hash(node: u32) -> usize {
        let h = u64::from(node).wrapping_mul(0x9e3779b97f4a7c15);
        (h ^ (h >> 32)) as usize
    }
    fn clear(&mut self) {
        self.used = 0;
        self.wide.clear();
        self.filter.fill(0);
        self.epoch += 1;
        if self.epoch > self.epoch_limit {
            self.entries.fill(0);
            self.epoch = 1;
        }
        self.tag = self.epoch << self.bits;
    }
    #[inline(always)]
    fn distance(&self, entry: u64) -> u64 {
        if entry as u32 & 0x80000000 != 0 {
            self.wide[(entry >> 32) as usize]
        } else {
            entry >> 32
        }
    }
    #[inline(always)]
    fn get(&self, node: u32) -> u64 {
        let mask = self.entries.len() - 1;
        let mut slot = Self::hash(node) & mask;
        loop {
            let entry = self.entries[slot];
            if entry as u32 & self.epoch_mask != self.tag {
                return u64::MAX;
            }
            if entry as u32 & self.node_mask == node {
                return self.distance(entry);
            }
            slot = (slot + 1) & mask;
        }
    }
    #[inline(always)]
    fn cross(&self, node: u32) -> u64 {
        if self.filter[((node >> 6) & 1023) as usize] & (1u64 << (node & 63)) == 0 {
            u64::MAX
        } else {
            self.get(node)
        }
    }
    #[inline(always)]
    fn relax(&mut self, node: u32, value: u64) -> bool {
        if (self.used + 1) * 4 > self.entries.len() * 3 {
            let length = self.entries.len() * 2;
            let old = std::mem::replace(&mut self.entries, vec![0; length]);
            for entry in old {
                if entry as u32 & self.epoch_mask == self.tag {
                    let mut slot = Self::hash(entry as u32 & self.node_mask) & (length - 1);
                    while self.entries[slot] as u32 & self.epoch_mask == self.tag {
                        slot = (slot + 1) & (length - 1);
                    }
                    self.entries[slot] = entry;
                }
            }
        }
        let mask = self.entries.len() - 1;
        let mut slot = Self::hash(node) & mask;
        while self.entries[slot] as u32 & self.epoch_mask == self.tag
            && self.entries[slot] as u32 & self.node_mask != node
        {
            slot = (slot + 1) & mask;
        }
        let fresh = self.entries[slot] as u32 & self.epoch_mask != self.tag;
        if !fresh && value >= self.distance(self.entries[slot]) {
            return false;
        }
        let mut packed = value;
        let mut flags = 0u32;
        if value > u64::from(u32::MAX) {
            packed = self.wide.len() as u64;
            assert!(packed <= u64::from(u32::MAX));
            self.wide.push(value);
            flags = 0x80000000;
        }
        self.entries[slot] = (packed << 32) | u64::from(self.tag | flags | node);
        if fresh {
            self.used += 1;
            self.filter[((node >> 6) & 1023) as usize] |= 1u64 << (node & 63);
        }
        true
    }
}
#[repr(C, align(16))]
pub struct Node {
    arcs: [u32; 3],
    range: u32,
}
#[repr(C)]
pub struct Query {
    source: u32,
    target: u32,
}
struct Graph<'a> {
    nodes: [&'a [Node]; 2],
    edges: [&'a [u32]; 2],
    wide: [*const u64; 2],
    weight_bits: u32,
    weight_mask: u32,
}
// C++ owns immutable graph arrays until this scoped solver has joined every
// worker. Wide indices and slice lengths were produced by its graph builder.
unsafe impl Sync for Graph<'_> {}
impl Graph<'_> {
    #[inline(always)]
    fn prefetch(&self, side: usize, u: u32, state: &State) {
        unsafe {
            std::arch::x86_64::_mm_prefetch(self.nodes[side].as_ptr().add(u as usize).cast(), 3);
            std::arch::x86_64::_mm_prefetch(
                state
                    .entries
                    .as_ptr()
                    .add(State::hash(u) & (state.entries.len() - 1))
                    .cast(),
                3,
            );
        }
    }

    #[inline(always)]
    fn decode(&self, side: usize, code: u32) -> (u32, u32) {
        if code >> 31 == 0 {
            (code >> self.weight_bits, code & self.weight_mask)
        } else {
            let value = unsafe { self.wide[side].add((code & 0x7fffffff) as usize).read() };
            ((value >> 32) as u32, value as u32)
        }
    }
    #[inline(always)]
    fn visit(&self, side: usize, u: u32, mut consume: impl FnMut(u32, u32)) {
        let node = &self.nodes[side][u as usize];
        for &code in &node.arcs[..(node.range >> 30) as usize] {
            let (v, w) = self.decode(side, code);
            consume(v, w);
        }
        let first = (node.range & 0x3fffffff) as usize;
        let last = (self.nodes[side][u as usize + 1].range & 0x3fffffff) as usize;
        for &code in &self.edges[side][first..last] {
            let (v, w) = self.decode(side, code);
            consume(v, w);
        }
    }
}
/// The caller supplies immutable arrays with the layouts above, valid arc
/// indices, dense node IDs, and a writable answer per query. No C++ access to
/// answers occurs until return; workers claim distinct answer indices.
#[no_mangle]
pub unsafe extern "C" fn vayu_solve(
    n: u32,
    nodes0: *const Node,
    nodes1: *const Node,
    edges0: *const u32,
    edges1: *const u32,
    wide0: *const u64,
    wide1: *const u64,
    weight_bits: u32,
    queries: *const Query,
    count: usize,
    answers: *mut i64,
    workers: usize,
) {
    let ns = [
        slice::from_raw_parts(nodes0, n as usize + 1),
        slice::from_raw_parts(nodes1, n as usize + 1),
    ];
    let empty: [u32; 0] = [];
    let es0 = if ns[0][n as usize].range & 0x3fffffff == 0 {
        &empty[..]
    } else {
        slice::from_raw_parts(edges0, (ns[0][n as usize].range & 0x3fffffff) as usize)
    };
    let es1 = if ns[1][n as usize].range & 0x3fffffff == 0 {
        &empty[..]
    } else {
        slice::from_raw_parts(edges1, (ns[1][n as usize].range & 0x3fffffff) as usize)
    };
    let graph = Graph {
        nodes: ns,
        edges: [es0, es1],
        wide: [wide0, wide1],
        weight_bits,
        weight_mask: ((1u64 << weight_bits) - 1) as u32,
    };
    let queries = if count == 0 {
        &[][..]
    } else {
        slice::from_raw_parts(queries, count)
    };
    let next = AtomicUsize::new(0);
    let output = answers as usize;
    thread::scope(|scope| {
        for _ in 0..workers.clamp(1, 12) {
            let (graph, next) = (&graph, &next);
            scope.spawn(move || {
                let mut state = [State::new(n), State::new(n)];
                let mut heap = [Heap::default(), Heap::default()];
                loop {
                    let i = next.fetch_add(1, Ordering::Relaxed);
                    if i >= queries.len() {
                        break;
                    }
                    let Query {
                        source: s,
                        target: t,
                    } = queries[i];
                    let answer = AtomicI64::from_ptr((output as *mut i64).add(i));
                    if s == t {
                        answer.store(0, Ordering::Relaxed);
                        continue;
                    }
                    if graph.nodes[0][s as usize].range >> 30 == 0
                        || graph.nodes[1][t as usize].range >> 30 == 0
                    {
                        continue;
                    }
                    state[0].clear();
                    state[1].clear();
                    heap[0].clear();
                    heap[1].clear();
                    state[0].relax(s, 0);
                    state[1].relax(t, 0);
                    heap[0].push(0, s);
                    heap[1].push(0, t);
                    let mut best = u64::MAX;
                    while heap[0].count != 0 && heap[1].count != 0 {
                        let a = heap[0].minimum_with(|u| graph.prefetch(0, u, &state[0]));
                        let b = heap[1].minimum_with(|u| graph.prefetch(1, u, &state[1]));
                        if best != u64::MAX && a + b >= best {
                            break;
                        }
                        let side = usize::from(heap[0].count > heap[1].count);
                        let other = side ^ 1;
                        let mut batch = [(0u64, 0u32); 16];
                        let mut count = 0;
                        loop {
                            let item = heap[side].pop();
                            batch[count] = item;
                            count += 1;
                            graph.prefetch(side, item.1, &state[side]);
                            if count == 16 || heap[side].buckets[0].is_empty() {
                                break;
                            }
                        }
                        for &(d, u) in &batch[..count] {
                            if d != state[side].get(u) {
                                continue;
                            }
                            let opposite = state[other].cross(u);
                            if opposite != u64::MAX {
                                best = best.min(d + opposite);
                            }
                            graph.visit(side, u, |v, w| {
                                let nd = d + u64::from(w);
                                if state[side].relax(v, nd) {
                                    heap[side].push(nd, v);
                                    let opposite = state[other].cross(v);
                                    if opposite != u64::MAX {
                                        best = best.min(nd + opposite);
                                    }
                                }
                            });
                        }
                    }
                    if best != u64::MAX {
                        answer.store(best as i64, Ordering::Relaxed);
                    }
                }
            });
        }
    });
}
