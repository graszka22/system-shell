#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include "config.h"
#include "siparse.h"
#include "utils.h"
#include "builtins.h"

#define EXEC_SUCCESS 0
#define MAX_FOREGROUND 1000
#define MAX_BACKGROUND 1000

/** how many processes runs in foreground **/
volatile int counter = 0;
/** pids of processes running in foreground **/
volatile pid_t foreground[MAX_FOREGROUND];
/** pids of processes ended from background **/
volatile pid_t background[MAX_BACKGROUND];
/** their exit status **/
volatile int background_status[MAX_BACKGROUND];
/** their counter **/
volatile int queue_ct = 0;

void sigchild_handler(int sig_nb)
{
    pid_t child;
    int status = 0;
    while ((child = waitpid(-1, &status, WNOHANG)) > 0)
    {
        int fg = 0;
        /** check if process is from the foreground **/
        /** and remove it from the list **/
        for(int i = 0; i < counter; ++i)
		{
			if(child == foreground[i])
			{
				foreground[i] = foreground[--counter];
				fg = 1;
				break;
			}
		}
        
        /** if process is from the background, add it to the list of 
         * ended background processes **/
        if (!fg && queue_ct < MAX_BACKGROUND)
        {
            background[queue_ct] = child;
            background_status[queue_ct] = status;
            queue_ct++;
        }
    }
}

void set_handlers()
{
    struct sigaction act;
    act.sa_handler = sigchild_handler;
    act.sa_flags = 0;
    sigemptyset(&act.sa_mask);
    sigaction(SIGCHLD, &act, NULL);
    signal(SIGINT, SIG_IGN);
}

void set_child_pipes(int *fildes1, int *fildes2)
{
    if (fildes1[1] != -1)
        close(fildes1[1]);
    if (fildes2[0] != -1)
        close(fildes2[0]);
    if (fildes1[0] != -1)
    {
        dup2(fildes1[0], 0);
        close(fildes1[0]);
    }
    if (fildes2[1] != -1)
    {
        dup2(fildes2[1], 1);
        close(fildes2[1]);
    }
}

void set_redirections(command * com)
{
    for (redirection ** r = com->redirs; (*r) != NULL; ++r)
    {
        if (IS_RIN((*r)->flags))
        {
            close(0);
            if (open((*r)->filename, O_RDONLY) != 0)
            {
                if (errno == EACCES)
                    fprintf(stderr, "%s: permission denied\n", (*r)->filename);
                else if (errno == ENOENT)
                    fprintf(stderr, "%s: no such file or directory\n", (*r)->filename);
                exit(1);
            }
        }
        if (IS_ROUT((*r)->flags))
        {
            close(1);
            if (open((*r)->filename, O_RDWR | O_TRUNC | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH) != 1)
            {
                if (errno == EACCES)
                    fprintf(stderr, "%s: permission denied\n", (*r)->filename);
                exit(1);
            }
        }
        if (IS_RAPPEND((*r)->flags))
        {
            close(1);
            if (open((*r)->filename, O_RDWR | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH) != 1)
            {
                if (errno == EACCES)
                    fprintf(stderr, "%s: permission denied\n", (*r)->filename);
                exit(1);
            }
        }
    }
}

int handle_builtin(command * com)
{
    if (com->argv[0] == NULL)
        return 1;
    for (builtin_pair * b = builtins_table; b->name != NULL; ++b)
    {
        if (strcmp(b->name, com->argv[0]) == 0)
        {
            if (b->fun(com->argv) == BUILTIN_ERROR)
            {
                fprintf(stderr, "Builtin %s error.\n", b->name);
                return 1;
            }
            return 2;
        }
    }
    return 0;
}
/** executes command "com" with pipes fd1, fd2 **/
int run_command(command * com, int *fd1, int *fd2, int background)
{
    if (com->argv[0] == NULL)
        return 0;
    pid_t pid = fork();
    if (pid == 0)
    {
        /** set default SIGINT handler - the child inherits SIG_IGN handler **/
        signal(SIGINT, SIG_DFL);
        if (background)
            setsid();
        set_child_pipes(fd1, fd2);
        set_redirections(com);
        int e = execvp(com->argv[0], com->argv);
        if (errno == ENOENT)
        {
            fprintf(stderr, "%s: no such file or directory\n", com->argv[0]);
            exit(EXEC_FAILURE);
        }
        else if (errno == EACCES)
        {
            fprintf(stderr, "%s: permission denied\n", com->argv[0]);
            exit(EXEC_FAILURE);
        }
        else if (e < 0)
        {
            fprintf(stderr, "%s: exec error\n", com->argv[0]);
            exit(EXEC_FAILURE);
        }
        exit(EXEC_SUCCESS);
    }
    if (pid < 0)
        exit(1);
    if (background)
        return 0;
    /** add foreground process to the list of running processes **/ 
	foreground[counter++] = pid;
    return 0;
}

void wait_for_children()
{
    sigset_t sigset;
    sigemptyset(&sigset);
    while (counter) {
        sigsuspend(&sigset);
	}
	sigemptyset(&sigset);
    sigaddset(&sigset, SIGCHLD);
    sigprocmask(SIG_UNBLOCK, &sigset, NULL);
}

