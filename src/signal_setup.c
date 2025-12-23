/**
 * @file signal_setup.c
 * @brief Signal handling implementation for TinyShell.
 *
 * Phase 3 notes:
 *   - The shell must not be killed/stopped by Ctrl-C/Ctrl-Z while waiting
 *     for input.
 *   - Foreground jobs must receive terminal signals (SIGINT, SIGTSTP).
 *   - Background jobs must not receive terminal signals.
 *
 * We achieve this by:
 *   - Placing the shell in its own process group (interactive mode)
 *   - Transferring terminal control (tcsetpgrp) to foreground job PGIDs
 *   - Ignoring SIGTSTP/SIGTTIN/SIGTTOU in the shell
 *   - Installing a SIGCHLD handler to reap and record job state changes
 */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <signal.h>
#include <termios.h>
#include <unistd.h>

#include "signal_setup.h"
#include "error.h"

/* Provided by pipeline.c (job-control module). */
extern void jobs_sigchld_reap(void);

/**
 * @brief Empty signal handler for SIGINT.
 *
 * Does nothing, but having a handler (vs SIG_IGN) allows
 * blocking calls like fgets() to be interrupted.
 */
static void
sigint_handler(int sig)
{
	(void)sig;
}

/**
 * @brief SIGCHLD handler: reap children and update job state.
 */
static void
sigchld_handler(int sig)
{
	(void)sig;
	jobs_sigchld_reap();
}

static int
install_handler(int signum, void (*handler)(int), int flags)
{
	struct sigaction sa;

	sa.sa_handler = handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = flags;

	if (sigaction(signum, &sa, NULL) == -1)
		return -1;

	return 0;
}

static int
install_ignore(int signum)
{
	return install_handler(signum, SIG_IGN, 0);
}

/**
 * @brief Set up signal handlers for the shell.
 */
int
signal_setup(void)
{
	/* Interactive job control is only relevant with a controlling terminal. */
	if (isatty(STDIN_FILENO)) {
		/* Put shell in its own process group. */
		if (setpgid(0, 0) == -1) {
			/* If already a process group leader, setpgid can fail with EPERM. */
			if (errno != EPERM) {
				error_print(__func__, "setpgid", errno);
				return -1;
			}
		}

		/* Ensure the shell owns the terminal before entering the REPL. */
		if (tcsetpgrp(STDIN_FILENO, getpgrp()) == -1) {
			/* If this fails in your environment, the shell still works without
			 * terminal control, but Ctrl-C/Ctrl-Z forwarding may be limited.
			 */
			error_print(__func__, "tcsetpgrp", errno);
			/* Non-fatal: continue. */
		}
	}

	/* Shell should not stop when touching the terminal while a job runs. */
	if (install_ignore(SIGTSTP) == -1) {
		error_print(__func__, "sigaction SIGTSTP", errno);
		return -1;
	}
	if (install_ignore(SIGTTIN) == -1) {
		error_print(__func__, "sigaction SIGTTIN", errno);
		return -1;
	}
	if (install_ignore(SIGTTOU) == -1) {
		error_print(__func__, "sigaction SIGTTOU", errno);
		return -1;
	}

	/* Ctrl-C should interrupt fgets() but not kill the shell. */
	if (install_handler(SIGINT, sigint_handler, 0) == -1) {
		error_print(__func__, "sigaction SIGINT", errno);
		return -1;
	}

	/* Reap child processes and track job states. */
	if (install_handler(SIGCHLD, sigchld_handler, SA_RESTART) == -1) {
		error_print(__func__, "sigaction SIGCHLD", errno);
		return -1;
	}

	return 0;
}

/**
 * @brief Restore default signal handlers for child processes.
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
	sigaction(SIGTSTP, &sa, NULL);
	sigaction(SIGTTIN, &sa, NULL);
	sigaction(SIGTTOU, &sa, NULL);
	sigaction(SIGCHLD, &sa, NULL);
}
