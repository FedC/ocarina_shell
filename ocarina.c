/*
 * Name:		Ocarina
 * Version: 		0.3 - alpha
 * By: 			Federico Commisso
 * Last Revision:	12/09/2011
*/

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>

#define TRUE 1
#define FALSE !TRUE
#define FOREGROUND 'F'
#define BACKGROUND 'B'
#define SUSPENDED 'S'
#define WAITING_INPUT 'W'

#define STDIN 1
#define STDOUT 2

#define BY_PROCESS_ID 1
#define BY_JOB_ID 2
#define BY_JOB_STATUS 3
#define BUFFER_MAX_LENGTH 50
static char* currentDirectory;
static char userInput = '\0';
static char buffer[BUFFER_MAX_LENGTH];
static int bufferChars = 0;

static char *commandArgv[5];
static int commandArgc = 0;
static int numActiveJobs = 0;

/*********************/
/*                   */
/*  Data Structures  */
/*                   */
/*********************/
typedef struct job {
    int spid;              /* job number */
    int numProgs;           /* total number of programs in job */
    int runningProgs;       /* number of programs running */
    char * text;            /* name of job */
    char * cmdBuffer;          /* buffer various argv's point into */
    pid_t pgrpid;             /* process group ID for the job */
    struct childProgram * progs; /* array of programs in job */
    struct job * next;      /* to track background commands */
} t_job;

struct jobSet {
    struct job * head;      /* head of list of running jobs */
    struct job * fg;        /* current foreground job */
};

enum redirectionType { REDIRECT_INPUT, REDIRECT_OVERWRITE, REDIRECT_APPEND };

struct redirectionSpecifier {
    enum redirectionType type;  /* type of redirection */
    int fd;                 /* file descriptor being redirected */
    char * filename;        /* file to redirect fd to */
};

struct childProgram {
    pid_t pid;              /* 0 if exited */
    char ** argv;           /* program name and arguments */
    int numRedirections;    /* elements in redirection array */
    struct redirectionSpecifier * redirections;  /* I/O redirections */
};

#define MAX_COMMAND_LEN 250


/*********************/
/*                   */
/*     UTILITIES     */
/*                   */
/*********************/
void freeJob(struct job * cmd) {
    int i;

    for (i = 0; i < cmd->numProgs; i++) {
        free(cmd->progs[i].argv);
        if (cmd->progs[i].redirections) free(cmd->progs[i].redirections);
    }
    free(cmd->progs);
    if (cmd->text) free(cmd->text);
    free(cmd->cmdBuffer);
}

int getCommand(FILE * source, char * command) {
    //printf("DEBUG: ------------ getting command\n");
    if (source == stdin) {
        //printf("DEBUG: ------------ printing propmt\n");
        printf("Ocarina$ ");
        //printf("DEBUG: ------------ flushing stdout\n");
        fflush(stdout);
        //printf("DEBUG: ------------ flushed stdout\n");
        
    }
    
    
    //printf("DEBUG: ------------ check fgets\n");
    if (!fgets(command, MAX_COMMAND_LEN, source)) {
        if (source == stdin) {
            printf("\n");
           // printf("DEBUG: ------------ fgetrutrns\n");
        } 
        return 1;
    }// else { printf("DEBUG: ------------ fgets bypass\n"); }

    /* remove trailing newline */
    command[strlen(command) - 1] = '\0';
    
    return 0;
}


void destroyCommand()
{
        while (commandArgc != 0) {
                commandArgv[commandArgc] = NULL;
                commandArgc--;
        }
        bufferChars = 0;
}

void populateCommand()
{
        char* bufferPointer;
        bufferPointer = strtok(buffer, " ");
        while (bufferPointer != NULL) {
                commandArgv[commandArgc] = bufferPointer;
                bufferPointer = strtok(NULL, " ");
                commandArgc++;
        }
}

void getTextLine()
{
        destroyCommand();
        while ((userInput != '\n') && (bufferChars < BUFFER_MAX_LENGTH)) {
                buffer[bufferChars++] = userInput;
                userInput = getchar();
        }
        buffer[bufferChars] = 0x00;
        populateCommand();
}



