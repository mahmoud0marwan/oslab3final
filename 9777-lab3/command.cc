
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
#include <errno.h>
#include <glob.h>
#include <wordexp.h>
#include <vector>
#include <string>
#include <cctype>
#include <iostream>
#include <sstream>
#include "command.h"
#include "tokenizer.h"


void childHandler(int sig)
{
    int savedErrno = errno;

    while (waitpid(-1, NULL, WNOHANG) > 0)
    {
        const char *msg = "Child process terminated\n";
        int fd = open("myshell.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd >= 0)
        {
            write(fd, msg, strlen(msg));
            close(fd);
        }
    }

    errno = savedErrno;
}

void parse(std::vector<Token> &tokens)
{
    if (tokens.size() <= 1)
        return;

    std::vector<Token> words;
    for (size_t i = 0; i < tokens.size(); i++)
    {
        Token t = tokens[i];

        if ((t.type == TOKEN_COMMAND || t.type == TOKEN_ARGUMENT) &&
            (t.value[0] == '"' || t.value[0] == '\''))
        {
            char q = t.value[0];
            std::string s = t.value.substr(1);

            while (s.empty() || s[s.size() - 1] != q)
            {
                i++;
                if (i >= tokens.size() || tokens[i].type == TOKEN_EOF)
                {
                    fprintf(stderr, "Error: missing closing quote\n");
                    return;
                }
                s += " " + tokens[i].value;
            }

            s.erase(s.size() - 1);
            t.value = s;
        }

        words.push_back(t);
    }

    Command currentCommand;
    SimpleCommand *currentSimpleCommand = new SimpleCommand();
    bool error = false;

    for (size_t i = 0; i < words.size() && !error; i++)
    {
        TokenType type = words[i].type;

        if (type == TOKEN_EOF)
        {
            break;
        }
            else if (type == TOKEN_COMMAND || type == TOKEN_ARGUMENT)
        {
            const std::string &w = words[i].value;
            bool expanded = false;

            if (w.find('*') != std::string::npos || w.find('?') != std::string::npos)
            {
                glob_t g;
                if (glob(w.c_str(), 0, NULL, &g) == 0)
                {
                    for (size_t k = 0; k < g.gl_pathc; k++)
                        currentSimpleCommand->insertArgument(strdup(g.gl_pathv[k]));
                    expanded = true;
                }
                globfree(&g);
            }

            if (!expanded)
                currentSimpleCommand->insertArgument(strdup(w.c_str()));
        }
        else if (type == TOKEN_PIPE)
        {
            if (currentSimpleCommand->_numberOfArguments == 0)
            {
                fprintf(stderr, "Error: missing command before pipe\n");
                error = true;
            }
            else
            {
                currentCommand.insertSimpleCommand(currentSimpleCommand);
                currentSimpleCommand = new SimpleCommand();
            }
        }
        else if (type == TOKEN_BACKGROUND)
        {
            if (words[i + 1].type != TOKEN_EOF)
            {
                fprintf(stderr, "Error: & must be at the end of the command\n");
                error = true;
            }
            else
            {
                currentCommand._background = 1;
            }
        }
        else
        {
            if (words[i + 1].type != TOKEN_COMMAND &&
                words[i + 1].type != TOKEN_ARGUMENT)
            {
                fprintf(stderr, "Error: missing file name after redirection\n");
                error = true;
            }
            else
            {
                char *file = strdup(words[i + 1].value.c_str());

                if (type == TOKEN_REDIRECT)
                {
                    free(currentCommand._outFile);
                    currentCommand._outFile = file;
                    currentCommand._append = 0;
                }
                else if (type == TOKEN_APPEND)
                {
                    free(currentCommand._outFile);
                    currentCommand._outFile = file;
                    currentCommand._append = 1;
                }
                else if (type == TOKEN_INPUT)
                {
                    free(currentCommand._inputFile);
                    currentCommand._inputFile = file;
                }
                else if (type == TOKEN_ERROR)
                {
                    free(currentCommand._errFile);
                    currentCommand._errFile = file;
                }
                else
                {
                    free(currentCommand._outFile);
                    free(currentCommand._errFile);
                    currentCommand._outFile = file;
                    currentCommand._errFile = strdup(file);
                    currentCommand._out_error = 1;
                    currentCommand._append = 1;
                }
                i++;
            }
        }
    }


    currentCommand.insertSimpleCommand(currentSimpleCommand);

    if (!error && currentSimpleCommand->_numberOfArguments == 0)
    {
        if (currentCommand._numberOfSimpleCommands > 1)
            fprintf(stderr, "Error: missing command after pipe\n");
        else
            fprintf(stderr, "Error: missing command\n");
        error = true;
    }

    if (error)
    {
        currentCommand.clear();
        return;
    }

    currentCommand.execute();
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
    _simpleCommands = (SimpleCommand **)
        malloc(_numberOfAvailableSimpleCommands * sizeof(SimpleCommand *));

    _numberOfSimpleCommands = 0;
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
        delete _simpleCommands[i];
    }

    free(_simpleCommands);
    _numberOfAvailableSimpleCommands = 1;
    _simpleCommands = (SimpleCommand **)malloc(sizeof(SimpleCommand *));

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
           _inputFile ? _inputFile : "default", _errFile ? _errFile : "default", _out_error == 1 ? _errFile : "default"
           ,_background ? "YES" : "NO");
    printf("\n\n");
}

