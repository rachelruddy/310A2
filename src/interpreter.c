//#define DEBUG 1

#ifdef DEBUG
#   define debug(...) fprintf(stderr, __VA_ARGS__)
#else
#   define debug(...)
// NDEBUG disables asserts
#   define NDEBUG
#endif

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>              // tolower, isdigit
#include <dirent.h>             // scandir
#include <unistd.h>             // chdir
#include <sys/stat.h>           // mkdir
// for run:
#include <sys/types.h>          // pid_t
#include <sys/wait.h>           // waitpid

#include "shellmemory.h"
#include "shell.h"
#include "scheduler.h"

int badcommand() {
    printf("Unknown Command\n");
    return 1;
}

// For source command only
int badcommandFileDoesNotExist() {
    printf("Bad command: File not found\n");
    return 3;
}

int badcommandMkdir() {
    printf("Bad command: my_mkdir\n");
    return 4;
}

int badcommandCd() {
    printf("Bad command: my_cd\n");
    return 5;
}

int help();
int quit();
int set(char *var, char *value);
int print(char *var);
int echo(char *tok);
int ls();
int my_mkdir(char *name);
int touch(char *path);
int cd(char *path);
int source(char *script);
int run(char *args[], int args_size);
int exec(char *args[], int args_size);

static int enqueue_batch_script_process(void);

// Interpret commands and their arguments
int interpreter(char *command_args[], int args_size) {
    int i;

    // these bits of debug output were very helpful for debugging
    // the changes we made to the parser!
    debug("#args: %d\n", args_size);
#ifdef DEBUG
    for (size_t i = 0; i < args_size; ++i) {
        debug("  %ld: %s\n", i, command_args[i]);
    }
#endif

    if (args_size < 1) {
        // This shouldn't be possible but we are defensive programmers.
        fprintf(stderr, "interpreter called with no words?\n");
        exit(1);
    }

    for (i = 0; i < args_size; i++) {   // terminate args at newlines
        command_args[i][strcspn(command_args[i], "\r\n")] = 0;
    }

    if (strcmp(command_args[0], "help") == 0) {
        //help
        if (args_size != 1)
            return badcommand();
        return help();

    } else if (strcmp(command_args[0], "quit") == 0) {
        //quit
        if (args_size != 1)
            return badcommand();
        return quit();

    } else if (strcmp(command_args[0], "set") == 0) {
        //set
        if (args_size != 3)
            return badcommand();
        return set(command_args[1], command_args[2]);

    } else if (strcmp(command_args[0], "print") == 0) {
        if (args_size != 2)
            return badcommand();
        return print(command_args[1]);

    } else if (strcmp(command_args[0], "echo") == 0) {
        if (args_size != 2)
            return badcommand();
        return echo(command_args[1]);

    } else if (strcmp(command_args[0], "my_ls") == 0) {
        if (args_size != 1)
            return badcommand();
        return ls();

    } else if (strcmp(command_args[0], "my_mkdir") == 0) {
        if (args_size != 2)
            return badcommand();
        return my_mkdir(command_args[1]);

    } else if (strcmp(command_args[0], "my_touch") == 0) {
        if (args_size != 2)
            return badcommand();
        return touch(command_args[1]);

    } else if (strcmp(command_args[0], "my_cd") == 0) {
        if (args_size != 2)
            return badcommand();
        return cd(command_args[1]);

    } else if (strcmp(command_args[0], "source") == 0) {
        if (args_size != 2)
            return badcommand();
        return source(command_args[1]);

    } else if (strcmp(command_args[0], "run") == 0) {
        if (args_size < 2)
            return badcommand();
        return run(&command_args[1], args_size - 1);
    } 
    else if (strcmp(command_args[0], "exec") == 0){
        if (args_size < 3 || args_size > 7)
            return badcommand();
        return exec(&command_args[1], (args_size-1));
    }
    else
        return badcommand();
}

