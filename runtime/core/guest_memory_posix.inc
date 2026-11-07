// GuestMemory on Linux and Android (included by guest_memory.cpp inside namespace mh3u::core, in its anonymous
// namespace), x86-64 and arm64. The 4 GB block is an mmap(PROT_NONE) reservation; commit() makes pages readable and
// writable. Pages are the host's (4 KB on x86-64; 4 or 16 KB on arm64 Android): protection and the write epochs
// work per host page.
// Write tracking emulates Windows' write watch: pages in the watched ranges are made read-only; the first write to one
// faults, the SIGSEGV handler marks the page written and makes it writable again, and the scanner stamps it and makes
// it read-only again. A page stays marked for two passes, so a write that lands just after the handler returned (the
// faulting instruction itself) is still stamped. The kernel does not fault on its own writes (read() into a read-only
// page fails with EFAULT): file reads into guest memory go through a bounce buffer (fs/host_tree.cpp).

constexpr uint8_t kCommitted = 1, kTracked = 2;  // page state bits; kTracked: read-only, waiting for a write
size_t g_shift = 12;                              // log2 of the host page size (set by the constructor)
inline uint64_t page_bytes() { return uint64_t{1} << g_shift; }
inline uint64_t page_mask() { return page_bytes() - 1; }

// Whether the faulting access was a write. x86-64: the page-fault error code; arm64: ESR_EL1's WnR bit, from the
// esr_context record the kernel puts in the signal frame (layout from <asm/sigcontext.h>, declared here because that
// header clashes with glibc's <signal.h>), else the faulting instruction's encoding.
bool fault_is_write(const ucontext_t* uc) {
#if defined(__x86_64__)
    return (uc->uc_mcontext.gregs[REG_ERR] & 2) != 0;
#elif defined(__aarch64__)
    struct Record {
        uint32_t magic, size;
    };
    constexpr uint32_t kEsrMagic = 0x45535201;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(uc->uc_mcontext.__reserved);
    const uint8_t* end = p + sizeof(uc->uc_mcontext.__reserved);
    while (p + sizeof(Record) <= end) {
        Record r;
        std::memcpy(&r, p, sizeof r);
        if (!r.magic || r.size < sizeof r) break;
        if (r.magic == kEsrMagic && p + 16 <= end) {
            uint64_t esr;
            std::memcpy(&esr, p + 8, 8);
            return (esr >> 6) & 1;  // WnR
        }
        p += r.size;
    }
    // no ESR record (emulators such as qemu-user): decode the faulting instruction
    uint32_t insn;
    std::memcpy(&insn, reinterpret_cast<const void*>(uc->uc_mcontext.pc), 4);
    const uint32_t cls = insn >> 27 & 7;  // bits 29:27
    if (cls == 7) {  // load / store register (every addressing form), and LSE atomics
        if ((insn >> 24 & 3) == 0 && (insn >> 21 & 1) && (insn >> 10 & 3) == 0) return true;  // atomic: read-modify-write
        const uint32_t opc = insn >> 22 & 3;
        return (insn >> 26 & 1) ? (opc & 1) == 0 : opc == 0;  // SIMD: opc 00 / 10 store; integer: 00 store, else loads
    }
    if (cls == 5 || cls == 1) return (insn >> 22 & 1) == 0;  // pairs, exclusives: L = 0 store
    return true;
#else
#error "guest memory: unsupported host architecture"
#endif
}
uint64_t fault_pc(const ucontext_t* uc) {
#if defined(__x86_64__)
    return static_cast<uint64_t>(uc->uc_mcontext.gregs[REG_RIP]);
#else
    return static_cast<uint64_t>(uc->uc_mcontext.pc);
#endif
}
std::unique_ptr<std::atomic<uint8_t>[]> g_page_state;
std::unique_ptr<std::atomic<uint8_t>[]> g_dirty;  // 2: written since the scanner last looked; 1: the pass before
bool g_report = false;
struct sigaction g_previous[2];

bool hit_watch(uint32_t addr, bool write) {
    std::lock_guard g(g_watch_m);
    for (size_t i = 0; i < g_watches.size(); ++i) {
        const Watch w = g_watches[i];
        if (addr < w.begin || addr >= w.end) continue;
        mprotect(g_base + w.begin, w.end - w.begin, PROT_READ | PROT_WRITE);
        g_watches.erase(g_watches.begin() + static_cast<std::ptrdiff_t>(i));
        std::fprintf(stderr, "watch: CPU %s guest %08X in watched range %08X-%08X (tag %08X)\n", write ? "write to" : "read from",
                     addr, w.begin, w.end, w.tag);
        return true;
    }
    return false;
}

