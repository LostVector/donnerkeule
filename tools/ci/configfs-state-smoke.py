#!/usr/bin/env python3
"""Check actual activation/release ordering with simulated publication failures."""
from smoke_support import ROOT, functions, run_c

source = (ROOT / "kernel/configfs.c").read_text()
body = functions(source, ("activate_store", "tbv_cfgfs_link_release"))
shim = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include "proto/config.h"
#define mutex_lock(p) ((void)(p))
#define mutex_unlock(p) ((void)(p))
#define mutex_destroy(p) ((void)(p))
#define kfree(p) free(p)
enum tbv_backend_type { TBV_BACKEND_NATIVE, TBV_BACKEND_APPLE };
struct config_item { int unused; };
struct tbv_state { int unused; };
struct tbv_cfgfs_link { struct config_item item; int lock; struct tbv_state *state; struct tbv_cfg_link cfg; };
static unsigned int activated, removed;
static int publication_result;
static struct tbv_cfgfs_link *tbv_cfgfs_to_link(struct config_item *item) { return (void *)item; }
static bool sysfs_streq(const char *buf, const char *value) { return !strcmp(buf, value); }
static const char *config_item_name(struct config_item *item) { return "usb4_rdma0"; }
static void trace_tbv_cfgfs_link_op(const char *name, const char *op, int ret) {}
static int tbv_link_activate_config(struct tbv_state *s, const char *name, enum tbv_backend_type backend, struct tbv_cfg_link *cfg) {
    assert(cfg->state == TBV_CFG_ACTIVE);
    assert(backend == (cfg->backend == TBV_CFG_BACKEND_NATIVE ? TBV_BACKEND_NATIVE : TBV_BACKEND_APPLE));
    activated++; return publication_result;
}
static void tbv_link_deactivate_config(struct tbv_state *s, unsigned int id) { removed++; }
"""
cases = r"""
int main(void) {
    for (unsigned int backend = TBV_CFG_BACKEND_NATIVE; backend <= TBV_CFG_BACKEND_APPLE; backend++) {
        struct tbv_cfgfs_link *link = calloc(1, sizeof(*link));
        assert(link); link->cfg.backend = backend;
        assert(activate_store(&link->item, "0", 1) == -EINVAL);
        for (unsigned int state = TBV_CFG_EMPTY; state <= TBV_CFG_ACTIVE; state++) {
            if (state == TBV_CFG_SEALED) continue;
            link->cfg.state = state;
            unsigned int before = activated;
            assert(activate_store(&link->item, "1", 1) == -EINVAL);
            assert(link->cfg.state == state && activated == before);
        }
        link->cfg.state = TBV_CFG_SEALED;
        publication_result = -ENOMEM;
        assert(activate_store(&link->item, "1", 1) == -ENOMEM);
        assert(link->cfg.state == TBV_CFG_SEALED);
        publication_result = 0;
        assert(activate_store(&link->item, "1", 1) == 1);
        assert(link->cfg.state == TBV_CFG_ACTIVE);
        unsigned int before = removed;
        tbv_cfgfs_link_release(&link->item); assert(removed == before + 1);
        link = calloc(1, sizeof(*link)); assert(link);
        link->cfg.state = TBV_CFG_SEALED;
        before = removed;
        tbv_cfgfs_link_release(&link->item); assert(removed == before);
    }
    puts("Configfs state: PASS (backend activation, invalid state, publication rollback, release)");
    return 0;
}
"""
# Link the production pure configuration helpers into the shim's translation unit.
run_c("configfs-state", shim + (ROOT / "proto/config.c").read_text().replace('#include "config.h"', '#include "proto/config.h"')
      + body + cases + "\n" + (ROOT / "proto/identity.c").read_text().replace('#include "identity.h"', '#include "proto/identity.h"'))
