<h1 align="center">TinyShell</h1>
<h3 align="center">Operating Systems</h3>

<p align="center">
  <em>Department of Electrical and Computer Engineering,</em><br>
  Aristotle University of Thessaloniki</em><br>
  <strong>Phase #1</strong>
</p>

## Overview

TinyShell is a lightweight command-line interpreter developed as part of the Operating Systems course at the Department of Electrical and Computer Engineering, Aristotle University of Thessaloniki. It demonstrates core shell functionality including process management, command execution, and built-in command handling.

## Features

### Core Functionality
- **Interactive Command Prompt**: Displays `username@hostname:path` format
- **Command Execution**: Executes external programs via fork() and execve()
- **PATH Resolution**: Automatically searches PATH directories for executables
- **Exit Status Reporting**: Properly handles and reports command exit codes
- **EOF Handling**: Termination on Ctrl+D

### Built-in Commands

#### `cd` - Change Directory
```bash
cd              # Change to HOME directory
cd <path>       # Change to specified path
cd ~            # Change to HOME directory
cd ~/Documents  # Change to subdirectory of HOME
cd -            # Change to previous directory (OLDPWD)
```

#### `exit` - Exit Shell
```bash
exit            # Exit with last command's status
exit 0          # Exit with status 0
exit 42         # Exit with status 42
```

### Additional Features
- **Tilde Expansion**: Automatically expands `~` to HOME directory
- **Environment Variables**: Maintains PWD and OLDPWD
- **Path Abbreviation**: Displays HOME as `~` in prompt

## Requirements

- **Operating System**: Linux
- **Compiler**: GCC with C99 support
- **Libraries**: Standard POSIX libraries

## Installation

### Clone or Download
Download the source files:
- `tinyshell.c` - Main implementation
- `Makefile` - Build configuration

### Compile
```bash
make
```

This will produce the `tinyshell` executable.

### Compilation Options
View build configuration:
```bash
make options
```

Manual compilation:
```bash
gcc -o tinyshell tinyshell.c -std=c99 -Wpedantic -Wall -Wextra -Os
```

## Usage

### Start the Shell
```bash
./tinyshell
```

### Example Session
```
TinyShell - Phase 1
Type 'exit' to quit or press Ctrl+D

user@hostname: ~
[0]-> ls -la

user@hostname: ~
[0]-> cd Documents

user@hostname: ~/Documents
[0]-> pwd
/home/user/Documents

user@hostname: ~/Documents
[0]-> cd -
/home/user

user@hostname: ~
[0]-> echo "Hello, World!"
Hello, World!

user@hostname: ~
[0]-> exit
```

##  Architecture

### Main Components

1. **Main Loop** (`main_loop`)
   - Prompt → Read → Parse → Execute cycle
   - Tracks last exit code

2. **Input Parsing** (`parse_input`)
   - Tokenizes input on whitespace
   - Creates NULL-terminated argument array

3. **Command Execution** (`execute_command`)
   - Searches PATH for executable
   - Forks child process
   - Executes via execve()
   - Reports exit status

4. **Built-in Handler** (`builtin_cd`)
   - Implements cd functionality
   - Updates environment variables

5. **PATH Resolution** (`find_in_path`)
   - Searches PATH directories
   - Checks file executability

6. **Prompt Display** (`print_prompt`)
   - Shows username@hostname:path
   - Abbreviates HOME directory
   - Reports exit code of previous command

## Limitations

TinyShell does not support:
- Pipes (`|`)
- Redirections (`>`, `<`, `>>`)
- Background jobs (`&`)
- Job control (fg, bg)
- Command history or editing
- Globbing (`*`, `?`)
- Environment variable expansion (`$VAR`)
- POSIX string expansion (using `'` or `"`)
- Aliases
- Shell scripts

## Testing

### Basic Commands
```bash
-> ls
-> pwd
-> echo hello
```

### Built-in Commands
```bash
-> cd /tmp
-> cd ~
-> cd -
-> exit 0
```

### Error Handling
```bash
-> nonexistentcommand    # Should report "command not found"
-> cd /nonexistent       # Should report error
```

### Exit Codes
```bash
-> /bin/true
-> /bin/false
```

## Error Handling

TinyShell provides clear error messages for:
- Command not found (exit code 127)
- Fork failures
- Execution failures
- Environment variable issues
- Directory change failures
- Signal terminations (exit code 128+signal)

## Cleanup

Remove compiled binary:
```bash
make clean
```

## Educational Purpose

This project demonstrates:
- Process creation with `fork()`
- Program execution with `execve()`
- Process synchronization with `waitpid()`
- Environment variable manipulation
- PATH resolution
- Signal handling basics
- String parsing and manipulation
- Error handling in system programming

## References

- [POSIX.1-2008 Standard](https://pubs.opengroup.org/onlinepubs/9699919799/)
- Linux man pages: `man 2 fork`, `man 2 execve`, `man 2 wait`