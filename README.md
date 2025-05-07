Linux kernel
============

There are several guides for kernel developers and users. These guides can
be rendered in a number of formats, like HTML and PDF. Please read
Documentation/admin-guide/README.rst first.

In order to build the documentation, use ``make htmldocs`` or
``make pdfdocs``.  The formatted documentation can also be read online at:

    https://www.kernel.org/doc/html/latest/

There are various text files in the Documentation/ subdirectory,
several of them using the Restructured Text markup notation.

Please read the Documentation/process/changes.rst file, as it contains the
requirements for building and running the kernel, and information about
the problems which may result by upgrading your kernel.

Build status for rpi-6.1.y:
[![Pi kernel build tests](https://github.com/raspberrypi/linux/actions/workflows/kernel-build.yml/badge.svg?branch=rpi-6.1.y)](https://github.com/raspberrypi/linux/actions/workflows/kernel-build.yml)
[![dtoverlaycheck](https://github.com/raspberrypi/linux/actions/workflows/dtoverlaycheck.yml/badge.svg?branch=rpi-6.1.y)](https://github.com/raspberrypi/linux/actions/workflows/dtoverlaycheck.yml)

Build status for rpi-6.6.y:
[![Pi kernel build tests](https://github.com/raspberrypi/linux/actions/workflows/kernel-build.yml/badge.svg?branch=rpi-6.6.y)](https://github.com/raspberrypi/linux/actions/workflows/kernel-build.yml)
[![dtoverlaycheck](https://github.com/raspberrypi/linux/actions/workflows/dtoverlaycheck.yml/badge.svg?branch=rpi-6.6.y)](https://github.com/raspberrypi/linux/actions/workflows/dtoverlaycheck.yml)

Build status for rpi-6.12.y:
[![Pi kernel build tests](https://github.com/raspberrypi/linux/actions/workflows/kernel-build.yml/badge.svg?branch=rpi-6.12.y)](https://github.com/raspberrypi/linux/actions/workflows/kernel-build.yml)
[![dtoverlaycheck](https://github.com/raspberrypi/linux/actions/workflows/dtoverlaycheck.yml/badge.svg?branch=rpi-6.12.y)](https://github.com/raspberrypi/linux/actions/workflows/dtoverlaycheck.yml)

Pamir AI Kernel
==============

This is the Pamir AI optimized kernel for Raspberry Pi CM5 and Distiller. It includes enhancements for performance and support for Pamir AI hardware.

## Build Status

[![Kernel Release CI](https://github.com/Pamir-AI/Distiller-CM5-Kernel/actions/workflows/kernel-release.yml/badge.svg)](https://github.com/Pamir-AI/Distiller-CM5-Kernel/actions/workflows/kernel-release.yml)

## Building the Kernel

The Pamir AI kernel can be built using our custom build script:

```bash
# Build with default settings (GCC)
./scripts/kernel-build

# Build with LTO optimization (recommended)
./scripts/kernel-build --mode lto

# View all build options
./scripts/kernel-build --help
```

## Continuous Integration

We use GitHub Actions for automated builds and releases:

1. **Kernel Release CI**: Builds and releases kernel packages automatically
   - Runs on manual trigger from the Actions tab
   - Scheduled weekly builds (Mondays at 2:00 AM UTC)
   - LTO mode enabled by default for optimal performance

For more information about our CI setup, see [.github/workflows/README.md](.github/workflows/README.md).