void on_fault(int sig, siginfo_t* info, void* context) {
    const auto host = reinterpret_cast<uint64_t>(info->si_addr), base = reinterpret_cast<uint64_t>(g_base);
    const auto* uc = static_cast<const ucontext_t*>(context);
    const bool write = fault_is_write(uc);
    if (g_base && host >= base && host < base + kSpace) {
        const auto addr = static_cast<uint32_t>(host - base);
        const size_t page = addr >> g_shift;
        const uint8_t state = g_page_state[page].load(std::memory_order_relaxed);
        if (state & kTracked) {  // a write to a tracked page
            g_dirty[page].store(2, std::memory_order_relaxed);
            g_page_state[page].fetch_and(static_cast<uint8_t>(~kTracked), std::memory_order_relaxed);
            mprotect(g_base + (page << g_shift), page_bytes(), PROT_READ | PROT_WRITE);
            return;
        }
        if (hit_watch(addr, write)) return;
        if (write && (state & kCommitted)) {
            // read-only without kTracked: the scanner queued this page for protection, this handler unprotected it
            // for an earlier write, then the scanner's mprotect landed. Still a tracked write (the next pass sets
            // kTracked again from the dirty mark).
            g_dirty[page].store(2, std::memory_order_relaxed);
            mprotect(g_base + (page << g_shift), page_bytes(), PROT_READ | PROT_WRITE);
            return;
        }
    }
    if (!g_report) {  // not ours: the previous handler (or the default action: re-raised when this returns)
        sigaction(sig, &g_previous[sig == SIGBUS ? 1 : 0], nullptr);
        return;
    }
    if (g_base && host >= base && host < base + kSpace)
        std::fprintf(stderr, "guest memory fault: %s guest address %08X (uncommitted)\n", write ? "write to" : "read from",
                     static_cast<uint32_t>(host - base));
    else
        std::fprintf(stderr, "host access violation at %p\n", info->si_addr);
    // where: module + offset of the faulting instruction (addr2line -e <module> <offset>)
    const auto pc = reinterpret_cast<void*>(fault_pc(uc));
    Dl_info dl{};
    if (dladdr(pc, &dl) && dl.dli_fname)
        std::fprintf(stderr, "  at %s+0x%llX (%s)\n", dl.dli_fname,
                     static_cast<unsigned long long>(reinterpret_cast<uint64_t>(pc) - reinterpret_cast<uint64_t>(dl.dli_fbase)),
                     dl.dli_sname ? dl.dli_sname : "?");
    std::fflush(stderr);
    _exit(3);
}

// [first, first + n) pages: one mprotect per run
void protect_pages(size_t first, size_t n, int prot) {
    if (n) mprotect(g_base + (first << g_shift), n << g_shift, prot);
}

}  // namespace

static GuestMemory* g_instance = nullptr;

GuestMemory::GuestMemory() {
    void* p = mmap(nullptr, kSpace, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (p == MAP_FAILED) throw std::runtime_error("cannot reserve 4 GB of address space for guest memory");
    base_ = static_cast<uint8_t*>(p);
    g_base = base_;
    g_instance = this;
    const long ps = sysconf(_SC_PAGESIZE);
    g_shift = 12;
    while (ps > 0 && (uint64_t{1} << g_shift) < static_cast<uint64_t>(ps) && g_shift < 20) ++g_shift;
    if (g_shift != 12) std::printf("guest memory: %llu KB host pages\n", static_cast<unsigned long long>(page_bytes() >> 10));
    page_epoch_ = std::make_unique<std::atomic<uint32_t>[]>(kSpace >> g_shift);
    g_page_state = std::make_unique<std::atomic<uint8_t>[]>(kSpace >> g_shift);
    g_dirty = std::make_unique<std::atomic<uint8_t>[]>(kSpace >> g_shift);
    struct sigaction sa{};
    sa.sa_sigaction = on_fault;
    sa.sa_flags = SA_SIGINFO | SA_NODEFER;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, &g_previous[0]);
    sigaction(SIGBUS, &sa, &g_previous[1]);
}

GuestMemory* GuestMemory::current() { return g_instance; }

void GuestMemory::set_watched(std::vector<Range> ranges) {
    {
        std::lock_guard g(watch_m_);
        watched_ = std::move(ranges);
    }
    if (!scanner_.joinable()) scanner_ = std::thread([this] { scan_loop(); });
}

