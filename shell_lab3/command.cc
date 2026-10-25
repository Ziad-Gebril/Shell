/*
 * Simple Mini Shell - CC373 Lab 3
 * Based on the instructor's command.cc template.
 *
 * Keep command.h and tokenizer.h/tokenizer.cc from your lab.
 */

 #include <stdio.h>
 #include <stdlib.h>
 #include <unistd.h>
 #include <sys/types.h>
 #include <sys/wait.h>
 #include <string.h>
 #include <signal.h>
 #include <fcntl.h>
 #include <errno.h>
 #include <vector>
 #include <string>
 #include <iostream>
 #include <glob.h>
 
 #include "command.h"
 #include "tokenizer.h"
 
 static int logFile = -1;
 
 
 static void childFinished(int)
 {
     int savedErrno = errno;
     int status;
     pid_t pid;
 
     while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
     {
         if (logFile >= 0)
         {
             const char message[] = "A child process terminated\n";
             write(logFile, message, sizeof(message) - 1);
         }
     }
 
     errno = savedErrno;
 }
 

 static char *copyString(const char *text)
 {
     char *copy = (char *)malloc(strlen(text) + 1);
     if (copy == NULL)
     {
         perror("malloc");
         exit(1);
     }
 
     strcpy(copy, text);
     return copy;
 }
 
 static bool isWord(TokenType type)
 {
     return type == TOKEN_COMMAND || type == TOKEN_ARGUMENT;
 }
 

 static void addArgument(SimpleCommand *command, const std::string &word)
 {
     if (word.find('*') == std::string::npos &&
         word.find('?') == std::string::npos)
     {
         command->insertArgument(copyString(word.c_str()));
         return;
     }
 
     glob_t matches = {};
     int result = glob(word.c_str(), GLOB_NOCHECK, NULL, &matches);
 
     if (result == 0)
     {
         for (size_t i = 0; i < matches.gl_pathc; i++)
             command->insertArgument(copyString(matches.gl_pathv[i]));
     }
 
     globfree(&matches);
 }
 
 static void parseError(const char *message)
 {
     fprintf(stderr, "Error: %s\n", message);
 
     Command::_currentCommand.clear();
     delete Command::_currentSimpleCommand;
     Command::_currentSimpleCommand = NULL;
 }
 

 void parse(std::vector<Token> &tokens)
 {
     Command::_currentCommand.clear();
     Command::_currentSimpleCommand = new SimpleCommand();
 
     bool waitingForFile = false;
     bool pipeNeedsCommand = false;
     TokenType redirection = TOKEN_EOF;
 
     for (size_t i = 0; i < tokens.size(); i++)
     {
         Token &token = tokens[i];
 
         if (token.type == TOKEN_EOF)
             break;
 

         if (waitingForFile)
         {
             if (!isWord(token.type))
             {
                 parseError("redirection needs a filename");
                 return;
             }
 
             char *file = copyString(token.value.c_str());
 
             if (redirection == TOKEN_INPUT)
             {
                 free(Command::_currentCommand._inputFile);
                 Command::_currentCommand._inputFile = file;
             }
             else if (redirection == TOKEN_ERROR)
             {
                 free(Command::_currentCommand._errFile);
                 Command::_currentCommand._errFile = file;
             }
             else if (redirection == TOKEN_REDIRECT_AND_ERROR)
             {
                 free(Command::_currentCommand._outFile);
                 free(Command::_currentCommand._errFile);
 
                 Command::_currentCommand._outFile = copyString(token.value.c_str());
                 Command::_currentCommand._errFile = file;
                 Command::_currentCommand._out_error = 1;
                 Command::_currentCommand._append = 1;
             }
             else
             {
                 free(Command::_currentCommand._outFile);
                 Command::_currentCommand._outFile = file;
                 Command::_currentCommand._append =
                     (redirection == TOKEN_APPEND);
             }
 
             waitingForFile = false;
             continue;
         }
 
         if (isWord(token.type))
         {
             addArgument(Command::_currentSimpleCommand, token.value);
             pipeNeedsCommand = false;
         }
         else if (token.type == TOKEN_PIPE)
         {
             if (Command::_currentSimpleCommand->_numberOfArguments == 0)
             {
                 parseError("pipe is missing a command before it");
                 return;
             }
 
             Command::_currentCommand.insertSimpleCommand(
                 Command::_currentSimpleCommand);
 
             Command::_currentSimpleCommand = new SimpleCommand();
             pipeNeedsCommand = true;
         }
         else if (token.type == TOKEN_REDIRECT ||
                  token.type == TOKEN_APPEND ||
                  token.type == TOKEN_INPUT ||
                  token.type == TOKEN_ERROR ||
                  token.type == TOKEN_REDIRECT_AND_ERROR)
         {
             waitingForFile = true;
             redirection = token.type;
         }
         else if (token.type == TOKEN_BACKGROUND)
         {
             Command::_currentCommand._background = 1;
         }
         else
         {
             parseError("unrecognized token");
             return;
         }
     }
 
     if (waitingForFile)
     {
         parseError("redirection needs a filename");
         return;
     }
 
     if (pipeNeedsCommand)
     {
         parseError("pipe is missing a command after it");
         return;
     }
 
     if (Command::_currentSimpleCommand->_numberOfArguments > 0)
     {
         Command::_currentCommand.insertSimpleCommand(
             Command::_currentSimpleCommand);
         Command::_currentSimpleCommand = NULL;
     }
     else
     {
         delete Command::_currentSimpleCommand;
         Command::_currentSimpleCommand = NULL;
     }
 
     Command::_currentCommand.execute();
 }
 
 SimpleCommand::SimpleCommand()
 {
     _numberOfAvailableArguments = 5;
     _numberOfArguments = 0;
     _arguments = (char **)malloc(_numberOfAvailableArguments * sizeof(char *));
 
     if (_arguments == NULL)
     {
         perror("malloc");
         exit(1);
     }
 
     _arguments[0] = NULL;
 }
 
 void SimpleCommand::insertArgument(char *argument)
 {
     if (_numberOfArguments + 1 >= _numberOfAvailableArguments)
     {
         _numberOfAvailableArguments *= 2;
         char **newArguments = (char **)realloc(
             _arguments, _numberOfAvailableArguments * sizeof(char *));
 
         if (newArguments == NULL)
         {
             perror("realloc");
             exit(1);
         }
 
         _arguments = newArguments;
     }
 
     _arguments[_numberOfArguments] = argument;
     _numberOfArguments++;
     _arguments[_numberOfArguments] = NULL;
 }
 
 Command::Command()
 {
     _numberOfAvailableSimpleCommands = 2;
     _numberOfSimpleCommands = 0;
     _simpleCommands = (SimpleCommand **)malloc(
         _numberOfAvailableSimpleCommands * sizeof(SimpleCommand *));
 
     if (_simpleCommands == NULL)
     {
         perror("malloc");
         exit(1);
     }
 
     _outFile = NULL;
     _inputFile = NULL;
     _errFile = NULL;
     _out_error = 0;
     _background = 0;
     _append = 0;
 }
 
 void Command::insertSimpleCommand(SimpleCommand *simpleCommand)
 {
     if (_numberOfSimpleCommands == _numberOfAvailableSimpleCommands)
     {
         _numberOfAvailableSimpleCommands *= 2;
         SimpleCommand **newCommands = (SimpleCommand **)realloc(
             _simpleCommands,
             _numberOfAvailableSimpleCommands * sizeof(SimpleCommand *));
 
         if (newCommands == NULL)
         {
             perror("realloc");
             exit(1);
         }
 
         _simpleCommands = newCommands;
     }
 
     _simpleCommands[_numberOfSimpleCommands] = simpleCommand;
     _numberOfSimpleCommands++;
 }
 
 void Command::clear()
 {
     for (int i = 0; i < _numberOfSimpleCommands; i++)
     {
         SimpleCommand *command = _simpleCommands[i];
 
         for (int j = 0; j < command->_numberOfArguments; j++)
             free(command->_arguments[j]);
 
         free(command->_arguments);
         delete command;
     }
 
     free(_outFile);
     free(_inputFile);
     free(_errFile);
 
     _numberOfSimpleCommands = 0;
     _outFile = NULL;
     _inputFile = NULL;
     _errFile = NULL;
     _out_error = 0;
     _background = 0;
     _append = 0;
 }
 
 void Command::print()
 {
     printf("\nCOMMAND TABLE\n");
 
     for (int i = 0; i < _numberOfSimpleCommands; i++)
     {
         printf("Command %d: ", i + 1);
 
         for (int j = 0; j < _simpleCommands[i]->_numberOfArguments; j++)
             printf("\"%s\" ", _simpleCommands[i]->_arguments[j]);
 
         printf("\n");
     }
 
     printf("Output: %s | Input: %s | Error: %s | Background: %s\n\n",
            _outFile ? _outFile : "default",
            _inputFile ? _inputFile : "default",
            _errFile ? _errFile : "default",
            _background ? "yes" : "no");
 }
 
 static int openFile(const char *filename, int flags)
 {
     int fd = open(filename, flags, 0666);
 
     if (fd < 0)
         perror(filename);
 
     return fd;
 }
 

 static bool runBuiltIn(Command *command)
 {
     if (command->_numberOfSimpleCommands != 1)
         return false;
 
     char **args = command->_simpleCommands[0]->_arguments;
 
     if (args[0] == NULL)
         return false;
 
     if (strcmp(args[0], "exit") == 0)
     {
         printf("Good bye!!\n");
         command->clear();
         exit(0);
     }
 
     if (strcmp(args[0], "cd") == 0)
     {
         const char *directory = args[1];
 
         if (directory == NULL)
             directory = getenv("HOME");
 
         if (directory == NULL)
             directory = "/";
 
         if (chdir(directory) < 0)
             perror("cd");
 
         return true;
     }
 
     return false;
 }
 
 void Command::execute()
 {
     if (_numberOfSimpleCommands == 0)
     {
         clear();
         return;
     }
 
     if (runBuiltIn(this))
     {
         clear();
         return;
     }
 
     print();
     fflush(stdout);
 
     int count = _numberOfSimpleCommands;

     std::vector<std::vector<int> > pipes;
     for (int i = 0; i < count - 1; i++)
     {
         std::vector<int> onePipe(2);
 
         if (pipe(&onePipe[0]) < 0)
         {
             perror("pipe");
 
             for (size_t j = 0; j < pipes.size(); j++)
             {
                 close(pipes[j][0]);
                 close(pipes[j][1]);
             }
 
             clear();
             return;
         }
 
         pipes.push_back(onePipe);
     }
 
     std::vector<pid_t> processIDs;
 
     for (int i = 0; i < count; i++)
     {
         pid_t pid = fork();
 
         if (pid < 0)
         {
             perror("fork");
             break;
         }
 
         if (pid == 0)
         {

             signal(SIGINT, _background ? SIG_IGN : SIG_DFL);
             signal(SIGCHLD, SIG_DFL);
 

             if (i > 0)
                 dup2(pipes[i - 1][0], STDIN_FILENO);
 

             if (i < count - 1)
                 dup2(pipes[i][1], STDOUT_FILENO);
 

             for (size_t j = 0; j < pipes.size(); j++)
             {
                 close(pipes[j][0]);
                 close(pipes[j][1]);
             }
 

             if (i == 0 && _inputFile != NULL)
             {
                 int fd = openFile(_inputFile, O_RDONLY);
 
                 if (fd < 0)
                     _exit(1);
 
                 dup2(fd, STDIN_FILENO);
                 close(fd);
             }
 
             int outputFlags = O_WRONLY | O_CREAT |
                               (_append ? O_APPEND : O_TRUNC);
 

             if (i == count - 1 && _outFile != NULL)
             {
                 int fd = openFile(_outFile, outputFlags);
 
                 if (fd < 0)
                     _exit(1);
 
                 dup2(fd, STDOUT_FILENO);
 
                 if (_out_error)
                     dup2(fd, STDERR_FILENO);
 
                 close(fd);
             }
 

             if (_errFile != NULL && !_out_error)
             {
                 int fd = openFile(_errFile, outputFlags);
 
                 if (fd < 0)
                     _exit(1);
 
                 dup2(fd, STDERR_FILENO);
                 close(fd);
             }
 
             execvp(_simpleCommands[i]->_arguments[0],
                    _simpleCommands[i]->_arguments);
 
             perror(_simpleCommands[i]->_arguments[0]);
             _exit(1);
         }
 
         processIDs.push_back(pid);
     }
 

     for (size_t i = 0; i < pipes.size(); i++)
     {
         close(pipes[i][0]);
         close(pipes[i][1]);
     }
 

     if (!_background)
     {
         for (size_t i = 0; i < processIDs.size(); i++)
             waitpid(processIDs[i], NULL, 0);
     }
 
     clear();
 }
 
 void Command::prompt()
 {

     signal(SIGINT, SIG_IGN);
 
     logFile = open("shell.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
 
     struct sigaction action = {};
     action.sa_handler = childFinished;
     sigemptyset(&action.sa_mask);
     action.sa_flags = SA_RESTART | SA_NOCLDSTOP;
     sigaction(SIGCHLD, &action, NULL);
 
     std::string input;
 
     while (true)
     {
         std::cout << "myshell> " << std::flush;
 
         if (!std::getline(std::cin, input))
         {
             std::cout << "\n";
             break;
         }
 
         std::vector<Token> tokens = tokenize(input);
         parse(tokens);
     }
 
     if (logFile >= 0)
         close(logFile);
 }
 
 Command Command::_currentCommand;
 SimpleCommand *Command::_currentSimpleCommand = NULL;
 
 int main()
 {
     Command::_currentCommand.prompt();
     return 0;
 }
 