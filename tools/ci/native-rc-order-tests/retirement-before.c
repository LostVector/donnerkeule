/* Before-fix production function, base4908c75; regression fixture only. */
static void old_run_build_job(struct tbv_send_build_job *job)
{
	struct tbv_send_ctx *ctx = job->send;
	u32 unsent = job->nfrags;

	if (!tbv_send_build_needless(ctx))
		unsent = tbv_native_send_build_block(ctx, job->path,
						     job->first_frag,
						     job->nfrags);
	/*
	 * Frames that will not be sent must not keep the send pending; the
	 * send is then retransmitted after its timeout, or flushed.
	 */
	if (unsent)
		atomic_sub(unsent, &ctx->tx_pending);
}
