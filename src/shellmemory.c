#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "shell.h"
#include "shellmemory.h"

struct memory_struct {
    char *var;
    char *value;
};

struct program_line {
    char *line;
    int in_use;
};


struct memory_struct shellmemory[MEM_SIZE];
static struct program_line program_memory[PROGRAM_MEM_SIZE];

// Helper functions
int match(char *model, char *var) {
    int i, len = strlen(var), matchCount = 0;
    for (i = 0; i < len; i++) {
        if (model[i] == var[i])
            matchCount++;
    }
    if (matchCount == len) {
        return 1;
    } else
        return 0;
}

// Shell memory functions

void mem_init() {
    int i;
    for (i = 0; i < MEM_SIZE; i++) {
        shellmemory[i].var = "none";
        shellmemory[i].value = "none";
    }
    for (i = 0; i < PROGRAM_MEM_SIZE; i++) {
        program_memory[i].line = NULL;
        program_memory[i].in_use = 0;
    }
}

// Set key value pair
void mem_set_value(char *var_in, char *value_in) {
    int i;

    for (i = 0; i < MEM_SIZE; i++) {
        if (strcmp(shellmemory[i].var, var_in) == 0) {
            shellmemory[i].value = strdup(value_in);
            return;
        }
    }

    //Value does not exist, need to find a free spot.
    for (i = 0; i < MEM_SIZE; i++) {
        if (strcmp(shellmemory[i].var, "none") == 0) {
            shellmemory[i].var = strdup(var_in);
            shellmemory[i].value = strdup(value_in);
            return;
        }
    }

    return;
}

//get value based on input key
char *mem_get_value(char *var_in) {
    int i;

    for (i = 0; i < MEM_SIZE; i++) {
        if (strcmp(shellmemory[i].var, var_in) == 0) {
            return strdup(shellmemory[i].value);
        }
    }
    return NULL;
}

static int program_find_contiguous_free(int count) { // find closes free program
    if (count <= 0) {
        return 0;
    }

    for (int i = 0; i <= PROGRAM_MEM_SIZE - count; i++) {
        int ok = 1;
        for (int j = 0; j < count; j++) {
            if (program_memory[i + j].in_use) {
                ok = 0;
                i = i + j;
                break;
            }
        }
        if (ok) {
            return i;
        }
    }

    return -1;
}

int program_store_script(FILE *p, int *code_start, int *code_len) {
    char line[MAX_USER_INPUT]; // store where you can
    size_t capacity = 16;
    size_t count = 0;
    char **lines = malloc(capacity * sizeof(char *));

    if (!lines) {
        return 1;
    }

    while (fgets(line, MAX_USER_INPUT - 1, p) != NULL) { // read line by line
        if (count == capacity) {
            capacity *= 2;
            char **next = realloc(lines, capacity * sizeof(char *));
            if (!next) {
                for (size_t i = 0; i < count; i++) {
                    free(lines[i]); // free the lines we already have before giving up
                }
                free(lines);
                return 1;
            }
            lines = next;
        }
        lines[count] = strdup(line);
        if (!lines[count]) { // if strdup fails, free everything and return error
            for (size_t i = 0; i < count; i++) {
                free(lines[i]);
            }
            free(lines);
            return 1;
        }
        count++;
    }

    int start = program_find_contiguous_free((int)count);
    if (start < 0) {
        for (size_t i = 0; i < count; i++) {
            free(lines[i]);
        }
        free(lines);
        return 1;
    }

    for (size_t i = 0; i < count; i++) {
        program_memory[start + (int)i].line = lines[i];
        program_memory[start + (int)i].in_use = 1;
    }

    free(lines);

    *code_start = start;
    *code_len = (int)count;
    return 0;
}

const char *program_get_line(int index) {  // get line at index
    if (index < 0 || index >= PROGRAM_MEM_SIZE) {
        return NULL;
    }
    if (!program_memory[index].in_use) {
        return NULL;
    }
    return program_memory[index].line;
}

void program_free(int code_start, int code_len) {
    if (code_len <= 0) {
        return;
    }

    for (int i = 0; i < code_len; i++) {
        int idx = code_start + i; // calculate the actual index in program memory
        if (idx < 0 || idx >= PROGRAM_MEM_SIZE) {
            continue;
        }
        if (program_memory[idx].in_use) {
            free(program_memory[idx].line);
            program_memory[idx].line = NULL;
            program_memory[idx].in_use = 0;
        }
    }
}

