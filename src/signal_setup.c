/**
 * @file signal_setup.c
 * @brief Signal handling implementation for TinyShell.
 *
 * Sets up signal handlers so that:
 *   - The shell ignores SIGINT (Ctrl+C doesn't kill the shell)
 *   - Child processes receive default SIGINT behavior
 */

#define _POSIX_C_SOURCE 200809L

#include <signal.h>
#include <errno.h>
#include <stddef.h>

#include "signal_setup.h"
#include "error.h"

/**
 * @brief Empty signal handler for SIGINT.
 *
 * Does nothing, but having a handler (vs SIG_IGN) allows
 * blocking calls like read() to be interrupted.
 *
 * @param sig  Signal number (unused).
 */
static void
sigint_handler(int sig)
{
	(void)sig;
	/* Empty - just interrupts blocking syscalls */
}

/**
 * @brief Set up signal handlers for the shell.
 *
 * Installs an empty SIGINT handler so Ctrl+C interrupts blocking
 * reads but doesn't kill the shell. Child processes restore
 * default behavior after fork().
 *
 * @return 0 on success, -1 on failure.
 */
int
signal_setup(void)
{
	struct sigaction sa;

	sa.sa_handler = sigint_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;  /* No SA_RESTART - allow interruption */

	if (sigaction(SIGINT, &sa, NULL) == -1) {
		error_print(__func__, "sigaction SIGINT", errno);
		return -1;
	}

	return 0;
}

/**
 * @brief Restore default signal handlers.
 *
 * Called in child processes after fork() to restore normal
 * signal behavior before exec().
 */
void
signal_restore_defaults(void)
{
	struct sigaction sa;

	sa.sa_handler = SIG_DFL;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;

	/* Ignore errors - we're in a child about to exec anyway */
	sigaction(SIGINT, &sa, NULL);
}