/** executes the line beginning in *buf **/
void run_parser(char *buf)
{
    if (strlen(buf) == 0)
        return;
    line *ln;
    command *com;
    ln = parseline(buf);
    int fildes1[2] = { -1, -1 }, fildes2[2] = { -1, -1};
    int line_background = (ln->flags) & LINBACKGROUND;
    for (pipeline * pl = ln->pipelines; (*pl) != NULL; ++pl)
    {
        int comm_ct = 0;
        int empty_comm = 0;
        for (command ** com = *pl; (*com) != NULL; ++com)
        {
            ++comm_ct;
            if((*com)->argv[0] == NULL) 
                ++empty_comm;
        }
        if(empty_comm && comm_ct > 1)
        {
            fprintf(stderr, "%s\n", SYNTAX_ERROR_STR);
            return;
        }
    }
    for (pipeline * pl = ln->pipelines; (*pl) != NULL; ++pl)
    {
        fildes1[0] = fildes1[1] = -1;
        fildes2[0] = fildes2[1] = -1;
        /** if the pipeline is empty **/
        if ((*pl)[0] == NULL)
            continue;
        /** if it is foreground process then block sigchild signal **/
        if (!line_background)
        {
            sigset_t sigset;
            sigemptyset(&sigset);
            sigaddset(&sigset, SIGCHLD);
            sigprocmask(SIG_BLOCK, &sigset, NULL);
        }
        /** if there is just one command **/
        if ((*pl)[1] == NULL)
        {
            if (handle_builtin((*pl)[0]))
                continue;
            run_command((*pl)[0], fildes1, fildes2, line_background);
            if (!line_background)
                wait_for_children();
            continue;
        }
        if (pipe(fildes2) < 0)
            exit(1);
        for (command ** com = *pl; (*com) != NULL; ++com)
        {
            run_command(*com, fildes1, fildes2, line_background);
            if (fildes1[0] != -1)
                close(fildes1[0]);
            if (fildes2[1] != -1)
                close(fildes2[1]);
            fildes1[0] = fildes2[1] = -1;

            /** if this is the last command **/
            if (*(com + 1) == NULL)
                break;
            /** if there is just one command left **/
            if (*(com + 2) == NULL)
            {
                fildes1[0] = fildes2[0];
                fildes1[1] = fildes2[1];
                if (fildes2[1] != -1)
                    close(fildes2[1]);
                fildes2[0] = fildes2[1] = -1;
                fildes1[1] = -1;
            }
            else
            {
                fildes1[0] = fildes2[0];
                fildes1[1] = fildes2[1];
                if (pipe(fildes2) < 0)
                    exit(1);
            }
        }
        if (fildes1[0] != -1)
            close(fildes1[0]);
        if (fildes1[1] != -1)
            close(fildes1[1]);
        if (fildes2[0] != -1)
            close(fildes2[0]);
        if (fildes2[1] != -1)
            close(fildes2[1]);
        if (!line_background)
            wait_for_children();
    }
}

void write_prompt()
{
    sigset_t sigset;
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGCHLD);
    sigprocmask(SIG_BLOCK, &sigset, NULL);
    for (int i = 0; i < queue_ct; ++i)
    {
        if (WIFSIGNALED(background_status[i]))
            printf("Background process %d terminated. (killed by signal %d)\n",
                   background[i], WTERMSIG(background_status[i]));
        else
            printf("Background process %d terminated. (exited with status %d)\n", background[i], background_status[i]);
    }
    queue_ct = 0;
    write(1, PROMPT_STR, strlen(PROMPT_STR));
    sigprocmask(SIG_UNBLOCK, &sigset, NULL);
}

int main(int argc, char *argv[])
{
    set_handlers();
    for (int i = 0; i < MAX_FOREGROUND; ++i)
        foreground[i] = 0;
    char buf[MAX_LINE_LENGTH + 1];
    int cur = 0, beg = 0;
    struct stat s;
    fstat(0, &s);
    char console = 0;
    if (S_ISCHR(s.st_mode))
        console = 1;

    while (1)
    {
        if (console)
            write_prompt();
        int r;
        while (1)
        {
            r = read(0, buf + cur, MAX_LINE_LENGTH - cur);
            if (r < 0 && errno == EINTR)
                continue;
            if (r < 0)
                exit(1);
            break;
        }
        if (r == 0)
            break;
        while (1)
        {
            int end = -1;
            for (int i = 0; i < MAX_LINE_LENGTH && i < cur + r; ++i)
            {
                if (buf[i] == '\n')
                {
                    buf[i] = 0;
                    end = i;
                    break;
                }
            }
            if (end == -1)
            {
                if (cur + r == MAX_LINE_LENGTH && beg == 0)
                {
                    while (1)
                    {
                        r = read(0, buf, MAX_LINE_LENGTH);
                        beg = -1;
                        for (int i = 0; i < r; ++i)
                        {
                            if (buf[i] == '\n')
                            {
                                beg = i + 1;
                                cur = r;
                                break;
                            }
                        }
                        if (beg != -1)
                            break;
                    }
                    for (int i = beg; i < MAX_LINE_LENGTH; ++i)
                        buf[i - beg] = buf[i];
                    cur -= beg;
                    beg = 0;
                    fprintf(stderr, "%s\n", SYNTAX_ERROR_STR);
                    break;
                }
                else if (cur + r == MAX_LINE_LENGTH)
                {
                    for (int i = beg; i < MAX_LINE_LENGTH; ++i)
                        buf[i - beg] = buf[i];
                    cur -= beg;
                    cur += r;
                    beg = 0;
                    break;
                }
                else
                {
                    cur += r;
                    break;
                }
            }
            else
            {
                run_parser(buf + beg);
                r -= end - cur + 1;
                beg = cur = end + 1;
            }
        }
    }
}
