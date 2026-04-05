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

// Define the global frame table once here.
int frames[NUM_FRAMES];

static Program *frame_owner_program[NUM_FRAMES];
static int frame_owner_page[NUM_FRAMES];
static unsigned long frame_last_used[NUM_FRAMES];
static unsigned long lru_tick = 1;

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
        frame_owner_program[i] = NULL;
        frame_owner_page[i] = -1;
        frame_last_used[i] = 0;
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

static int frame_find_free(void) {
    for (int i = 0; i < NUM_FRAMES; i++) {
        if (frames[i] == 0) {
            return i;
        }
    }
    return -1;
}

static int frame_select_lru(void) {
    int victim = -1;
    unsigned long oldest = 0;

    for (int i = 0; i < NUM_FRAMES; i++) {
        if (!frames[i]) {
            continue;
        }
        if (victim == -1 || frame_last_used[i] < oldest) {
            victim = i;
            oldest = frame_last_used[i];
        }
    }
    return victim;
}

static void frame_touch(int frame) {
    if (frame >= 0 && frame < NUM_FRAMES) {
        frame_last_used[frame] = lru_tick++;
    }
}

static int program_loadp(Program *program, int page, int is_page_fault) {
    if (!program || page < 0 || page >= program->num_pages) {
        return 1;
    }

    if (program->frames[page] != -1) {
        int frame_start = program->frames[page];
        frame_touch(frame_start / FRAME_SIZE);
        return 0;
    }

    int frame = frame_find_free();
    if (frame == -1) {
        frame = frame_select_lru();
        if (frame == -1) {
            return 1;
        }

        Program *victim_program = frame_owner_program[frame];
        int victim_page = frame_owner_page[frame];
        int victim_frame_start = frame * FRAME_SIZE;

        if (is_page_fault) {
            printf("Page fault! Victim page contents:\n\n");
            for (int i = 0; i < FRAME_SIZE; i++) {
                int idx = frame * FRAME_SIZE + i;
                if (program_memory[idx].in_use && program_memory[idx].line) {
                    printf("%s", program_memory[idx].line);
                }
            }
            printf("\nEnd of victim page contents.\n");
        }

        if (victim_program && victim_page >= 0 && victim_page < victim_program->num_pages) {
            victim_program->frames[victim_page] = -1;
        }
        invalidate_frame(victim_frame_start);

        for (int i = 0; i < FRAME_SIZE; i++) {
            int idx = frame * FRAME_SIZE + i;
            if (program_memory[idx].in_use) {
                free(program_memory[idx].line);
                program_memory[idx].line = NULL;
                program_memory[idx].in_use = 0;
            }
        }
    }
    else if (is_page_fault) {
        printf("Page fault!\n");
    }

    int base_line = page * FRAME_SIZE;
    for (int i = 0; i < FRAME_SIZE; i++) {
        int idx = frame * FRAME_SIZE + i;
        int src = base_line + i;

        if (src < program->backing_len && program->backing_store[src]) {
            program_memory[idx].line = strdup(program->backing_store[src]);
            if (!program_memory[idx].line) {
                return 1;
            }
            program_memory[idx].in_use = 1;
        } else {
            program_memory[idx].line = NULL;
            program_memory[idx].in_use = 0;
        }
    }

    frames[frame] = 1;
    frame_owner_program[frame] = program;
    frame_owner_page[frame] = page;
    program->frames[page] = frame * FRAME_SIZE;
    frame_touch(frame);
    return 0;
}

int program_store(FILE *p, int *code_start, int *code_len, Program *program) {
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

    int page_num = (int)((count + FRAME_SIZE - 1) / FRAME_SIZE);
    if (page_num > MAX_PAGES) {
        for (size_t i = 0; i < count; i++) {
            free(lines[i]);
        }
        free(lines);
        return 1;
    }

    for (int i = 0; i < MAX_PAGES; i++) {
        program->frames[i] = -1;
    }

    program->backing_store = lines;
    program->backing_len = (int)count;
    program->num_pages = page_num;

    int pages_to_preload = (page_num < 2) ? page_num : 2;
    for (int pg = 0; pg < pages_to_preload; pg++) {
        if (program_loadp(program, pg, 0) != 0) {
            return 1;
        }
    }

    *code_start = 0;
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

//assignment 3: get line based on virtual address, VA
const char* get_va(PCB *pcb){
    if (!pcb || !pcb->program || pcb->pc < 0 || pcb->pc >= pcb->code_len) {
        return NULL;
    }

    pcb->page_faulted = 0;

    int virtual_page = pcb->pc / FRAME_SIZE;
    int page_offset = pcb->pc % FRAME_SIZE;

    if (virtual_page < 0 || virtual_page >= pcb->program->num_pages) {
        return NULL;
    }

    int frame_start = pcb->program->frames[virtual_page];
    if (frame_start == -1) {
        if (program_loadp(pcb->program, virtual_page, 1) != 0) {
            return NULL;
        }
        frame_start = pcb->program->frames[virtual_page];
        pcb->page_faulted = 1;
        pcb->page_table[virtual_page] = frame_start;
        return NULL;
    }

    pcb->page_table[virtual_page] = frame_start;
    frame_touch(frame_start / FRAME_SIZE);
    return program_get_line(frame_start + page_offset);
}

//Assignment 3: change this so that it frees a program's space in memory based on the frames it takes up
void program_free(Program *program) {
    if (!program) return;

    for (int i = 0; i < program->backing_len; i++) {
        free(program->backing_store[i]);
    }
    free(program->backing_store);
    program->backing_store = NULL;
    program->backing_len = 0;

    for (int i = 0; i < program->num_pages; i++) {
        int frame_start = program->frames[i];
        if (frame_start >= 0) {
            int frame = frame_start / FRAME_SIZE;
            if (frame >= 0 && frame < NUM_FRAMES && frame_owner_program[frame] == program && frame_owner_page[frame] == i) {
                for (int j = 0; j < FRAME_SIZE; j++) {
                    int idx = frame * FRAME_SIZE + j;
                    if (program_memory[idx].in_use) {
                        free(program_memory[idx].line);
                        program_memory[idx].line = NULL;
                        program_memory[idx].in_use = 0;
                    }
                }
                frames[frame] = 0;
                frame_owner_program[frame] = NULL;
                frame_owner_page[frame] = -1;
                frame_last_used[frame] = 0;
            }
        }
    }
}

