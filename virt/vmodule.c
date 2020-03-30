/*
 * Copyright (C) 2018 Min Le (lemin9538@gmail.com)
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
#include <virt/vmodule.h>
#include <minos/init.h>
#include <minos/mm.h>
#include <minos/spinlock.h>
#include <virt/vm.h>

extern unsigned char __vmodule_start;
extern unsigned char __vmodule_end;

static int vmodule_class_nr = 0;
static LIST_HEAD(vmodule_list);

static struct vmodule *create_vmodule(struct module_id *id)
{
	struct vmodule *vmodule;
	vmodule_init_fn fn;

	vmodule = malloc(sizeof(*vmodule));
	if (!vmodule) {
		return NULL;
	}

	memset(vmodule, 0, sizeof(*vmodule));
	strncpy(vmodule->name, id->name, sizeof(vmodule->name) - 1);
	init_list(&vmodule->list);
	vmodule->id = vmodule_class_nr++;

	/* call init routine */
	if (id->data) {
		fn = (vmodule_init_fn)id->data;
		fn(vmodule);
	}

	list_add(&vmodule_list, &vmodule->list);
	return vmodule;
}

int register_vcpu_vmodule(const char *name, vmodule_init_fn fn)
{
	struct vmodule *vmodule;
	struct module_id mid = { .name = name, .comp = NULL, .data = fn };

	vmodule = create_vmodule(&mid);
	if (!vmodule) {
		pr_err("create vmodule %s failed\n", name);
		return -ENOMEM;
	}

	return 0;
}

void *get_vmodule_data_by_id(struct vcpu *vcpu, int id)
{
	return vcpu->context[id];
}

int vcpu_vmodules_init(struct vcpu *vcpu)
{
	struct list_head *list;
	struct vmodule *vmodule;
	void *data;
	int size;

	/*
	 * firset allocate memory to store each vmodule
	 * context's context data
	 */
	size = vmodule_class_nr * sizeof(void *);
	if (size == 0)
		return 0;

	vcpu->context = zalloc(size);
	if (!vcpu->context)
		panic("No more memory for vcpu vmodule cotnext\n");

	list_for_each(&vmodule_list, list) {
		vmodule = list_entry(list, struct vmodule, list);
		if (vmodule->context_size) {
			/* for reboot if memory is areadly allocated skip it */
			data = vcpu->context[vmodule->id];
			if (!data) {
				data = malloc(vmodule->context_size);
				vcpu->context[vmodule->id] = data;
			}

			memset(data, 0, vmodule->context_size);
			if (vmodule->state_init)
				vmodule->state_init(vcpu, data);
		}
	}

	return 0;
}

int vcpu_vmodules_deinit(struct vcpu *vcpu)
{
	struct vmodule *vmodule;
	void *data;

	if (NULL == vcpu->context)
		return 0;

	list_for_each_entry(vmodule, &vmodule_list, list) {
		data = vcpu->context[vmodule->id];
		if (vmodule->state_deinit && data)
			vmodule->state_deinit(vcpu, data);

		if (data)
			free(data);
	}

	return 0;
}

int vcpu_vmodules_reset(struct vcpu *vcpu)
{
	struct vmodule *vmodule;
	void *data;

	list_for_each_entry(vmodule, &vmodule_list, list) {
		data = vcpu->context[vmodule->id];
		if (vmodule->state_reset && data)
			vmodule->state_reset(vcpu, data);
	}

	return 0;
}

void restore_vcpu_vmodule_state(struct vcpu *vcpu)
{
	struct vmodule *vmodule;
	void *context;

	list_for_each_entry(vmodule, &vmodule_list, list) {
		context = vcpu->context[vmodule->id];
		if (vmodule->state_restore && context)
			vmodule->state_restore(vcpu, context);
	}
}

void save_vcpu_vmodule_state(struct vcpu *vcpu)
{
	struct vmodule *vmodule;
	void *context;

	list_for_each_entry(vmodule, &vmodule_list, list) {
		context = vcpu->context[vmodule->id];
		if (vmodule->state_save && context)
			vmodule->state_save(vcpu, context);
	}
}

void suspend_vcpu_vmodule_state(struct vcpu *vcpu)
{
	struct vmodule *vmodule;
	void *context;

	list_for_each_entry(vmodule, &vmodule_list, list) {
		context = vcpu->context[vmodule->id];
		if (vmodule->state_suspend && context)
			vmodule->state_suspend(vcpu, context);
	}
}

void resume_vcpu_vmodule_state(struct vcpu *vcpu)
{
	struct vmodule *vmodule;
	void *context;

	list_for_each_entry(vmodule, &vmodule_list, list) {
		context = vcpu->context[vmodule->id];
		if (vmodule->state_resume && context)
			vmodule->state_resume(vcpu, context);
	}
}

void stop_vcpu_vmodule_state(struct vcpu *vcpu)
{
	struct vmodule *vmodule;
	void *context;

	list_for_each_entry(vmodule, &vmodule_list, list) {
		context = vcpu->context[vmodule->id];
		if (vmodule->state_stop && context)
			vmodule->state_stop(vcpu, context);
	}
}

int vmodules_init(void)
{
	struct module_id *mid;
	struct vmodule *vmodule;

	section_for_each_item(__vmodule_start, __vmodule_end, mid) {
		vmodule = create_vmodule(mid);
		if (!vmodule)
			pr_err("create vmodule %s failed\n", mid->name);
	}

	return 0;
}
