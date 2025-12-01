/**
 * @file signal_setup.h
 * @brief Signal handling interface for TinyShell.
 *
 * Provides functions for setting up and restoring signal handlers.
 * The shell ignores SIGINT to prevent Ctrl+C from killing it,
 * while child processes restore default behavior.
 */

#ifndef SIGNAL_SETUP_H
#define SIGNAL_SETUP_H

/**
 * @brief Set up signal handlers for the shell.
 *
 * Installs handlers to ignore SIGINT in the parent shell process.
 * Should be called once at startup before entering the main loop.
 *
 * @return 0 on success, -1 on failure.
 */
int signal_setup(void);

/**
 * @brief Restore default signal handlers.
 *
 * Restores SIGINT to default behavior. Should be called in child
 * processes after fork() but before exec().
 */
void signal_restore_defaults(void);

#endif /* SIGNAL_SETUP_H */
