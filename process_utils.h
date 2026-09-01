/* See LICENSE file for copyright and license details. */
#ifndef PROCESS_UTILS_H
#define PROCESS_UTILS_H

#include <stddef.h>
#include "strbuf.h"

/*
 * Executes a command directly via execvp / CreateProcess without shell interpretation.
 * Eliminates command injection.
 */
int run_cmd_argv(const char *const *argv,
                 const void *stdin_data, size_t stdin_len,
                 struct strbuf *stdout_buf,
                 struct strbuf *stderr_buf,
                 const char *const *envp);

#endif
