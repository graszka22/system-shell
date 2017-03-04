#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <dirent.h>
#include <signal.h>

#include "builtins.h"

int fexit(char*[]);
int echo(char*[]);
int cd(char*[]);
int ls(char*[]);
int fkill(char*[]);

builtin_pair builtins_table[]=
{
    {"exit",    &fexit},
    {"lecho",   &echo},
    {"lcd",     &cd},
    {"lkill",   &fkill},
    {"lls",     &ls},
    {NULL,NULL}
};

int fexit(char * argv[])
{
    exit(0);
}

int echo( char * argv[])
{
    int i =1;
    if (argv[i]) printf("%s", argv[i++]);
    while  (argv[i])
        printf(" %s", argv[i++]);

    printf("\n");
    fflush(stdout);
    return 0;
}

int cd(char * argv[])
{
    char* path = NULL;
    if(argv[1] == NULL)
        path = getenv("HOME");
    else if(argv[2] != NULL)
        return BUILTIN_ERROR;
    else
        path = argv[1];
    if(chdir(path) == -1)
        return BUILTIN_ERROR;
    return 0;
}

int ls(char * argv[])
{
    if(argv[1] != NULL)
        return BUILTIN_ERROR;
    struct dirent * dp;
    DIR * dir = opendir(".");
    if(dir == NULL)
        return BUILTIN_ERROR;
    while((dp = readdir (dir)) != NULL)
    {
        if(dp->d_name[0] == '.')
            continue;
        printf("%s\n", dp->d_name);
    }
    closedir(dir);
    fflush(stdout);
    return 0;
}

int fkill(char * argv[])
{
    if(argv[1] == NULL) return BUILTIN_ERROR;
    if(argv[2] == NULL)
    {
        int pid = atoi(argv[1]);
        if(pid == 0 && strcmp(argv[1], "0") != 0) return BUILTIN_ERROR;
        if(kill(pid, SIGTERM) == -1)
            return BUILTIN_ERROR;
    }
    else
    {
        if(argv[3] != NULL) return BUILTIN_ERROR;
        int pid = atoi(argv[2]);
        if(pid == 0 && strcmp(argv[2], "0") != 0) return BUILTIN_ERROR;
        if(argv[1][0] != '-') return BUILTIN_ERROR;
        int signal = atoi(&argv[1][1]);
        if(signal == 0 && strcmp(&argv[1][1], "0") != 0) return BUILTIN_ERROR;
        if(kill(pid, signal) == -1)
            return BUILTIN_ERROR;
    }
    return 0;
}

