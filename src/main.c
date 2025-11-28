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
#include <linux/limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "error.h"
#include "parser.h"

/* ======== Constant Definitions ======== */

#ifndef HOST_NAME_MAX
#define HOST_NAME_MAX 256
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#define INPUT_MAX 1024
#define ARGS_MAX  64

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

/* ======== Function Prototypes ======== */

static int  builtin_cd(char **args);
static int  execute_command(char **args);
static int  expand_tilde(const char *path, char *expanded, size_t size);
static int  find_in_path(const char *command, char *filepath);
static int  main_loop(void);
static int  print_prompt(const unsigned int code);

/* ======== Implementation ======== */

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
	char expanded_path[PATH_MAX];
	char cwd[PATH_MAX];

	if (args[1] == NULL) {
		// cd with no arguments -> go to HOME
		home = getenv("HOME");
		if (!home) {
			print_error(__func__, "HOME not set", 0);
			return 1;
		}
		path = home;
	}
	else if (strcmp(args[1], "-") == 0) {
		// cd - -> go to previous directory (OLDPWD)
		char *oldpwd = getenv("OLDPWD");
		if (!oldpwd) {
			print_error(__func__, "OLDPWD not set", 0);
			return 1;
		}
		path = oldpwd;
		printf("%s\n", path);
	}
	else {
		// cd <path> -> go to specified path (Expand ~)
		if (expand_tilde(args[1], expanded_path, PATH_MAX) == 0)
			path = expanded_path;
		else
			return 1;
	}

	if (getcwd(cwd, PATH_MAX))
		setenv("OLDPWD", cwd, 1);

	if (chdir(path)) {
		print_error("cd", "directory error", errno);
		return 1;
	}

	if (getcwd(cwd, PATH_MAX))
		setenv("PWD", cwd, 1);

	return 0;
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
static int
execute_command(char **args)
{
	pid_t pid;
	int  status;
	char path[PATH_MAX];

	if (!find_in_path(args[0], path)) {
		print_error(args[0], "command not found", 0);
		return 127;
	}

	pid = fork();
	if (pid == -1) {
		print_error(__func__, "fork() failed", errno);
		return 1;
	}

	if (!pid) {
		// Child process - execve replaces process or we exit
		execve(path, args, environ);
		print_error(__func__, "execve() failed", errno);
		exit(1);
	}

	// Parent process
	if (waitpid(pid, &status, 0) == -1) {
		print_error(__func__, "waitpid() failed", errno);
		return 1;
	}

	if (WIFEXITED(status)) {
		return WEXITSTATUS(status);
	} else if (WIFSIGNALED(status)) {
		return 128 + WTERMSIG(status);
	}

	return 0;
}

/**
 * @brief Expand a leading '~' into the user's HOME directory.
 *
 * Supports:
 *   "~"      -> $HOME
 *   "~/foo"  -> $HOME/foo
 *
 * "~username" is intentionally not implemented and is copied verbatim.
 *
 * @param path      Input path string.
 * @param expanded  Output buffer for expanded path.
 * @param size      Maximum size of the output buffer.
 *
 * @return 0 on success, -1 if HOME is not set.
 */
static int
expand_tilde(const char *path, char *expanded, size_t size)
{
	if (path[0] != '~') {
		strncpy(expanded, path, size - 1);
		expanded[size - 1] = '\0';
		return 0;
	}

	char *home = getenv("HOME");
	if (!home) {
		print_error(__func__, "HOME not set", 0);
		return -1;
	}

	if (path[1] == '\0' || path[1] == '/') {
		snprintf(expanded, size, "%s%s", home, path + 1);
		return 0;
	}

	strncpy(expanded, path, size - 1);
	expanded[size - 1] = '\0';
	return 0;
}

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
static int
main_loop(void)
{
	char buf[INPUT_MAX];
	Command *cmd;
	int exit_code = 0;

	while (1) {
		if (print_prompt(exit_code))
			return -1;

		if (!fgets(buf, INPUT_MAX, stdin)) {
			// EOF
			printf("\n");
			break;
		}

		cmd = parser_parse(buf);

		if (cmd->argc <= 0)
			continue;
		
		if (!strcmp(cmd->argv[0], "exit")) {
			if (cmd->argc > 1)
				exit_code = strtol(cmd->argv[1], NULL, 10);
			return exit_code;
		}

		if (!strcmp(cmd->argv[0], "cd")) {
			exit_code = builtin_cd(cmd->argv);
			continue;
		}

		exit_code = execute_command(cmd->argv);
		while (parser_free_cmd(cmd));
	}
	return 0;
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

	printf("TinyShell - Phase 1\n");
	printf("Type 'exit' to quit or press Ctrl+D\n");

	return main_loop();
}
