/**
 * @file tinyshell.c
 * @brief Implementation of TinyShell, a simple but functionall shell program.
 *
 * This implementation was developed for the purposes of the class:
 * Operating Systems,
 * Department of Electrical and Computer Engineering,
 * Aristotle University of Thessaloniki.
 *
 * Usage: ./tinyshell
 * It takes no input arguments.
 */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "error.h"
#include "parser.h"
#include "pipeline.h"

/* ======== Constant Definitions ======== */

#define INPUT_MAX 4096

/* ======== Global Variables Definitions ======== */

/**
 * @brief Global variable storing the program name for error messages.
 *
 * This variable should be initialized by calling `set_program_name()`
 * before any calls to `print_error()`.
 */
const char *program_name = "tinyshell";

/**
 * @brief Global variable storing the environment variables.
 */
extern char **environ;

unsigned int exit_code = 0;

/* ======== Function Prototypes ======== */

static void main_loop(void);
static int  print_prompt(const unsigned int code);

/* ======== Implementation ======== */


/**
 * @brief Main read–eval–print loop of the tiny shell.
 *
 * Repeatedly:
 *   • prints the prompt
 *   • reads a line
 *   • parses it
 *   • handles builtins ("cd", "exit")
 *   • executes external commands
 *
 * Keeps track of the last exit code from executed commands. The "exit"
 * builtin may override this with an explicit exit code.
 *
 * @return The value to be returned from main().
 */
static void
main_loop(void)
{
	char buf[INPUT_MAX];
	Command *cmd;

	while (1) {
		if (print_prompt(exit_code)) {
			exit_code = 255;
			return;
		}

		if (!fgets(buf, INPUT_MAX, stdin)) {
			// EOF
			printf("\n");
			break;
		}

		cmd = parser_parse(buf);

		if (cmd->argc <= 0)
			continue;
		
		if (execute_pipeline(cmd) == -1) {
			while (parser_free_cmd(cmd));
			return;
		}
		while (parser_free_cmd(cmd));
	}
	return;
}

/**
 * @brief Print the interactive shell prompt.
 *
 * The prompt has the form:
 *     username@hostname: cwd
 *     [code]->
 *
 * The current working directory is shortened by replacing the user's
 * home directory prefix with '~'.
 *
 * @param code Exit code of the previous command.
 * @return 0 on success, -1 on failure (HOME/USER/getcwd/gethostname issues).
 */
static int
print_prompt(const unsigned int code) {
	char *username = NULL;
	char *home     = NULL;
	char hostname[HOST_NAME_MAX + 1];
	char cwd[PATH_MAX];
	char display_path[PATH_MAX];

	home = getenv("HOME");
	if (!home) {
		print_error(__func__, "getenv() failed for \"HOME\" env variable", 0);
		return -1;
	}
		
	username = getenv("USER");
	if (!username) {
		print_error(__func__, "getenv() failed for \"USER\" env variable", 0);
		return -1;
	}
		
	if (gethostname(hostname, HOST_NAME_MAX)) {
		print_error(__func__, "gethostname() failed", errno);
		return -1;
	}
	hostname[HOST_NAME_MAX] = '\0';

	if (!getcwd(cwd, PATH_MAX)) {
		print_error(__func__, "getcwd() failed", errno);
		return -1;
	}
		
	size_t home_len = strlen(home);
	if (!strncmp(cwd, home, home_len) && (cwd[home_len] == '/' || cwd[home_len] == '\0')) {
		if (cwd[home_len] == '\0') {
			snprintf(display_path, PATH_MAX, "~");
		} else {
			snprintf(display_path, PATH_MAX, "~%s", cwd + home_len);
		}
	} else {
		snprintf(display_path, PATH_MAX, "%s", cwd);
	}
		
	printf("\n%s@%s: %s\n[%u]-> ", username, hostname, display_path, code);
	return 0;
}

/**
 * @brief Program entry point.
 *
 * Initializes the program name, prints the startup banner, and then
 * transfers control to @ref main_loop.
 *
 * @return Exit code from main_loop().
 */
int
main(int argc __attribute__((unused)), char *argv[])
{
	set_program_name(argv[0]);

	printf("TinyShell - Phase 2\n");
	printf("Type 'exit' to quit or press Ctrl+D\n");

	main_loop();

	return exit_code;
}
