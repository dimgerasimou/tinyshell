/**
 * @file pipeline.c
 * @brief Pipeline execution for TinyShell.
 *
 * Executes command pipelines with support for:
 *   - Multiple piped commands
 *   - Input/output/stderr redirection on any command
 *   - Builtin commands (in parent for single commands, child for pipelines)
 *   - Phase 3: basic job control (background '&', fg/bg, process groups)
 *
 * Job id behavior:
 *   - Uses the smallest free jid in [1..MAX_JOBS]
 *   - When the job table becomes empty, jid display naturally restarts at 1
 *     for the next job, mimicking bash-style behavior.
 */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include "pipeline.h"
#include "error.h"
#include "builtin.h"
#include "signal_setup.h"

extern char **environ;
extern int exit_code;

#define DEFAULT_FILE_MODE 0644

/* ------------------------------------------------------------------------- */
/*                                Job Control                                */
/* ------------------------------------------------------------------------- */

#define MAX_JOBS      64
#define MAX_PROCS     64
#define CMDLINE_MAX   1024

typedef enum {
	JOB_UNUSED = 0,
	JOB_RUNNING,
	JOB_STOPPED,
	JOB_DONE
} job_state_t;

typedef struct job {
	int used;
	int jid;
	unsigned long long seq; /* monotonic sequence for default +/- marking */
	pid_t pgid;
	job_state_t state;

	int nprocs;
	int alive;
	pid_t pids[MAX_PROCS];

	pid_t last_pid;
	int last_status_valid;
	int last_status;

	char cmdline[CMDLINE_MAX];
	int notified;
} job_t;

static job_t jobs[MAX_JOBS];

/* “Current” and “previous” jobs for %+ and %- */
static int current_jid = 0;
static int previous_jid = 0;

/* Monotonic sequence for stable “most recently started job” ordering */
static unsigned long long next_seq = 1;

/* Forward declarations (used by helpers that appear before their definitions). */
static job_t* job_by_jid(int jid);

static int
jobs_any_used(void)
{
	for (int i = 0; i < MAX_JOBS; i++) {
		if (jobs[i].used)
			return 1;
	}
	return 0;
}

static int
is_interactive(void)
{
	return isatty(STDIN_FILENO);
}

static char
job_mark(int jid)
{
	if (jid == current_jid)
		return '+';
	if (jid == previous_jid)
		return '-';
	return ' ';
}

static const char*
job_state_str(job_state_t state)
{
	switch (state) {
	case JOB_RUNNING:
		return "Running";
	case JOB_STOPPED:
		return "Stopped";
	case JOB_DONE:
		return "Done";
	default:
		return "";
	}
}

static void
sigchld_block(sigset_t *prev)
{
	sigset_t mask;

	sigemptyset(&mask);
	sigaddset(&mask, SIGCHLD);
	(void)sigprocmask(SIG_BLOCK, &mask, prev);
}

static void
sigchld_restore(const sigset_t *prev)
{
	(void)sigprocmask(SIG_SETMASK, prev, NULL);
}

/*
 * Recompute current/previous jobs based on the monotonic start sequence.
 * This gives a sane default for + and - when jobs are created/removed.
 */
static void
recompute_current_previous(void)
{
	unsigned long long newest_seq = 0;
	unsigned long long second_seq = 0;
	int newest_jid = 0;
	int second_jid = 0;

	for (int i = 0; i < MAX_JOBS; i++) {
		if (!jobs[i].used)
			continue;

		if (jobs[i].seq > newest_seq) {
			second_seq = newest_seq;
			second_jid = newest_jid;
			newest_seq = jobs[i].seq;
			newest_jid = jobs[i].jid;
		} else if (jobs[i].seq > second_seq) {
			second_seq = jobs[i].seq;
			second_jid = jobs[i].jid;
		}
	}

	current_jid = newest_jid;
	previous_jid = second_jid;
}

/*
 * Allocate the smallest unused jid (1..MAX_JOBS).
 * This makes job ids reuse holes like bash does.
 */
static int
alloc_jid(void)
{
	for (int jid = 1; jid <= MAX_JOBS; jid++) {
		if (!job_by_jid(jid))
			return jid;
	}
	return -1;
}

static job_t*
job_by_jid(int jid)
{
	for (int i = 0; i < MAX_JOBS; i++) {
		if (jobs[i].used && jobs[i].jid == jid)
			return &jobs[i];
	}
	return NULL;
}

static job_t*
job_by_pid(pid_t pid)
{
	for (int i = 0; i < MAX_JOBS; i++) {
		job_t *j = &jobs[i];
		if (!j->used)
			continue;
		for (int k = 0; k < j->nprocs; k++) {
			if (j->pids[k] == pid)
				return j;
		}
	}
	return NULL;
}