int help() {

    // note the literal tab characters here for alignment
    char help_string[] = "COMMAND			DESCRIPTION\n \
help			Displays all the commands\n \
quit			Exits / terminates the shell with “Bye!”\n \
set VAR STRING		Assigns a value to shell memory\n \
print VAR		Displays the STRING assigned to VAR\n \
source SCRIPT.TXT		Executes the file SCRIPT.TXT\n ";
    printf("%s\n", help_string);
    return 0;
}

int quit() {
    printf("Bye!\n");

    if (scheduler_mt_is_enabled()) {
        scheduler_mt_request_quit();
        if (!scheduler_mt_is_worker_thread()) {
            scheduler_mt_shutdown();
            exit(0);
        }
        return 0;
    }
    exit(0);
}

static int enqueue_batch_script_process(void) {
    char line[MAX_USER_INPUT];
    FILE *tmp = tmpfile();
    if (!tmp) {
        return 1;
    }

    while (fgets(line, MAX_USER_INPUT - 1, stdin) != NULL) {
        fputs(line, tmp);
    }

    rewind(tmp);

    int code_start = 0;
    int code_len = 0;

    Program *batch_program = malloc(sizeof(Program));
    if (!batch_program) {
        fclose(tmp);
        return 1;
    }
    memset(batch_program, 0, sizeof(Program));
    batch_program->count = 0;


    int load_rc = program_store(tmp, &code_start, &code_len, batch_program);
    fclose(tmp);

    if (load_rc != 0) {
        free(batch_program);
        printf("Error: Script memory full\n");
        return 1;
    }

    batch_program->code_start = code_start;
    batch_program->code_len = code_len;

    PCB *pcb = malloc(sizeof(PCB));
    if (!pcb) {
        program_free(batch_program);
        free(batch_program);
        return 1;
    }

    pcb->pid = get_next_pid();
    pcb->code_start = code_start;
    pcb->code_len = code_len;
    pcb->pc = 0;
    pcb->page_faulted = 0;
    pcb->score = pcb->code_len;
    pcb->next = NULL;
    pcb->program = batch_program;
    //Assignment 3: page table logic
    pcb->num_pages = batch_program->num_pages;
    for (int i = 0; i < MAX_PAGES; i++) {
        if (i < batch_program->num_pages) {
            pcb->page_table[i] = batch_program->frames[i]; // copy frame start index
        } else {
            pcb->page_table[i] = -1; // unused pages
        }
    }

    enqueue_front(pcb);

    // increment count now that PCB points to program
    batch_program->count = 1;

    return 0;
}

int set(char *var, char *value) {
    mem_set_value(var, value);
    return 0;
}

int print(char *var) {
    char *value = mem_get_value(var);
    if (value) {
        printf("%s\n", value);
        free(value);
    } else {
        printf("Variable does not exist\n");
    }
    return 0;
}

int echo(char *tok) {
    int must_free = 0;
    // is it a var?
    if (tok[0] == '$') {
        tok++;                  // advance pointer, so that tok is now the stuff after '$'
        tok = mem_get_value(tok);
        if (tok == NULL) {
            tok = "";           // must use empty string, can't pass NULL to printf
        } else {
            must_free = 1;
        }
    }

    printf("%s\n", tok);

    // memory management technically optional for this assignment
    if (must_free) free(tok);

    return 0;
}

// We can hide dotfiles in ls using either the filter operand to scandir,
// or by checking the first character ourselves when we go to print
// the names. That would work, and is less code, but this is more robust.
// And this is also better since it won't allocate extra dirents.
int ls_filter(const struct dirent *d) {
    if (d->d_name[0] == '.') return 0;
    return 1;
}

