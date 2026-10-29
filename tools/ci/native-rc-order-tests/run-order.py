from pathlib import Path
import tempfile,subprocess,sys
here=Path(__file__).resolve().parent;root=here.parents[2];sys.path.insert(0,str(root/'tools/ci/framework-desktop-tests'))
from production import functions
s=(root/'kernel/ibdev.c').read_text();names=['tbv_native_sq_ordered','tbv_native_sq_kick_locked','tbv_qp_queue_send','tbv_qp_drain_ready_sends_locked','tbv_native_sq_work']
with tempfile.TemporaryDirectory() as d:
 d=Path(d);(d/'helpers.c').write_text(functions(s,names))
 subprocess.run(['cc','-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=address,undefined','-pthread','-I',str(d),str(here/'order.c'),'-o',str(d/'order')],check=True)
 subprocess.run([str(d/'order')],check=True,timeout=30)
p=functions(s,['tbv_post_send_one']);assert p.index('tbv_send_ctx_get(ctx);\n\tif (tbv_qp_queue_send')<p.index('tbv_native_send_ctx_post_frames(ctx, TBV_SEND_POST_INITIAL)')
assert 'if (ctx->native_deferred)' not in p
assert functions(s,['tbv_post_send_impl']).count('tbv_dmabuf_verbs_enabled() || tbv_native_sq_ordered(tqp)')==3
p=functions(s,['tbv_destroy_qp']);assert p.index('tqp->closing = true')<p.index('cancel_delayed_work_sync(&tqp->native_sq_work)')<p.index('tbv_qp_flush_sends')
print('PASS: caller ref before deferred publication; immutable admission decision; native post serialization; close cancels worker before flush')
