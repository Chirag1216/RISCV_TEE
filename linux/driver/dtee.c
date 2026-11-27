// SPDX-License-Identifier: GPL-2.0
/*
 * /dev/dtee: the normal world's door to the trusted verifier.
 *
 * This driver is UNTRUSTED. It only carries commands: it copies each one into
 * a page shared with the monitor and makes the secure call (an SBI ecall).
 * The monitor and the verifier re-check everything it sends.
 */
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>
#include <linux/gfp.h>
#include <linux/io.h>
#include <linux/string.h>
#include <asm/sbi.h>

#include "dtee_ioctl.h"
#include "layout.h"

static unsigned long shared_page;       /* kernel address of the page shared with the monitor */
static DEFINE_MUTEX(dtee_lock);         /* one command in the shared page at a time */

static long dtee_submit(unsigned long arg)
{
	struct dtee_submit s;
	struct sbiret ret;

	if (copy_from_user(&s, (void __user *)arg, sizeof(s)))
		return -EFAULT;

	mutex_lock(&dtee_lock);
	memcpy((void *)shared_page, &s.cmd, sizeof(s.cmd));
	ret = sbi_ecall(SBI_EID_DTEE, DTEE_FID_SUBMIT, 0, 0, 0, 0, 0, 0);
	mutex_unlock(&dtee_lock);

	if (ret.error)
		return -EIO;

	s.verdict = (u32)ret.value;
	if (copy_to_user((void __user *)arg, &s, sizeof(s)))
		return -EFAULT;
	return 0;
}

static long dtee_stats(unsigned long arg)
{
	struct dtee_stats st;

	st.accepted = sbi_ecall(SBI_EID_DEBUG, DEBUG_FID_ACCEPTED, 0, 0, 0, 0, 0, 0).value;
	st.failsafe = sbi_ecall(SBI_EID_DEBUG, DEBUG_FID_FAILSAFE, 0, 0, 0, 0, 0, 0).value;

	if (copy_to_user((void __user *)arg, &st, sizeof(st)))
		return -EFAULT;
	return 0;
}

/*
 * Test only: act like a compromised kernel and write the actuator directly.
 * PMP must stop it. __put_kernel_nofault() is a store with a recovery point:
 * if the store faults, execution continues at the given label instead of
 * crashing the kernel.
 */
static int actuator_write_is_blocked(void)
{
	u64 value = 0x1234;

	__put_kernel_nofault(phys_to_virt(ACT_BASE), &value, u64, fault);
	return 0;
fault:
	return 1;
}

static long dtee_test_bypass(unsigned long arg)
{
	int blocked = actuator_write_is_blocked();

	if (copy_to_user((void __user *)arg, &blocked, sizeof(blocked)))
		return -EFAULT;
	return 0;
}

static long dtee_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case DTEE_IOC_SUBMIT:
		return dtee_submit(arg);
	case DTEE_IOC_STATS:
		return dtee_stats(arg);
	case DTEE_IOC_TEST_BYPASS:
		return dtee_test_bypass(arg);
	}
	return -ENOTTY;
}

static const struct file_operations dtee_fops = {
	.owner          = THIS_MODULE,
	.unlocked_ioctl = dtee_ioctl,
};

static struct miscdevice dtee_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name  = "dtee",
	.fops  = &dtee_fops,
};

static int __init dtee_init(void)
{
	struct sbiret ret;
	int err;

	if (sbi_probe_extension(SBI_EID_DTEE) <= 0) {
		pr_err("dtee: the monitor does not offer the secure call\n");
		return -ENODEV;
	}

	shared_page = get_zeroed_page(GFP_KERNEL);
	if (!shared_page)
		return -ENOMEM;

	/* Tell the monitor where the shared page is. It accepts this once per boot. */
	ret = sbi_ecall(SBI_EID_DTEE, DTEE_FID_REGISTER, virt_to_phys((void *)shared_page),
			0, 0, 0, 0, 0);
	if (ret.error) {
		pr_err("dtee: the monitor refused the shared page (error %ld)\n", ret.error);
		free_page(shared_page);
		return -EIO;
	}

	err = misc_register(&dtee_dev);
	if (err) {
		/* The page stays allocated: the monitor still holds its address. */
		return err;
	}

	pr_info("dtee: ready, shared page at physical address 0x%llx\n",
		(unsigned long long)virt_to_phys((void *)shared_page));
	return 0;
}

static void __exit dtee_exit(void)
{
	misc_deregister(&dtee_dev);
	/* The shared page is deliberately not freed: the monitor keeps using its
	 * address until the next boot, so it must never be handed to anyone else. */
}

module_init(dtee_init);
module_exit(dtee_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Secure-call driver for the rvtee monitor");
