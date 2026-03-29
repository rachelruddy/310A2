#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "shell.h"
#include "shellmemory.h"
#include "scheduler.h"

struct memory_struct {
    char *var;
    char *value;
};

struct program_line {
    char *line;
    int in_use;
};

// Assignment 3: this is essentially just the variable storage
struct memory_struct shellmemory[MEM_SIZE];
//Assignment 3: this stores program code, in frames
static struct program_line program_memory[PROGRAM_MEM_SIZE];

Program programs[MAX_PROGRAMS];
int prog_count = 0;



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
    for (int i = 0; i < NUM_FRAMES; i++) {
        frames[i] = 0;  // all frames free initially
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



int program_store_script(FILE *p, int *code_start, int *code_len, Program *program) {
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

    // Assign pages to frames
    int page_num = 0;
    int lines_index = 0;
    while (lines_index < (int)count) {
        // Find free frame
        int frame = -1;
        for (int f = 0; f < NUM_FRAMES; f++) {
            if (frames[f] == 0) { 
                frame = f; 
                break; 
            }
        }
        if (frame == -1) {
            // Out of memory
            for (size_t i = 0; i < count; i++) free(lines[i]);
            free(lines);
            return 1;
        }

        // Copy lines into frame
        for (int i = 0; i < FRAME_SIZE && lines_index < (int)count; i++, lines_index++) {
            int mem_index = frame * FRAME_SIZE + i;
            program_memory[mem_index].line = lines[lines_index];
            program_memory[mem_index].in_use = 1;
        }

        frames[frame] = 1;          // mark frame used
        program->frames[page_num] = frame; // store in program
        page_num++;
    }

    // Fill remaining program->frames with -1
    for (int i = page_num; i < MAX_PAGES; i++) program->frames[i] = -1;

    *code_start = 0;  // can keep 0 or remove, code now in frames
    *code_len = count;
    program->num_pages = page_num;

    free(lines); // lines themselves now in memory
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

//assignment 3: get line based on virtual address, VA
const char* program_get_line_VA(PCB *pcb){
    int virtual_page = pcb->pc / FRAME_SIZE;
    int page_offset = pcb->pc % FRAME_SIZE;

    // check if page is loaded- HANDLE PAGE FAULTS PART 2, rn just return statement because all pages are loaded
    if (pcb->page_table[virtual_page] == -1) {
        return NULL;
    }

    int frame_number = pcb->page_table[virtual_page];
    return program_get_line(frame_number * FRAME_SIZE + page_offset);
}

//Assignment 3: change this so that it frees a program's space in memory based on the frames it takes up
void program_free(Program *program) {
    if (!program) return;

    for (int i = 0; i < program->num_pages; i++) {
        int frame = program->frames[i];
        if (frame >= 0 && frame < NUM_FRAMES) {
            // Free lines inside the frame
            for (int j = 0; j < FRAME_SIZE; j++) {
                int idx = frame * FRAME_SIZE + j;
                if (program_memory[idx].in_use) {
                    free(program_memory[idx].line);
                    program_memory[idx].line = NULL;
                    program_memory[idx].in_use = 0;
                }
            }
            // Mark the frame as free
            frames[frame] = 0;
        }
    }
}

