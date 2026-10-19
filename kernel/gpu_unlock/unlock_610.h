/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef GPU_UNLOCK_UNLOCK_610_H
#define GPU_UNLOCK_UNLOCK_610_H

struct firmware;
struct pci_dev;

/* Read-only firmware every unlock uses: loaded once, shared by concurrent unlocks. */
struct gpu_unlock_610_shared {
	const struct firmware *gsp;
};

/*
 * Load the shared firmware. pdev is any target card; the request leaves no
 * device resources behind on it. Returns 0 or a negative errno.
 */
int gpu_unlock_610_load_shared(struct pci_dev *pdev, struct gpu_unlock_610_shared *shared);

/* Release the shared firmware once every unlock using it has returned. */
void gpu_unlock_610_release_shared(struct gpu_unlock_610_shared *shared);

/*
 * Run the unlock sequence on one cold, unbound RTX 3090 or an explicitly
 * opted-in experimental RTX 3060 GA106. The caller validates the model and
 * opt-in policy and holds pci_dev_lock. Returns 0 after selector readback and
 * driver-handoff checks; this is not proof of a throughput improvement.
 * A negative errno does not guarantee rollback of partial GPU state changes;
 * firmware-visible DMA may be retained until power-off. Different cards may
 * run concurrently with the same shared firmware.
 */
int gpu_unlock_610_unlock(struct pci_dev *pdev, const struct gpu_unlock_610_shared *shared);

#endif
