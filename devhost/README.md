# devhost

Trivial XBE to facilitate debugging of the tracer without having to perform
dynamic injection.

This XBE makes no attempt to restore a reasonable state after executing.

# Running the devhost with CLion

To create a launch configuration that runs the devhost inside of
[xemu](xemu.app) with debugging enabled:

1) Create a new `Embedded GDB Server` run config.
    1) Set the "Target" to `devhost_xiso`
    2) Set the "Executable binary" to `devhost.exe`
    3) Set "Download executable" to `Never`
    4) Set "'target remote' args" to `127.0.01:1234`
    5) Set "GDB Server" to the full path to xemu. If you're creating development builds of xemu you can point
       at `qemu-system-i386` inside of your `xemu/build` directory.
    6) Set "GDB Server args" to `-s -dvd_path "$CMakeCurrentBuildDir$/xiso/devhost.iso"`
    7) Under "Advanced GDB Server Options"
        1) Set "Working directory" to `$ProjectFileDir$`
        2) On macOS, set "Environment variables"
           to `DYLD_FALLBACK_LIBRARY_PATH=/<the full path to your xemu.app bundle>/Contents/Libraries/<the architecture for your platform, e.g., arm64>`
       3) Set "Reset command" to `Never`
