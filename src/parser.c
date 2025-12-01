/**
 * @file parser.c
 * @brief Command line parser for shell input.
 *
 * Tokenizes and parses shell command lines into a pipeline of
 * Command structures. Supports:
 *
 *   - Pipes (|)
 *   - Input redirection (<)
 *   - Output redirection (>, >>)
 *   - Stderr redirection (2>, 2>>)
 *   - Single and double quotes
 *   - Backslash escapes within double quotes
 *   - Tilde expansion (~, ~/path)
 */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "parser.h"
#include "error.h"

#define TOKEN_BUFFER_SIZE 4096

enum token_type {
	TOK_WORD,
	TOK_PIPE,
	TOK_REDIR_IN,
	TOK_REDIR_OUT,
	TOK_REDIR_OUT_APPEND,
	TOK_REDIR_ERR,
	TOK_REDIR_ERR_APPEND,
	TOK_END,
	TOK_ERROR
};

/**
 * @brief Expand a leading '~' into the user's HOME directory.
 *
 * Supports:
 *   "~"      -> $HOME
 *   "~/foo"  -> $HOME/foo
 *
 * "~user" is not implemented and is copied verbatim.
 *
 * @param path      Input path string.
 * @param expanded  Output buffer for expanded path.
 * @param size      Maximum size of the output buffer.
 * @return          0 on success, -1 if HOME is not set.
 */
static int
parser_expand_tilde(const char *path, char *expanded, size_t size)
{
	const char *env;

	/* return if not "~" or "~/<path>" */
	if (path[0] != '~' || (path[1] != '\0' && path[1] != '/')) {
		strncpy(expanded, path, size - 1);
		expanded[size - 1] = '\0';
		return 0;
	}

	if (!(env = getenv("HOME"))) {
		error_print(__func__, "getenv \"HOME\"", errno);
		return -1;
	}

	snprintf(expanded, size, "%s%s", env, path + 1);
	return 0;
}

/**
 * @brief Get next token from input string.
 *
 * Advances *input to point past the consumed token.
 * For TOK_WORD, allocates and returns the word value.
 *
 * @param input  Pointer to input string pointer (updated on return).
 * @param value  Output: allocated string for TOK_WORD (NULL for operators).
 * @return       Token type.
 */
static enum token_type
parser_next_token(const char **input, char **value)
{
	char buf[TOKEN_BUFFER_SIZE];
	char temp[TOKEN_BUFFER_SIZE];
	size_t len = 0;
	int sq = 0, dq = 0;
	const char *p = *input;

	*value = NULL;

	/* skip whitespace */
	while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
		p++;

	/* end of input */
	if (!*p) {
		*input = p;
		return TOK_END;
	}

	/* single char operators */
	if (*p == '|') {
		*input = p + 1;
		return TOK_PIPE;
	}

	if (*p == '<') {
		*input = p + 1;
		return TOK_REDIR_IN;
	}

	if (*p == '>') {
		if (*(p + 1) == '>') {
			*input = p + 2;
			return TOK_REDIR_OUT_APPEND;
		}
		*input = p + 1;
		return TOK_REDIR_OUT;
	}

	/* stderr redirection: 2> or 2>> */
	if (*p == '2' && *(p+1) == '>') {
		if (*(p + 2) == '>') {
			*input = p + 3;
			return TOK_REDIR_ERR_APPEND;
		}
		*input = p + 2;
		return TOK_REDIR_ERR;
	}

	/* build word token */
	while (*p && len < TOKEN_BUFFER_SIZE - 1) {
		if (*p == '\'' && !dq) {
			sq = !sq;
			p++;
			continue;
		}

		if (*p == '"' && !sq) {
			dq = !dq;
			p++;
			continue;
		}

		/* Backslash escapes within double quotes */
		if (*p == '\\' && dq &&
		    (*(p + 1) == '"' || *(p + 1) == '\\')) {
			p++;
			buf[len++] = *p++;
			continue;
		}

		/* Unquoted: stop at whitespace or operators */
		if (!sq && !dq) {
			if (*p == ' ' || *p == '\t' ||
			    *p == '|' || *p == '<' || *p == '>' ||
			    *p == '\n' || *p == '\r')
				break;
		}

		buf[len++] = *p++;
	}

	if (len >= TOKEN_BUFFER_SIZE - 1) {
		error_print("parse error", "token too long", 0);
		return TOK_ERROR;
	}

	/* check for unclosed quotes */
	if (sq || dq) {
		error_print("parse error", "unclosed quote", 0);
		return TOK_ERROR;
	}

	buf[len] = '\0';

	/* expand tilde if applicable */
	if (parser_expand_tilde(buf, temp, TOKEN_BUFFER_SIZE))
		return TOK_ERROR;

	*value = strdup(temp);

	if (!*value) {
		error_print(__func__, "strdup", errno);
		return TOK_ERROR;
	}

	*input = p;
	return TOK_WORD;
}

/**
 * @brief Allocate and initialize a new Command structure.
 *
 * @return  New Command or NULL on allocation failure.
 */
