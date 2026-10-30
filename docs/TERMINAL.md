# Terminal

The desktop Terminal app does not interpret commands itself. It runs the real
`/bin/shell` behind two pipes and acts as the terminal emulator for it, the way a
tty and a terminal window do together on other systems.

```
 keyboard --> terminal --- stdin pipe ---> /bin/shell -i --> pipelines, jobs, programs
 window   <-- terminal <-- stdout/stderr pipe (one pipe, so order is kept)
```

The code is split along that line:

| Part | File | Job |
|------|------|-----|
| Terminal core | `userspace/lib/terminal.c` | Scrollback, escape sequence parsing, line editing, completion, shell process. No window code. |
| Terminal app | `userspace/desktopdemo/app_terminal.c` | Draws the core's frame into a window with the theme colours. |
| Shell mode | `userspace/shell/shell.c` (`shell -i`) | Prompt, line input from stdin, Ctrl+C forwarding. |
| Boot test | `userspace/termtest/termtest.c` | Drives a real shell through the core without a display and prints `TERM PASS`. |

## Line editing lives in the terminal

The terminal echoes what is typed, handles the editing keys and sends a line
to the shell's stdin only when Enter is pressed. The shell reads stdin one
byte at a time up to the newline, so anything typed ahead stays in the pipe
for a command that reads standard input.

| Key | Action |
|-----|--------|
| Left, Right, Home, End, Ctrl+A, Ctrl+E | Move the cursor |
| Backspace, Delete, Ctrl+W, Ctrl+U, Ctrl+K | Delete a character, word, to the start, to the end |
| Up, Down | History (64 entries) |
| Tab | Complete a command name (builtins, programs in `/`) or a path relative to the shell's working directory; lists the candidates when the common prefix cannot grow |
| Ctrl+C | Interrupt the foreground command; at an idle prompt, abandon the line |
| Ctrl+D | At an empty idle prompt: end of input, the shell exits and a clean exit closes the window. On a non-empty line: delete the character under the cursor |
| Ctrl+L | Scroll the screen so that the prompt is at the top |
| Page Up, Page Down, mouse wheel | Scroll back through the 2000 line buffer |

Ctrl+D cannot end the input of a single foreground command, because a pipe has
one end of file for the whole shell.

## Interrupts

The terminal sends `SIGINT` (`SYS_KILL`) to the shell. A shell in `-i` mode
installs a handler that forwards the signal to the processes of the foreground
pipeline it is waiting for and makes builtins and the command list stop early.
The shell itself keeps running. The foreground set is registered in
`userspace/shell/interrupt.c`; a pid is removed as soon as it is reaped, so a
reused pid is never signalled.

Closing the window interrupts the foreground command, closes the shell's
input, and lets the shell end itself, which also terminates its background
jobs. A shell that does not leave within 500 ms is killed.

## What the shell sends besides text

Output is rendered with a small subset of ANSI:

| Sequence | Meaning |
|----------|---------|
| `ESC [ 0 m`, `1`, `22`, `30-37`, `39`, `90-97` `m` | Reset, bold, foreground colours |
| `ESC [ 2 J`, `ESC [ H`, `ESC [ K` | Clear the screen (old content stays in the scrollback), home the cursor, erase to end of line |
| `ESC [ A B C D` | Cursor movement |
| `\r`, `\b`, `\t` | Overwrite from the line start, move left, tab stops every 8 columns |

Everything else is ignored. The buffer stores logical lines and wraps them
when drawing, so resizing the window reflows the whole scrollback. Lines
longer than 256 cells continue on a new line.

Operating system commands, `ESC ] <number> ; <text> BEL`:

| Number | Sender | Meaning |
|--------|--------|---------|
| `7` | shell prompt | The shell's working directory, as a plain path. Used for path completion. |
| `133;B` | shell prompt | The prompt ends here and the shell waits for a line. Marks the shell idle and tells the terminal where input starts. |
| `7770;edit;<absolute path>` | `edit` builtin | Ask the desktop to open the Editor, with the file if the path is not empty. |
| `7770;files` | `files` builtin | Ask the desktop to open the Files app. |

The `clear`, `edit` and `files` builtins exist only in `shell -i`. Requests
only open applications; they never run or write anything.

## Colours in redirected output

Error messages are tinted with ANSI sequences only when they go to the shell's
own stderr. A builtin whose output is redirected, and a message written to a
child's stderr, stay plain text.
