#include <uapi/machine/cpufunc.h>
#include <uapi/machine/io.h>
#include <uapi/machine/trap.h>
#include <uapi/console.h>
#include <uapi/elf.h>
#include <uapi/pci.h>
#include <uapi/pcidb.h>
#include <uapi/pcireg.h>
#include <machine/memlayout.h>
#include <bootmem.h>
#include <cpuid.h>
#include <init.h>
#include <iommu.h>
#include <multiboot.h>
#include <string.h>
#include <time.h>
#include "device.h"
#include "ioport.h"
#include "memlayout.h"
#include "proc.h"
#include "vm.h"
#include "invariants.h"
#include <libs/stdio.h>
#include <libs/riscv.h>

static void vm_init(void);
static void user_init(pid_t);

static pn_t boot_alloc(void);
static pn_t page_walk(pid_t pid, uintptr_t va);
static void setup_kernel_map(pid_t pid);

static void arch_init(void);
static void arch_user_init(pid_t pid);

static void print_config(void);
static void print_version(void);

void main(void)
{
    libs_cprintf("%s\n\n", "hv6-riscv os is loading ...\n");
    libs_cprintf("Hello World\n");
	print_config();
	libs_cprintf("init_syscalls:\n");
	init_syscalls();
    libs_cprintf("arch_init:\n");
    arch_init();
    libs_cprintf("vm_init:\n");
    vm_init();
    libs_cprintf("user_init:\n");
    libs_cprintf("pages = 0x%x\n", pages);
    user_init(INITPID);

    print_config();
    print_version();
    /// check_invariants();

    libs_cprintf("run_current:\n");
    write_csr(dcsr,1);//open RISCVEMU debug
    // run_current();
    hvm_run_user_init(get_page(get_proc(INITPID)->hvm));
    panic(NULL);
}

static void print_config(void)
{
    pr_info("CONFIG_FPU           = %d\n", CONFIG_FPU);
    pr_info("CONFIG_PREEMPT       = %d\n", CONFIG_PREEMPT);
    pr_info("CONFIG_PREEMPT_TIMER = %d\n", CONFIG_PREEMPT_TIMER);
    pr_info("CONFIG_VPID          = %d\n", CONFIG_VPID);
    pr_info("CONFIG_MEMFS         = %d\n", CONFIG_MEMFS);
    pr_info("CONFIG_NVME          = %d\n", CONFIG_NVME);
    pr_info("CONFIG_CGA           = %d\n", CONFIG_CGA);
}

static void print_version(void)
{
#if !defined(COMMIT_HASH) || !defined(COMMIT_BRANCH)
#error COMMIT_HASH and COMMIT_BRANCH must be defined
#endif
    pr_info("commit " STRINGIFY(COMMIT_HASH) " on " STRINGIFY(COMMIT_BRANCH) "\n");
}

static void vm_init(void)
{
    size_t i;

    /* seal bootmem */
    bootmem_seal();

    FREELIST_INIT(page_desc_table, link, 0);
    for (i = 1; i < NPAGE; ++i)
        FREELIST_ADD_TAIL(page_desc_table, link, i, 0);

    ///write_csr(satp, (0x8000000000000000) | (boot_cr3 >> RISCV_PGSHIFT));///enable_paging

#if 0
	size_t i;

	for (i = 0; i < NPAGE; ++i) {
		struct page_desc *desc = get_page_desc(i);
		bool usable;

		usable = bootmem_usable((physaddr_t)get_page(i), PAGE_SIZE);
		if (!usable) {
			pr_info("vm: reserve page %zu\n", i);
			desc->type = PAGE_TYPE_RESERVED;
		}
	}
#endif
}

static uintptr_t load_elf(pid_t pid, void *p)
{
    struct elf64_ehdr *ehdr = p;
    struct elf64_phdr *phdr;
    size_t i, n;
    int r;

    assert(IS_ELF(*ehdr), "must be a valid elf file");
    phdr = (void *)ehdr + ehdr->e_phoff;
    for (i = 0, n = ehdr->e_phnum; i < n; ++i, ++phdr) {
        size_t off, va, filesz, memsz;
        void *binary;

        if (phdr->p_type != PT_LOAD)
            continue;
        va = phdr->p_vaddr;
        filesz = phdr->p_filesz;
        memsz = phdr->p_memsz;
        assert(filesz <= memsz, "file size must be no larger than memory size");
        assert(va + memsz >= va, "must not overflow in elf");
        /* XXX: too restrictive? */
        assert(va % PAGE_SIZE == 0, "segment is not page-aligned");
        binary = (void *)ehdr + phdr->p_offset;
        for (off = 0; off < memsz; off += PAGE_SIZE, va += PAGE_SIZE) {
            pn_t pt, frame;
            pte_t perm = PTE_P | PTE_W | PTE_R | PTE_X | PTE_U;

            pt = page_walk(pid, va);
            frame = boot_alloc();
            r = sys_alloc_frame(pid, pt, PT_INDEX(va), frame, perm);
            assert(r == 0, "sys_alloc_frame");
            /* no need to clear bss as pages are zeroized */
            if (off >= filesz)
                continue;
            memcpy(get_page(frame), binary + off, min(filesz - off, (size_t)PAGE_SIZE));
        }
    }

    return ehdr->e_entry;
}

