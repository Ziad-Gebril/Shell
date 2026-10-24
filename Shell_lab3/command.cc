
/*
 *
 *
 * Template file.
 * You will need to add more code here to execute the command table.
 *
 * NOTE: You are responsible for fixing any bugs this code may have!
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <time.h>
#include <glob.h>
#include <errno.h>
#include <vector>
#include <string>
#include <cctype>
#include <iostream>
#include "command.h"
#include "tokenizer.h"

static int g_logFd = -1;
static bool g_handlersInstalled = false;

static char *dupstr(const char *s)
{
    if (s == NULL)
    {
        return NULL;
    }
    char *copy = (char *)malloc(strlen(s) + 1);
    if (copy == NULL)
    {
        perror("malloc");
        exit(1);
    }
    strcpy(copy, s);
    return copy;
}

static void logChildExit(pid_t pid)
{
    if (g_logFd < 0)
    {
        return;
    }

    char buffer[128];
    int length = snprintf(buffer, sizeof(buffer), "Child %d terminated\n", (int)pid);
    if (length > 0)
    {
        ssize_t ignored = write(g_logFd, buffer, (size_t)length);
        (void)ignored;
    }
}

static void sigchldHandler(int /*signum*/)
{
    int savedErrno = errno;
    int status = 0;
    pid_t pid;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
    {
        logChildExit(pid);
    }

    errno = savedErrno;
}

static void installHandlers()
{
    if (g_handlersInstalled)
    {
        return;
    }

    signal(SIGINT, SIG_IGN);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigchldHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa, NULL);

    g_logFd = open("shell.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
    g_handlersInstalled = true;
}

static bool hasWildcard(const char *argument)
{
    return strchr(argument, '*') != NULL || strchr(argument, '?') != NULL;
}

static void insertExpandedArgument(SimpleCommand *simpleCommand, const std::string &argument)
{
    if (!hasWildcard(argument.c_str()))
    {
        simpleCommand->insertArgument(dupstr(argument.c_str()));
        return;
    }

    glob_t results;
    memset(&results, 0, sizeof(results));
    int status = glob(argument.c_str(), GLOB_NOCHECK, NULL, &results);
    if (status == 0)
    {
        for (size_t i = 0; i < results.gl_pathc; i++)
        {
            simpleCommand->insertArgument(dupstr(results.gl_pathv[i]));
        }
    }
    else
    {
        simpleCommand->insertArgument(dupstr(argument.c_str()));
    }
    globfree(&results);
}

static bool isWordToken(TokenType type)
{
    return type == TOKEN_COMMAND || type == TOKEN_ARGUMENT;
}

static void parseError(const char *message)
{
    fprintf(stderr, "%s\n", message);
    Command::_currentCommand.clear();
    delete Command::_currentSimpleCommand;
    Command::_currentSimpleCommand = NULL;
}

void parse(std::vector<Token> &tokens)
{
    Command::_currentCommand.clear();
    Command::_currentSimpleCommand = new SimpleCommand();

    bool expectFile = false;
    TokenType redirectType = TOKEN_EOF;
    bool sawPipe = false;

    for (size_t i = 0; i < tokens.size(); i++)
    {
        Token &token = tokens[i];

        if (expectFile)
        {
            if (!isWordToken(token.type))
            {
                parseError("Invalid redirection: missing file name");
                return;
            }

            if (redirectType == TOKEN_REDIRECT || redirectType == TOKEN_APPEND)
            {
                if (Command::_currentCommand._outFile != 0)
                {
                    parseError("Ambiguous output redirect");
                    return;
                }
                Command::_currentCommand._outFile = dupstr(token.value.c_str());
                Command::_currentCommand._append = (redirectType == TOKEN_APPEND);
            }
            else if (redirectType == TOKEN_INPUT)
            {
                if (Command::_currentCommand._inputFile != 0)
                {
                    parseError("Ambiguous input redirect");
                    return;
                }
                Command::_currentCommand._inputFile = dupstr(token.value.c_str());
            }
            else if (redirectType == TOKEN_ERROR)
            {
                if (Command::_currentCommand._errFile != 0)
                {
                    parseError("Ambiguous error redirect");
                    return;
                }
                Command::_currentCommand._errFile = dupstr(token.value.c_str());
            }
            else if (redirectType == TOKEN_REDIRECT_AND_ERROR)
            {
                if (Command::_currentCommand._outFile != 0 || Command::_currentCommand._errFile != 0)
                {
                    parseError("Ambiguous output redirect");
                    return;
                }
                Command::_currentCommand._outFile = dupstr(token.value.c_str());
                Command::_currentCommand._errFile = dupstr(token.value.c_str());
                Command::_currentCommand._out_error = 1;
                Command::_currentCommand._append = 1;
            }

            expectFile = false;
            continue;
        }

        if (token.type == TOKEN_EOF)
        {
            break;
        }

        if (token.type == TOKEN_COMMAND || token.type == TOKEN_ARGUMENT)
        {
            insertExpandedArgument(Command::_currentSimpleCommand, token.value);
            sawPipe = false;
        }
        else if (token.type == TOKEN_PIPE)
        {
            if (Command::_currentSimpleCommand->_numberOfArguments == 0)
            {
                parseError("Invalid pipe usage");
                return;
            }
            Command::_currentCommand.insertSimpleCommand(Command::_currentSimpleCommand);
            Command::_currentSimpleCommand = new SimpleCommand();
            sawPipe = true;
        }
        else if (token.type == TOKEN_REDIRECT ||
                 token.type == TOKEN_APPEND ||
                 token.type == TOKEN_INPUT ||
                 token.type == TOKEN_ERROR ||
                 token.type == TOKEN_REDIRECT_AND_ERROR)
        {
            expectFile = true;
            redirectType = token.type;
        }
        else if (token.type == TOKEN_BACKGROUND)
        {
            Command::_currentCommand._background = 1;
        }
        else
        {
            parseError("Invalid input");
            return;
        }
    }

    if (expectFile)
    {
        parseError("Invalid redirection: missing file name");
        return;
    }

    if (sawPipe || Command::_currentSimpleCommand->_numberOfArguments == 0)
    {
        if (sawPipe)
        {
            parseError("Invalid pipe usage");
            return;
        }
        delete Command::_currentSimpleCommand;
        Command::_currentSimpleCommand = NULL;
        if (Command::_currentCommand._numberOfSimpleCommands == 0)
        {
            return;
        }
    }
    else
    {
        Command::_currentCommand.insertSimpleCommand(Command::_currentSimpleCommand);
        Command::_currentSimpleCommand = NULL;
    }

    Command::_currentCommand.execute();
}

