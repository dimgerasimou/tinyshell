<h1 align="center">TinyShell</h1>
<h3 align="center">Operating Systems</h3>

<p align="center">
  <em>Department of Electrical and Computer Engineering,</em><br>
  Aristotle University of Thessaloniki<br>
  <strong>Phase #2</strong>
</p>

## Overview

TinyShell is a lightweight command-line interpreter demonstrating core shell functionality including process management, pipelines, I/O redirection, and signal handling.

It was developed as part of the Operating Systems course at the Department of Electrical and Computer Engineering, Aristotle University of Thessaloniki.

## Features

- **Interactive Prompt**: `username@hostname: path` with exit code display
- **Command Execution**: External programs via `fork()` and `execve()`
- **Pipelines**: Multiple commands connected with `|`
- **I/O Redirection**: `<`, `>`, `>>`, `2>`, `2>>`
- **Signal Handling**: Ctrl+C interrupts running commands, not the shell
- **Quoting**: Single (`'`) and double (`"`) quotes with backslash escapes
- **Tilde Expansion**: `~` expands to `$HOME`

### Built-in Commands

```bash
cd              # Change to HOME
cd <path>       # Change to path
cd -            # Change to previous directory (OLDPWD)
exit [n]        # Exit with optional status code
```

## Building

```bash
make            # Build
make clean      # Remove build artifacts
make run        # Build and run
```

## Requirements

- **Operating System**: Linux
- **Compiler**: GCC with C99 support
- **Libraries**: Standard POSIX libraries

## Usage

```
user@host: ~
[0]-> ls -la | grep ".c" | wc -l
6

user@host: ~
[0]-> cat < input.txt > output.txt

user@host: ~
[0]-> sleep 100
^C
user@host: ~
[130]-> exit
```

## Architecture

| File | Purpose |
|------|---------|
| `main.c` | Entry point and REPL loop |
| `parser.c` | Tokenizer and command parser |
| `pipeline.c` | Pipeline execution and I/O redirection |
| `builtin.c` | Built-in command implementations |
| `signal_setup.c` | Signal handler configuration |
| `error.c` | Error reporting utilities |

## Limitations

- No background jobs (`&`) or job control
- No globbing (`*`, `?`)
- No environment variable expansion (`$VAR`)
- No command history or line editing
- No shell scripts or aliases

## References

- [POSIX.1-2008 Standard](https://pubs.opengroup.org/onlinepubs/9699919799/)
- Linux man pages: `fork(2)`, `execve(2)`, `pipe(2)`, `dup2(2)`, `sigaction(2)`
