/**
 * @file signal_setup.h
 * @brief Signal handling interface for TinyShell.
 *
 * Sets up the shell's signal disposition for interactive use and job control:
 *   - SIGINT is handled (not ignored) so blocking reads (e.g., fgets) can be interrupted
 *     without terminating the shell.
 *   - SIGTSTP/SIGTTIN/SIGTTOU are ignored in the shell to prevent it from being stopped
 *     when the terminal is controlled by foreground jobs.
 *   - SIGCHLD is handled to reap children and update job state.
 *
 * Child processes should restore default signal handlers before exec().
 */

#ifndef SIGNAL_SETUP_H
#define SIGNAL_SETUP_H

/**
 * @brief Set up signal handlers for the shell.
 *
 * Should be called once at startup, before entering the main loop.
 *
 * @return 0 on success, -1 on failure.
 */
int signal_setup(void);

/**
 * @brief Restore default signal handlers for a child process.
 *
 * Should be called in the child after fork() and before exec(), so that
 * terminal-generated signals (SIGINT, SIGTSTP, etc.) affect the program
 * normally.
 */
void signal_restore_defaults(void);

#endif /* SIGNAL_SETUP_H */