void Command::execute()
{
    print();


    if (_numberOfSimpleCommands == 1 &&
        strcmp(_simpleCommands[0]->_arguments[0], "exit") == 0)
    {
        clear();
        printf("Good bye!!\n");
        exit(0);
    }


    if (_numberOfSimpleCommands == 1 &&
        strcmp(_simpleCommands[0]->_arguments[0], "cd") == 0)
    {
        char *dir = _simpleCommands[0]->_arguments[1];
        if (dir == NULL)
            dir = getenv("HOME");
        if (dir == NULL || chdir(dir) != 0)
            perror("cd");
        clear();
        return;
    }

    int count = _numberOfSimpleCommands;
    int prevRead = -1;
    pid_t lastPid = -1;

    for (int i = 0; i < count; i++)
    {
        int fd[2];

        if (i < count - 1)
        {
            if (pipe(fd) == -1)
            {
                perror("pipe");
                break;
            }
        }

        pid_t pid = fork();

        if (pid == -1)
        {
            perror("fork");
            break;
        }

        if (pid == 0)
        {
            signal(SIGINT, SIG_DFL);


            if (i == 0 && _inputFile != NULL)
            {
                int in = open(_inputFile, O_RDONLY);
                if (in == -1)
                {
                    perror(_inputFile);
                    _exit(1);
                }
                dup2(in, 0);
                close(in);
            }
            else if (i > 0)
            {
                dup2(prevRead, 0);
            }
            if (i > 0)
                close(prevRead);

            if (i < count - 1)
            {
                dup2(fd[1], 1);
                close(fd[0]);
                close(fd[1]);
            }
            else
            {
                if (_outFile != NULL)
                {
                    int flags = O_WRONLY | O_CREAT | (_append ? O_APPEND : O_TRUNC);
                    int out = open(_outFile, flags, 0644);
                    if (out == -1)
                    {
                        perror(_outFile);
                        _exit(1);
                    }
                    dup2(out, 1);
                    if (_out_error)
                        dup2(out, 2);
                    close(out);
                }
                if (_errFile != NULL && !_out_error)
                {
                    int err = open(_errFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                    if (err == -1)
                    {
                        perror(_errFile);
                        _exit(1);
                    }
                    dup2(err, 2);
                    close(err);
                }
            }

            execvp(_simpleCommands[i]->_arguments[0], _simpleCommands[i]->_arguments);
            perror(_simpleCommands[i]->_arguments[0]);
            _exit(1);
        }


        lastPid = pid;
        if (i > 0)
            close(prevRead);
        if (i < count - 1)
        {
            close(fd[1]);
            prevRead = fd[0];
        }
    }

    if (!_background && lastPid != -1)
        waitpid(lastPid, NULL, 0);

    clear();
}

void Command::prompt()
{
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
    signal(SIGINT, SIG_IGN);
    signal(SIGCHLD, childHandler);
    Command::_currentCommand.prompt();
    return 0;
}
