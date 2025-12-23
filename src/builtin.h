/**
 * @file builtin.h
 * @brief Interface for shell builtin commands.
 *
 * Provides execution of builtin commands (cd, exit) that must run
 * in the shell process rather than forked children.
 */

#ifndef BUILTIN_H
#define BUILTIN_H

#include "parser.h"

/**
 * @brief Execute a builtin command if applicable.
 *
 * Builtins may need to run in the shell process to affect shell state
 * (e.g., cd, exit). When executed in a forked child, state changes do
 * not propagate back to the shell.
 *
 * @param cmd  Command structure to execute.
 * @return     0 if builtin executed successfully,
 *             1 if cmd is not a builtin,
 *             2 to signal shell exit,
 *            -1 on error.
 */
int builtin_exec(Command *cmd);

#endif /* BUILTIN_H */
