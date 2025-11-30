#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "parser.h"
#include "error.h"

#define MAX_TOKENS 128
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
 * @brief Get next token from input string.
 *
 * Advances *input to point past the consumed token.
 * For TOK_WORD, allocates and returns the word value.
 *
 * @param input Pointer to input string pointer (updated on return)
 * @param value Output: allocated string for TOK_WORD (NULL for operators)
 * @return Token type
 */
static enum token_type
next_token(const char **input, char **value)
{
	char buf[TOKEN_BUFFER_SIZE];
	char temp[TOKEN_BUFFER_SIZE];
	size_t len = 0;
	int sq = 0, dq = 0;
	const char *p = *input;

	*value = NULL;

	// Skip whitespace
	while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
		p++;

	// End of input
	if (!*p) {
		*input = p;
		return TOK_END;
	}

	// Check operators
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

	if (*p == '2') {
		if (*(p+1) == '>') {
			if (*(p + 2) == '>') {
				*input = p + 3;
				return TOK_REDIR_ERR_APPEND;
			}
			*input = p + 2;
			return TOK_REDIR_ERR;
		}
	}

	// Build word token
	while (*p && len < TOKEN_BUFFER_SIZE - 1) {
		// Handle quotes
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

		// Handle escape in double quotes
		if (*p == '\\' && dq &&
		    (*(p + 1) == '"' || *(p + 1) == '\\')) {
			p++;
			buf[len++] = *p++;
			continue;
		}

		// Stop at whitespace or operators when not quoted
		if (!sq && !dq) {
			if (*p == ' ' || *p == '\t' ||
			    *p == '|' || *p == '<' || *p == '>' ||
			    *p == '\n' || *p == '\r')
				break;
		}

		buf[len++] = *p++;
	}

	buf[len] = '\0';
	if (!expand_tilde(buf, temp, TOKEN_BUFFER_SIZE))
		*value = strdup(temp);
	else
		*value = strdup(buf);

	if (!*value) {
		print_error(__func__, "strdup() failed", errno);
		return TOK_ERROR;
	}

	*input = p;
	return TOK_WORD;
}

static Command*
new_cmd(void)
{
	Command *ret = NULL;

	ret = malloc(sizeof(Command));
	if (!ret) {
		print_error(__func__, "malloc() failed", errno);
		return NULL;
	}

	ret->argc = 0;
	ret->argv = malloc((ret->argc + 1) * sizeof(char*));

	if (!ret->argv) {
		print_error(__func__, "malloc() failed", errno);
		free(ret);
		return NULL;
	}

	ret->argv[0] = NULL;

	ret->redirect[0] = NULL;
	ret->redirect[1] = NULL;
	ret->redirect[2] = NULL;
	ret->next = NULL;
	ret->append = 0;

	return ret;
}

static int
append_arg(Command *cmd, char **input)
{
	char **argv;

	cmd->argc += 1;
	argv = realloc(cmd->argv, (cmd->argc + 1) * sizeof(char*));

	if (!argv) {
		print_error(__func__, "realloc() failed", errno);
		return 1;
	}

	argv[cmd->argc - 1] = *input;
	argv[cmd->argc] = NULL;

	cmd->argv = argv;
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
int
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

Command*
parser_parse(char *input)
{
	Command *ret;
	Command *cur;

	const char *p = input;
	char *value;
	enum token_type type;

	ret = new_cmd();
	if (!ret)
		return NULL;

	cur = ret;

	while ((type = next_token(&p, &value)) != TOK_END) {
		switch (type) {
		case TOK_WORD:
			if (append_arg(cur, &value)) {
				if (value)
					free(value);
				goto fail;
			}
			break;

		case TOK_PIPE:
		{
			Command *next = new_cmd();
			if (next == NULL)
				goto fail;

			cur->next = next;
			cur = next;
			break;
		}

		case TOK_REDIR_IN:
			if (cur->redirect[0] || next_token(&p, &value) != TOK_WORD) {
				print_error(NULL, "parse error near '<'", 0);
				goto fail;
			}

			cur->redirect[0] = value;
			break;

		case TOK_REDIR_OUT:
			if (cur->redirect[1] || next_token(&p, &value) != TOK_WORD) {
				print_error(NULL, "parse error near '>'", 0);
				goto fail;
			}

			cur->redirect[1] = value;
			break;

		case TOK_REDIR_OUT_APPEND:
			if (cur->redirect[1] || next_token(&p, &value) != TOK_WORD) {
				print_error(NULL, "parse error near \">>\"", 0);
				goto fail;
			}

			cur->redirect[1] = value;
			cur->append |= 1 << 1;
			break;

		case TOK_REDIR_ERR:
			if (cur->redirect[2] || next_token(&p, &value) != TOK_WORD) {
				print_error(NULL, "parse error near '2>'", 0);
				goto fail;
			}

			cur->redirect[2] = value;
			break;

		case TOK_REDIR_ERR_APPEND:
			if (cur->redirect[2] || next_token(&p, &value) != TOK_WORD) {
				print_error(NULL, "parse error near \"2>>\"", 0);
				goto fail;
			}

			cur->redirect[2] = value;
			cur->append |= 1 << 2;
			break;

		case TOK_END: /* dead code */
			if (!cur->argv[0]) {
				print_error(NULL, "parse error near '|'", 0);
				goto fail;
			}
			break;

		case TOK_ERROR:
			if (value)
				free(value);
			goto fail;
		}
	}

	if (!cur->argv[0]) {
		print_error(NULL, "parse error: expected command", 0);
		goto fail;
	}

	return ret;

fail:
	parser_free_cmd(ret);
	return NULL;
}

void
parser_free_cmd(Command *cmd)
{
	while (cmd) {
		if (cmd->argv) {
			for (int i = 0; i < cmd->argc; i++) {
				if (cmd->argv[i])
					free(cmd->argv[i]);
			}
			free(cmd->argv);
		}

		if (cmd->redirect[0])
			free(cmd->redirect[0]);
		if (cmd->redirect[1])
			free(cmd->redirect[1]);
		if (cmd->redirect[2])
			free(cmd->redirect[2]);

		Command *n = cmd->next;

		free(cmd);
		cmd = n;
	}
}
