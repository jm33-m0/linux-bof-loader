# linux-bof-loader

A standalone C implementation of a "Beacon Object File" (BOF) loader designed for Linux x86-64 environments.

It simulates the "Beacon Object File" (BOF) execution model found in tools like Cobalt Strike or Sliver but adapted for Linux ELF binaries. It includes a custom argument packing protocol that allows you to pass integers, shorts, strings, and binary blobs from the command line directly to your C payload.

## Features

- **ELF Loading:** Parses and loads x64 ELF relocatable objects (`ET_REL`) into memory.
- **Dynamic Relocation:** Handles standard x64 relocations (`R_X86_64_64`, `PC32`, `PLT32`) to ensure code executes correctly at arbitrary memory addresses.
- **Symbol Resolution:** Automatically resolves external function calls (like `malloc`, `snprintf`) using the host's standard library via `dlsym`.
- **Argument Packing:** Includes a built-in CLI argument packer that serializes arguments into a binary format compatible with the included payload parser.

## File Overview

- **`loader_linux_amd64.c`**: The main loader. It reads `c.o`, maps it to memory, resolves symbols, packs arguments from the command line, and executes the `go` function.
- **`c.c`**: An example payload (BOF). It includes a small header-only parsing library to extract arguments passed by the loader and returns a formatted string.

## Building

You will need `gcc` to compile both the loader and the payload.

### 1. Compile the Loader

The loader requires the `dl` library for dynamic symbol resolution.

```bash
gcc loader_linux_amd64.c -o loader -ldl

```

### 2. Compile the Payload

The payload must be compiled as a position-independent relocatable object (`.o`). **Do not link it.**

```bash
gcc -c c.c -o c.o -fPIC

```

> **Note:** The loader is currently hardcoded to look for a file named `c.o` in the current working directory.

## Usage

Run the loader and pass arguments in the format `type:value`.

The example payload (`c.c`) expects arguments in this specific order:

1. **Integer** (ID)
2. **Short** (Age)
3. **String** (Name)

### Command Syntax

```bash
./loader [arg1] [arg2] ...

```

### Supported Argument Types

| Prefix   | Type           | Description             | Example      |
| -------- | -------------- | ----------------------- | ------------ |
| `int:`   | 4-byte Integer | standard integer        | `int:1234`   |
| `short:` | 2-byte Short   | short integer           | `short:42`   |
| `str:`   | String         | null-terminated string  | `str:Hello`  |
| `bin:`   | Binary         | hex-encoded byte string | `bin:aabbcc` |

### Example Run

```bash
./loader example.o go int:1337 short:25 str:Alice

```

**Expected Output:**

```text
Invoking go at 0x7f...
Result: [1337] Hello, Alice (25)!

```

## How It Works

1. **Packing:** The loader parses your command line arguments (e.g., `int:10`) and writes them into a contiguous binary buffer, prepended by the total size.
2. **Loading:** It reads `c.o`, allocates memory using `mmap`, and copies the ELF sections.
3. **Linking:** It iterates through the relocation tables, calculating addresses for internal symbols and resolving external symbols (like `snprintf` used in `c.c`) against the host process.
4. **Execution:** It jumps to the `go` entry point, passing the packed argument buffer. The payload uses `BeaconData*` helper functions to unpack the values and execute its logic.