static void user_init(pid_t pid)
{
    extern char _binary_init_start[], _binary_ulib_start[];
    size_t i, n;
    pn_t page_table_root, stack;
    pn_t hvm;
    int r;
    uintptr_t user_entry, ulib_entry;
    void *phvm;

    current = pid;

    page_table_root = boot_alloc();
    stack = boot_alloc();
    hvm = boot_alloc();
    libs_cprintf("user_init：alloc_proc(pid, page_table_root, stack, hvm)\n");
    r = alloc_proc(pid, page_table_root, stack, hvm);
    assert(r == 0, "alloc_proc");
    setup_kernel_map(pid);

    libs_cprintf("user_init：load_elf(pid, _binary_init_start)\n");
    /* load & allocate pages for init & ulib */
    user_entry = load_elf(pid, _binary_init_start);
    ulib_entry = load_elf(pid, _binary_ulib_start);
    libs_cprintf("user_init：end load_elf(pid, *)\n");

    /* map page_desc_table as read-only */
    n = bytes_to_pages(NPAGE * sizeof(struct page_desc));
    for (i = 0; i < n; ++i) {
        pn_t pt;
        uintptr_t va;
        pte_t perm = PTE_P | PTE_R | PTE_U;

        va = UPAGES_START + i * PAGE_SIZE;
        pt = page_walk(pid, va);
        r = sys_map_page_desc(pid, pt, PT_INDEX(va), i, perm);
        assert(r == 0, "map_page_desc");
    }

    arch_user_init(pid);
#if ENABLED(CONFIG_FPU)
    fpu_user_init(get_page(stack) + PAGE_SIZE - sizeof(struct xsave_area));
#endif

    phvm = get_page(hvm);
    hvm_user_init(phvm, user_entry, ulib_entry);
    hvm_set_pid(phvm, pid);
    get_proc(pid)->state = PROC_RUNNING;
    FREELIST_INIT(proc_table, ready, pid);
}

/* return a new free page */
static pn_t boot_alloc(void)
{
    static pn_t i;

    while (i < NPAGE) {
        pn_t j = i++;

        if (is_page_type(j, PAGE_TYPE_FREE))
            return j;
    }
    panic("oom");
}

static pn_t page_walk(pid_t pid, uintptr_t va)
{
    uintptr_t entry;
    size_t pml4_pn, pdpt_pn, pd_pn, pt_pn;
    size_t pml4_index, pdpt_index, pd_index, pt_index;
    pte_t *pml4, *pdpt, *pd, *pt;
    pte_t perm = PTE_P | PTE_W | PTE_R;
    int r;

    pml4_pn = get_proc(pid)->page_table_root;

    pml4 = get_page(pml4_pn);
    pml4_index = PML4_INDEX(va);
    entry = pml4[pml4_index];
    if (!pte_valid(entry)) {
        pdpt_pn = boot_alloc();
        r = sys_alloc_pdpt(pid, pml4_pn, pml4_index, pdpt_pn, perm);
        assert(r == 0, "alloc_pdpt");
    } else {
        pdpt_pn = pfn_to_pn(PTE_ADDR(entry) / PAGE_SIZE);
    }

    pdpt = get_page(pdpt_pn);
    pdpt_index = PDPT_INDEX(va);
    entry = pdpt[pdpt_index];
    if (!pte_valid(entry)) {
        pd_pn = boot_alloc();
        r = sys_alloc_pd(pid, pdpt_pn, pdpt_index, pd_pn, perm);
        assert(r == 0, "alloc_pd");
    } else {
        pd_pn = pfn_to_pn(PTE_ADDR(entry) / PAGE_SIZE);
    }

    pd = get_page(pd_pn);
    pd_index = PD_INDEX(va);
    entry = pd[pd_index];
    if (!pte_valid(entry)) {
        pt_pn = boot_alloc();
        r = sys_alloc_pt(pid, pd_pn, pd_index, pt_pn, perm);
        assert(r == 0, "alloc_pt");
    } else {
        pt_pn = pfn_to_pn(PTE_ADDR(entry) / PAGE_SIZE);
    }

    pt = get_page(pt_pn);
    pt_index = PT_INDEX(va);
    entry = pt[pt_index];
    assert(!(pte_valid(entry)), "this address already mapped");

    return pt_pn;
}

