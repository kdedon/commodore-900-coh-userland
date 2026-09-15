# DEPS -- what this repository consumes from other repositories.
#
#	name  kind  url  [ref]  [asset]  [dir]
#
# Read by `make deps' (mk/deps-fetch.sh).  The build resolves dependencies
# through mk/deps.sh; a named variable wins over anything here.
#
# kind git      cloned beside this repository, floating on <ref>
# kind release  a tag's published assets, unpacked into deps/<dir>/; the ref
#               `latest' is the newest published release
#
# Every edge is a RELEASE at `latest': a clone of this repository alone, with
# `make deps', has everything its build and its checks consume, and no sibling
# checkout is needed or searched for.  The release a build used is recorded
# (toolchain.sh reports the shape and VERSION; the kernel archive carries its
# own VERSION and .provenance), so which one ran is never a matter of
# inspection.
#
#   toolchain  the Z8001 cross compiler, assembler, linker, and libc-z8001.a.
#   emu        the c900 emulator binary, for the checks that run a Z8001
#              program (`c900 --exec') and the libc smoke test.
#   kernel     the kernel's EXPORT, c900-kernel-v<V>: the header set this tree
#              compiles against, and the kernel-side binaries the image
#              carries.  The headers are the machine layer this tree cannot
#              own -- <sys/machz8001.h> is the load-bearing one: the
#              toolchain's <sys/machine.h> includes it under Z8001 and it
#              exists in no other repository.  The binaries are the console
#              drivers staged on /drv and the symboled kernel staged as
#              /coherent, which that repository builds and packs (`make
#              kernel-dist'); its hostbuild/ view (kobj/kernel.out,
#              build/drv) is the one path a consumer here reads, the same
#              path a checkout of that repository has under os/hostbuild.
#              /drv/hr is the one loadable driver whose SOURCE is here,
#              because it is versioned with the window system that depends on
#              it; it is bound to the exported kernel image by `ld -k'.  A
#              c900-kernel-headers-v<V> archive resolves this edge too, and
#              serves the compiles and not the staging, which then refuses by
#              name.
#
toolchain  release  https://github.com/kdedon/commodore-900-toolchain    latest  c900-toolchain-@REF@-@HOST@  commodore-900-toolchain
emu        release  https://github.com/kdedon/commodore-900-emulator     latest  c900-@REF@-@HOST@            commodore-900-emulator
kernel     release  https://github.com/kdedon/commodore-900-coh-kernel3  latest  c900-kernel-@REF@.tar.gz     commodore-900-coh-kernel3
