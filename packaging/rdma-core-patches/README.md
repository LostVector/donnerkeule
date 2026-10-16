# rdma-core patches

The userspace `usb4_rdma` provider is an out-of-tree libibverbs provider that
plugs into rdma-core via the standard `providers/` mechanism. Rather than
fork rdma-core or vendor its internal headers, we ship **three minimal
patches** that any rdma-core source tree can apply to produce a build that
includes our provider alongside the upstream ones.

```
0001-providers-usb4_rdma-add-USB4-soft-RDMA-provider.patch
   Adds providers/usb4_rdma/ (the provider source files)

0002-CMakeLists.txt-build-the-usb4_rdma-provider.patch
   Adds `add_subdirectory(providers/usb4_rdma)` to the top-level
   CMakeLists.txt so the provider is built and installed.

0003-libibverbs-verbs.h-declare-verbs_provider_usb4_rdma.patch
   Declares the provider for the static libibverbs build.
```

The ordered `series` file is shared by Nix, distro packaging and patch tests.
The patches are generated with `git format-patch`; the provider files come
from `userspace/usb4_rdma/`. Compatibility is checked against the rdma-core
version selected by the locked Nix input, including its static build.

## How each consumer applies them

### Nix (this repo)

`rdma-core-usb4` in `flake.nix` reads `series` and appends those patches to
`pkgs.rdma-core`. `checks.x86_64-linux.rdma-core-static` explicitly builds
with `ENABLE_STATIC=1`.

### Debian / Ubuntu

For a system rebuild that drops our provider into the distro's rdma-core:

```sh
apt-get source rdma-core
cd rdma-core-*
cp .../packaging/rdma-core-patches/000*.patch debian/patches/
ls debian/patches/000*.patch >> debian/patches/series
dpkg-buildpackage -b -uc -us
sudo dpkg -i ../librdmacm1_*.deb ../libibverbs1_*.deb ../ibverbs-providers_*.deb
```

### Arch (AUR)

In a PKGBUILD that derives from `extra/rdma-core`, add the patches to
`source=(...)` and a `prepare()` function:

```bash
source=(
  "git+https://github.com/linux-rdma/rdma-core.git#tag=v62.0"
  "0001-providers-usb4_rdma-add-USB4-soft-RDMA-provider.patch"
  "0002-CMakeLists.txt-build-the-usb4_rdma-provider.patch"
  "0003-libibverbs-verbs.h-declare-verbs_provider_usb4_rdma.patch"
)
prepare() {
  cd "$srcdir/rdma-core"
  patch -p1 < ../0001-providers-usb4_rdma-add-USB4-soft-RDMA-provider.patch
  patch -p1 < ../0002-CMakeLists.txt-build-the-usb4_rdma-provider.patch
  patch -p1 < ../0003-libibverbs-verbs.h-declare-verbs_provider_usb4_rdma.patch
}
```

### Fedora / RHEL

Add to `rdma-core.spec`:

```spec
Patch1000: 0001-providers-usb4_rdma-add-USB4-soft-RDMA-provider.patch
Patch1001: 0002-CMakeLists.txt-build-the-usb4_rdma-provider.patch
Patch1002: 0003-libibverbs-verbs.h-declare-verbs_provider_usb4_rdma.patch
...
%prep
%autosetup -n rdma-core-%{version} -p1
```

Then `rpmbuild -ba rdma-core.spec` produces our-provider-included RPMs.

## Regenerating the patches

When the upstream provider source in `userspace/usb4_rdma/` changes, refresh
the patches:

```sh
./packaging/regen-rdma-core-patches.sh
```

Uses `packages.x86_64-linux.rdma-core-source` from the locked flake. For
offline use, set `RDMA_SRC` to a clean upstream source directory or a tar
archive containing one top-level source directory. Generation validates
the upstream anchors and patch names before replacing the three files.

`tools/ci/run-smoke.py` checks provider source equality and regeneration
from both input forms. `packaging/test-rdma-patches.sh` applies the complete
series in a distro container, compares the provider files with their source,
and builds with `ENABLE_STATIC=1`.
It shares the locked source by default; set `RDMA_CORE_TAG` to test a
different upstream release, or `RDMA_SRC` to use an offline source.