static void
job_remove(job_t *j)
{
	if (!j || !j->used)
		return;

	memset(j, 0, sizeof(*j));

	/* If the job table is now empty, restart marks/sequence cleanly. */
	if (!jobs_any_used()) {
		current_jid = 0;
		previous_jid = 0;
		next_seq = 1;
		return;
	}

	recompute_current_previous();
}

static void
format_cmdline(Command *pipeline, char out[CMDLINE_MAX])
{
	size_t used = 0;
	Command *cmd;

	out[0] = '\0';

	for (cmd = pipeline; cmd; cmd = cmd->next) {
		for (int i = 0; i < cmd->argc; i++) {
			int n;

			n = snprintf(out + used, CMDLINE_MAX - used, "%s%s",
			             used ? " " : "", cmd->argv[i]);
			if (n < 0)
				return;
			if ((size_t)n >= CMDLINE_MAX - used) {
				out[CMDLINE_MAX - 1] = '\0';
				return;
			}
			used += (size_t)n;
		}

		if (cmd->next) {
			int n;

			n = snprintf(out + used, CMDLINE_MAX - used, " | ");
			if (n < 0)
				return;
			if ((size_t)n >= CMDLINE_MAX - used) {
				out[CMDLINE_MAX - 1] = '\0';
				return;
			}
			used += (size_t)n;
		}
	}

	if (pipeline && pipeline->background)
		(void)snprintf(out + used, CMDLINE_MAX - used, " &");
}

static job_t*
job_add(pid_t pgid, pid_t *pids, int nprocs, pid_t last_pid, Command *pipeline)
{
	job_t *j = NULL;
	char cmdline[CMDLINE_MAX];
	int jid;

	for (int i = 0; i < MAX_JOBS; i++) {
		if (!jobs[i].used) {
			j = &jobs[i];
			break;
		}
	}

	if (!j)
		return NULL;

	memset(j, 0, sizeof(*j));
	j->used = 1;

	jid = alloc_jid();
	if (jid < 0) {
		memset(j, 0, sizeof(*j));
		return NULL;
	}

	j->jid = jid;
	j->seq = next_seq++;
	j->pgid = pgid;
	j->state = JOB_RUNNING;
	j->nprocs = nprocs;
	j->alive = nprocs;
	j->last_pid = last_pid;
	j->last_status_valid = 0;
	j->notified = 0;

	if (nprocs > MAX_PROCS)
		nprocs = MAX_PROCS;
	for (int k = 0; k < nprocs; k++)
		j->pids[k] = pids[k];

	format_cmdline(pipeline, cmdline);
	snprintf(j->cmdline, sizeof(j->cmdline), "%s", cmdline);

	recompute_current_previous();
	return j;
}

static int
status_to_exitcode(int status)
{
	if (WIFEXITED(status))
		return WEXITSTATUS(status);
	if (WIFSIGNALED(status))
		return 128 + WTERMSIG(status);
	return 0;
}

/*
 * Reap all available child status changes.
 *
 * This is called from the SIGCHLD handler in signal_setup.c.
 * Must not malloc(), printf(), etc.
 */
void
jobs_sigchld_reap(void)
{
	int saved_errno = errno;
	int status;
	pid_t pid;

	while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0) {
		job_t *j = job_by_pid(pid);
		if (!j)
			continue;

		if (WIFSTOPPED(status)) {
			j->state = JOB_STOPPED;
			j->notified = 0;
			continue;
		}

		if (WIFCONTINUED(status)) {
			j->state = JOB_RUNNING;
			j->notified = 0;
			continue;
		}

		if (WIFEXITED(status) || WIFSIGNALED(status)) {
			if (pid == j->last_pid) {
				j->last_status = status;
				j->last_status_valid = 1;
			}

			if (j->alive > 0)
				j->alive--;
			if (j->alive == 0) {
				j->state = JOB_DONE;
				j->notified = 0;
			}
		}
	}

	errno = saved_errno;
}

void
pipeline_notify_jobs(void)
{
	sigset_t prev;

	sigchld_block(&prev);

	for (int i = 0; i < MAX_JOBS; i++) {
		job_t *j = &jobs[i];

		if (!j->used || j->notified)
			continue;

		if (j->state == JOB_STOPPED) {
			fprintf(stderr, "[%d]%c  %s\t%s\n",
			        j->jid, job_mark(j->jid), job_state_str(j->state), j->cmdline);
			j->notified = 1;
			continue;
		}

		if (j->state == JOB_DONE) {
			fprintf(stderr, "[%d]%c  %s\t%s\n",
			        j->jid, job_mark(j->jid), job_state_str(j->state), j->cmdline);
			j->notified = 1;
			job_remove(j);
		}
	}

	sigchld_restore(&prev);
}