static Command*
parser_init_cmd(void)
{
	Command *cmd;

	cmd = malloc(sizeof(Command));
	if (!cmd) {
		error_print(__func__, "malloc", errno);
		return NULL;
	}

	cmd->argc = 0;
	cmd->argv = malloc(sizeof(char *));
	if (!cmd->argv) {
		error_print(__func__, "malloc", errno);
		free(cmd);
		return NULL;
	}

	cmd->argv[0] = NULL;
	cmd->redirect[REDIR_STDIN]  = NULL;
	cmd->redirect[REDIR_STDOUT] = NULL;
	cmd->redirect[REDIR_STDERR] = NULL;
	cmd->next = NULL;
	cmd->append = 0;

	return cmd;
}

/**
 * @brief Append an argument to a command's argv array.
 *
 * Takes ownership of the argument string.
 *
 * @param cmd  Command to append to.
 * @param arg  Argument string (ownership transferred).
 * @return     0 on success, -1 on allocation failure.
 */
static int
parser_arg_append(Command *cmd, char *arg)
{
	char **argv;

	cmd->argc++;
	argv = realloc(cmd->argv, (cmd->argc + 1) * sizeof(char *));
	if (!argv) {
		error_print(__func__, "realloc", errno);
		return -1;
	}

	argv[cmd->argc - 1] = arg;
	argv[cmd->argc] = NULL;
	cmd->argv = argv;

	return 0;
}

/**
 * @brief Parse a command line into a pipeline of Commands.
 *
 * @param input  Null-terminated input string.
 * @return       Head of Command list, or NULL on parse error or empty input.
 */
Command*
parser_parse(char *input)
{
	Command *head;
	Command *cur;
	enum token_type type;
	const char *p = input;
	char *value;

	/* Check for empty/whitespace-only input */
	while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
		p++;
	if (!*p)
		return NULL;

	p = input;  /* Reset for actual parsing */

	head = parser_init_cmd();
	if (!head)
		return NULL;

	cur = head;

	while ((type = parser_next_token(&p, &value)) != TOK_END) {
		switch (type) {
		case TOK_WORD:
			if (parser_arg_append(cur, value)) {
				free(value);
				goto fail;
			}
			break;

		case TOK_PIPE:
			if (!cur->argv[0]) {
				error_print(NULL, "parse error near '|'", 0);
				goto fail;
			}
			cur->next = parser_init_cmd();
			if (!cur->next)
				goto fail;
			cur = cur->next;
			break;

		case TOK_REDIR_IN:
			if (cur->redirect[REDIR_STDIN] || parser_next_token(&p, &value) != TOK_WORD) {
				error_print(NULL, "parse error near '<'", 0);
				goto fail;
			}

			cur->redirect[REDIR_STDIN] = value;
			break;

		case TOK_REDIR_OUT:
			if (cur->redirect[REDIR_STDOUT] || parser_next_token(&p, &value) != TOK_WORD) {
				error_print(NULL, "parse error near '>'", 0);
				goto fail;
			}

			cur->redirect[REDIR_STDOUT] = value;
			break;

		case TOK_REDIR_OUT_APPEND:
			if (cur->redirect[REDIR_STDOUT] || parser_next_token(&p, &value) != TOK_WORD) {
				error_print(NULL, "parse error near '>>'", 0);
				goto fail;
			}

			cur->redirect[REDIR_STDOUT] = value;
			cur->append |= APPEND_STDOUT;
			break;

		case TOK_REDIR_ERR:
			if (cur->redirect[REDIR_STDERR] || parser_next_token(&p, &value) != TOK_WORD) {
				error_print(NULL, "parse error near '2>'", 0);
				goto fail;
			}

			cur->redirect[REDIR_STDERR] = value;
			break;

		case TOK_REDIR_ERR_APPEND:
			if (cur->redirect[REDIR_STDERR] || parser_next_token(&p, &value) != TOK_WORD) {
				error_print(NULL, "parse error near '2>>'", 0);
				goto fail;
			}

			cur->redirect[REDIR_STDERR] = value;
			cur->append |= APPEND_STDERR;
			break;

		case TOK_ERROR:
			goto fail;

		case TOK_END:
			break;
		}
	}

	if (!cur->argv[0]) {
		error_print(NULL, "parse error: empty command", 0);
		goto fail;
	}

	return head;

fail:
	parser_free_cmd(head);
	return NULL;
}

/**
 * @brief Free a Command pipeline.
 *
 * Frees all Commands in the linked list, including their
 * argv arrays and redirection targets.
 *
 * @param cmd  Head of Command list (may be NULL).
 */
void
parser_free_cmd(Command *cmd)
{
	Command *next;

	while (cmd) {
		if (cmd->argv) {
			for (int i = 0; i < cmd->argc; i++)
				free(cmd->argv[i]);
			free(cmd->argv);
		}

		free(cmd->redirect[REDIR_STDIN]);
		free(cmd->redirect[REDIR_STDOUT]);
		free(cmd->redirect[REDIR_STDERR]);

		next = cmd->next;
		free(cmd);
		cmd = next;
	}
}