SimpleCommand::SimpleCommand()
{
    _numberOfAvailableArguments = 5;
    _numberOfArguments = 0;
    _arguments = (char **)malloc(_numberOfAvailableArguments * sizeof(char *));
}

void SimpleCommand::insertArgument(char *argument)
{
    if (_numberOfAvailableArguments == _numberOfArguments + 1)
    {
        _numberOfAvailableArguments *= 2;
        _arguments = (char **)realloc(_arguments,
                                      _numberOfAvailableArguments * sizeof(char *));
    }

    _arguments[_numberOfArguments] = argument;
    _arguments[_numberOfArguments + 1] = NULL;

    _numberOfArguments++;
}

Command::Command()
{
    _numberOfAvailableSimpleCommands = 1;
    _numberOfSimpleCommands = 0;
    _simpleCommands = (SimpleCommand **)
        malloc(_numberOfAvailableSimpleCommands * sizeof(SimpleCommand *));

    _outFile = 0;
    _inputFile = 0;
    _errFile = 0;
    _background = 0;
    _out_error = 0;
    _append = 0;
}

void Command::insertSimpleCommand(SimpleCommand *simpleCommand)
{
    if (_numberOfAvailableSimpleCommands == _numberOfSimpleCommands)
    {
        _numberOfAvailableSimpleCommands *= 2;
        _simpleCommands = (SimpleCommand **)realloc(_simpleCommands,
                                                    _numberOfAvailableSimpleCommands * sizeof(SimpleCommand *));
    }

    _simpleCommands[_numberOfSimpleCommands] = simpleCommand;
    _numberOfSimpleCommands++;
}

void Command::clear()
{
    for (int i = 0; i < _numberOfSimpleCommands; i++)
    {
        for (int j = 0; j < _simpleCommands[i]->_numberOfArguments; j++)
        {
            free(_simpleCommands[i]->_arguments[j]);
        }

        free(_simpleCommands[i]->_arguments);
        free(_simpleCommands[i]);
    }

    if (_outFile)
    {
        free(_outFile);
    }

    if (_inputFile)
    {
        free(_inputFile);
    }

    if (_errFile)
    {
        free(_errFile);
    }

    _numberOfSimpleCommands = 0;
    _outFile = 0;
    _inputFile = 0;
    _errFile = 0;
    _background = 0;
    _out_error = 0;
    _append = 0;
}

void Command::print()
{
    printf("\n\n");
    printf("              COMMAND TABLE                \n");
    printf("\n");
    printf("  #   Simple Commands\n");
    printf("  --- ----------------------------------------------------------\n");

    for (int i = 0; i < _numberOfSimpleCommands; i++)
    {
        printf("  %-3d ", i);
        for (int j = 0; j < _simpleCommands[i]->_numberOfArguments; j++)
        {
            printf("\"%s\" \t", _simpleCommands[i]->_arguments[j]);
        }
        printf("\n");
    }

    printf("\n\n");
    printf("  Output       Input        Error        Err&Out       Background\n");
    printf("  ------------ ------------ ------------ ------------ ------------\n");
    printf("  %-12s %-12s %-12s %-12s %-12s\n", _outFile ? _outFile : "default",
           _inputFile ? _inputFile : "default", _errFile ? _errFile : "default", _out_error == 1 ? _errFile : "default", _background ? "YES" : "NO");
    printf("\n\n");
}