static int
parse_job_spec(const char *s)
{
	char *end;
	long v;

	if (!s)
		return current_jid;

	if (s[0] == '%') {
		if (s[1] == '+' || s[1] == '%' || s[1] == '\0')
			return current_jid;
		if (s[1] == '-')
			return previous_jid;
		s++;
	}

	errno = 0;
	v = strtol(s, &end, 10);
	if (errno || end == s || *end != '\0' || v <= 0 || v > INT_MAX)
		return -1;

	return (int)v;
}

static int
builtin_jobs(void)
{
	sigset_t prev;

	sigchld_block(&prev);

	for (int i = 0; i < MAX_JOBS; i++) {
		job_t *j = &jobs[i];
		if (!j->used)
			continue;
		printf("[%d]%c  %s\t%s\n",
		       j->jid, job_mark(j->jid), job_state_str(j->state), j->cmdline);
	}

	sigchld_restore(&prev);

	exit_code = 0;
	return 0;
}

static void
wait_foreground(job_t *j)
{
	sigset_t mask, prev;

	sigemptyset(&mask);
	sigaddset(&mask, SIGCHLD);
	sigprocmask(SIG_BLOCK, &mask, &prev);

	while (j->used && j->state == JOB_RUNNING)
		sigsuspend(&prev);

	sigprocmask(SIG_SETMASK, &prev, NULL);
}

static int
builtin_fg(Command *cmd)
{
	int jid;
	job_t *j;
	pid_t shell_pgid;

	jid = parse_job_spec(cmd->argc >= 2 ? cmd->argv[1] : NULL);
	if (jid <= 0) {
		error_print("fg", "no such job", 0);
		exit_code = 1;
		return -1;
	}

	sigset_t prev;
	sigchld_block(&prev);
	j = job_by_jid(jid);
	if (!j) {
		sigchld_restore(&prev);
		error_print("fg", "no such job", 0);
		exit_code = 1;
		return -1;
	}

	/* bash-like: bring chosen job to “current” */
	previous_jid = current_jid;
	current_jid = j->jid;

	j->notified = 0;
	j->state = JOB_RUNNING;

	sigchld_restore(&prev);

	shell_pgid = getpgrp();
	kill(-j->pgid, SIGCONT);

	if (is_interactive())
		tcsetpgrp(STDIN_FILENO, j->pgid);

	wait_foreground(j);

	if (is_interactive())
		tcsetpgrp(STDIN_FILENO, shell_pgid);

	sigchld_block(&prev);
	if (j->used && j->state == JOB_DONE) {
		if (j->last_status_valid)
			exit_code = status_to_exitcode(j->last_status);
		else
			exit_code = 0;
		job_remove(j);
	} else {
		/* stopped */
		exit_code = 0;
		if (j->used)
			j->notified = 0;
	}
	sigchld_restore(&prev);

	pipeline_notify_jobs();
	return 0;
}

static int
builtin_bg(Command *cmd)
{
	int jid;
	job_t *j;

	jid = parse_job_spec(cmd->argc >= 2 ? cmd->argv[1] : NULL);
	if (jid <= 0) {
		error_print("bg", "no such job", 0);
		exit_code = 1;
		return -1;
	}

	sigset_t prev;
	sigchld_block(&prev);
	j = job_by_jid(jid);
	if (!j) {
		sigchld_restore(&prev);
		error_print("bg", "no such job", 0);
		exit_code = 1;
		return -1;
	}

	/* bash-like: make chosen job current */
	previous_jid = current_jid;
	current_jid = j->jid;

	j->state = JOB_RUNNING;
	j->notified = 0;
	sigchld_restore(&prev);

	kill(-j->pgid, SIGCONT);
	printf("[%d]%c  %s\t%s &\n",
	       j->jid, job_mark(j->jid), job_state_str(j->state), j->cmdline);

	exit_code = 0;
	return 0;
}

static int
builtin_jobctl(Command *cmd)
{
	if (!strcmp(cmd->argv[0], "jobs"))
		return builtin_jobs();
	if (!strcmp(cmd->argv[0], "fg"))
		return builtin_fg(cmd);
	if (!strcmp(cmd->argv[0], "bg"))
		return builtin_bg(cmd);
	return 1;
}

/* ------------------------------------------------------------------------- */
/*                              Exec Utilities                               */
/* ------------------------------------------------------------------------- */

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

/* ------------------------------------------------------------------------- */
/*                           Pipeline Execution                               */
/* ------------------------------------------------------------------------- */

