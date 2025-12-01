/**
 * @file pipeline.c
 * @brief Pipeline execution for TinyShell.
 *
 * Executes command pipelines with support for:
 *   - Multiple piped commands
 *   - Input/output/stderr redirection on any command
 *   - Builtin commands (in parent for single commands, child for pipelines)
 */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <limits.h>
#include <unistd.h>

#include "pipeline.h"
#include "error.h"
#include "builtin.h"
#include "signal_setup.h"

extern char **environ;
extern int exit_code;

#define DEFAULT_FILE_MODE 0644

/**
 * @brief Search PATH for an executable command.
 *
 * If @p cmd contains a '/', it is treated as a literal path. Otherwise,
 * each directory in PATH is searched for an executable file.
 *
 * @param cmd   Command name or path.
 * @param path  Output buffer (PATH_MAX size) for the resolved executable path.
 * @return      0 if found and executable, -1 if not found.
 */
static int
get_path(const char *cmd, char *path)
{
	const char *env;
	char *temp;
	char *tok;

	if (strchr(cmd, '/')) {
		if (access(cmd, X_OK))
			return -1;

		strncpy(path, cmd, PATH_MAX - 1);
		path[PATH_MAX - 1] = '\0';
		return 0;
	}

	env = getenv("PATH");
	if (!env) {
		error_print(__func__, "getenv \"PATH\"", errno);
		return -1;
	}

	temp = strdup(env);
	if (!temp) {
		error_print(__func__, "strdup", errno);
		return -1;
	}

	tok = strtok(temp, ":");
	while (tok) {
		snprintf(path, PATH_MAX, "%s/%s", tok, cmd);
		if (!access(path, X_OK)) {
			free(temp);
			return 0;
		}
		tok = strtok(NULL, ":");
	}

	free(temp);
	return -1;
}

/**
 * @brief Set up I/O redirections for a command.
 *
 * Handles stdin, stdout, and stderr redirects. For pipelines:
 *   - Input redirect overrides pipe input (typically on first command)
 *   - Output redirect overrides pipe output (typically on last command)
 *   - Stderr redirect can be on any command
 *
 * @param cmd  Command with redirect info.
 * @return     0 on success, -1 on failure (with error printed).
 */
static int
setup_redirects(Command *cmd)
{
	int fd;
	int flags;

	/* Input redirect */
	if (cmd->redirect[REDIR_STDIN]) {
		fd = open(cmd->redirect[REDIR_STDIN], O_RDONLY);
		if (fd == -1) {
			error_print("open", cmd->redirect[REDIR_STDIN], errno);
			return -1;
		}
		if (dup2(fd, STDIN_FILENO) == -1) {
			error_print("dup2", "stdin redirect", errno);
			close(fd);
			return -1;
		}
		close(fd);
	}

	/* Output redirect */
	if (cmd->redirect[REDIR_STDOUT]) {
		flags = O_WRONLY | O_CREAT;
		flags |= (cmd->append & APPEND_STDOUT) ? O_APPEND : O_TRUNC;
		fd = open(cmd->redirect[REDIR_STDOUT], flags, DEFAULT_FILE_MODE);
		if (fd == -1) {
			error_print("open", cmd->redirect[REDIR_STDOUT], errno);
			return -1;
		}
		if (dup2(fd, STDOUT_FILENO) == -1) {
			error_print("dup2", "stdout redirect", errno);
			close(fd);
			return -1;
		}
		close(fd);
	}

	/* Stderr redirect */
	if (cmd->redirect[REDIR_STDERR]) {
		flags = O_WRONLY | O_CREAT;
		flags |= (cmd->append & APPEND_STDERR) ? O_APPEND : O_TRUNC;
		fd = open(cmd->redirect[REDIR_STDERR], flags, DEFAULT_FILE_MODE);
		if (fd == -1) {
			error_print("open", cmd->redirect[REDIR_STDERR], errno);
			return -1;
		}
		if (dup2(fd, STDERR_FILENO) == -1) {
			error_print("dup2", "stderr redirect", errno);
			close(fd);
			return -1;
		}
		close(fd);
	}

	return 0;
}

/**
 * @brief Execute a child process.
 *
 * Called after fork() in the child. Sets up pipes and redirects,
 * handles builtins, or execs external command. Never returns.
 *
 * @param cmd       Command to execute.
 * @param prev_fd   Read end of previous pipe (-1 if first command).
 * @param pipe_fd   Current pipe fds, or NULL if last command.
 */
