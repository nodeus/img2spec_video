#ifndef IMG2SPEC_NONWIN32_H
#define IMG2SPEC_NONWIN32_H

#define ALL_FILES "*"
#define PATH_SEP "/"

#ifdef __APPLE__
#define ALL_FILES "*.*"
#endif

#include <limits.h>
#include <stdio.h>
#include <unistd.h>

#define MAX_PATH PATH_MAX
#define _snprintf snprintf
#define INVALID_FILE_ATTRIBUTES ((unsigned)-1)
#define GetCurrentDirectoryA(n, buf) getcwd((buf), (n))
#define GetFileAttributesA(p) (access((p), F_OK) == 0 ? 0u : INVALID_FILE_ATTRIBUTES)

#endif //IMG2SPEC_NONWIN32_H
