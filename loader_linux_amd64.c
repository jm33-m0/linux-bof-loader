#define _GNU_SOURCE
#include <dlfcn.h>
#include <elf.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
  uint8_t *buf;
  size_t size;
  size_t capacity;
} Buffer;

void buf_write(Buffer *b, void *data, size_t len) {
  if (b->size + len > b->capacity) {
    b->capacity = (b->capacity * 2) + len;
    b->buf = realloc(b->buf, b->capacity);
  }
  memcpy(b->buf + b->size, data, len);
  b->size += len;
}

void buf_write_int(Buffer *b, int val) { buf_write(b, &val, 4); }

void buf_write_short(Buffer *b, short val) { buf_write(b, &val, 2); }

void buf_write_str(Buffer *b, const char *str) {
  int len = strlen(str) + 1;      // +1 for null terminator
  buf_write_int(b, len);          // Length prefix
  buf_write(b, (void *)str, len); // Data
}

void buf_write_binary(Buffer *b, const char *hex) {
  size_t len = strlen(hex) / 2;
  uint8_t *raw = malloc(len);
  for (size_t i = 0; i < len; i++)
    sscanf(hex + 2 * i, "%2hhx", &raw[i]);

  buf_write_int(b, (int)len);
  buf_write(b, raw, len);
  free(raw);
}

// Parses "type:value" strings
Buffer *pack_args(int argc, char **argv) {
  Buffer *b = malloc(sizeof(Buffer));
  b->buf = malloc(128);
  b->size = 0;
  b->capacity = 128;

  // Total size header placeholder (will fill at end)
  buf_write_int(b, 0);

  for (int i = 0; i < argc; i++) {
    char *arg = argv[i];
    char *val = strchr(arg, ':');

    if (!val) {
      fprintf(stderr, "Error: Arg '%s' missing type prefix (e.g. int:10)\n",
              arg);
      return NULL;
    }
    *val = 0; // Split string
    val++;    // Point to value

    if (strcmp(arg, "int") == 0) {
      buf_write_int(b, atoi(val));
    } else if (strcmp(arg, "short") == 0) {
      buf_write_short(b, (short)atoi(val));
    } else if (strcmp(arg, "str") == 0) {
      buf_write_str(b, val);
    } else if (strcmp(arg, "bin") == 0) {
      buf_write_binary(b, val);
    } else {
      fprintf(stderr, "Unknown type: %s\n", arg);
      return NULL;
    }
  }

  // Write final total size (excluding the 4-byte header itself) to index 0
  int total_payload_size = b->size - 4;
  memcpy(b->buf, &total_payload_size, 4);

  return b;
}

// Helper to align values (e.g. to section alignment)
static uint64_t align_up(uint64_t val, uint64_t align) {
  if (align <= 1)
    return val;
  return (val + align - 1) & ~(align - 1);
}

