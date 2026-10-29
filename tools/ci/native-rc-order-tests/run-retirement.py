from pathlib import Path
import sys,tempfile,subprocess
here=Path(__file__).resolve().parent;root=here.parents[2];sys.path.insert(0,str(root/'tools/ci/framework-desktop-tests'))
from production import functions
new=(root/'kernel/ibdev.c').read_text()
helpers=functions(new,['tbv_send_retire_frames','tbv_send_build_needless','tbv_native_send_run_build_job','tbv_qp_entry_expired'])
helpers+=(here/'retirement-before.c').read_text()
with tempfile.TemporaryDirectory() as d:
 d=Path(d);(d/'helpers.c').write_text(helpers)
 subprocess.run(['cc','-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-pthread','-I',str(d),str(here/'retirement.c'),'-o',str(d/'retire')],check=True)
 subprocess.run([str(d/'retire')],check=True,timeout=30)
# Both retirement producers must share the tested accounting helper.
assert 'tbv_send_retire_frames(send, 1, status)' in functions(new,['tbv_send_tx_done'])
assert 'tbv_send_retire_frames(ctx, unsent, 0)' in functions(new,['tbv_native_send_run_build_job'])
print('PASS: normal completion and unbuilt-frame paths share tested production helper')
