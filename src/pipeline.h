/**
 * @file pipeline.h
 * @brief Pipeline execution interface.
 *
 * Provides the main execution engine for shell commands. Handles
 * forking, piping between processes, I/O redirection, and builtin
 * command dispatch.
 */

#ifndef PIPELINE_H
#define PIPELINE_H

#include "parser.h"

/**
 * @brief Execute a pipeline of commands.
 *
 * Forks all commands, connecting them with pipes. Waits for all
 * children and sets exit_code to the last command's exit status.
 *
 * Single builtins without redirects run in the parent process
 * (required for cd, exit to affect shell state).
 *
 * @param pipeline  Linked list of commands.
 * @return          0 on success,
 *                  1 to signal shell should exit,
 *                 -1 on fatal error.
 */
int execute_pipeline(Command *pipeline);

#endif /* PIPELINE_H */
