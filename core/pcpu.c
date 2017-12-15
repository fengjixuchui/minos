#include <core/vcpu.h>
#include <core/pcpu.h>
#include <core/vm.h>

static struct vmm_pcpu pcpus[MAX_CPU_NR];

void init_pcpus(void)
{
	int i;
	struct vmm_pcpu *pcpu;

	for (i = 0; i < MAX_CPU_NR; i++) {
		pcpu = &pcpus[i];
		init_list(&pcpu->vcpu_list);
		pcpu->pcpu_id = i;
	}
}

uint32_t pcpu_affinity(struct vmm_vcpu *vcpu, uint32_t affinity)
{
	int i, found;
	uint32_t af;
	struct vmm_pcpu *pcpu;
	struct list_head *list;
	struct vmm_vcpu *tvcpu;

	/*
	 * first check the other vcpu belong to the same
	 * VM is areadly affinity to this pcpu
	 */
	if (affinity >= MAX_CPU_NR)
		goto step2;

	pcpu = &pcpus[affinity];
	list_for_each(&pcpu->vcpu_list, list) {
		tvcpu = list_entry(list, struct vmm_vcpu, pcpu_list);
		if ((vcpu->vm_belong_to->vmid) ==
				(tvcpu->vm_belong_to->vmid))
			goto step2;
	}

	/*
	 * can affinity to this pcpu
	 */
	list_add_tail(&pcpu->vcpu_list, &vcpu->pcpu_list);
	vcpu->pcpu_affinity = affinity;
	return affinity;

step2:
	for (i = 0; i < MAX_CPU_NR; i++) {
		found = 0;
		if (i == affinity)
			continue;

		pcpu = &pcpus[i];
		list_for_each(&pcpu->vcpu_list, list) {
			tvcpu = list_entry(list, struct vmm_vcpu, pcpu_list);
			if ((vcpu->vm_belong_to->vmid) ==
					(tvcpu->vm_belong_to->vmid)) {
				found = 1;
				break;
			}
		}

		if (!found) {
			list_add_tail(&pcpu->vcpu_list, &vcpu->pcpu_list);
			vcpu->pcpu_affinity = i;
			return i;
		}
	}

	return PCPU_AFFINITY_FAIL;
}
