extern "C"
{
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <math.h>
#ifndef INFINITY
# define INFINITY   (__builtin_inff())
#endif
#define HAVE_STDARG_H
#include "../86box.h"
#include "cpu.h"
#include "x86.h"
#include "x87.h"
#include "../nmi.h"
#include "../mem.h"
#include "../pic.h"
#include "../pit.h"
#include "../timer.h"
#include "../floppy/fdd.h"
#include "../floppy/fdc.h"

#define CPU_BLOCK_END()

uint16_t flags,eflags;
uint32_t oldds,oldss,olddslimit,oldsslimit,olddslimitw,oldsslimitw;

x86seg gdt,ldt,idt,tr;
x86seg _cs,_ds,_es,_ss,_fs,_gs;
x86seg _oldds;

uint32_t cr2, cr3, cr4;
uint32_t dr[8];

int trap;

#include "x86_flags.h"

#ifdef ENABLE_386_REF_LOG
int x386_ref_do_log = ENABLE_386_REF_LOG;
#endif


static void
x386_ref_log(const char *fmt, ...)
{
#ifdef ENABLE_386_REF_LOG
    va_list ap;

    if (x386_ref_do_log) {
    	va_start(ap, fmt);
    	pclog_ex(fmt, ap);
    	va_end(ap);
    }
#endif
}


enum class translate_kind
{
    TRANSLATE_READ, TRANSLATE_WRITE, TRANSLATE_EXEC
};


enum class exception_type
{
    FAULT, TRAP, ABORT
};


struct cpu_exception
{
    exception_type type;
    uint8_t fault_type;
    uint32_t error_code;
    bool error_code_valid;
    cpu_exception(exception_type _type, uint8_t _fault_type, uint32_t errcode, bool errcodevalid)
    : type(_type)
    , fault_type(_fault_type)
    , error_code(errcode)
    , error_code_valid(errcodevalid) {}

    cpu_exception(const cpu_exception& other) = default;
};


void
type_check_ref(x86seg* segment, uint32_t offset, translate_kind kind)
{
    bool system_seg = !((segment->flags_ref >> 4) & 1);
    bool executable = (segment->flags_ref >> 3) & 1;

    if (!system_seg) {
	if (executable) {
		bool readable = (segment->flags_ref >> 1) & 1;
		switch(kind) {
			case translate_kind::TRANSLATE_READ:
				if (!readable)
					throw cpu_exception(exception_type::FAULT, ABRT_GPF, 0, true);
				break;
			case translate_kind::TRANSLATE_WRITE:
				throw cpu_exception(exception_type::FAULT, ABRT_GPF, 0, true);
				break;
			default:
				break;
		}
	} else {
		bool writable = (segment->flags_ref >> 1) & 1;
		switch(kind) {
			case translate_kind::TRANSLATE_WRITE:
				if (!writable)
					throw cpu_exception(exception_type::FAULT, ABRT_GPF, 0, true);
				break;
			case translate_kind::TRANSLATE_EXEC:
				throw cpu_exception(exception_type::FAULT, ABRT_GPF, 0, true);
				break;
			default:
				break;
		}
	}
    } else {
	// TODO
	x386_ref_log("type_check_ref called with a system-type segment! Execution correctness is not guaranteed past this point!\n");
    }
}


void
limit_check_ref(x86seg* segment, uint32_t offset, translate_kind kind)
{
    uint8_t fault_type = ABRT_GPF;
    uint32_t addr = offset & ((1 << (32 - 12)) - 1);

    if (segment == &_ss)
	fault_type = ABRT_SS;

    switch(kind) {
	case translate_kind::TRANSLATE_READ:
	case translate_kind::TRANSLATE_WRITE:
    	{
		// Data segment.
		bool expand_down = (segment->flags_ref >> 2) & 1;
		bool big_seg = (segment->flags_ref >> 14) & 1;		// TODO: Not sure if this is ever used. Test this!
		bool granularity = (segment->flags_ref >> 15) & 1;
		uint32_t lower_bound;
		uint32_t upper_bound;
		if (big_seg != granularity)
			x386_ref_log("B bit doesn't equal granularity bit! Execution correctness is not guaranteed past this point!\n");
		if (expand_down) {
			if (granularity) {
				lower_bound = ((addr << 12) | 0xfff) + 1;
				upper_bound = 0xffffffff;	//4G - 1
			} else {
				lower_bound = addr + 1;
				upper_bound = 0xffff;		//64K - 1
			}
		} else {
			lower_bound = 0;
			if (granularity)
				upper_bound = (addr << 12) | 0xfff;
			else
				upper_bound = addr;
		}
		if ((addr < lower_bound) || (addr > upper_bound))
			throw cpu_exception(exception_type::FAULT, fault_type, 0, true);
		break;
    	}
	default:
    	{
    		bool granularity = (segment->flags_ref >> 15) & 1;
		uint32_t limit;

		if (granularity)
			limit = (addr << 12) | 0xfff;
		else
			limit = addr;

		if (addr > limit)
			throw cpu_exception(exception_type::FAULT, fault_type, 0, true);
		break;
    	}
    }
}


void
privilege_check_ref(x86seg* segment, uint32_t offset, translate_kind kind)
{
    bool system_seg = !((segment->flags_ref >> 4) & 1);
    bool executable = (segment->flags_ref >> 3) & 1;

    if (!system_seg) {
	if (executable) {
		bool conforming = (segment->flags_ref >> 2) & 1;
		if (conforming)
			return;
		else {
			int seg_rpl = segment->seg & 3;
			int dpl = (segment->flags_ref >> 5) & 3;
			if (dpl < CPL)
				throw cpu_exception(exception_type::FAULT, ABRT_GPF, 0, true);
			if (dpl < seg_rpl)
				throw cpu_exception(exception_type::FAULT, ABRT_GPF, 0, true);
		}
	} else {
		int seg_rpl = segment->seg & 3;
		int dpl = (segment->flags_ref >> 5) & 3;
		if (dpl < CPL)
			throw cpu_exception(exception_type::FAULT, ABRT_GPF, 0, true);
		if (dpl < seg_rpl)
			throw cpu_exception(exception_type::FAULT, ABRT_GPF, 0, true);
	}
    } else {
	// TODO
	x386_ref_log("privilege_check_ref called with a system-type segment! Execution correctness is not guaranteed past this point!\n");
    }
}

bool
page_privilege_check(uint32_t page_dir_entry, uint32_t page_tbl_entry)
{
    bool page_dir_user = (page_dir_entry >> 2) & 1;
    bool page_tbl_user = (page_tbl_entry >> 2) & 1;

    // If either page table entry is supervisor mode, the page is in supervisor mode.
    return (!page_dir_user || !page_tbl_user) ? true : false;
}


bool
page_writability_check(uint32_t page_dir_entry, uint32_t page_tbl_entry, bool is_user_page)
{
    bool page_dir_writable = (page_dir_entry >> 1) & 1;
    bool page_tbl_writable = (page_tbl_entry >> 1) & 1;

    // If we're in a supervisor page, it's writable.
    if (!is_user_page)
	return true;
    // If and only if both page table entries are writable, the page is writable.
    else if (page_dir_writable && page_tbl_writable)
	return true;
    else
	return false;
}


uint32_t
translate_addr_ref(x86seg* segment, uint32_t offset, translate_kind kind)
{
    // Segment-level checks.
    type_check_ref(segment, offset, kind);
    limit_check_ref(segment, offset, kind);
    privilege_check_ref(segment, offset, kind);

    uint32_t addr = segment->base + offset;

    if (!(cr0 >> 31))
	return addr;

    //And now the paging stuff.
    uint32_t page_dir_base = cr3 & 0xfffff000;
    uint32_t page_dir_entry_addr = ((addr >> 22) & 0x3ff) << 2;
    page_dir_entry_addr += page_dir_base;

    uint32_t page_dir_entry = mem_read_raml_dma(page_dir_entry_addr);

    uint32_t page_tbl_base = page_dir_entry & 0xfffff000;
    uint32_t page_tbl_entry_addr = ((addr >> 12) & 0x3ff) << 2;
    page_tbl_entry_addr += page_tbl_base;

    uint32_t page_tbl_entry = mem_read_raml_dma(page_tbl_entry_addr);

    bool page_present = (page_dir_entry & 1) && (page_tbl_entry & 1);

    if (!page_present)
    {
	cr2 = addr;
	uint32_t error = 0;
	if (kind == translate_kind::TRANSLATE_WRITE)
		error |= (1 << 1);
	if (CPL > 0)
		error |= (1 << 2);
	throw cpu_exception(exception_type::FAULT, ABRT_PF, error, true);
    }

    bool is_user_page = page_privilege_check(page_dir_entry, page_tbl_entry);

    if (CPL > 0)
    {
    	if (!is_user_page) {
		cr2 = addr;
		uint32_t error = 0x5;
		if (kind == translate_kind::TRANSLATE_WRITE)
			error |= (1 << 1);
		throw cpu_exception(exception_type::FAULT, ABRT_PF, error, true);
	}
    }

    if (kind == translate_kind::TRANSLATE_WRITE)
    {
	bool is_writable_page = page_writability_check(page_dir_entry, page_tbl_entry, is_user_page);
	if (!is_writable_page) {
		cr2 = addr;
		uint32_t error = 3;
		if (CPL > 0)
			error |= (1 << 2);
		throw cpu_exception(exception_type::FAULT, ABRT_PF, error, true);
	}
    }

    // Now that ALL the checks are finally done, return a translated address.
    addr &= 0xfff;
    page_tbl_entry &= 0xfffff000;
    return page_tbl_entry + addr;
}


uint8_t
readmemb_ref(x86seg* segment, uint32_t offset)
{
    uint32_t addr = translate_addr_ref(segment, offset, translate_kind::TRANSLATE_READ);
    return mem_readb_phys_dma(addr);
}

uint16_t
readmemw_ref(x86seg* segment, uint32_t offset)
{
    uint32_t addr = translate_addr_ref(segment, offset + 1, translate_kind::TRANSLATE_READ) - 1;
    return mem_readw_phys_dma(addr);
}

uint32_t
readmeml_ref(x86seg* segment, uint32_t offset)
{
    uint32_t addr = translate_addr_ref(segment, offset + 3, translate_kind::TRANSLATE_READ) - 3;
    return mem_readl_phys_dma(addr);
}

void
writememb_ref(x86seg* segment, uint32_t offset, uint8_t data)
{
    uint32_t addr = translate_addr_ref(segment, offset, translate_kind::TRANSLATE_READ);
    mem_writeb_phys_dma(addr, data);
}

void
writememw_ref(x86seg* segment, uint32_t offset, uint16_t data)
{
    uint32_t addr = translate_addr_ref(segment, offset + 1, translate_kind::TRANSLATE_READ) - 1;
    mem_writew_phys_dma(addr, data);
}

void
writememl_ref(x86seg* segment, uint32_t offset, uint32_t data)
{
    uint32_t addr = translate_addr_ref(segment, offset + 3, translate_kind::TRANSLATE_READ) - 3;
    mem_writel_phys_dma(addr, data);
}

#define OP_TABLE(name) ops_ ## name

#define CLOCK_CYCLES(c) cycles -= (c)
#define CLOCK_CYCLES_ALWAYS(c) cycles -= (c)

#undef readmemb
#undef readmemw
#undef readmeml
#undef writememb
#undef writememw
#undef writememl

#define readmemb readmemb_ref
#define readmemw readmemw_ref
#define readmeml readmeml_ref
#define writememb writememb_ref
#define writememw writememw_ref
#define writememl writememl_ref

#include "x86_ops.h"

void
fault_ref(uint8_t fault_type, uint32_t error_code, bool error_code_valid)
{
    //TODO
    x386_ref_log("Fault type %02x at %04x:%08x!\n", fault_type, CS, cpu_state.pc);
}

void
exec386_ref(int cycs)
{
    uint8_t temp;
    uint32_t addr;
    int tempi;
    int cycdiff;
    int oldcyc;

    cycles += cycs;

    while (cycles > 0) {
    	int cycle_period = (timer_count >> TIMER_SHIFT) + 1;

    	x86_was_reset = 0;
    	cycdiff=0;
    	oldcyc=cycles;
	timer_start_period(cycles << TIMER_SHIFT);

    	while (cycdiff < cycle_period)
    	{
		oldcs = CS;
    		cpu_state.oldpc = cpu_state.pc;
		oldcpl = CPL;
		cpu_state.op32 = use32;

		x86_was_reset = 0;

		cpu_state.ea_seg = &_ds;
		cpu_state.ssegs = 0;

		try {
    			uint32_t fetchdat = readmeml_ref(&_cs, cpu_state.pc);

	    		trap = flags & T_FLAG;
    			opcode = fetchdat & 0xFF;
    			fetchdat >>= 8;

    			cpu_state.pc++;
    			x86_opcodes[(opcode | cpu_state.op32) & 0x3ff](fetchdat);
    			if (x86_was_reset)
    				break;
    			if(x86_was_reset) break;
		} catch(const cpu_exception& e) {
    			if(e.type == exception_type::FAULT) {
    				fault_ref(e.fault_type, e.error_code, e.error_code_valid);
	    		}
		}
    		cycdiff=oldcyc-cycles;
    		ins++;
    	}

	timer_end_period(cycles << TIMER_SHIFT);
    }
}
}