int parseCommand(char ** commandPtr, struct job *job, int *isBackground) {
    /*  returns 0 if no command present.
     *  if a command is found, commandPtr is set to point to next command
     *  or NULL if no more commands are found 
    */
    char * command;
    char * returnCommand = NULL;
    char * src, * buf, * chptr;
    int argc = 0;
    int done = 0;
    int argvAlloced;
    int i;
    char quote = '\0';  
    int count;
    struct childProgram * prog;

    /* skip leading white space */
    while (**commandPtr && isspace(**commandPtr)) (*commandPtr)++;
    /* handle empty lines and leading '#' characters */
        if (!**commandPtr || (**commandPtr=='#')) {
        job->numProgs = 0;
        *commandPtr = NULL;
        return 0;
    }

    *isBackground = 0;
    job->numProgs = 1;
    job->progs = malloc(sizeof(*job->progs));
    job->cmdBuffer = command = calloc(1, strlen(*commandPtr) + 1);
    job->text = NULL;

    prog = job->progs;
    prog->numRedirections = 0;
    prog->redirections = NULL;

    argvAlloced = 5;
    prog->argv = malloc(sizeof(*prog->argv) * argvAlloced);
    prog->argv[0] = job->cmdBuffer;

    buf = command;
    src = *commandPtr;
    while (*src && !done) {
        if (quote == *src) {
            quote = '\0';
        } else if (quote) {
            if (*src == '\\') {
                src++;
                if (!*src) {
                    printf("Error: character expected after \\\n");
                    freeJob(job);
                    return 1;
                }
                if (*src != quote) *buf++ = '\\';
            }
            *buf++ = *src;
        } else if (isspace(*src)) {
            if (*prog->argv[argc]) {
                buf++, argc++;
     
                if ((argc + 1) == argvAlloced) {
                    argvAlloced += 5;
                    prog->argv = realloc(prog->argv, 
				    sizeof(*prog->argv) * argvAlloced);
                }
                prog->argv[argc] = buf;
            }
        } else switch (*src) {
          case '"':
          case '\'':
            quote = *src;
            break;

          case '#':                         /* comment */
            done = 1;
            break;

          case '>':                         /* redirections */
          case '<':
            i = prog->numRedirections++;
            prog->redirections = realloc(prog->redirections, 
                                sizeof(*prog->redirections) * (i + 1));

            prog->redirections[i].fd = -1;
            if (buf != prog->argv[argc]) {
                prog->redirections[i].fd = strtol(prog->argv[argc], &chptr, 10);

                if (*chptr && *prog->argv[argc]) {
                    buf++, argc++;
                }
            }

            if (prog->redirections[i].fd == -1) {
                if (*src == '>')
                    prog->redirections[i].fd = 1;
                else
                    prog->redirections[i].fd = 0;
            }

            if (*src++ == '>') {
                if (*src == '>')
                    prog->redirections[i].type = REDIRECT_APPEND, src++;
                else 
                    prog->redirections[i].type = REDIRECT_OVERWRITE;
            } else {
                prog->redirections[i].type = REDIRECT_INPUT;
            }
            chptr = src;
            while (isspace(*chptr)) chptr++;

            if (!*chptr) {
                printf("Error: no file name found after %c\n", *src);
                freeJob(job);
                return 1;
            }

            prog->redirections[i].filename = buf;
            while (*chptr && !isspace(*chptr)) 
                *buf++ = *chptr++;

            src = chptr - 1;                
            prog->argv[argc] = ++buf;
            break;

          case '|':                         /* pipe */
            /* finish this command */
            if (*prog->argv[argc]) argc++;
            if (!argc) {
                fprintf(stderr, "empty command in pipe\n");
                freeJob(job);
                return 1;
            }
            prog->argv[argc] = NULL;

            /* start the next */
            job->numProgs++;
            job->progs = realloc(job->progs, 
                                 sizeof(*job->progs) * job->numProgs);
            prog = job->progs + (job->numProgs - 1);
            prog->numRedirections = 0;
            prog->redirections = NULL;
            argc = 0;

            argvAlloced = 5;
            prog->argv = malloc(sizeof(*prog->argv) * argvAlloced);
            prog->argv[0] = ++buf;

            src++;
            while (*src && isspace(*src)) src++;

            if (!*src) {
                fprintf(stderr, "empty command in pipe\n");
                return 1;
            }
            src--;            

            break;

          case '&':                         /* background */
            *isBackground = 1;
          case ';':                         /* multiple commands */
            done = 1;
            returnCommand = *commandPtr + (src - *commandPtr) + 1;
            break;

          case '\\':
            src++;
            if (!*src) {
                freeJob(job);
                fprintf(stderr, "character expected after \\\n");
                return 1;
            }
            /* fallthrough */
          default:
            *buf++ = *src;
        }

        src++; /* set src back to orig value */
    }

    if (*prog->argv[argc]) {
        argc++;
    }
    if (!argc) {
        freeJob(job);
        return 0;
    }
    prog->argv[argc] = NULL;

    if (!returnCommand) {
        job->text = malloc(strlen(*commandPtr) + 1);
        strcpy(job->text, *commandPtr);
    } else {
        count = returnCommand - *commandPtr;
        job->text = malloc(count + 1);
        strncpy(job->text, *commandPtr, count);
        job->text[count] = '\0';
    }

    *commandPtr = returnCommand;

    return 0;
}

