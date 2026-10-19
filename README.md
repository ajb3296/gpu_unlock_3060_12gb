# GPU unlock — experimental RTX 3060 GA106 port

**Status: source-port candidate, NOT hardware-validated. Do not treat a successful
build or mocked transaction as proof that an RTX 3060 unlocks or becomes faster.**

This local fork is based on upstream commit
`0ebdb1c6308b6a2072be5bf04513472942541956`. It adds an explicitly gated path
for the observed desktop RTX 3060 LHR PCI identity `10de:2504`, revision `a1`.
The observed board has 12,288 MiB VRAM and runs NVIDIA `580.178.04` on
Linux `7.0.0-38-generic`. These are inventory readings, not unlock results.

## Changes and safety gates

- `gpu_unlock.experimental_ga106=1` is an additional, default-off opt-in.
- A GA106 target must have an explicit full PCI address in `gpu_unlock.targets=`.
  `targets=all` continues to select RTX 3090 only, even when the GA106 opt-in is on.
- Other 3060 device IDs, the GA104-based 3060, laptop variants and revisions other
  than `a1` are not enabled. PCI identity/revision alone does not distinguish VRAM
  capacities; only the observed 12-GiB board is this port's intended test target.
- The Booter, post-FLR GFW and post-FLR PCI checks compare the card's original
  vendor/device identity rather than pretending every card is `10de:2204`.
- Existing bound-driver, cold-state, DMA, firmware-geometry, reservation and FLR
  guards remain in place. There is no driver unload or live-unlock helper.

No new GA106 firmware image is invented. NVIDIA's **610.43.03** generated
`g_kernel_gsp_nvoc.c` maps GA106 and GA102 to the same
`kgspGetBinArchiveBooterLoadUcode_GA102` (lines 1646–1648) and
`kgspGetBinArchiveGspRmBoot_GA102` (lines 833–835), and to the same WPR metadata
routine (lines 1044–1046). This supports testing the common loader assets; it
**does not prove** that the V67 payload, selector semantics, board fuses or the
handoff to installed driver 580 work on the user's card.

Official source reference:
https://github.com/NVIDIA/open-gpu-kernel-modules/blob/610.43.03/src/nvidia/generated/g_kernel_gsp_nvoc.c

The pinned 610.43.03 inputs and V67 payload are unchanged. SS1's effect remains
untested, as in upstream. No throughput or numerical-correctness claim is made.

## FWSEC safety backports

The bounded DMEM block-rounding helper and DMA-exposure tracking fix are adapted
from `IHaBiS02/ga107-unlock` commit
`5e6b3600bb01142891d43d62120727e96911d1a6` (GPL-2.0-only).
DMEM transfers round up to whole 256-byte blocks only when the existing image
contains the complete transfer; overflow, misalignment and range violations fail
closed. Buffers become potentially firmware-visible immediately before DMA
source/base programming, not before geometry or initial-queue checks. Failures
after possible DMA exposure still retain resources unless quiescence is proven.

Regressions cover partial DMEM, pre-DMA cleanup, post-DMA retention and the
unchanged initial-BME rejection in both GA102 and synthetic 12-GiB GA106 profiles.
These changes do **not** normalize initial Bus Master state or change SS0/SS1
policy. They are source/build changes only, not installation into the running
kernel or the previously created trial initramfs. Updating that image and any
physical trial require a separate operation. Falcon destination encoding checks
also do not establish the physical IMEM/DMEM capacity of an unfamiliar engine.

## Build and host verification only

```sh
# Does not load or install the module:
make -C /lib/modules/$(uname -r)/build M="$PWD/kernel/gpu_unlock" modules
python3 -m unittest discover -s tests -v
# Set GPU_UNLOCK_ASSETS to the generated asset directory to also exercise
# pinned-asset transactions for GA102 and a SYNTHETIC 12-GiB GA106 fixture.
```

The tests use mock PCI/MMIO/DMA and a synthetic ROM. The GA106 fixture is not a
ROM or register dump taken from the live 3060. Both success and failure paths
are exercised; these tests cannot establish physical execution or performance.
The native packages below build committed `HEAD`, so commit a reviewed port
before using their packaging targets. Packaging version remains upstream 1.0.0.

## Hardware validation is a separate operation

**This work did not install packages, load `gpu_unlock`, write GPU registers,
change initramfs/boot arguments, change the NVIDIA driver, or reboot.**
Before any future hardware trial, arrange physical access and a recoverable boot
entry. The experimental opt-in and an explicit BDF would both be necessary;
never unload a live display/compute driver merely to force an attempt. A full
power-off, cold-state verification, board-specific firmware/fuse checks, stock
NVIDIA handoff, CUDA numerical checks and controlled before/after benchmarks
remain unperformed. The currently installed 580 driver is not a validated
combination with this 610.43.03 unlock path.

## Upstream 1.0.0 packaging candidate notes