static void setup_kernel_map(pid_t pid)
{
	assert(KERNSTART % PAGE_SIZE == 0, "KERNBASE % PAGE_SIZE must be 0");
	uintptr_t va;
	pte_t perm = PTE_P | PTE_W | PTE_R | PTE_X;
	for(va = KERNSTART; va < KERNEND; va += PAGE_SIZE) {
		pn_t pt = page_walk(pid, va);
		assert((va - (uintptr_t)pages) % PAGE_SIZE == 0, "(va - pages) % PAGE_SIZE != 0");
		pte_t* pt_page = get_page(pt);
		pt_page[PT_INDEX(va)] = ((va >> PAGE_SHIFT) << PTE_PFN_SHIFT) | perm;
		enum page_type type = get_page_desc(get_proc(pid)->page_table_root)->type;
		if (type == PAGE_TYPE_X86_PML4 || type == PAGE_TYPE_X86_PDPT || type == PAGE_TYPE_X86_PD || type == PAGE_TYPE_X86_PT) {
			pt_page[PT_INDEX(va)] = ((va >> PAGE_SHIFT) << PTE_PFN_SHIFT) | perm | PTE_U;
		}
	}
}

static struct pci_driver drivers[] = {
    PCI_DRIVER_CLASS(PCI_CLASS_MASS_STORAGE, PCI_SUBCLASS_MASS_STORAGE_NVM, PCI_INTERFACE_NVM_NVME),
    /* e1000 (qemu) */
    PCI_DRIVER_ID(PCI_VENDOR_INTEL, PCI_PRODUCT_E1000_82540EM),
    /* e1000e (qemu) */
    PCI_DRIVER_ID(PCI_VENDOR_INTEL, PCI_PRODUCT_E1000_82574L),
    /* I217 (sysv) */
    PCI_DRIVER_ID(PCI_VENDOR_INTEL, PCI_PRODUCT_E1000_PCH_LPT_I217_LM),
};

static void arch_init(void)
{
    size_t i;
    libs_cprintf("porte9_init\n");
    porte9_init();
    libs_cprintf("cga_init %x\n",pa2kva(CGA_START));
    cga_init(pa2kva(CGA_START));
    libs_cprintf("tsc_init\n");
    tsc_init();
    libs_cprintf("trap_init\n");
    trap_init();
    libs_cprintf("cpuid_init\n");
    ///cpuid_init();//unknown
    libs_cprintf("acpi_init\n");
    ///acpi_init();//unknown
    libs_cprintf("pmap_init\n");
    ///pmap_init();//unknown
    libs_cprintf("mtrr_init\n");
    ///mtrr_init();//useless
    libs_cprintf("xapic_init\n");
    ///xapic_init();//useless
    libs_cprintf("fpu_init\n");
    ///fpu_init();//temporarily disabled
    libs_cprintf("hvm_init\n");
    hvm_init();//useless
    libs_cprintf("iommu_init\n");
    iommu_init();
    libs_cprintf("iommu_early_set_dev_region %x %x\n",kva2pa(dmapages), kva2pa(dmapages + NDMAPAGE));
    iommu_early_set_dev_region(kva2pa(dmapages), kva2pa(dmapages + NDMAPAGE));

    /* allow access to some ports in user space */
    libs_cprintf("reserve_ports\n");
    reserve_ports(0, SZ_64K);
    libs_cprintf("allow_ports\n");
    allow_ports(PORT_COM1, 6);
    allow_ports(PORT_COM2, 6);
    allow_port(PORT_KBD_DATA);
    allow_port(PORT_KBD_STATUS);
    allow_ports(PORT_CRT, 2);

    libs_cprintf("reserve_vectors\n");
    /* reserve vectors: 0-31 */
    reserve_vectors(0, 32);

    libs_cprintf("set pci callbacks\n");
    /* set pci callbacks */
    #if 0///
    pci_register_func = device_add;

    for (i = 0; i < countof(drivers); ++i)
        pci_register_driver(&drivers[i]);

    pci_init();

    /* set preemption handler */
    hvm_preempt = preempt;
    /* set external interrupt handler */
    hvm_extintr = extintr;
    /* set triple fault handler */
    hvm_fault = fault;
    #endif

    /*
     * For syscall:
     *   cs: STAR[47:32]
     *   ds: cs + 8
     * No need to set sysret as we just use the regular ret.
     */
    ///wrmsr(MSR_STAR, (uint64_t)GDT_CS << 32);
    ///wrmsr(MSR_LSTAR, USYSCALL_START);
    /* no need to mask anything */
    ///wrmsr(MSR_SFMASK, 0);

    /* check fsgsbase */
    ///if (!cpuid_has(CPUID_FEATURE_FSGSBASE))
    ///    panic("no fsgsbase support");
    libs_cprintf("arch_init end.\n");
}

static void arch_user_init(pid_t pid)
{
    int r;
    /* map kernel command line */
    if (multiboot_info && (multiboot_info->flags & MULTIBOOT_INFO_CMDLINE)) {
        uintptr_t va = UCMDLINE;
        pn_t pt, frame;
        pte_t perm = PTE_P;

        pt = page_walk(pid, va);
        frame = boot_alloc();
        r = sys_alloc_frame(pid, pt, PT_INDEX(va), frame, perm);
        assert(r == 0, "sys_alloc_frame");
        strncpy(get_page(frame), (void *)(uintptr_t)multiboot_info->cmdline, PAGE_SIZE);
    }
}

/* for testing with qemu's -device isa-debug-exit */
void sys_debug_exit(int r)
{
    outw(0x501, r);
}
