/**
 * @file builtin.c
 * @brief Implementation of shell builtin commands.
 *
 * Builtins are commands that affect shell state and cannot be
 * executed as external processes. Currently implements:
 *
 *   cd <dir>   - Change working directory
 *   exit <n>   - Exit the shell with optional status
 *
 * Return conventions for builtin_exec():
 *   -1  Error during builtin execution
 *    0  Builtin executed successfully
 *    1  Command is not a builtin
 *    2  Shell should exit
 */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <unistd.h>
#include <sys/stat.h>

#include "builtin.h"
#include "error.h"

extern int exit_code;

/**
 * @brief Parse a string as an exit code.
 *
 * Converts the string to a long, validates it, and masks to 8 bits
 * (standard shell exit code behavior).
 *
 * @param s  String to parse.
 * @return   Exit code (0-255) on success, -1 on parse failure.
 */
static int
parse_exit_code(const char *s)
{
	char *end;

	errno = 0;
	long v = strtol(s, &end, 10);

	if (end == s || errno == ERANGE || *end != '\0')
		return -1;

	return (unsigned int)(v & 0xFF);
}
/**
 * @brief Handle the builtin `cd` command.
 *
 * Behavior:
 *   cd            -> change to $HOME
 *   cd <path>     -> change to path
 *   cd -          -> change to $OLDPWD and print it
 *
 * Updates PWD and OLDPWD environment variables on success.
 *
 * @param cmd  Command structure with argv and argc.
 * @return     0 on success, -1 on failure.
 */
static int
builtin_cd(Command *cmd)
{
	struct stat st;
	char *path;
	char cwd[PATH_MAX];

	if (cmd->argc > 2) {
		error_print("cd", "too many arguments", 0);
		exit_code = 1;
		return -1;
	}

	if (cmd->argc == 1) {
		/* cd with no arguments -> go to HOME */
		const char *env = getenv("HOME");
		if (!env) {
			exit_code = 1;
			error_print("cd", "\"HOME\" env variable not set", 0);
			return -1;
		}

		path = strdup(env);
	} else if (!strcmp(cmd->argv[1], "-")) {
		/* cd - -> go to previous directory (OLDPWD) */
		const char *env = getenv("OLDPWD");
		if (!env) {
			exit_code = 1;
			error_print("cd", "\"OLDPWD\" env variable not set", 0);
			return -1;
		}

		path = strdup(env);
	} else {
		path = strdup(cmd->argv[1]);
	}

	if (!path) {
		error_print("cd", "strdup", errno);
		exit_code = 1;
		return -1;
	}

	/* check if path exists, is a directory and with exec permision*/
	if (stat(path, &st)) {
		error_print("cd", path, errno);
		exit_code = 1;
		free(path);
		return -1;
	}

	if (!S_ISDIR(st.st_mode)) {
		error_print("cd", path, ENOTDIR);
		exit_code = 1;
		free(path);
		return -1;
	}

	if (access(path, X_OK)) {
		error_print("cd", path, errno);
		exit_code = 1;
		free(path);
		return -1;
	}
	
	if (!getcwd(cwd, sizeof(cwd))) {
		error_print("cd", "getcwd", errno);
		exit_code = 1;
		free(path);
		return -1;
	}

	/* actually change directory */
	if (chdir(path)) {
		error_print("cd", path, errno);
		exit_code = 1;
		free(path);
		return -1;
	}

	/* cd - prints the new directory */
	if (cmd->argc == 2 && !strcmp(cmd->argv[1], "-"))
		puts(path);

	free(path);

	/* set oldpwd as cwd */
	if (setenv("OLDPWD", cwd, 1)) {
		error_print("cd", "setenv \"OLDPWD\"", errno);
		exit_code = 1;
		return -1;
	}

	/* set new pwd */
	if (!getcwd(cwd, sizeof(cwd))) {
		error_print("cd", "getcwd", errno);
		exit_code = 1;
		return -1;
	}

	if (setenv("PWD", cwd, 1)) {
		error_print("cd", "setenv \"PWD\"", errno);
		exit_code = 1;
		return -1;
	}

	exit_code = 0;
	return 0;
}

/**
 * @brief Handle the builtin `exit` command.
 *
 * Behavior:
 *   exit       -> exit with code 0
 *   exit <n>   -> exit with code (n & 0xFF)
 *
 * @param cmd  Command structure with argv and argc.
 * @return     2 to signal shell exit, -1 on error.
 */
static int
builtin_exit(Command *cmd)
{
	if (cmd->argc == 1) {
		exit_code = 0;
		return 2;
	}
	
	if (cmd->argc == 2) {
		int code = parse_exit_code(cmd->argv[1]);
		if (code < 0) {
			size_t len = strlen(cmd->argv[1]) + strlen(": numeric argument required") + 1;
			char *msg = malloc(len * sizeof(char));
			
			if (!msg) {
				error_print("exit", "malloc", errno);
				exit_code = 1;
				return -1;
			}

			snprintf(msg, len, "%s: numeric argument required", cmd->argv[1]);

			error_print("exit", msg, 0);
			free(msg);
			exit_code = 2;
			return -1;
		}
		exit_code = code;
		return 2;
	}

	error_print("exit", "too many arguments", 0);
	exit_code = 1;
	return -1;
}

/**
 * Execute a builtin command if applicable.
 *
 * @param cmd  Command structure to execute.
 * @return     0 if builtin is executed normally,
 *             1 if not a builtin, 2 to signal exit,
 *             or -1 on error.
 */
int
builtin_exec(Command *cmd)
{
	if (!strcmp(cmd->argv[0], "exit"))
		return builtin_exit(cmd);

	if (!strcmp(cmd->argv[0], "cd"))
		return builtin_cd(cmd);

	return 1;
}