int setupRedirections(struct childProgram * prog) {
    int i;
    int openfd;
    int mode;
    struct redirectionSpecifier * redir = prog->redirections;

    for (i = 0; i < prog->numRedirections; i++, redir++) {
        switch (redir->type) {
          case REDIRECT_INPUT:
            mode = O_RDONLY;
            break;
          case REDIRECT_OVERWRITE:
            mode = O_RDWR | O_CREAT | O_TRUNC; 
            break;
          case REDIRECT_APPEND:
            mode = O_RDWR | O_CREAT | O_APPEND;
            break;
        }

        openfd = open(redir->filename, mode, 0666);
        if (openfd < 0) {
            /* this could get lost if stderr has been redirected, but
               bash and ash both lose it as well (though zsh doesn't!) */
            fprintf(stderr, "error opening %s: %s\n", redir->filename,
                        strerror(errno));
            return 1;
        }

        if (openfd != redir->fd) {
            dup2(openfd, redir->fd);
            close(openfd);
        }
    }

    return 0;
}

/*void addNewJob(struct job * job, struct job *newJob, struct jobSet * jobs) {*/
/*    if (!jobs->head) {*/
/*        job = jobs->head = malloc(sizeof(*job));*/
/*    } else {*/
/*        for (job = jobs->head; job->next; job = job->next);*/
/*        job->next = malloc(sizeof(*job));*/
/*        job = job->next;*/
/*    }*/
/*}*/

/*void setJobID(struct job *job, struct job *newJob, struct jobSet * jobs) {*/
/*    newJob->spid = 1;*/
/*    for ( job = jobs->head; job; job = job->next) {*/
/*       // if (job->spid >= newJob.spid) */
/*          //  newJob.spid = job->spid + 1;*/
/*    }*/
/*}*/

