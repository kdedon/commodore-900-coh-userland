# DEPS -- what this repository consumes from other repositories.
#
#	name  kind  url  [ref]  [asset]  [dir]
#
# Read by `make deps' (mk/deps-fetch.sh).  The build resolves dependencies
# through mk/deps.sh; a named variable wins over anything here.
#
# kind git      cloned beside this repository, floating on <ref>
# kind release  a published archive, pinned to <ref>
#
#   toolchain  the Z8001 cross compiler, assembler, linker, and libc-z8001.a.
#              A RELEASE, not a checkout: the tag is a PIN, bumped by hand when
#              this tree wants what a newer compiler emits.  A floating `git'
#              edge would mean a tagged build of this repository links
#              differently tomorrow with nothing here changed.
#   emu        the c900 emulator binary for running guest test gates
#   kernel     the kernel's EXPORT: the header set this tree compiles against,
#              and the kernel-side binaries the image carries.  The headers are
#              the machine layer this tree cannot own -- <sys/machz8001.h> is
#              the load-bearing one: the toolchain's <sys/machine.h> includes it
#              under Z8001 and it exists in no other repository.  The binaries
#              are the console drivers staged on /drv and the symboled kernel
#              staged as /coherent, which that repository builds (`make kernel
#              drivers') and packages (`make kernel-dist'); this one stages them
#              and links no kernel of its own.  /drv/hr is the one loadable
#              driver whose SOURCE is here, because it is versioned with the
#              window system that depends on it; it compiles against those
#              headers, is built to the compiler contract the kernel publishes
#              beside its drivers, and is bound to the exported kernel image by
#              `ld -k'.  A `git' edge, not a release, because the headers track
#              the kernel that defines them and a stale pin here is a silently
#              wrong struct layout rather than a build failure.  An edge that
#              resolves to a headers-only release satisfies the compiles and
#              not the staging, which then refuses by name.
#
toolchain  release  https://github.com/kdedon/commodore-900-toolchain  v0.1.4  c900-toolchain-@REF@-@HOST@  commodore-900-toolchain
emu        release  https://github.com/kdedon/commodore-900-emulator   v0.1  c900-@REF@-@HOST@
kernel     git      https://github.com/kdedon/commodore-900-coh-kernel3  main
