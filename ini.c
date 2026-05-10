#include "ini.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static char *strip(char *s) {
  char *end;
  while (*s && isspace((unsigned char) *s)) s++;
  end = s + strlen(s);
  while (end > s && isspace((unsigned char) end[-1])) end--;
  *end = '\0';
  return s;
}

static void strip_inline_comment(char *s) {
  for (char *p = s; *p; p++) {
    if (*p == ';' || *p == '#') {
      *p = '\0';
      return;
    }
  }
}

int ini_load(const char *path, ini_handler_t handler, void *user_data) {
  FILE *fp = fopen(path, "r");
  if (fp == NULL) return -1;

  char line[INI_LINE_MAX];
  char section[128] = "";
  int rc = 0;

  while (fgets(line, sizeof(line), fp) != NULL) {
    strip_inline_comment(line);
    char *trimmed = strip(line);

    if (*trimmed == '\0') continue;

    if (*trimmed == '[') {
      char *end = strchr(trimmed, ']');
      if (end == NULL) continue;  // malformed, skip
      *end = '\0';
      char *name = strip(trimmed + 1);
      snprintf(section, sizeof(section), "%s", name);
      continue;
    }

    char *eq = strchr(trimmed, '=');
    if (eq == NULL) continue;
    *eq = '\0';
    char *key = strip(trimmed);
    char *value = strip(eq + 1);

    if (*key == '\0') continue;
    if (*section == '\0') continue;  // no section yet, drop

    rc = handler(section, key, value, user_data);
    if (rc != 0) break;
  }

  fclose(fp);
  return rc;
}
