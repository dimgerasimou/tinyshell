/**
 * @file main.c
 * @brief Implementation of TinyShell, a simple but functional shell program.
 *
 * This implementation was developed for the purposes of the class:
 * Operating Systems,
 * Department of Electrical and Computer Engineering,
 * Aristotle University of Thessaloniki.
 *
 * Usage: ./tinyshell
 * Takes no input arguments.
 */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "error.h"
#include "parser.h"
#include "pipeline.h"
#include "signal_setup.h"

#define INPUT_MAX 4096
#define EXIT_INTERNAL_ERROR 255

/**
 * @brief Global exit code of the last executed command.
 */
int exit_code = 0;

/**
 * @brief Print the interactive shell prompt.
 *
 * The prompt has the form:
 *     username@hostname: cwd
 *     [exit_code]->
 *
 * The current working directory is shortened by replacing the user's
 * home directory prefix with '~'.
 *
 * @param code  Exit code of the previous command.
 * @return      0 on success, -1 on failure.
 */
static int
print_prompt(unsigned int code)
{
	const char *user;
	const char *home;
	char hostname[HOST_NAME_MAX];
	char cwd[PATH_MAX];
	char display[PATH_MAX];
	size_t home_len;

	home = getenv("HOME");
	if (!home) {
		error_print(__func__, "getenv \"HOME\"", errno);
		return -1;
	}

	user = getenv("USER");
	if (!user) {
		error_print(__func__, "getenv \"USER\"", errno);
		return -1;
	}

	if (gethostname(hostname, sizeof(hostname))) {
		error_print(__func__, "gethostname", errno);
		return -1;
	}
	hostname[sizeof(hostname) - 1] = '\0';

	if (!getcwd(cwd, sizeof(cwd))) {
		error_print(__func__, "getcwd", errno);
		return -1;
	}

	/* Shorten home prefix to ~ */
	home_len = strlen(home);
	if (!strncmp(cwd, home, home_len) &&
	    (cwd[home_len] == '/' || cwd[home_len] == '\0')) {
		snprintf(display, sizeof(display), "~%s", cwd + home_len);
	} else {
		snprintf(display, sizeof(display), "%s", cwd);
	}

	printf("\n%s@%s: %s\n[%u]-> ", user, hostname, display, code);
	fflush(stdout);
	return 0;
}

/**
 * @brief Main read-eval-print loop of the shell.
 *
 * Repeatedly prints the prompt, reads a line, parses it,
 * and executes the resulting pipeline. Runs until EOF or
 * the exit builtin is invoked.
 */
static void
main_loop(void)
{
	char buf[INPUT_MAX];
	Command *cmd;
	int ret;

	while (1) {
		if (print_prompt(exit_code)) {
			exit_code = EXIT_INTERNAL_ERROR;
			return;
		}

		if (!fgets(buf, sizeof(buf), stdin)) {
			if (feof(stdin)) {
				printf("\n");
				break;
			}
			/* Interrupted by signal (Ctrl+C), print newline and reprompt */
			clearerr(stdin);
			printf("\n");
			continue;
		}

		cmd = parser_parse(buf);
		if (!cmd || cmd->argc <= 0)
			continue;

		ret = execute_pipeline(cmd);
		parser_free_cmd(cmd);

		if (ret == 1) {
			/* exit builtin was invoked */
			return;
		}

		if (ret == -1) {
			/* fatal error */
			return;
		}
	}
}

/**
 * @brief Program entry point.
 *
 * Initializes error reporting and signal handlers, then enters
 * the main loop.
 *
 * @return Exit code of last command.
 */
int
main(int argc __attribute__((unused)), char *argv[])
{
	error_set_name(argv[0]);

	if (signal_setup()) {
		return EXIT_INTERNAL_ERROR;
	}

	main_loop();
	return exit_code;
}