// Each pass: pages written since the last one (or the one before) are stamped with epoch e and made read-only again;
// committed pages of the ranges not tracked yet are made read-only. Then e is published (release), as on Windows.
void GuestMemory::scan_loop() {
    pthread_setname_np(pthread_self(), "guest-writes");
    std::vector<Range> ranges;
    while (!stop_.load()) {
        {
            std::lock_guard g(watch_m_);
            ranges = watched_;
        }
        const uint32_t e = epoch_.load(std::memory_order_relaxed) + 1;
        uint64_t stamped = 0;
        for (const Range& r : ranges) {
            const size_t first = r.addr >> g_shift, last = (size_t{r.addr} + r.size + page_mask()) >> g_shift;
            size_t run = 0, run_n = 0;  // pages to make read-only, as one run
            for (size_t pg = first; pg < last; ++pg) {
                const uint8_t st = g_page_state[pg].load(std::memory_order_relaxed);
                uint8_t d = g_dirty[pg].load(std::memory_order_relaxed);
                bool protect = false;
                if (d) {
                    g_page_state[pg].fetch_or(kTracked, std::memory_order_relaxed);
                    protect = true;
                    page_epoch_[pg].store(e, std::memory_order_relaxed);
                    ++stamped;
                    g_dirty[pg].compare_exchange_strong(d, static_cast<uint8_t>(d - 1), std::memory_order_relaxed);
                } else if ((st & kCommitted) && !(st & kTracked)) {
                    g_page_state[pg].fetch_or(kTracked, std::memory_order_relaxed);
                    protect = true;
                }
                if (protect && run_n && run + run_n == pg) {
                    ++run_n;
                } else if (protect) {
                    protect_pages(run, run_n, PROT_READ);
                    run = pg;
                    run_n = 1;
                }
            }
            protect_pages(run, run_n, PROT_READ);
        }
        epoch_.store(e, std::memory_order_release);
        scan_pages_.fetch_add(stamped, std::memory_order_relaxed);
        scans_.fetch_add(1, std::memory_order_relaxed);
        std::this_thread::sleep_for(std::chrono::microseconds(500));
    }
}

bool GuestMemory::written_since(uint32_t addr, uint32_t size, uint32_t since) const {
    if (!size) return false;
    const uint64_t end = std::min<uint64_t>(uint64_t{addr} + size, kSpace);
    for (uint64_t p = addr >> g_shift; p < (end + page_mask()) >> g_shift; ++p)
        if (page_epoch_[p].load(std::memory_order_relaxed) > since) return true;
    return false;
}

GuestMemory::~GuestMemory() {
    stop_ = true;
    if (scanner_.joinable()) scanner_.join();
    if (base_) munmap(base_, kSpace);
    if (g_base == base_) g_base = nullptr;
}

bool GuestMemory::commit(uint32_t addr, uint32_t size) {
    const size_t first = addr >> g_shift, last = (size_t{addr} + size + page_mask()) >> g_shift;
    if (mprotect(base_ + (first << g_shift), (last - first) << g_shift, PROT_READ | PROT_WRITE) != 0) return false;
    for (size_t p = first; p < last; ++p) g_page_state[p].store(kCommitted, std::memory_order_relaxed);
    return true;
}

bool GuestMemory::committed(uint32_t addr) const { return g_page_state[addr >> g_shift].load(std::memory_order_relaxed) & kCommitted; }

std::vector<uint32_t> GuestMemory::find(const uint8_t* pattern, size_t n, uint32_t from, uint32_t to, size_t max) const {
    std::vector<uint32_t> out;
    if (!n) return out;
    const std::boyer_moore_horspool_searcher search(pattern, pattern + n);
    const auto readable = [&](uint64_t page) {
        if (!(g_page_state[page].load(std::memory_order_relaxed) & kCommitted)) return false;
        std::lock_guard g(g_watch_m);  // diagnostic watches are inaccessible
        for (const Watch& w : g_watches)
            if ((page << g_shift) >= w.begin && (page << g_shift) < w.end) return false;
        return true;
    };
    for (uint64_t at = from >> g_shift; at < (uint64_t{to} + page_mask()) >> g_shift && out.size() < max;) {
        if (!readable(at)) {
            ++at;
            continue;
        }
        uint64_t end = at + 1;
        while (end < (uint64_t{to} + page_mask()) >> g_shift && readable(end)) ++end;
        const uint8_t* lo = base_ + std::max<uint64_t>(at << g_shift, from);
        const uint8_t* hi = base_ + std::min<uint64_t>(end << g_shift, to);
        for (const uint8_t* p = lo; out.size() < max;) {
            p = std::search(p, hi, search);
            if (p == hi) break;
            out.push_back(static_cast<uint32_t>(p - base_));
            ++p;
        }
        at = end;
    }
    return out;
}

uint32_t GuestMemory::alloc_hle(uint32_t size, uint32_t align) {
    uint32_t a = (hle_next_ + align - 1) & ~(align - 1);
    if (a + size > layout::kHleEnd || a + size < a) throw std::runtime_error("HLE guest object area exhausted");
    hle_next_ = a + size;
    return a;
}

void GuestMemory::watch(uint32_t addr, uint32_t size, uint32_t tag) {
    const uint32_t m = static_cast<uint32_t>(page_mask());
    const uint32_t begin = (addr + m) & ~m, end = (addr + size) & ~m;
    if (end <= begin) return;
    std::lock_guard g(g_watch_m);
    for (const Watch& w : g_watches)
        if (w.begin < end && begin < w.end) return;
    if (mprotect(base_ + begin, end - begin, PROT_NONE) != 0) return;
    g_watches.push_back({begin, end, tag});
}

void GuestMemory::install_fault_reporter() { g_report = true; }
