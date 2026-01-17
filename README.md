# Vib-OS

**ARM64 Operating System with GUI**

![Build Status](https://img.shields.io/badge/build-passing-brightgreen)
![Platform](https://img.shields.io/badge/platform-ARM64-blue)
![Apple Silicon](https://img.shields.io/badge/Apple%20Silicon-M1%20%7C%20M2%20%7C%20M3-orange)
![Raspberry Pi](https://img.shields.io/badge/Raspberry%20Pi-4%20%7C%205-red)
![Lines](https://img.shields.io/badge/lines-18k+-yellow)
![License](https://img.shields.io/badge/license-MIT-green)

```
        _  _         ___  ____ 
 __   _(_)| |__     / _ \/ ___| 
 \ \ / / || '_ \   | | | \___ \ 
  \ V /| || |_) |  | |_| |___) |
   \_/ |_||_.__/    \___/|____/ 

Vib-OS v0.5.0 - ARM64 with Full GUI
```

## Overview

Vib-OS is a from-scratch,  Unix-like operating system for ARM64. Built with **18,000+ lines** of C and Assembly:

- ✅ **macOS-style GUI** - Window manager with traffic light buttons, dock, menu bar
- ✅ **Double-Buffered Compositor** - Flicker-free rendering
- ✅ **Virtio Input** - Mouse (tablet) and keyboard support
- ✅ **Applications** - Terminal, Calculator, File Manager, Notepad
- ✅ **Full TCP/IP Stack** - Ethernet, ARP, IP, TCP, UDP, DNS
- ✅ **Apple Silicon + Raspberry Pi** - M1/M2/M3 and Pi 4/5

## Quick Start

### Build & Run with GUI

```bash
git clone git@github.com:viralcode/vib-OS.git
cd vib-OS

make kernel
make run-gui    # Launch with GUI display
```

### Run in QEMU (Terminal Only)

```bash
make run        # Text mode
```

## Screenshot

The OS features a modern macOS-inspired desktop with:
- **Menu Bar** - Vib-OS branding, File/Edit/View/Help menus, clock
- **Dock** - Quick launch: Terminal, Files, Calculator, Notepad, Help
- **Windows** - Draggable with traffic light buttons (close/minimize/maximize)
- **Double Buffering** - Smooth, tear-free rendering

## Features

### GUI System
| Component | Status |
|-----------|--------|
| Window Manager | ✅ Draggable windows, focus, z-order |
| Traffic Light Buttons | ✅ Close (×), Minimize (−), Maximize (+) |
| Menu Bar | ✅ Click to open apps |
| Dock | ✅ 5 app launchers with icons |
| Compositor | ✅ Double-buffered, optimized |

### Applications
| App | Features |
|-----|----------|
| **Terminal** | VT100 emulation, command prompt |
| **Calculator** | Full arithmetic (+−×÷), chained operations |
| **Files** | Directory browser |
| **Notepad** | Text editor with keyboard input |
| **Help** | Usage instructions |

### Input Drivers
| Device | Status |
|--------|--------|
| Virtio Tablet (mouse) | ✅ Absolute positioning |
| Virtio Keyboard | ✅ Scancode to ASCII |
| UART | ✅ Serial console input |

### Kernel
| Component | Status |
|-----------|--------|
| MMU (4-level pages) | ✅ |
| GIC v3 Interrupts | ✅ |
| Buddy Allocator PMM | ✅ |
| Kernel Heap (kmalloc) | ✅ 8MB heap |
| Scheduler | ✅ |

### Filesystems
| FS | Status |
|----|--------|
| VFS + ramfs | ✅ |
| ext4 | ✅ |
| APFS (read-only) | ✅ |

### Networking
| Layer | Status |
|-------|--------|
| Ethernet/ARP/IP/ICMP | ✅ |
| UDP/TCP | ✅ |
| DNS resolver | ✅ |

## Project Structure

```
vib-OS/
├── kernel/
│   ├── core/          # Main, panic, init
│   ├── gui/           # Window manager, compositor
│   │   ├── window.c   # Windows, dock, menu bar
│   │   ├── terminal.c # VT100 terminal
│   │   └── font.c     # 8x16 bitmap font
│   ├── mm/            # Memory management
│   └── net/           # TCP/IP stack
├── drivers/
│   ├── input/         # Virtio tablet & keyboard
│   ├── gpu/           # Framebuffer, ramfb
│   └── platform/      # RPi, Apple Silicon
├── userspace/         # Init, shell
└── scripts/           # Build utilities
```

## Build Commands

```bash
make clean          # Clean build
make kernel         # Build kernel only
make all            # Build everything
make run            # Run text mode
make run-gui        # Run with GUI display
```

## Platforms

| Platform | Status |
|----------|--------|
| QEMU ARM64 | ✅ Primary target |
| UTM (macOS) | ✅ Apple Silicon |
| Apple M1/M2/M3 | ✅ |
| Raspberry Pi 4/5 | ✅ |

## License

MIT License

---

**Vib-OS** - Built with ❤️ for ARM64