This Linux kernel module combines the CMP unlocker's V67/PLM technique with the
pre-driver timing of the UEFI approach. It runs the unlock, writes
the speed selectors (SS0 `0x88888888`, which selects the full BF16 rate, and
the 4-bit SS1 `0x8`, whose effect is untested) and performs FLR before the unmodified
NVIDIA driver binds the card.
It is neither a UEFI application nor a patched NVIDIA driver.

The observed result is GPU1, an RTX 3090 at `0000:21:00.0`, on the main
CachyOS `7.2.8-1-cachyos` kernel with NVIDIA `615.71.09`. GPU0 at
`0000:01:00.0` was an unmodified control. Other RTX 3090 boards and
environments have not been verified. Other GA102 PCI device IDs are rejected.

## Local build

Every build downloads the pinned NVIDIA/CachyOS inputs over HTTPS, checks
their SHA-256, and generates firmware only in the build directory and the
binary package. All three formats install through the same `make install-*`
targets. Packages are built from the committed `HEAD`.

Build, install build dependencies, and install the packages in one step
(asks for your sudo password):

```sh
make install-arch   # makepkg -sif
make install-deb    # apt-get build-dep, dpkg-buildpackage, apt-get install
make install-rpm    # dnf builddep, rpmbuild, dnf install
```

`make arch`, `make deb` and `make rpm` only build. Debian and RPM packages go
to `build/deb` and `build/rpm`. The NVIDIA inputs are cached in
`~/.cache/gpu-unlock/inputs` (`make inputs`; override with `INPUTS=`).
`dpkg-buildpackage` itself never uses the network, so a clean-chroot build (sbuild or
pbuilder) needs `GPU_UNLOCK_INPUTS` pointing at that cache.
The `make dist` archive, the Debian source package and the `.nosrc.rpm`
contain no NVIDIA archives or generated firmware.

## Boot flow and opt-in

The packages put `gpu_unlock` and its firmware in the initramfs and load it
there, before `nvidia` can bind a card:

- **mkinitcpio:** `/etc/mkinitcpio.conf.d/zz-gpu-unlock.conf` inserts
  `gpu_unlock` right before `nvidia` in `MODULES`, so a display card is blank
  for as short a time as possible. Without `nvidia` in the image, it goes
  last and still runs before an `nvidia` loaded from the root filesystem.
  Images are rebuilt on install. mkinitcpio ignores drop-ins for an image
  built with `-c <config>` or a preset `_config`; put `gpu_unlock` right
  before `nvidia` in that config's `MODULES` yourself.
- **initramfs-tools:** `/usr/share/initramfs-tools/modules.d/gpu-unlock.conf`.
- **dracut:** `/usr/lib/dracut/dracut.conf.d/gpu-unlock.conf`.

There is no `softdep nvidia pre:` rule: kmod honours only the first
`softdep nvidia` line it reads, so other packages' rules (`nvidia-utils`,
`gddr6_hwmon`) would silently override it, or it would override theirs.

The module does nothing unless the kernel command line opts in:

```
gpu_unlock.targets=all                          # every unbound 10de:2204 card
gpu_unlock.targets=0000:21:00.0                 # only these cards
gpu_unlock.targets=0000:21:00.0,0000:22:00.0    # comma-separated
```

Addresses are full PCI names as `lspci -D` prints them (hex digits in either
case), independent of GPU numbering and display role. An entry that is not
an RTX 3090 or an explicitly opted-in eligible experimental GA106 target is
logged and ignored. An `all` token mixed with addresses is ignored; it does
not expand to other cards, while separately listed eligible addresses remain
eligible. Use `all` alone only for RTX 3090 cards.
The cards are unlocked concurrently, about a second in all however many
there are, and each on its own: a card that is already bound, powered down
or bus mastering is skipped. Then `nvidia` loads and binds every card as
usual.

The display card works too. Its firmware framebuffer (the boot console and
Plymouth) keeps running while the unlock's firmware steps run, and is
removed just before the card's reset, as `nvidia-drm` would do a moment
later anyway. Its screen is blank from that reset until `nvidia-drm` takes
over.

The unlock needs a cold card: power the machine off rather than rebooting.
Warm reboots are not supported; with the option set, the unlock is expected
to fail and leave the card as it was. Loading the module by hand after boot
does nothing to a card that `nvidia` has bound. A card that a driver already
used this power cycle is refused before any firmware runs (`card already
initialised since power on`).

Check the result with:

```sh
journalctl -k -b | grep gpu_unlock
lspci -nnk -s 0000:21:00.0
```

To stop unlocking, remove `gpu_unlock.targets=` from the kernel command line;
for a one-off boot, delete it in the bootloader's edit screen. Removing the
packages also removes the module and the initramfs drop-ins.

If the firmware on a card never stops, the module logs it and keeps the DMA
buffers that firmware may still use until the next power off. Power off
before trying again.

The kernel module and local packaging are GPL-2.0-only. The generated payload
derives from the pinned CMP reference; NVIDIA firmware remains under its own
terms. See `firmware/NOTICE.md` and `licenses/` for attribution and notices.
