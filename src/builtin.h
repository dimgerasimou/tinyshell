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
 * Execute a builtin command if applicable.
 *
 * @param cmd  Command structure to execute.
 * @return     0 if builtin is executed normally,
 *             1 if not a builtin, 2 to signal exit,
 *             or -1 on error.
 */
int builtin_exec(Command *cmd);

#endif /* BUILTIN_H */
