#ifndef PARSER_H
#define PARSER_H

typedef struct Command Command;

struct Command{
	char **argv;
	char *redirect[3];
	int  argc;
	unsigned int append;
	Command *next;
};

Command* parser_parse(char *input);

Command* parser_free_cmd(Command *cmd);

#endif /* PARSER_H */
