#ifndef PARSER_H
#define PARSER_H

#include <stdlib.h>

typedef struct Command Command;

struct Command{
	char **argv;
	char *redirect[3];
	int  argc;
	unsigned int append;
	Command *next;
};

Command* parser_parse(char *input);

void parser_free_cmd(Command *cmd);

int  expand_tilde(const char *path, char *expanded, size_t size);

#endif /* PARSER_H */
