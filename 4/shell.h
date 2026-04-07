#ifndef SHELL_H
#define SHELL_H

#include <stddef.h>

#define MAX_LINE 256
#define MAX_PROCESSED 1024
#define MAX_TOKENS 128
#define MAX_ARGS 32
#define MAX_CMDS 32

struct command {
    char *argv[MAX_ARGS];
    char *input_file;
    char *output_file;
};

int preprocess_line(const char *src, char *dst, size_t dst_size);
int tokenize(char *line, char *tokens[], int max_tokens);
int parse_commands(char *tokens[], int token_count, struct command cmds[], int max_cmds);
void execute_pipeline(struct command cmds[], int cmd_count);

#endif