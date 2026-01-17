# linux-bof-loader

A standalone C implementation of a "Beacon Object File" (BOF) loader designed for Linux x86-64 environments.

It simulates the "Beacon Object File" (BOF) execution model found in tools like Cobalt Strike or Sliver but adapted for Linux ELF binaries. It includes a custom argument packing protocol that allows you to pass integers, shorts, strings, and binary blobs from the command line directly to your C payload.

## Features

- **ELF Loading:** Parses and loads x64 ELF relocatable objects (`ET_REL`) into memory.
- **Dynamic Relocation:** Handles standard x64 relocations (`R_X86_64_64`, `PC32`, `PLT32`) to ensure code executes correctly at arbitrary memory addresses.
- **Symbol Resolution:** Automatically resolves external function calls (like `malloc`, `snprintf`) using the host's standard library via `dlsym`.
- **Argument Packing:** Includes a built-in CLI argument packer that serializes arguments into a binary format compatible with the included payload parser.

## Building

You will need `gcc` to compile both the loader and the payload.

```bash
make
```

## Usage

Run the loader and pass arguments in the format `type:value`.

The example payload (`example.c`) expects arguments in this specific order:

1. **Integer** (ID)
2. **Short** (Age)
3. **String** (Name)

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
