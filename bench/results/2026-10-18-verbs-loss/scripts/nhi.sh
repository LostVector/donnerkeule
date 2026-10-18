#!/bin/bash
for h in fuzzy misty; do echo -n "$h irq_sum_by_vector: "; ssh $h 'grep -E " thunderbolt$" /proc/interrupts | awk "{s=0; for(i=2;i<NF;i++) if (\$i ~ /^[0-9]+\$/) s+=\$i; printf \"%s%d \", \$1, s}"'; echo; done
