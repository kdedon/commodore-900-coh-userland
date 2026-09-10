# Commodore 900 COHERENT userland

User-space sources for COHERENT on the Z8001-based Commodore 900. Each
component is a top-level directory named for what it is, with the same internal
layout -- `cmd/`, `lib/`, `include/`, `drv/`, `etc/`, `test/` and whatever else
that component needs: `base/` the general command set and its libraries, `hr/`
the HR window system, and `mgr/`, `net/`, `games/`, `archive/`, `comms/` and
`editors/` beside them. A distribution is a descriptor that selects
components, not a place sources live. Kernel and disk-image builds live in
separate repositories.

## Build

`hostbuild/Makefile` is the entry point and stages the userland:

    make -C hostbuild            # the sweep, the manual and the staged tree

`hr/Makefile` builds the hi-res window system, which is kernel-adjacent and
not part of the flat command sweep:

    make -C hr compiler-info
    make -C hr                 # headers, libraries, hr and hrgui
    make -C hr hr              # the /drv/hr driver and the window system
    make -C hr hrgui           # the GUI launcher and its applications
    make -C hr vellum          # the drawing suite on its own
    make -C hr help

The compiler is the one built from source in `commodore-900-toolchain`.

The `kernel` and `image` targets have moved and report the repository that now
owns them.

## Build the extended userland

    make -C hostbuild help
    make -C hostbuild userland net netcmds mgr man

Extended builds require `C900_TOOLCHAIN`. Tests that execute Z8001 binaries
also require `C900_EMU`.

## Dependencies

`make deps` installs inputs listed in `DEPS`. Use `C900_TOOLCHAIN`, `C900_EMU`,
`C900_MWC1985`, or `C900_KERNEL` to select local copies. Unset inputs are also
searched for in adjacent checkout directories.

## License

Project-authored code is MIT licensed. Historical and third-party code remains
under its original terms. See `LICENSE` and the notices shipped with the
relevant source.