static int openRedirectFile(const char *path, int flags)
{
    int fd = open(path, flags, 0664);
    if (fd < 0)
    {
        perror(path);
    }
    return fd;
}

static void setupRedirection(Command *command, int index, int commandCount)
{
    if (index == 0 && command->_inputFile)
    {
        int fd = openRedirectFile(command->_inputFile, O_RDONLY);
        if (fd < 0)
        {
            _exit(1);
        }
        dup2(fd, 0);
        close(fd);
    }

    int writeFlags = O_WRONLY | O_CREAT | (command->_append ? O_APPEND : O_TRUNC);

    if (index == commandCount - 1 && command->_outFile)
    {
        int fd = openRedirectFile(command->_outFile, writeFlags);
        if (fd < 0)
        {
            _exit(1);
        }
        dup2(fd, 1);
        if (command->_out_error)
        {
            dup2(fd, 2);
        }
        close(fd);
    }

    if (command->_errFile && !command->_out_error)
    {
        int fd = openRedirectFile(command->_errFile, writeFlags);
        if (fd < 0)
        {
            _exit(1);
        }
        dup2(fd, 2);
        close(fd);
    }
}

static bool handleBuiltin(Command *command)
{
    if (command->_numberOfSimpleCommands != 1)
    {
        return false;
    }

    char **args = command->_simpleCommands[0]->_arguments;
    if (args == NULL || args[0] == NULL)
    {
        return false;
    }

    if (strcmp(args[0], "exit") == 0)
    {
        printf("Good bye!!\n");
        command->clear();
        exit(0);
    }

    if (strcmp(args[0], "cd") == 0)
    {
        const char *path = args[1];
        if (path == NULL)
        {
            path = getenv("HOME");
            if (path == NULL)
            {
                path = "/";
            }
        }
        if (chdir(path) != 0)
        {
            perror("cd");
        }
        return true;
    }

    return false;
}

void Command::execute()
{
    print();
    fflush(stdout);

    if (_numberOfSimpleCommands == 0)
    {
        clear();
        return;
    }

    if (handleBuiltin(this))
    {
        clear();
        return;
    }

    int commandCount = _numberOfSimpleCommands;
    int pipeCount = commandCount - 1;
    int pipefds[pipeCount > 0 ? pipeCount : 1][2];

    for (int i = 0; i < pipeCount; i++)
    {
        if (pipe(pipefds[i]) < 0)
        {
            perror("pipe");
            clear();
            return;
        }
    }

    sigset_t blockSet;
    sigset_t oldSet;
    sigemptyset(&blockSet);
    sigaddset(&blockSet, SIGCHLD);
    sigprocmask(SIG_BLOCK, &blockSet, &oldSet);

    pid_t pids[commandCount];
    bool launchFailed = false;

    for (int i = 0; i < commandCount; i++)
    {
        pid_t pid = fork();
        if (pid < 0)
        {
            perror("fork");
            launchFailed = true;
            break;
        }

        if (pid == 0)
        {
            sigprocmask(SIG_SETMASK, &oldSet, NULL);
            signal(SIGINT, _background ? SIG_IGN : SIG_DFL);
            signal(SIGCHLD, SIG_DFL);

            if (i > 0)
            {
                dup2(pipefds[i - 1][0], 0);
            }
            if (i < commandCount - 1)
            {
                dup2(pipefds[i][1], 1);
            }

            for (int p = 0; p < pipeCount; p++)
            {
                close(pipefds[p][0]);
                close(pipefds[p][1]);
            }

            setupRedirection(this, i, commandCount);

            execvp(_simpleCommands[i]->_arguments[0], _simpleCommands[i]->_arguments);
            perror(_simpleCommands[i]->_arguments[0]);
            _exit(1);
        }

        pids[i] = pid;
    }

    for (int p = 0; p < pipeCount; p++)
    {
        close(pipefds[p][0]);
        close(pipefds[p][1]);
    }

    if (!launchFailed && !_background)
    {
        for (int i = 0; i < commandCount; i++)
        {
            int status = 0;
            pid_t waited = waitpid(pids[i], &status, 0);
            if (waited > 0)
            {
                logChildExit(waited);
            }
        }
    }

    sigprocmask(SIG_SETMASK, &oldSet, NULL);

    clear();
}

void Command::prompt()
{
    installHandlers();

    std::string input;
    while (true)
    {
        printf("myshell> ");
        fflush(stdout);

        if (!std::getline(std::cin, input))
        {
            printf("\n");
            break;
        }

        std::vector<Token> tokens = tokenize(input);
        parse(tokens);
    }
}

Command Command::_currentCommand;
SimpleCommand *Command::_currentSimpleCommand;

int main()
{
    Command::_currentCommand.prompt();
    return 0;
}
