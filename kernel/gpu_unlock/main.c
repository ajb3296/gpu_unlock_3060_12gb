// SPDX-License-Identifier: GPL-2.0-only
#include <linux/async.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/string.h>

#include "unlock_610.h"

static char *targets;
module_param(targets, charp, 0444);
MODULE_PARM_DESC(targets, "\"all\" (RTX 3090 only) or comma-separated PCI addresses");

/* Not included by targets=all: a physical-board validation is still required. */
static bool experimental_ga106;
module_param(experimental_ga106, bool, 0444);
MODULE_PARM_DESC(experimental_ga106, "Allow explicit RTX 3060 GA106 10de:2504 rev a1 targets (UNVALIDATED)");

#define RTX_3090 0x2204
#define RTX_3060_GA106_LHR 0x2504

static bool eligible(const struct pci_dev *pdev, bool all)
{
	if (pdev->device == RTX_3090)
		return true;
	return pdev->device == RTX_3060_GA106_LHR && pdev->revision == 0xa1 &&
		experimental_ga106 && !all;
}

static ASYNC_DOMAIN_EXCLUSIVE(unlock_domain);
static struct gpu_unlock_610_shared shared;

/* Runs concurrently for each card; consumes the reference it is given. */
static void unlock_one(void *data, async_cookie_t cookie)
{
	struct pci_dev *pdev = data;
	int ret;

	pci_dev_lock(pdev);
	ret = pdev->driver ? -EBUSY : gpu_unlock_610_unlock(pdev, &shared);
	pci_dev_unlock(pdev);
	if (ret)
		pci_err(pdev, "gpu_unlock: unlock failed (%d)\n", ret);
	else
		pci_info(pdev, "gpu_unlock: unlocked\n");
	pci_dev_put(pdev);
}

/* True when the comma-separated targets list names this PCI device. */
static bool listed(const char *name)
{
	const char *entry, *end;

	for (entry = targets; *entry; entry = *end ? end + 1 : end) {
		end = strchrnul(entry, ',');
		if (strlen(name) == end - entry && !strncasecmp(name, entry, end - entry))
			return true;
	}
	return false;
}

static void report_unmatched(void)
{
	const char *entry, *end;
	struct pci_dev *pdev;

	for (entry = targets; *entry; entry = *end ? end + 1 : end) {
		end = strchrnul(entry, ',');
		pdev = NULL;
		while ((pdev = pci_get_device(PCI_VENDOR_ID_NVIDIA, PCI_ANY_ID, pdev)))
			if (eligible(pdev, false) && strlen(pci_name(pdev)) == end - entry &&
			    !strncasecmp(pci_name(pdev), entry, end - entry))
				break;
		if (pdev)
			pci_dev_put(pdev);
		else
			pr_err("gpu_unlock: no eligible GPU at \"%.*s\" (GA106 requires experimental_ga106=1 and rev a1)\n",
			       (int)(end - entry), entry);
	}
}

static int __init gpu_unlock_init(void)
{
	struct pci_dev *pdev = NULL;
	bool all;
	int ret;

	/* Loaded ahead of nvidia on every boot; the command line is the opt-in. */
	if (!targets || !*targets)
		return 0;
	all = !strcmp(targets, "all");
	if (!all)
		report_unmatched();
	while ((pdev = pci_get_device(PCI_VENDOR_ID_NVIDIA, PCI_ANY_ID, pdev))) {
		if (!eligible(pdev, all))
			continue;
		if (!all && !listed(pci_name(pdev)))
			continue;
		if (pdev->device == RTX_3060_GA106_LHR)
			pci_info(pdev, "gpu_unlock: EXPERIMENTAL GA106 path; firmware execution and performance are not hardware-validated\n");
		if (!shared.gsp) {
			ret = gpu_unlock_610_load_shared(pdev, &shared);
			if (ret) {
				pci_err(pdev, "gpu_unlock: firmware load failed (%d)\n", ret);
				pci_dev_put(pdev);
				return 0;
			}
		}
		async_schedule_domain(unlock_one, pci_dev_get(pdev), &unlock_domain);
	}
	/* nvidia loads once this returns: every card must be finished by then. */
	async_synchronize_full_domain(&unlock_domain);
	gpu_unlock_610_release_shared(&shared);
	return 0;
}

static void __exit gpu_unlock_exit(void)
{
}

module_init(gpu_unlock_init);
module_exit(gpu_unlock_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("RTX 3090 unlock with opt-in experimental RTX 3060 GA106 port");
MODULE_VERSION("1.0.0");
