#include <stdio.h>

#define MEM_SIZE 1000
#define PROGRAM_MEM_SIZE 1000

void mem_init();
char *mem_get_value(char *var);
void mem_set_value(char *var, char *value);

int program_store_script(FILE *p, int *code_start, int *code_len);
const char *program_get_line(int index);
void program_free(int code_start, int code_len);