int ls_compare_char(char a, char b) {
    // assumption: a,b are both either digits or letters.
    // If this is not true, the characters will be effectively compared
    // as ASCII when we do the lower_a - lower_b fallback.

    // if both are digits, compare them
    if (isdigit(a) && isdigit(b)) {
        return a - b;
    }
    // if only a is a digit, then b isn't, so a wins.
    if (isdigit(a)) {
        return -1;
    }

    // lowercase both letters so we can compare their alphabetic position.
    char lower_a = tolower(a), lower_b = tolower(b);
    if (lower_a == lower_b) {
        // a and b are the same letter, possibly in different cases.
        // If they are really the same letter, this returns 0.
        // Otherwise, it's negative if A was capital,
        // and positive if B is capital.
        return a - b;
    }

    // Otherwise, compare their alphabetic position by comparing
    // them at a known case.
    return lower_a - lower_b;
}

int ls_compare_str(const char *a, const char *b) {
    // a simple strcmp implementation that uses ls_compare_char.
    // We only check if *a is zero, since if *b is zero earlier,
    // it would've been unequal to *a at that time and we would return.
    // If *b is zero at the same point or later than *a, we'll exit the
    // loop and return the correct value with the last comparison.

    while (*a != '\0') {
        int d = ls_compare_char(*a, *b);
        if (d != 0) return d;
        a++, b++;
    }
    return ls_compare_char(*a, *b);
}

int ls_compare(const struct dirent **a, const struct dirent **b) {
    return ls_compare_str((*a)->d_name, (*b)->d_name);
}

int ls() {
    // straight out of the man page examples for scandir
    // alphasort uses strcoll instead of strcmp,
    // so we have to implement our own comparator to match the ls spec.
    // Note that the test cases weren't very picky about the specified order,
    // so if you just used alphasort with scandir, you should have passed.
    // This was intentional on our part.
    struct dirent **namelist;
    int n;

    n = scandir(".", &namelist, NULL, ls_compare);
    if (n == -1) {
        // something is catastrophically wrong, just give up.
        perror("my_ls couldn't scan the directory");
        return 0;
    }

    for (size_t i = 0; i < n; ++i) {
        printf("%s\n", namelist[i]->d_name);
        free(namelist[i]);
    }
    free(namelist);

    return 0;
}

int str_isalphanum(char *name) {
    for (char c = *name; c != '\0'; c = *++name) {
        if (!(isdigit(c) || isalpha(c))) return 0;
    }
    return 1;
}

int my_mkdir(char *name) {
    int must_free = 0;

    debug("my_mkdir: ->%s<-\n", name);

    if (name[0] == '$') {
        ++name;
        // lookup name
        name = mem_get_value(name);
        debug("  lookup: %s\n", name ? name : "(NULL)");
        if (name) {
            // name exists, should free whatever we got
            must_free = 1;
        }
    }
    if (!name || !str_isalphanum(name)) {
        // either name doesn't exist, or isn't valid, error.
        if (must_free) free(name);
        return badcommandMkdir();
    }
    // at this point name is definitely OK

    // 0777 means "777 in octal," aka 511. This value means
    // "give the new folder all permissions that we can."
    int result = mkdir(name, 0777);

    if (result) {
        // description doesn't specify what to do in this case,
        // (including if the directory already exists)
        // so we just give an error message on stderr and ignore it.
        perror("Something went wrong in my_mkdir");
    }

    if (must_free) free(name);
    return 0;
}

int touch(char *path) {
    // we're told we can assume this.
    assert(str_isalphanum(path));
    // if things go wrong, just ignore it.
    FILE *f = fopen(path, "a");
    fclose(f);
    return 0;
}

int cd(char *path) {
    // we're told we can assume this.
    assert(str_isalphanum(path));

    int result = chdir(path);
    if (result) {
        return badcommandCd();
    }
    return 0;
}

