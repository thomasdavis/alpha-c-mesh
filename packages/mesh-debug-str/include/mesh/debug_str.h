/* @ajax/mesh-debug-str: a string quoted the way Rust's `{:?}` prints a `&str`, so the port's error
 * messages read exactly as the Rust's: "items \"x\": ...". */
#ifndef MESH_DEBUG_STR_H
#define MESH_DEBUG_STR_H

#include "mesh/strbuf.h"

#include <stddef.h>

/* Appends s (n bytes, UTF-8) in double quotes, escaping as Rust does: \" \\ \t \r \n \0, other
 * control characters as \u{..}; other characters (printable ASCII and UTF-8) as they are. */
void mdebug_str(msb *b, const char *s, size_t n);
/* The same into a new string. */
char *mdebug(const char *s);

#endif