int BOFLoader(char *obj_buf, size_t object_size, const char *func_name,
              int argc, char **argv) {
  Elf64_Ehdr *ehdr = (Elf64_Ehdr *)obj_buf;

  // Validate ELF Header
  if (memcmp(ehdr->e_ident, ELFMAG, SELFMAG) != 0 ||
      ehdr->e_ident[EI_CLASS] != ELFCLASS64 || ehdr->e_machine != EM_X86_64 ||
      ehdr->e_type != ET_REL) {
    fprintf(stderr,
            "Error: Input must be an x86_64 ELF relocatable object (ET_REL)\n");
    return 1;
  }

  Elf64_Shdr *shdrs = (Elf64_Shdr *)(obj_buf + ehdr->e_shoff);
  const char *shstrtab =
      (const char *)(obj_buf + shdrs[ehdr->e_shstrndx].sh_offset);

  // Calculate Layout for SHF_ALLOC sections
  // We map section index -> offset in our new memory block
  uintptr_t *sec_offsets = calloc(ehdr->e_shnum, sizeof(uintptr_t));
  uint64_t total_size = 0;

  for (int i = 0; i < ehdr->e_shnum; i++) {
    if (!(shdrs[i].sh_flags & SHF_ALLOC))
      continue;

    uint64_t align = shdrs[i].sh_addralign;
    total_size = align_up(total_size, align);
    sec_offsets[i] = total_size;
    total_size += shdrs[i].sh_size;
  }

  if (total_size == 0) {
    fprintf(stderr, "No allocatable sections found.\n");
    return 1;
  }

  // Allocate Executable Memory (Start as RW-, finalize to R-X later)
  // mmap returns page-aligned memory, which is required for mprotect.
  uint8_t *mem_base = mmap(NULL, total_size, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (mem_base == MAP_FAILED) {
    return -1;
  }

  // Copy section data into allocated memory
  for (int i = 0; i < ehdr->e_shnum; i++) {
    if (!(shdrs[i].sh_flags & SHF_ALLOC))
      continue;

    if (shdrs[i].sh_type != SHT_NOBITS) {
      memcpy(mem_base + sec_offsets[i], obj_buf + shdrs[i].sh_offset,
             shdrs[i].sh_size);
    } else {
      // SHT_NOBITS (like .bss) is already zeroed by mmap
    }
  }

  // Locate Symbol Table
  Elf64_Sym *symtab = NULL;
  const char *strtab = NULL;
  int num_syms = 0;

  for (int i = 0; i < ehdr->e_shnum; i++) {
    if (shdrs[i].sh_type == SHT_SYMTAB) {
      symtab = (Elf64_Sym *)(obj_buf + shdrs[i].sh_offset);
      num_syms = shdrs[i].sh_size / sizeof(Elf64_Sym);
      strtab = (const char *)(obj_buf + shdrs[shdrs[i].sh_link].sh_offset);
      break;
    }
  }

  // Apply Relocations
  for (int i = 0; i < ehdr->e_shnum; i++) {
    if (shdrs[i].sh_type != SHT_RELA)
      continue; // We only support RELA (x86-64 standard)

    // The section inside our memory we are modifying
    uint32_t target_sec_idx = shdrs[i].sh_info;
    if (sec_offsets[target_sec_idx] == 0 &&
        (shdrs[target_sec_idx].sh_flags & SHF_ALLOC) == 0) {
      continue; // Relocation for a section we didn't load (e.g. debug info)
    }

    uintptr_t target_base_offset = sec_offsets[target_sec_idx];
    int num_rels = shdrs[i].sh_size / sizeof(Elf64_Rela);
    Elf64_Rela *rels = (Elf64_Rela *)(obj_buf + shdrs[i].sh_offset);

    for (int r = 0; r < num_rels; r++) {
      Elf64_Rela rel = rels[r];
      uint32_t sym_idx = ELF64_R_SYM(rel.r_info);
      uint32_t type = ELF64_R_TYPE(rel.r_info);

      // Where to write the patch (Address in our process)
      uintptr_t patch_addr =
          (uintptr_t)mem_base + target_base_offset + rel.r_offset;

      // Resolve Symbol Address
      Elf64_Sym sym = symtab[sym_idx];
      uintptr_t sym_addr = 0;

      if (sym.st_shndx == SHN_UNDEF) {
        // External Symbol (e.g., printf)
        const char *name = strtab + sym.st_name;
        // RTLD_DEFAULT finds symbols in the global scope (libc, etc.)
        void *handle = dlsym(RTLD_DEFAULT, name);
        if (!handle) {
          fprintf(stderr, "Unresolved symbol: %s\n", name);
          return -1;
        }
        sym_addr = (uintptr_t)handle;
      } else if (sym.st_shndx == SHN_ABS) {
        sym_addr = sym.st_value;
      } else {
        // Internal Symbol (defined in this object)
        sym_addr =
            (uintptr_t)mem_base + sec_offsets[sym.st_shndx] + sym.st_value;
      }

      // Perform Calculation based on type
      switch (type) {
      case R_X86_64_64: // *p = S + A
        *(uint64_t *)patch_addr = sym_addr + rel.r_addend;
        break;
      case R_X86_64_32: // *p = (uint32)(S + A)
        *(uint32_t *)patch_addr = (uint32_t)(sym_addr + rel.r_addend);
        break;
      case R_X86_64_32S: // *p = (int32)(S + A)
        *(int32_t *)patch_addr = (int32_t)(sym_addr + rel.r_addend);
        break;
      case R_X86_64_PC32:  // *p = S + A - P
      case R_X86_64_PLT32: // Treated same as PC32 here (direct binding)
      {
        int64_t val = (int64_t)sym_addr + rel.r_addend - (int64_t)patch_addr;
        *(uint32_t *)patch_addr = (uint32_t)val;
        break;
      }
      default:
        fprintf(stderr, "Unsupported relocation type: %d\n", type);
        return -1;
      }
    }
  }

  // Finalize Memory Permissions: RW- -> R-X
  // This makes the memory executable and prevents further writing (security).
  if (mprotect(mem_base, total_size, PROT_READ | PROT_EXEC) < 0) {
    return 1;
  }

  // Find and Invoke the requested function
  uintptr_t entry_addr = 0;
  for (int i = 0; i < num_syms; i++) {
    if (strcmp(strtab + symtab[i].st_name, func_name) == 0) {
      if (symtab[i].st_shndx == SHN_UNDEF)
        continue; // Skip undefined
      entry_addr = (uintptr_t)mem_base + sec_offsets[symtab[i].st_shndx] +
                   symtab[i].st_value;
      break;
    }
  }

  if (!entry_addr) {
    fprintf(stderr, "Function '%s' not found in object file.\n", func_name);
    return 1;
  }

  printf("Invoking %s at %p...\n", func_name, (void *)entry_addr);

  // Pack Arguments
  Buffer *arg_buf = pack_args(argc, argv);

  // Cast and Call
  typedef char *(*func_ptr)(uint8_t *, int);
  func_ptr f = (func_ptr)entry_addr;
  char *result = f(arg_buf->buf, arg_buf->size);

  printf("Result: %s\n", result);

  // Cleanup (Optional for simple runner)
  munmap(mem_base, total_size);
  free(sec_offsets);

  return 0;
}

int main(int argc, char **argv) {
  if (argc < 3) {
    fprintf(stderr, "Usage: %s <object_file> <function_name> [args...]\n",
            argv[0]);
    return 1;
  }

  const char *obj_path = argv[1];
  const char *func_name = argv[2];

  // Read Object File into Memory
  FILE *f = fopen(obj_path, "rb");
  if (!f) {
    return 1;
  }

  fseek(f, 0, SEEK_END);
  size_t object_size = ftell(f);
  fseek(f, 0, SEEK_SET);

  char *obj_buf = malloc(object_size);
  if (fread(obj_buf, 1, object_size, f) != object_size) {
    fclose(f);
    free(obj_buf);
    return 1;
  }
  fclose(f);

  // Load and Execute
  int ret = BOFLoader(obj_buf, object_size, func_name, argc - 3, &argv[3]);

  free(obj_buf);
  return ret;
}