int source(char *script) {
    FILE *p = fopen(script, "rt");      // the program is in a file

    if (p == NULL) { // soruce changed
        return badcommandFileDoesNotExist();
    }

    int code_start = 0;
    int code_len = 0;

    Program *source_program = malloc(sizeof(Program));
    if (!source_program) {
        fclose(p);
        return 1;
    }
    memset(source_program, 0, sizeof(Program));
    source_program->count = 0;       //no pcbs assigned yet

    int load_rc = program_store(p, &code_start, &code_len, source_program);
    fclose(p);

    if (load_rc != 0) {
        free(source_program);
        printf("Error: Script memory full\n");
        return 1;
    }

    source_program->code_start = code_start;
    source_program->code_len = code_len;

    PCB *pcb = malloc(sizeof(PCB));
    if (!pcb) {
        program_free(source_program);
        free(source_program);
        return 1;
    }

    pcb->pid = get_next_pid();
    pcb->code_start = code_start;
    pcb->code_len = code_len;
    pcb->pc = 0;
    pcb->page_faulted = 0;
    pcb->next = NULL;
    pcb->program = source_program;
    //Assignment 3: page table logic
    pcb->num_pages = source_program->num_pages;
    for (int i = 0; i < MAX_PAGES; i++) {
        if (i < source_program->num_pages) {
            pcb->page_table[i] = source_program->frames[i]; // copy frame start index
        } else {
            pcb->page_table[i] = -1; // unused pages
        }
    }

    enqueue(pcb);

    // increment count now that PCB points to program
    source_program->count = 1;

    run_scheduler("FCFS");

    return 0;
}

int run(char *args[], int arg_size) {
    // copy the args into a new NULL-terminated array.
    char **adj_args = calloc(arg_size + 1, sizeof(char *));
    for (int i = 0; i < arg_size; ++i) {
        adj_args[i] = args[i];
    }

    // always flush output streams before forking.
    fflush(stdout);
    // attempt to fork the shell
    pid_t pid = fork();
    if (pid < 0) {
        // fork failed. Report the error and move on.
        perror("fork() failed");
        return 1;
    } else if (pid == 0) {
        // we are the new child process.
        execvp(adj_args[0], adj_args);
        perror("exec failed");
        // avoid duplicated stdin reads in some batch-mode failure cases
        fclose(stdin);
        exit(1);
    } else {
        // we are the parent process.
        waitpid(pid, NULL, 0);
    }

    return 0;
}

