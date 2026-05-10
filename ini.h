// Minimal INI parser. Standalone, no external deps.
//
// Format supported:
//   [section]
//   key = value     ; line comment
//   # also comment
// Blank lines and surrounding whitespace are ignored.
//
// Usage:
//   int cb(const char *section, const char *key, const char *value, void *ud) {
//     // ... return 0 to keep going, non-zero to stop
//   }
//   ini_load("pet.ini", cb, &my_data);
//
// Limitations:
//   - line length capped at INI_LINE_MAX (1024 bytes)
//   - no quoted strings, no escapes, no multi-line values
//   - no key/value when there is no [section] header (lines silently dropped)

#ifndef INI_H
#define INI_H

#define INI_LINE_MAX 1024

typedef int (*ini_handler_t)(const char *section, const char *key,
                             const char *value, void *user_data);

// Returns 0 on success, -1 if file cannot be opened, or whatever the handler
// returns if it short-circuits.
int ini_load(const char *path, ini_handler_t handler, void *user_data);

#endif
