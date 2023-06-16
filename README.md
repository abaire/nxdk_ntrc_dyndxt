# nxdk_ntrc_dyndxt

Provides functions to trace ntrc interactions using the
[nxdk_dyndxt](https://github.com/abaire/nxdk_dyndxt) dynamic loader.
This project was heavily inspired by [nv2a-trace](https://github.com/XboxDev/nv2a-trace).

The DLL produced by this project installs a 'ntrc' XBDM command processor
that responds to various commands.

Intended for use with [xbdm_gdb_bridge](https://github.com/abaire/xbdm_gdb_bridge)'s
`@load` command.

# git hooks

This project uses [git hooks](https://git-scm.com/book/en/v2/Customizing-Git-Git-Hooks)
to automate some aspects of keeping the code base healthy.

Please copy the files from the `githooks` subdirectory into `.git/hooks` to
enable them.

# Building

## `CLion`

The CMake target can be configured to use the toolchain from the nxdk:

* CMake options

    `-DCMAKE_TOOLCHAIN_FILE=<absolute_path_to_nxdk>/share/toolchain-nxdk.cmake`

* Environment

    `NXDK_DIR=<absolute_path_to_nxdk>`

On macOS you may also have to modify PATH such that a homebrew version of LLVM
is preferred over Xcode's (to supply `dlltool`).
