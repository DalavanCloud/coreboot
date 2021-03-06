/*
 * This file is part of the coreboot project.
 *
 * Copyright (C) 2007-2009 coresystems GmbH
 *               2012 secunet Security Networks AG
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; version 2 of
 * the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <console/console.h>
#include <cpu/x86/mtrr.h>
#include <cpu/x86/mp.h>
#include <cpu/intel/microcode.h>
#include <cpu/intel/smm/gen1/smi.h>
#include <cpu/intel/common/common.h>

/* Parallel MP initialization support. */
static const void *microcode_patch;

static void pre_mp_init(void)
{
	/* Setup MTRRs based on physical address size. */
	x86_setup_mtrrs_with_detect();
	x86_mtrr_check();
}

static int get_cpu_count(void)
{
	const struct cpuid_result cpuid1 = cpuid(1);
	const char cores = (cpuid1.ebx >> 16) & 0xf;

	printk(BIOS_DEBUG, "CPU has %u cores.\n", cores);

	return cores;
}

/* the SMRR enable and lock bit need to be set in IA32_FEATURE_CONTROL
   to enable SMRR so configure IA32_FEATURE_CONTROL early on */
static void pre_mp_smm_init(void)
{
	smm_initialize();
}

#define SMRR_SUPPORTED (1 << 11)

static void per_cpu_smm_trigger(void)
{
	msr_t mtrr_cap = rdmsr(MTRR_CAP_MSR);
	if (cpu_has_alternative_smrr() && mtrr_cap.lo & SMRR_SUPPORTED) {
		set_feature_ctrl_vmx();
		msr_t ia32_ft_ctrl = rdmsr(IA32_FEATURE_CONTROL);
		/* We don't care if the lock is already setting
		   as our smm relocation handler is able to handle
		   setups where SMRR is not enabled here. */
		if (!IS_ENABLED(CONFIG_SET_IA32_FC_LOCK_BIT))
			printk(BIOS_INFO,
			       "Overriding CONFIG_SET_IA32_FC_LOCK_BIT to enable SMRR\n");
		ia32_ft_ctrl.lo |= (1 << 3) | (1 << 0);
		wrmsr(IA32_FEATURE_CONTROL, ia32_ft_ctrl);
	} else {
		set_vmx_and_lock();
	}

	/* Relocate the SMM handler. */
	smm_relocate();

	/* After SMM relocation a 2nd microcode load is required. */
	intel_microcode_load_unlocked(microcode_patch);
}

static void post_mp_init(void)
{
	/* Now that all APs have been relocated as well as the BSP let SMIs
	 * start flowing. */
	southbridge_smm_init();

	/* Lock down the SMRAM space. */
	smm_lock();
}

static const struct mp_ops mp_ops = {
	.pre_mp_init = pre_mp_init,
	.get_cpu_count = get_cpu_count,
	.get_smm_info = smm_info,
	.pre_mp_smm_init = pre_mp_smm_init,
	.per_cpu_smm_trigger = per_cpu_smm_trigger,
	.relocation_handler = smm_relocation_handler,
	.post_mp_init = post_mp_init,
};

void bsp_init_and_start_aps(struct bus *cpu_bus)
{
	if (mp_init_with_smm(cpu_bus, &mp_ops))
		printk(BIOS_ERR, "MP initialization failure.\n");
}
