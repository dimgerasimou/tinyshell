/**
 * @file parser.h
 * @brief Shell command line parser interface.
 *
 * Parses shell input into a linked list of Command structures
 * representing a pipeline. Each Command holds arguments and
 * optional I/O redirections.
 */

#ifndef PARSER_H
#define PARSER_H

/**
 * Redirect array indices.
 */
enum {
	REDIR_STDIN  = 0,
	REDIR_STDOUT = 1,
	REDIR_STDERR = 2,
	REDIR_COUNT  = 3
};

/**
 * Append mode flags (bitfield).
 */
enum {
	APPEND_STDOUT = 1 << REDIR_STDOUT,  /* 0x02 */
	APPEND_STDERR = 1 << REDIR_STDERR   /* 0x04 */
};

/**
 * @struct Command
 * @brief Represents a single command in a pipeline.
 */
typedef struct Command Command;
struct Command {
	int argc;                    /* Argument count */
	char **argv;                 /* NULL-terminated argument vector */
	char *redirect[REDIR_COUNT]; /* I/O redirection targets (NULL if unused) */
	unsigned int append;         /* Append flags (APPEND_STDOUT, APPEND_STDERR) */
	int background;              /* Pipeline runs in background if '&' (head only) */
	Command *next;               /* Next command in pipeline */
};

/**
 * @brief Parse a command line into a pipeline of Commands.
 *
 * @param input  Null-terminated input string.
 * @return       Head of Command list, or NULL on parse error.
 */
Command *parser_parse(char *input);

/**
 * @brief Free a Command pipeline.
 *
 * Frees all Commands in the linked list, including their
 * argv arrays and redirection targets.
 *
 * @param cmd  Head of Command list (may be NULL).
 */
void parser_free_cmd(Command *cmd);

#endif /* PARSER_H */
