/*
 * Copyright (C) 2020 Min Le (lemin9538@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <minos/minos.h>
#include <minos/tty.h>
#include <virt/vmm.h>
#include <virt/virq.h>
#include <common/hypervisor.h>
#include <virt/hypercall.h>
#include <asm/svccc.h>
#include <minos/console.h>
#include <virt/vmm.h>
#include <minos/of.h>
#include <virt/resource.h>

#define DCON_TTY_MAGIC		0xabcd0000

#define DCON_RX_RING_SIZE	2048
#define DCON_TX_RING_SIZE	4096
#define DCON_RING_SIZE		8192

#define NR_DC			8

struct vm_debug_console {
	uint32_t irq;
	int open;
	struct vm *vm;
	struct tty *tty;
	unsigned long ring_addr;
	struct vm_ring *tx;
	struct vm_ring *rx;
};

static char *vm_debug_console_match[] = {
	"minos,vm_console",
	NULL
};

static struct vm_debug_console *dcons[NR_DC];

static int dcon_putc(struct tty *tty, char ch)
{
	struct vm_debug_console *dcon = tty->pdata;
	struct vm_ring *tx = dcon->tx;

	if (!dcon->open)
		return 0;

	if ((tx->widx - tx->ridx) > tx->size) {
		pr_err("write buffer overflow\n");
		return -EIO;
	}

	tx->buf[tx->widx++] = ch;
	mb();

	send_virq_to_vm(dcon->vm, dcon->irq);

	return 0;
}

static int dcon_putcs(struct tty *tty, char *str, int count)
{
	return 0;
}

static int dcon_open(struct tty *tty)
{
	return 0;
}

static void dcon_close(struct tty *tty)
{

}

struct tty_ops vm_tty_ops = {
	.put_char	= dcon_putc,
	.put_chars	= dcon_putcs,
	.open		= dcon_open,
	.close		= dcon_close,
};

static int dcon_get_resource(struct vm *vm, struct device_node *node,
		struct vmm_area **__va, uint32_t *__irq)
{
	int ret;
	struct vmm_area *va;
	unsigned long flags;
	uint64_t base, size;
	uint32_t irq = 0;

	if (!of_get_bool(node, "vc-dynamic-res")) {
		ret = translate_device_address(node, &base, &size);
		if (ret)
			return -EINVAL;

		if (size < DCON_RING_SIZE) {
			pr_err("vm console size too small\n");
			return -EINVAL;
		}

		ret = vm_get_device_irq_index(vm, node, &irq, &flags, 0);
		if (ret)
			return -EINVAL;

		request_virq(vm, irq, 0);
		va = request_vmm_area(&vm->mm, base, 0, size,
				VM_IO | VM_MAP_PRIVATE);

	} else {
		/*
		 * since the debug console is only for native vm which
		 * will never released, so it is ok not set the physical
		 * address in the vmm_area
		 */
		va = alloc_free_vmm_area(&vm->mm, DCON_RING_SIZE,
			PAGE_MASK, VM_IO | VM_MAP_PRIVATE);
		if (!va)
			return -ENOMEM;
	}

	*__va = va;
	*__irq = irq;

	return 0;
}

static int dcon_init(struct vm *vm, struct device_node *node,
		struct vm_debug_console *dcon, void *ring)
{
	struct vm_ring *r;
	struct vmm_area *va;

	if (dcon_get_resource(vm, node, &va, &dcon->irq))
		return -ENODEV;

	pr_notice("debug_console base: 0x%x\n", va->start);

	dcon->ring_addr = va->start;
	map_vmm_area(&vm->mm, va, (unsigned long)ring);

	/*
	 * init the vm ring struct
	 */
	r = (struct vm_ring *)ring;
	r->ridx = 0;
	r->widx = 0;
	r->size = DCON_RX_RING_SIZE;
	dcon->tx = r;

	r = (struct vm_ring *)(ring + sizeof(struct vm_ring) +
			DCON_RX_RING_SIZE);
	r->ridx = 0;
	r->widx = 0;
	r->size = DCON_TX_RING_SIZE;
	dcon->rx = r;

	return 0;
}

static int __init_text create_dconsole(struct vm *vm, struct device_node *node)
{
	void *ring;
	struct tty *tty;
	struct vm_debug_console *dcon;
	char name[16];

	if (!vm_is_native(vm) || (vm->vmid >= NR_DC))
		return 0;

	memset(name, 0, 16);
	sprintf(name, "vm%d", vm->vmid);

	tty = alloc_tty(name, vm->vmid | DCON_TTY_MAGIC, 0);
	if (!tty)
		return -ENOENT;

	dcon = zalloc(sizeof(struct vm_debug_console));
	if (!dcon)
		goto release_tty;

	ring = get_io_pages(PAGE_NR(DCON_RING_SIZE));
	if (!ring)
		goto release_dcon;

	if (dcon_init(vm, node, dcon, ring))
		goto free_ring;

	dcon->tty = tty;
	dcon->vm = vm;
	dcons[vm->vmid] = dcon;

	tty->ops = &vm_tty_ops;
	tty->pdata = dcon;
	register_tty(tty);

	return 0;

free_ring:
	free(ring);
release_dcon:
	free(dcon);
release_tty:
	release_tty(tty);
	return -ENOMEM;
}

VDEV_DECLARE(vm_debug_console, vm_debug_console_match, create_dconsole);

static void dcon_write(struct vm_debug_console *dcon)
{
	struct vm_ring *ring = dcon->rx;
	uint32_t ridx, widx;

	if (!dcon->tty->open) {
		ring->ridx = ring->widx;
		wmb();

		return;
	}

	ridx = ring->ridx;
	widx = ring->widx;
	mb();

	while (ridx != widx)
		console_putc(ring->buf[VM_RING_IDX(ridx++, ring->size)]);

	ring->ridx = ring->widx;
	mb();
}

static int dcon_hvc_handler(gp_regs *c, uint32_t id, uint64_t *args)
{
	struct vm *vm = get_current_vm();
	struct vm_debug_console *dcon;

	dcon = dcons[vm->vmid];
	if (!dcon)
		HVC_RET1(c, 0);

	switch (id) {
	case HVC_DC_GET_STAT:
		HVC_RET1(c, DCON_TTY_MAGIC | vm->vmid);
		break;
	case HVC_DC_GET_RING:
		HVC_RET1(c, dcon->ring_addr);
		break;
	case HVC_DC_GET_IRQ:
		if (dcon->irq == 0) {
			dcon->irq = alloc_vm_virq(vm);
			if (dcon->irq < 0)
				dcon->irq = 0;
		}
		HVC_RET1(c, dcon->irq);
		break;
	case HVC_DC_WRITE:
		dcon_write(dcon);
		break;
	case HVC_DC_OPEN:
		dcon->open = 1;
		break;
	case HVC_DC_CLOSE:
		dcon->open = 0;
		break;
	}

	HVC_RET1(c, 0);
}
DEFINE_HVC_HANDLER("debug_console_hvc", HVC_TYPE_DEBUG_CONSOLE,
		HVC_TYPE_DEBUG_CONSOLE, dcon_hvc_handler);
