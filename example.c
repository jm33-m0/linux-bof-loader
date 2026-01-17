#include <stdio.h>
#include <stdlib.h>

#include <stdint.h>
#include <string.h>

// The parsing context
typedef struct {
  char *buffer; // Current pointer
  int length;   // Remaining length
} datap;

// Initialize the parser
// We skip the first 4 bytes (total size header) to match standard tooling
static inline void BeaconDataParse(datap *parser, char *buffer, int size) {
  if (!parser || !buffer)
    return;
  parser->buffer = buffer + 4;
  parser->length = size - 4;
}

// Extract a 4-byte Integer
static inline int BeaconDataInt(datap *parser) {
  if (parser->length < 4)
    return 0;
  int32_t val;
  memcpy(&val, parser->buffer, 4);
  parser->buffer += 4;
  parser->length -= 4;
  return (int)val;
}

// Extract a 2-byte Short
static inline short BeaconDataShort(datap *parser) {
  if (parser->length < 2)
    return 0;
  int16_t val;
  memcpy(&val, parser->buffer, 2);
  parser->buffer += 2;
  parser->length -= 2;
  return (short)val;
}

// Extract a Binary Blob (or String)
// Returns a pointer to the data in the buffer.
static inline char *BeaconDataExtract(datap *parser, int *size) {
  if (parser->length < 4)
    return NULL;
  uint32_t len;
  memcpy(&len, parser->buffer, 4);
  parser->buffer += 4;

  char *out = parser->buffer;
  parser->buffer += len;
  parser->length -= (4 + len);

  if (size)
    *size = (int)len;
  return out;
}

char *go(char *args, int size) {
  char *buffer = malloc(128);
  datap parser;
  BeaconDataParse(&parser, args, size);

  // Order matters! Must match how you packed them.
  int id = BeaconDataInt(&parser);
  short age = BeaconDataShort(&parser);
  char *name = BeaconDataExtract(&parser, NULL); // Reads string
  snprintf(buffer, 128, "[%d] Hello, %s (%d)!", id, name, age);
  return buffer;
}
