#!/usr/bin/env bash
set -euo pipefail
lab=/home/sn/tbv/hardware-20261007-xdomain-done
cp -a "$lab/source" "$lab/fault-source"
cp "$lab/test-signed.ko" "$lab/production-signed.ko"
python3 - "$lab" <<'PY'
from pathlib import Path
import sys
lab=Path(sys.argv[1]); p=lab/'fault-source/kernel/verbs_control.c'
s=p.read_text()
needle='static bool dmabuf_push_done;'
assert s.count(needle)==1
s=s.replace(needle,needle+'\n/* Lab-only: deliberately suppress every third DONE notification. */\nstatic unsigned int lab_done_drop_every = 3;\nmodule_param(lab_done_drop_every, uint, 0644);')
needle='\t\tret = tbv_dmabuf_wire_build(packet, sizeof(packet), &m, ctrl->xd->route);'
assert s.count(needle)==1
s=s.replace(needle,'\t\tif (lab_done_drop_every &&\n\t\t    !(atomic64_read(&ctrl->counters[CTRL_DONE]) % lab_done_drop_every)) {\n\t\t\tatomic64_inc(&ctrl->counters[CTRL_NOTIFY_FAILED]);\n\t\t\treturn;\n\t\t}\n'+needle)
p.write_text(s)
PY
diff -u "$lab/source/kernel/verbs_control.c" "$lab/fault-source/kernel/verbs_control.c" > "$lab/results/fault-injection.patch" || test "$?" = 1
make -C "$lab/fault-source" KDIR="/lib/modules/$(uname -r)/build" -j2 W=1 modules > "$lab/results/fault-build.log" 2>&1
cp "$lab/fault-source/kernel/thunderbolt_ibverbs.ko" "$lab/fault-signed.ko"
sudo -n "/lib/modules/$(uname -r)/build/scripts/sign-file" sha256 /var/lib/dkms/mok.key /var/lib/dkms/mok.pub "$lab/fault-signed.ko"
sha256sum "$lab/fault-signed.ko" "$lab/production-signed.ko"
