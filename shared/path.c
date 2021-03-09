#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "path.h"

#define BUFFER_SIZE 4096

static char *_selfexe = NULL;
static char *_selfdir = NULL;

static int pathinit(void)
{
    char buf[BUFFER_SIZE];
    int readlen;

    readlen = readlink("/proc/self/exe", buf, BUFFER_SIZE);
    if (readlen <= 0)
    {
        printf("Failed to read program path\n");
        return -1;
    }

    _selfexe = malloc(strlen(buf) + 1);
    strcpy(_selfexe, buf);

    if ((_selfdir = pathup(_selfexe)) == NULL)
    {
        printf("Failed to find program directory\n");
        return -1;
    }

    return 0;
}

char *pathselfexe(void)
{
    if (!_selfexe)
        pathinit();

    return _selfexe;
}

char *pathselfdir(void)
{
    if (!_selfdir)
        pathinit();

    return _selfdir;
}

char *pathmake(char *file)
{
    if (!_selfdir)
        pathinit();

    if (!_selfdir)
        return (char *)NULL;

    char buf[BUFFER_SIZE];
    sprintf(buf, "%s/%s", _selfdir, file);

    char *result = malloc(strlen(buf) + 1);
    strcpy(result, buf);

    return result;
}

char *pathup(char *path)
{
    char *lastslash;
    if ((lastslash = strrchr(path, '/')) == NULL)
    {
        printf("Failed to find parent of: %s\n", path);
        return (char *)NULL;
    }

    int len = lastslash - path;
    if (len == 0)
        return (char *)"/";

    char *result = malloc(len + 1);
    memcpy(result, path, len);
    result[len] = '\0';

    return result;
}

char *pathfile(char *path)
{
    char *lastslash;
    if ((lastslash = strrchr(path, '/')) == NULL)
    {
        printf("Failed to find file of: %s\n", path);
        return (char *)NULL;
    }

    int len = (path + strlen(path)) - lastslash - 1;
    if (len == 0)
        return (char *)"";

    char *result = malloc(len + 1);
    strcpy(result, lastslash + 1);

    return result;
}

char *strtokplus(char *s, char c, char **r)
{
    char *p = s ? s : *r;
    if (!*p)
        return 0;
    *r = strchr(p, c);
    if (*r)
        *(*r)++ = 0;
    else
        *r = p+strlen(p);
    return p;
}