#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <unistd.h>
#include <sys/stat.h>

#include "bultin_commands.h"

extern int exit_code;

/**
 * @brief Handle the builtin `cd` command.
 *
 * Behavior:
 *   cd            -> change to $HOME
 *   cd <path>     -> change to path (supports ~)
 *   cd -          -> change to $OLDPWD and print it
 *
 * Updates PWD and OLDPWD environment variables.
 *
 * @param args Argument vector where args[0] = "cd", args[1] optional.
 *
 * @return 0 on success, 1 on failure.
 */
static int
builtin_cd(char **args)
{
	const char *path;
	char *home;
	char cwd[PATH_MAX];
	struct stat st;
	
	if (args[1] == NULL) {
		// cd with no arguments -> go to HOME
		home = getenv("HOME");
		if (!home) {
			exit_code = EINVAL;
			fprintf(stderr, "cd: HOME not set\n");
			return 1;
		}
		path = home;
	} else if (strcmp(args[1], "-") == 0) {
		// cd - -> go to previous directory (OLDPWD)
		char *oldpwd = getenv("OLDPWD");
		if (!oldpwd) {
			exit_code = EINVAL;
			fprintf(stderr, "cd: OLDPWD not set\n");
			return 1;
		}
		path = oldpwd;
		printf("%s\n", path);
	} else {
		path = args[1];
	}
	
	// Check if path exists and is a directory
	if (stat(path, &st) == -1) {
		exit_code = errno;
		fprintf(stderr, "cd: %s: %s\n", path, strerror(exit_code));
		return 1;
	}

	if (!S_ISDIR(st.st_mode)) {
		exit_code = ENOTDIR;
		fprintf(stderr, "cd: %s: %s\n", path, strerror(exit_code));
		return 1;
	}
	
	if (getcwd(cwd, PATH_MAX))
		setenv("OLDPWD", cwd, 1);

	if (chdir(path)) {
		exit_code = errno;
		fprintf(stderr, "cd: %s: %s\n", path, strerror(exit_code));
		return 1;
	}

	if (getcwd(cwd, PATH_MAX))
		setenv("PWD", cwd, 1);

	return 0;
}

int
built_in(Command *cmd)
{
	if (!strcmp(cmd->argv[0], "exit")) {
		if (cmd->argc > 1)
			exit_code = strtol(cmd->argv[1], NULL, 10);
		return -1;
	}

	if (!strcmp(cmd->argv[0], "cd")) {
		builtin_cd(cmd->argv);
		return 1;
	}

	return 0;
}
