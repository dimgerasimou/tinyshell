#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <limits.h>
#include <unistd.h>

#include "pipeline.h"
#include "error.h"
#include "builtin_commands.h"

extern char **environ;
extern int exit_code;

/**
 * @brief Search PATH for an executable command.
 *
 * If @p command contains a '/', it is treated as a literal path. Otherwise,
 * each directory in PATH is searched for an executable file.
 *
 * @param command  Command name or path.
 * @param filepath Output buffer (PATH_MAX size) for the resolved executable path.
 *
 * @return 1 if found and executable, 0 if not found.
 */
static int
find_in_path(const char *command, char *filepath)
{
	char *path_env = getenv("PATH");
	char *path_copy;
	char *dir;
	int found = 0;

	// Check if command is already a path
	if (strchr(command, '/')) {
		if (!access(command, X_OK)) {
			strncpy(filepath, command, PATH_MAX - 1);
			filepath[PATH_MAX - 1] = '\0';
			return 1;
		}
		return 0;
	}

	if (!path_env) {
		print_error(__func__, "getenv(\"PATH\") failed", 0);
		return 0;
	}

	path_copy = strdup(path_env);
	if (!path_copy) {
		print_error(__func__, "strdup() failed", errno);
		return 0;
	}

	dir = strtok(path_copy, ":");
	while (dir) {
		snprintf(filepath, PATH_MAX, "%s/%s", dir, command);
		if (!access(filepath, X_OK)) {
			found = 1;
			break;
		}
		dir = strtok(NULL, ":");
	}

	free(path_copy);
	return found;
}

/**
 * @brief Fork and execute an external command.
 *
 * Uses @ref find_in_path to locate the executable. Runs the command via
 * execve(2) in a child process, waits for it in the parent, and returns
 * the command's exit status.
 *
 * On errors, prints diagnostics and returns a conventional shell exit code:
 *   127 if command not found,
 *   1   if fork/exec/wait fails,
 *   128+sig if terminated by signal.
 *
 * @param args NULL-terminated argument array.
 *
 * @return The child exit code (or an error code as above).
 */
int
execute_pipeline(Command *cmd)
{
	pid_t pid;
	int  status;
	char path[PATH_MAX];

	char **args = cmd->argv;

	switch (built_in(cmd)) {
		case -1:
			return -1;
		case 0:
			break;
		default:
			return 0;
	}

	if (!find_in_path(args[0], path)) {
		print_error(args[0], "command not found", 0);
		exit_code = 127;
		return 1;
	}

	pid = fork();
	if (pid == -1) {
		exit_code = errno;
		print_error(__func__, "fork() failed", errno);
		return 1;
	}

	if (!pid) {
		// Child process - execve replaces process or we exit
		execve(path, args, environ);
		exit_code = errno;
		print_error(__func__, "execve() failed", errno);
		exit(1);
	}

	// Parent process
	if (waitpid(pid, &status, 0) == -1) {
		exit_code = errno;
		print_error(__func__, "waitpid() failed", errno);
		return 1;
	}

	if (WIFEXITED(status)) {
		exit_code = WEXITSTATUS(status);
	} else if (WIFSIGNALED(status)) {
		exit_code =  128 + WTERMSIG(status);
	}

	return 0;
}