int runCommand(struct job newJob, struct jobSet * jobs, 
                int inBackground) {
    struct job * job;
    int i;
    int nextin, nextout;
    int pipefds[2]; /* [0] is for reading, 
                        the rest for the two piped commands */
    
    /* ****built-ins**** */    
    //printf("checking built ins\n");
    if (!strcmp(newJob.progs[0].argv[0], "exit")) {
        exit(0);
    } else if (!strcmp(newJob.progs[0].argv[0], "jobs")) {
	for (job = jobs->head; job; job = job->next)
            printf("[%d]	%-22s	%.40s\n", job->spid, "Running", 
                    job->text);
            return 0;
    }
/*    else if (!strcmp(newJob.progs[0].argv[0], "fg")) {
/*	    /* Move a job from the background to the foreground*/
/*	    return 0;
/*    }*/
    
    nextin = 0, nextout = 1;
    int next = i +1;
   for (i = 0; i < newJob.numProgs; i++) {
        if ((i + 1) < newJob.numProgs) {
            pipe(pipefds);
            nextout = pipefds[1];
        } else {
            nextout = 1;
        }

        if (!(newJob.progs[i].pid = fork())) {
            if (nextin != 0) {
                dup2(nextin, 0);
                close(nextin);
            }

            if (nextout != 1) {
                dup2(nextout, 1);
                close(nextout);
            }
            
           // printf("Checking for redirections\n");
            /* redirections  */
            setupRedirections(newJob.progs + i);
                    
                    
            execvp(newJob.progs[i].argv[0], newJob.progs[i].argv);
            printf("Error: failed to execute %s\n", 
                        newJob.progs[i].argv[0]);
            exit(1);
        }
        /* assign the child pid */
        setpgid(newJob.progs[i].pid, newJob.progs[0].pid);
        
        if (nextin != 0) close(nextin);
        if (nextout != 1) close(nextout);

        nextin = pipefds[0];      /* there isn't another process next */
    }
    
    newJob.pgrpid = newJob.progs[0].pid;
    /* assign the spid to use next */
    //setJobID(job, newJob, &jobs);
    newJob.spid = 1;
    for (job = jobs->head; job; job = job->next)
        if (job->spid >= newJob.spid)
            newJob.spid = job->spid + 1;
    
    /* add the job to running jobs set */
    //addNewJob(job, newJob, &jobs);
    if (!jobs->head) {
        job = jobs->head = malloc(sizeof(*job));
    } else {
        for (job = jobs->head; job->next; job = job->next);
        job->next = malloc(sizeof(*job));
        job = job->next;
    }
    
    *job = newJob;
    job->next = NULL;
    job->runningProgs = job->numProgs;
    
    /* now check if it runs in bg */
    if (inBackground) {
        /* background jobs don't need waiting */
        int progIndx = newJob.numProgs-1;
        printf("[%d]    %d\n", job->spid, 
                    newJob.progs[newJob.numProgs-1].pid);
    } else {
        jobs->fg = job;
        /* not running in bg so move process group to fg */
        //printf("DEBUG: ------------ tcsetpgrp(0, newJob.pgrpid)\n");
        if ( tcsetpgrp(0, newJob.pgrpid)) {
            printf("Error: tcsetpgrp in runCommand()");
        }
    }
    
   return 0;
}

void removeJob(struct jobSet * jobs, struct job * job) {
    struct job * previousJob;
    
    freeJob(job);
    if (job== jobs->head) {
        jobs->head = job->next;
    } else {
        previousJob = jobs->head;
        while (previousJob->next != job) previousJob = previousJob->next;
        previousJob->next = job->next;
    }
    
    free(job);
}

void checkJobs(struct jobSet * jobList) {
    struct job * job;
    pid_t childpid;
    int status;
    int progNum;
   
    while ((childpid = waitpid(-1, &status, WNOHANG)) > 0) {
        for (job = jobList->head; job; job = job->next) {
            progNum = 0;
            while (progNum < job->numProgs && 
                        job->progs[progNum].pid != childpid)
                progNum++;
            if (progNum < job->numProgs) break;
        }

        job->runningProgs--;
        job->progs[progNum].pid = 0;

        if (!job->runningProgs) {
            printf("[%d]	%-22s	%.40s\n", job->spid, "Done", job->text);
            removeJob(jobList, job);
        }
    }

    if (childpid == -1 && errno != ECHILD)
        perror("waitpid");
}




/*********************/
/*                   */
/* MAIN PROGRAM LOOP */
/*                   */
/*********************/
int main(int argc, char ** argv) {
    char command[MAX_COMMAND_LEN + 1];
    char * nextCommand = NULL;
    struct jobSet jobList = { NULL, NULL };
    struct job newJob;
    FILE * input = stdin;
    int i;
    int status;
    int inBg;
    signal(SIGTTOU, SIG_IGN);
    
    while (1) {
        if (!jobList.fg) {
            /* no job is in the foreground */

            /* see if any background processes have exited */
            checkJobs(&jobList);

            if (!nextCommand) {
                if (getCommand(input, command)) break;
                nextCommand = command;
            }

            if (!parseCommand(&nextCommand, &newJob, &inBg) &&
                              newJob.numProgs) {
                runCommand(newJob, &jobList, inBg);
            }
        } else {
            /* a job is running in the foreground; wait for it */
            i = 0;
            while (!jobList.fg->progs[i].pid) i++;

            waitpid(jobList.fg->progs[i].pid, &status, 0);

            jobList.fg->runningProgs--;
            jobList.fg->progs[i].pid = 0;
        
            if (!jobList.fg->runningProgs) {
                /* child exited */

                removeJob(&jobList, jobList.fg);
                jobList.fg = NULL;

                /* move the shell to the foreground */
                if (tcsetpgrp(0, getpid()))
                    perror("tcsetpgrp");
            }
        }
    }

    return 0;
}