static void
execute_child(Command *cmd, int prev_fd, int pipe_fd[2])
{
	char path[PATH_MAX];
	int builtin_ret;

	/* Restore default signal handlers for child */
	signal_restore_defaults();

	/* Connect stdin to previous pipe (if not first command) */
	if (prev_fd != -1) {
		if (dup2(prev_fd, STDIN_FILENO) == -1)
			_exit(1);
		close(prev_fd);
	}

	/* Connect stdout to next pipe (if not last command) */
	if (pipe_fd) {
		close(pipe_fd[0]);
		if (dup2(pipe_fd[1], STDOUT_FILENO) == -1)
			_exit(1);
		close(pipe_fd[1]);
	}

	/* File redirects override pipe connections */
	if (setup_redirects(cmd) == -1)
		_exit(1);

	/* Try builtin first */
	builtin_ret = builtin_exec(cmd);
	if (builtin_ret != 1) {
		/*
		 * Builtin handled it:
		 *   0  = success
		 *  -1  = builtin error
		 *   2  = exit command (in child, just exit with the code)
		 */
		if (builtin_ret == 2)
			_exit(exit_code);
		_exit(builtin_ret == 0 ? 0 : 1);
	}

	/* External command */
	if (get_path(cmd->argv[0], path) == -1) {
		error_print(cmd->argv[0], "command not found", 0);
		_exit(127);
	}

	execve(path, cmd->argv, environ);
	error_print(cmd->argv[0], strerror(errno), 0);
	_exit(126);
}

/**
 * @brief Execute a pipeline of commands.
 *
 * Forks all commands, connecting them with pipes. Waits for all
 * children and sets exit_code to the last command's exit status.
 *
 * Single builtins without redirects run in the parent process
 * (required for cd, exit to affect shell state).
 *
 * @param pipeline  Linked list of commands.
 * @return          0 on success,
 *                  1 to signal shell should exit,
 *                 -1 on fatal error.
 */
int
execute_pipeline(Command *pipeline)
{
	Command *cmd;
	int prev_fd = -1;
	int pipe_fd[2];
	int cmd_count = 0;
	pid_t pid;
	pid_t *pids = NULL;
	int status;
	int i;

	/* Count commands */
	for (cmd = pipeline; cmd; cmd = cmd->next)
		cmd_count++;

	/*
	 * Single builtin without redirects: run in parent.
	 * This is required for builtins like cd and exit to work.
	 */
	if (cmd_count == 1 &&
	    !pipeline->redirect[REDIR_STDIN] &&
	    !pipeline->redirect[REDIR_STDOUT] &&
	    !pipeline->redirect[REDIR_STDERR]) {
		int ret = builtin_exec(pipeline);
		if (ret == 2) {
			/* exit builtin: signal main loop to terminate */
			return 1;
		}
		if (ret != 1) {
			/* builtin executed (success or error) */
			return 0;
		}
		/* ret == 1: not a builtin, fall through to fork/exec */
	}

	pids = malloc(cmd_count * sizeof(pid_t));
	if (!pids) {
		error_print(__func__, "malloc", errno);
		return -1;
	}

	cmd = pipeline;
	for (i = 0; i < cmd_count; i++, cmd = cmd->next) {
		/* Create pipe if not the last command */
		if (i < cmd_count - 1) {
			if (pipe(pipe_fd) == -1) {
				error_print(__func__, "pipe", errno);
				free(pids);
				return -1;
			}
		}

		pid = fork();
		if (pid == -1) {
			error_print(__func__, "fork", errno);
			if (i < cmd_count - 1) {
				close(pipe_fd[0]);
				close(pipe_fd[1]);
			}
			free(pids);
			return -1;
		}

		if (pid == 0) {
			/* Child process */
			free(pids);
			execute_child(cmd, prev_fd,
			              (i < cmd_count - 1) ? pipe_fd : NULL);
			/* execute_child never returns */
		}

		/* Parent process */
		pids[i] = pid;

		/* Close previous pipe read end (child has it now) */
		if (prev_fd != -1)
			close(prev_fd);

		/* Close write end, save read end for next command */
		if (i < cmd_count - 1) {
			close(pipe_fd[1]);
			prev_fd = pipe_fd[0];
		}
	}

	/* Wait for all children */
	for (i = 0; i < cmd_count; i++) {
		while (waitpid(pids[i], &status, 0) == -1) {
			if (errno == EINTR)
				continue;  /* Interrupted by signal, retry */
			error_print(__func__, "waitpid", errno);
			break;
		}

		/* Use last command's exit status */
		if (i == cmd_count - 1) {
			if (WIFEXITED(status))
				exit_code = WEXITSTATUS(status);
			else if (WIFSIGNALED(status))
				exit_code = 128 + WTERMSIG(status);
		}
	}

	free(pids);
	return 0;
}
