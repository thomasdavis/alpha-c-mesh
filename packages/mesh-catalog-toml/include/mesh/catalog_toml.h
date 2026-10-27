/* @ajax/mesh-catalog-toml: what building the program catalog reads from a program.toml, line by line,
 * exactly as alpha-rust-mesh scripts/build-programs.sh's grep and sed do (no TOML parser: the
 * same lines, the same answers). */
#ifndef MESH_CATALOG_TOML_H
#define MESH_CATALOG_TOML_H
#include <stdbool.h>
#include <stddef.h>
/* The first `name = "x"` line's x (sed -n 's/^name *= *"\(.*\)"/\1/p' | head -1). False if none. */
bool mesh_catalog_toml_name(const char *text, char *out, size_t cap);
/* A native program: a line `backends = [ "native" ]` (spaces optional) and nothing else in the list. */
bool mesh_catalog_toml_native(const char *text);
/* A `backends` line exists (grep -q '^backends'). */
bool mesh_catalog_toml_has_backends(const char *text);
/* A `backends = ...` line mentions "b" (grep -q '^backends *=.*"b"'). */
bool mesh_catalog_toml_backend(const char *text, const char *b);
/* The value of a `# key: value` comment line (the script's sed of it). Every match counts, as the
 * script's command substitution keeps every line: here the lines are joined with '\n'). False if none. */
bool mesh_catalog_toml_comment(const char *text, const char *key, char *out, size_t cap);
#endif