int
execute_pipeline(Command *pipeline)
{
	Command *cmd;
	int prev_fd = -1;
	int pipe_fd[2];
	int cmd_count = 0;
	pid_t pid;
	pid_t pgid = 0;
	pid_t last_pid = -1;
	pid_t *pids = NULL;
	int status;
	int i;
	int background;
	job_t *job;
	sigset_t prevmask;

	if (!pipeline)
		return 0;

	/* Opportunistic job notifications */
	pipeline_notify_jobs();

	background = pipeline->background ? 1 : 0;

	/* Count commands */
	for (cmd = pipeline; cmd; cmd = cmd->next)
		cmd_count++;

	if (cmd_count <= 0)
		return 0;

	/*
	 * Single builtin without redirects: run in parent.
	 * (required for cd/exit and job control builtins).
	 */
	if (cmd_count == 1 &&
	    !pipeline->redirect[REDIR_STDIN] &&
	    !pipeline->redirect[REDIR_STDOUT] &&
	    !pipeline->redirect[REDIR_STDERR]) {
		int ret;

		ret = builtin_jobctl(pipeline);
		if (ret != 1)
			return 0;

		ret = builtin_exec(pipeline);
		if (ret == 2)
			return 1;
		if (ret != 1)
			return 0;
	}

	if (cmd_count > MAX_PROCS) {
		error_print(__func__, "pipeline too long", 0);
		exit_code = 1;
		return -1;
	}

	pids = malloc((size_t)cmd_count * sizeof(pid_t));
	if (!pids) {
		error_print(__func__, "malloc", errno);
		return -1;
	}

	/* Block SIGCHLD while we fork and add the job (race prevention) */
	sigchld_block(&prevmask);

	cmd = pipeline;
	for (i = 0; i < cmd_count; i++, cmd = cmd->next) {
		/* Create pipe if not the last command */
		if (i < cmd_count - 1) {
			if (pipe(pipe_fd) == -1) {
				error_print(__func__, "pipe", errno);
				goto fatal;
			}
		}

		pid = fork();
		if (pid == -1) {
			error_print(__func__, "fork", errno);
			if (i < cmd_count - 1) {
				close(pipe_fd[0]);
				close(pipe_fd[1]);
			}
			goto fatal;
		}

		if (pid == 0) {
			/* Child */
			sigchld_restore(&prevmask);

			if (pgid == 0)
				pgid = getpid();
			setpgid(0, pgid);

			execute_child(cmd, prev_fd,
			              (i < cmd_count - 1) ? pipe_fd : NULL);
			/* execute_child never returns */
		}

		/* Parent */
		pids[i] = pid;
		if (pgid == 0)
			pgid = pid;

		/* Ensure child joins its process group (race-safe) */
		if (setpgid(pid, pgid) == -1) {
			if (errno != EACCES && errno != ESRCH)
				error_print(__func__, "setpgid", errno);
		}

		/* Give terminal to foreground job ASAP (avoid SIGTTIN races) */
		if (!background && is_interactive() && i == 0)
			tcsetpgrp(STDIN_FILENO, pgid);

		/* Close previous pipe read end */
		if (prev_fd != -1)
			close(prev_fd);

		/* Close write end, save read end for next command */
		if (i < cmd_count - 1) {
			close(pipe_fd[1]);
			prev_fd = pipe_fd[0];
		}

		last_pid = pid;
	}

	job = job_add(pgid, pids, cmd_count, last_pid, pipeline);
	sigchld_restore(&prevmask);

	if (!job) {
		error_print(__func__, "too many jobs", 0);
		exit_code = 1;
		goto fatal_unblocked;
	}

	if (background) {
		printf("[%d] %d\n", job->jid, (int)job->pgid);
		exit_code = 0;
		free(pids);
		return 0;
	}

	/* Foreground: wait for completion or stop */
	wait_foreground(job);

	/* Restore terminal to shell */
	if (is_interactive())
		tcsetpgrp(STDIN_FILENO, getpgrp());

	/* Collect status for foreground job */
	sigchld_block(&prevmask);
	if (job->used && job->state == JOB_DONE) {
		if (job->last_status_valid)
			exit_code = status_to_exitcode(job->last_status);
		else
			exit_code = 0;
		job_remove(job);
	} else if (job->used && job->state == JOB_STOPPED) {
		exit_code = 0;
		job->notified = 0;
	}
	sigchld_restore(&prevmask);

	free(pids);
	pipeline_notify_jobs();
	return 0;

fatal:
	/* Parent fatal path while SIGCHLD is still blocked */
	sigchld_restore(&prevmask);

fatal_unblocked:
	/* Best effort cleanup */
	if (prev_fd != -1)
		close(prev_fd);

	/* Reap any children we already started */
	for (int k = 0; k < i; k++) {
		while (waitpid(pids[k], &status, 0) == -1) {
			if (errno == EINTR)
				continue;
			break;
		}
	}

	free(pids);
	return -1;
}

