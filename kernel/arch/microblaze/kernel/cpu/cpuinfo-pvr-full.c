

#include <linux/init.h>
#include <linux/string.h>
#include <asm/pvr.h>
#include <asm/cpuinfo.h>


#define CI(c, p) { ci->c = PVR_##p(pvr); }

#if defined(CONFIG_EARLY_PRINTK) && defined(CONFIG_SERIAL_UARTLITE_CONSOLE)
#define err_printk(x) \
	early_printk("ERROR: Microblaze " x "-different for PVR and DTS\n");
#else
#define err_printk(x) \
	printk(KERN_INFO "ERROR: Microblaze " x "-different for PVR and DTS\n");
#endif

void set_cpuinfo_pvr_full(struct cpuinfo *ci, struct device_node *cpu)
{
	struct pvr_s pvr;
	int temp; /* for saving temp value */
	get_pvr(&pvr);

	CI(ver_code, VERSION);
	if (!ci->ver_code) {
		printk(KERN_ERR "ERROR: MB has broken PVR regs "
						"-> use DTS setting\n");
		return;
	}

	temp = PVR_USE_BARREL(pvr) | PVR_USE_MSR_INSTR(pvr) |\
		PVR_USE_PCMP_INSTR(pvr) | PVR_USE_DIV(pvr);
	if (ci->use_instr != temp)
		err_printk("BARREL, MSR, PCMP or DIV");
	ci->use_instr = temp;

	temp = PVR_USE_HW_MUL(pvr) | PVR_USE_MUL64(pvr);
	if (ci->use_mult != temp)
		err_printk("HW_MUL");
	ci->use_mult = temp;

	temp = PVR_USE_FPU(pvr) | PVR_USE_FPU2(pvr);
	if (ci->use_fpu != temp)
		err_printk("HW_FPU");
	ci->use_fpu = temp;

	ci->use_exc = PVR_OPCODE_0x0_ILLEGAL(pvr) |\
			PVR_UNALIGNED_EXCEPTION(pvr) |\
			PVR_ILL_OPCODE_EXCEPTION(pvr) |\
			PVR_IOPB_BUS_EXCEPTION(pvr) |\
			PVR_DOPB_BUS_EXCEPTION(pvr) |\
			PVR_DIV_ZERO_EXCEPTION(pvr) |\
			PVR_FPU_EXCEPTION(pvr) |\
			PVR_FSL_EXCEPTION(pvr);

	CI(pvr_user1, USER1);
	CI(pvr_user2, USER2);

	CI(mmu, USE_MMU);

	CI(use_icache, USE_ICACHE);
	CI(icache_tagbits, ICACHE_ADDR_TAG_BITS);
	CI(icache_write, ICACHE_ALLOW_WR);
	ci->icache_line_length = PVR_ICACHE_LINE_LEN(pvr) << 2;
	CI(icache_size, ICACHE_BYTE_SIZE);
	CI(icache_base, ICACHE_BASEADDR);
	CI(icache_high, ICACHE_HIGHADDR);

	CI(use_dcache, USE_DCACHE);
	CI(dcache_tagbits, DCACHE_ADDR_TAG_BITS);
	CI(dcache_write, DCACHE_ALLOW_WR);
	ci->dcache_line_length = PVR_DCACHE_LINE_LEN(pvr) << 2;
	CI(dcache_size, DCACHE_BYTE_SIZE);
	CI(dcache_base, DCACHE_BASEADDR);
	CI(dcache_high, DCACHE_HIGHADDR);

	temp = PVR_DCACHE_USE_WRITEBACK(pvr);
	if (ci->dcache_wb != temp)
		err_printk("DCACHE WB");
	ci->dcache_wb = temp;

	CI(use_dopb, D_OPB);
	CI(use_iopb, I_OPB);
	CI(use_dlmb, D_LMB);
	CI(use_ilmb, I_LMB);
	CI(num_fsl, FSL_LINKS);

	CI(irq_edge, INTERRUPT_IS_EDGE);
	CI(irq_positive, EDGE_IS_POSITIVE);

	CI(area_optimised, AREA_OPTIMISED);

	CI(hw_debug, DEBUG_ENABLED);
	CI(num_pc_brk, NUMBER_OF_PC_BRK);
	CI(num_rd_brk, NUMBER_OF_RD_ADDR_BRK);
	CI(num_wr_brk, NUMBER_OF_WR_ADDR_BRK);

	CI(fpga_family_code, TARGET_FAMILY);

	/* take timebase-frequency from DTS */
	ci->cpu_clock_freq = fcpu(cpu, "timebase-frequency");
}