int exec(char *args[], int args_size) {
    int mt_flag = 0;
    int bg_flag = 0;

    int end = args_size - 1;
    if (end >= 0 && strcmp(args[end], "MT") == 0) {
        mt_flag = 1;
        end--;
    }
    if (end >= 0 && strcmp(args[end], "#") == 0) {
        bg_flag = 1;
        end--;
    }
    if (end < 1) {
        return badcommand();
    }

    //get the last parameter (policy name)
    char *policy = args[end];
    if (strcmp(policy, "FCFS") != 0 && strcmp(policy, "SJF") != 0 && strcmp(policy, "RR") != 0 && strcmp(policy, "RR30") != 0 && strcmp(policy, "AGING") != 0){
        printf("Invalid POLICY. Please input one of: FCFS, SJF, RR, RR30, AGING as your last argument. \n");
        return 1;
    }

    if (mt_flag && !scheduler_policy_supports_mt(policy)) {
        return badcommand();
    }

    if (mt_flag && scheduler_mt_enable() != 0) {
        return 1;
    }

    int num_programs = end;

    if (num_programs < 1 || num_programs > 3) {
        return badcommand();
    }

    //check if each program file exists
    for(int i=0; i<num_programs; i++){
        FILE *p = fopen(args[i], "rt");      
        if (p == NULL) { 
            printf("Program file '%s' not found.\n", args[i]);
            return badcommandFileDoesNotExist();
        }
        fclose(p);
    }

    // THIS PART IS IGNORED FOR ASSIGNMENT 3
    // "exec now supports running the same script"
    // check for duplicate program filenames
    // for(int i = 0; i < num_programs; i++){
    //     for(int j = i + 1; j < num_programs; j++){
    //         if (strcmp(args[i], args[j]) == 0){
    //             printf("Error: Each program must be different.\n");
    //             return 1; 
    //         }
    //     }
    // }

    //////////////// FCFS and RR enqueuing ////////////////////
    if(strcmp(policy, "FCFS") == 0 || strcmp(policy, "RR") == 0 || strcmp(policy, "RR30") == 0){
        //load programs and enqueue PCBs
        for (int i=0; i<num_programs; i++){
            // Assignment 3: search to see if this program has already been added to shell memory before allocating memory
            Program *program = NULL;
            for (int j=0; j<prog_count; j++){
                if(strcmp(programs[j].name, args[i]) == 0){
                    program = &programs[j];
                    break;
                }
            }

            //If program does not already exist in shell memory, load it in
            int code_start = 0;
            int code_len = 0;
            if (program == NULL){
                //Create program object first 
                programs[prog_count].name = strdup(args[i]);
                programs[prog_count].count = 0;
                program = &programs[prog_count];


                FILE *p = fopen(args[i], "rt"); 

                //pass program object pointer to sotre script function
                int load_rc = program_store(p, &code_start, &code_len, program);
                fclose(p);

                if (load_rc != 0) {
                    printf("Error: Script memory full\n");
                    return 1;
                }

                program->code_start = code_start;
                program->code_len = code_len;
                prog_count++;
            }
            else{
                //program already exists in shell memory, PCB points to same memory
                code_start = program->code_start;
                code_len = program->code_len;
            }

            //Create PCB
            PCB *pcb = malloc(sizeof(PCB));
            if (!pcb) {
                if(program->count == 0){
                    program_free(program);
                }
                return 1;
            }
            pcb->pid = get_next_pid();
            pcb->code_start = code_start;
            pcb->code_len = code_len;
            pcb->pc = 0;
            pcb->page_faulted = 0;
            pcb->next = NULL;
            pcb->program = program;
            //Assignment 3: page table logic
            pcb->num_pages = program->num_pages;
            for (int i = 0; i < MAX_PAGES; i++) {
                if (i < program->num_pages) {
                    pcb->page_table[i] = program->frames[i]; // copy frame number
                } else {
                    pcb->page_table[i] = -1; // unused pages
                }
            }
            enqueue(pcb);
            
            //Increase count of program in memory
            program->count++;
        }
    }

    //////////////// SJF enqueuing ////////////////////
    else if (strcmp(policy, "SJF") == 0){
        //sort programs by length- shortest program first -> longest last
        PCB *pcb_list[num_programs];

        for (int i = 0; i < num_programs; i++) {
            // Assignment 3: search to see if this program has already been added to shell memory before allocating memory
            Program *program = NULL;
            for (int j=0; j<prog_count; j++){
                if(strcmp(programs[j].name, args[i]) == 0){
                    program = &programs[j];
                    break;
                }
            }

            int code_start = 0;
            int code_len = 0;
            // If program not in memory yet:
            if(program == NULL){
                //Create program object first 
                programs[prog_count].name = strdup(args[i]);
                programs[prog_count].count = 0;
                program = &programs[prog_count];

                FILE *p = fopen(args[i], "rt");

                int load_rc = program_store(p, &code_start, &code_len, program);
                fclose(p);

                if (load_rc != 0) {
                    printf("Error: Script memory full\n");
                    return 1;
                }

                program->code_start = code_start;
                program->code_len = code_len;
                prog_count++;
            }
            else{
                //program already exists in shell memory, PCB points to same memory
                code_start = program->code_start;
                code_len = program->code_len;
            }

            //Allocate PCB
            PCB *pcb = malloc(sizeof(PCB));
            if (!pcb) {
                if(program->count == 0){
                    program_free(program);
                }
                return 1;
            }

            pcb->pid = get_next_pid();
            pcb->code_start = code_start;
            pcb->code_len = code_len;
            pcb->pc = 0;
            pcb->page_faulted = 0;
            pcb->next = NULL;
            pcb->program = program;
            //Assignment 3: page table logic
            pcb->num_pages = program->num_pages;
            for (int i = 0; i < MAX_PAGES; i++) {
                if (i < program->num_pages) {
                    pcb->page_table[i] = program->frames[i]; // copy frame number
                } else {
                    pcb->page_table[i] = -1; // unused pages
                }
            }

            pcb_list[i] = pcb;   // store temporarily

            //Increase count of program in memory
            program->count++;
        }

        //sort
        for (int i = 0; i < num_programs - 1; i++) {
            for (int j = 0; j < num_programs - i - 1; j++) {
                if (pcb_list[j]->code_len > pcb_list[j + 1]->code_len) {
                    PCB *temp = pcb_list[j];
                    pcb_list[j] = pcb_list[j + 1];
                    pcb_list[j + 1] = temp;
                }
            }
        }

	//enqueue programs based on this sorting
        for (int i = 0; i < num_programs; i++) {
            enqueue(pcb_list[i]);
        }
    }

    //////////// AGING - 1.2.4 ASH ////////////
    else if (strcmp(policy, "AGING")==0) {
        //sort programs by length- shortest program first -> longest last
        PCB *pcb_list[num_programs];

        for (int i = 0; i < num_programs; i++) {
            // Assignment 3: search to see if this program has already been added to shell memory before allocating memory
            Program *program = NULL;
            for (int j=0; j<prog_count; j++){
                if(strcmp(programs[j].name, args[i]) == 0){
                    program = &programs[j];
                    break;
                }
            }

            int code_start = 0;
            int code_len = 0;
            // If program is not in memory yet, allocate to shell memory
            if(program == NULL){
                //Create program object first 
                programs[prog_count].name = strdup(args[i]);
                programs[prog_count].count = 0;
                program = &programs[prog_count];

                FILE *p = fopen(args[i], "rt");

                int load_rc = program_store(p, &code_start, &code_len, program);
                fclose(p);

                if (load_rc != 0) {
                    printf("Error: Script memory full\n");
                    return 1;
                }

                program->code_start = code_start;
                program->code_len = code_len;
                prog_count++;
            }
            else{
                //program already exists in shell memory, PCB points to same memory
                code_start = program->code_start;
                code_len = program->code_len;
            }

            //Allocate PCB
            PCB *pcb = malloc(sizeof(PCB));
            if (!pcb) {
                if(program->count == 0){
                    program_free(program);
                }
                return 1;
            }
            pcb->pid = get_next_pid();
            pcb->code_start = code_start;
            pcb->code_len = code_len;
            pcb->pc = 0;
            pcb->page_faulted = 0;
            pcb->score= pcb->code_len; // initialize score to code length
            pcb->next = NULL;
            pcb->program = program;
            //Assignment 3: page table logic
            pcb->num_pages = program->num_pages;
            for (int i = 0; i < MAX_PAGES; i++) {
                if (i < program->num_pages) {
                    pcb->page_table[i] = program->frames[i]; // copy frame number
                } else {
                    pcb->page_table[i] = -1; // unused pages
                }
            }

            pcb_list[i] = pcb;   // store temporarily

            //Increase count of program in memory
            program->count++;
        }

        //sort by score this time
        for (int i = 0; i < num_programs - 1; i++) {
            for (int j = 0; j < num_programs - i - 1; j++) {
                if (pcb_list[j]->score > pcb_list[j + 1]->score) {
                    PCB *temp = pcb_list[j];
                    pcb_list[j] = pcb_list[j + 1];
                    pcb_list[j + 1] = temp;
                }
            }
        }

	//enqueue programs based on this sorting
        for (int i = 0; i < num_programs; i++) {
            enqueue(pcb_list[i]);
        }
    }

    if (bg_flag) {
        if (enqueue_batch_script_process() != 0) {
            return 1;
        }
    }
      
    if (!scheduler_is_running()) {
        run_scheduler(policy);
    }
    return 0;
}

