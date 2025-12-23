# TinyShell

## Overview

**TinyShell** is a lightweight, POSIX-oriented command-line shell that demonstrates the core mechanisms of a Unix shell, including process creation, pipelines, I/O redirection, signal handling, and basic job control.

The project was developed as part of the **Operating Systems** course at the Department of Electrical and Computer Engineering, Aristotle University of Thessaloniki.

## Features

- **Interactive Prompt**
  - Format: `username@hostname: cwd`
  - Displays the exit status of the previous command

- **Command Execution**
  - External programs executed via `fork()` and `execve()`
  - PATH lookup for executables

- **Pipelines**
  - Arbitrary-length pipelines using `|`
  - Correct file descriptor setup with `pipe()` and `dup2()`

- **I/O Redirection**
  - Input: `<`
  - Output: `>`, `>>`
  - Error output: `2>`, `2>>`
  - Combinations of redirections and pipelines supported

- **Job Control**
  - Foreground execution with process groups
  - Background jobs using `&`
  - Job tracking with job IDs
  - Built-in job management commands (`jobs`, `fg`, `bg`)
  - Proper child reaping via `SIGCHLD`

- **Signal Handling**
  - `Ctrl+C` interrupts foreground jobs without terminating the shell
  - Shell ignores terminal stop signals (`SIGTSTP`, `SIGTTIN`, `SIGTTOU`)
  - Robust handling of interrupted system calls

- **Parsing Features**
  - Single (`'`) and double (`"`) quotes
  - Backslash escaping
  - Tilde expansion (`~ → $HOME`)

## Built-in Commands

```bash
cd              # Change to HOME
cd <path>       # Change to specified directory
cd -            # Change to previous directory (OLDPWD)

jobs            # List active jobs
fg <job_id>     # Resume job in the foreground
bg <job_id>     # Resume job in the background

exit [n]        # Exit the shell with optional status code
```

## Building

```bash
make            # Build TinyShell
make clean      # Remove build artifacts
make run        # Build and run interactively
```

## Requirements

- **Operating System**: Linux / POSIX-compliant Unix
- **Compiler**: GCC (C99 or later)
- **Libraries**: Standard POSIX libraries only

No external dependencies are required.

## Usage Examples

```text
user@host: ~
[0]-> ls -la | grep ".c" | wc -l
6
```

```text
user@host: ~
[0]-> cat < input.txt > output.txt
```

```text
user@host: ~
[0]-> sleep 100 &
[1] 12345
```

```text
user@host: ~
[0]-> fg 1
^C
user@host: ~
[130]->
```

## Architecture

| File | Description |
|-----:|------------|
| `main.c` | Entry point and interactive REPL loop |
| `parser.c` / `parser.h` | Tokenization and command parsing |
| `pipeline.c` / `pipeline.h` | Process execution, pipelines, redirections, and job control |
| `builtin.c` / `builtin.h` | Built-in command implementations |
| `signal_setup.c` / `signal_setup.h` | Signal handler installation and terminal behavior |
| `error.c` | Centralized error reporting utilities |


## Limitations

- No command history or line editing
- No tab completion
- No globbing (`*`, `?`)
- No environment variable expansion (`$VAR`)
- No scripting support or aliases

These omissions are intentional to keep the implementation focused on core OS concepts.

## References

- [POSIX.1-2008 Standard](https://pubs.opengroup.org/onlinepubs/9699919799/)
- Linux man pages:
  - `fork(2)`
  - `execve(2)`
  - `pipe(2)`
  - `dup2(2)`
  - `waitpid(2)`
  - `sigaction(2)`
  - `tcsetpgrp(3)`

---

<p align="center">
  <sub>Aristotle University of Thessaloniki • December 2025</sub>
</p>
