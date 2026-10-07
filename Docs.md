# Assemdows Documentation

Assemdows is a small assembly-style language. Programs are plain text files ending in `.asdw`, and the `assemdows` interpreter (a single C file) runs them. Because it is plain C, it works the same on Windows, Linux, and macOS.

This document has two parts: a **tutorial** that builds up from "Hello" to functions and arrays, and a **reference** that lists every instruction, directive, and rule.

- [Getting started](#getting-started)
- [Tutorial](#tutorial)
- [Reference](#reference)
- [Command-line options](#command-line-options)
- [Errors and debugging](#errors-and-debugging)
- [Limits](#limits)
- [Common mistakes](#common-mistakes)

---

## Getting started

### Build

You need a C compiler such as gcc (on Windows, MSYS2/MinGW provides it).

```
gcc -O2 -Wall -o assemdows.exe assemdows.c
```

### Run a program

```
assemdows examples\hello.asdw
```

On Linux or macOS the output file is usually named `assemdows` without `.exe`, and you run it with `./assemdows examples/hello.asdw`.

### Your first program

Save this as `hello.asdw`:

```
msg: .string "Hello, Assemdows!\n"

    PRINTS msg
    HALT
```

Run it and you should see `Hello, Assemdows!`.

Every program must end by running `HALT`. If execution runs past the last instruction, the interpreter reports an error.

---

## Tutorial

### 1. Registers and printing

A **register** is a named storage slot that holds one whole number. Assemdows has 16 of them: `R0`, `R1`, up to `R15`.

```
    MOV R0, 5       ; put 5 in R0
    MOV R1, 7
    ADD R0, R1      ; R0 = R0 + R1
    PRINT R0        ; prints 12
    HALT
```

The pattern `INSTRUCTION destination, source` is used almost everywhere. The destination is changed, and the source is read. The source can be another register or a plain number.

Anything after a `;` is a comment and is ignored.

### 2. Comparing and jumping

`CMP a, b` compares two values and remembers the result. The jump instructions then use that result to decide whether to jump to a **label**, a name followed by a colon.

```
    MOV R0, 10
    CMP R0, 5
    JG big          ; jump if R0 > 5
    PRINT 0
    HALT
big:
    PRINT 1
    HALT
```

The jumps are `JE` (equal), `JNE` (not equal), `JG` (greater), `JL` (less), `JGE` (greater or equal), `JLE` (less or equal), and `JMP` (always).

### 3. Loops

A loop is a jump backward. This counts down from 5:

```
    MOV R0, 5
loop:
    PRINT R0
    DEC R0          ; R0 = R0 - 1
    CMP R0, 0
    JG loop         ; keep going while R0 > 0
    HALT
```

This prints `5 4 3 2 1`, one per line. Forgetting the condition makes an endless loop. Use `--max-steps` while testing to guard against that (see [Command-line options](#command-line-options)).

### 4. Constants

`.equ` gives a name to a number. It doesn't use any memory.

```
.equ LIMIT, 3

    MOV R0, 1
again:
    PRINT R0
    INC R0
    CMP R0, LIMIT
    JLE again
    HALT
```

Constants can be used anywhere a number can.

### 5. Text and characters

`.string` stores text in memory and gives its first position a name. `PRINTS` prints text starting at an address, and `PRINTC` prints a single character.

```
greeting: .string "Hi there\n"

    PRINTS greeting
    PRINTC 'A'
    PRINTC 10           ; 10 is the newline character
    HALT
```

Inside strings and character literals you can use `\n` (newline), `\t` (tab), `\r`, `\0`, `\\`, `\"`, and `\'`.

### 6. Input

`INPUT` reads a whole number from the keyboard into a register.

```
ask: .string "Your number? "

    PRINTS ask
    INPUT R0
    MUL R0, R0
    PRINT R0            ; prints the square
    HALT
```

### 7. Functions with CALL and RET

`CALL label` jumps to a label and remembers where it came from. `RET` goes back. Put functions after `HALT`, so the program doesn't run into them by accident.

```
    MOV R0, 5
    CALL double
    PRINT R0            ; 10
    CALL double
    PRINT R0            ; 20
    HALT

double:
    ADD R0, R0
    RET
```

### 8. The stack

`PUSH` saves a value on the stack, and `POP` takes the most recent one back off. A function that calls itself (or other functions) uses the stack to protect registers it needs later.

```
    MOV R0, 5
    CALL fact
    PRINT R0            ; 120
    HALT

fact:
    CMP R0, 1
    JG recurse
    MOV R0, 1
    RET
recurse:
    PUSH R0             ; save n
    DEC R0
    CALL fact           ; R0 = fact(n - 1)
    POP R1              ; get n back
    MUL R0, R1
    RET
```

Every `PUSH` inside a function needs a matching `POP` before its `RET`. The stack is also where `CALL` stores the return address, so an extra `PUSH` makes `RET` go to the wrong place.

### 9. Arrays in memory

Memory is a long row of numbered cells. `.word` stores a list of numbers, and square brackets read or write a cell.

```
.equ N, 4
nums: .word 10, 20, 30, 40

    MOV R1, 0               ; index
    MOV R0, 0               ; total
sum:
    LOAD R2, [nums+R1]      ; R2 = nums[R1]
    ADD R0, R2
    INC R1
    CMP R1, N
    JL sum
    PRINT R0                ; 100
    HALT
```

`STORE [nums+R1], R2` writes to a cell. See [Memory](#memory) for all the address forms.

### 10. More programs

The `examples` folder has complete programs: `hello`, `fib` (Fibonacci numbers), `primes`, `sort` (bubble sort on an array), `guess` (a number-guessing game using `RAND` and `INPUT`), and `factorial`.

---

## Reference

### Program structure

- One statement per line. A statement is an instruction or a directive, optionally preceded by labels.
- Comments start with `;` and run to the end of the line. A `;` inside a string or character literal is not a comment.
- Instructions, registers, directives, and label names are **not case sensitive**: `mov r0, 1`, `MOV R0, 1`, and `Mov R0, 1` are the same.
- Blank lines and indentation don't matter.
- Operands are separated by commas.

### Labels

A label is a name followed by a colon. It marks a position in the program.

```
loop:
    DEC R0
```

- A label can share a line with what follows: `loop: DEC R0`.
- Several labels in a row all mark the same place.
- A label in front of an instruction is a **code label**: its value is the instruction's position, and it is the target of jumps and `CALL`.
- A label in front of `.word`, `.string`, or `.space` is a **data label**: its value is the memory address of the first cell.
- Names use letters, digits, and `_`, can't start with a digit, can be up to 63 characters long, and must be unique.
- A label can't be named like a register (`R0` to `R15`).
- Jump instructions only accept code labels. A data label or constant gives an error.

### Values

A **value** (written `v` in the tables below) is a register or one of these:

| Form | Example | Notes |
|---|---|---|
| Decimal | `42`, `-7` | |
| Hexadecimal | `0xFF` | |
| Binary | `0b1010` | |
| Character | `'A'`, `'\n'` | The character's code number |
| Name | `LIMIT`, `msg` | A constant (`.equ`) or label |

All numbers are 32-bit signed integers (about -2.1 billion to 2.1 billion). Arithmetic wraps around on overflow instead of crashing.

### Registers

`R0` through `R15`. They all start at 0 and are general purpose: no register has a special meaning.

### Instructions

`Rd` is a destination register, and `v` is a register or value.

#### Moving data

| Instruction | What it does |
|---|---|
| `MOV Rd, v` | Rd = v |

#### Arithmetic

| Instruction | What it does |
|---|---|
| `ADD Rd, v` | Rd = Rd + v |
| `SUB Rd, v` | Rd = Rd - v |
| `MUL Rd, v` | Rd = Rd * v |
| `DIV Rd, v` | Rd = Rd / v, whole-number division that rounds toward zero (`-7 / 2` is `-3`). Dividing by 0 is an error. |
| `MOD Rd, v` | Rd = the remainder of Rd / v. The sign follows Rd (`-7 MOD 3` is `-1`). Using 0 is an error. |
| `INC Rd` | Rd = Rd + 1 |
| `DEC Rd` | Rd = Rd - 1 |
| `NEG Rd` | Rd = -Rd |

#### Bits

| Instruction | What it does |
|---|---|
| `AND Rd, v` | Bitwise and |
| `OR Rd, v` | Bitwise or |
| `XOR Rd, v` | Bitwise exclusive or |
| `NOT Rd` | Flips every bit (`NOT` of 0 is -1) |
| `SHL Rd, v` | Shifts bits left by v (the amount is taken modulo 32) |
| `SHR Rd, v` | Shifts bits right by v, keeping the sign (the amount is taken modulo 32) |

#### Comparing and jumping

`CMP a, b` compares two values (each a register or a value) and stores whether `a` is less than, equal to, or greater than `b`. It changes no registers. Jumps look at the **most recent** `CMP`. Before any `CMP` has run, the stored result is "equal".

| Instruction | Jumps to the label when... |
|---|---|
| `JMP label` | always |
| `JE label` | a = b |
| `JNE label` | a ≠ b |
| `JG label` | a > b |
| `JL label` | a < b |
| `JGE label` | a ≥ b |
| `JLE label` | a ≤ b |

Keep in mind that other instructions between `CMP` and the jump don't change the stored result, so you can use several jumps in a row after one `CMP`:

```
    CMP R1, R0
    JE done
    JL toolow       ; still uses the same CMP
```

#### Functions and the stack

| Instruction | What it does |
|---|---|
| `CALL label` | Pushes the return address and jumps to the label |
| `RET` | Pops a return address and jumps back to it. An empty stack is an error. |
| `PUSH v` | Pushes a value onto the stack |
| `POP Rd` | Pops the top value into Rd. An empty stack is an error. |

The stack holds up to 4096 entries. It is separate from memory, so you can't take the address of a stack value.

#### Memory

| Instruction | What it does |
|---|---|
| `LOAD Rd, [addr]` | Rd = the number stored at addr |
| `STORE [addr], v` | Stores v at addr |

See [Memory](#memory) for address forms.

#### Output

| Instruction | What it does |
|---|---|
| `PRINT v` | Prints a number followed by a newline |
| `PRINTC v` | Prints one character (the value's low 8 bits as a character code), with no newline |
| `PRINTS v` | Prints text starting at memory address v, up to the first 0 cell. Usually `v` is a `.string` label. |

#### Input and random numbers

| Instruction | What it does |
|---|---|
| `INPUT Rd` | Reads a whole number from the keyboard into Rd. Anything else is an error. |
| `GETC Rd` | Reads one character into Rd as its code, or -1 at the end of the input |
| `RAND Rd, v` | Rd = a random number from 0 up to v - 1. v must be above 0. |

`RAND` uses a different seed each run unless you pass `--seed`.

#### Other

| Instruction | What it does |
|---|---|
| `NOP` | Does nothing |
| `HALT` | Stops the program with exit code 0 |
| `HALT v` | Stops the program with exit code v (read it with `%errorlevel%` on Windows, or `$?` on Linux and macOS) |

### Directives

Directives start with a `.` and set up constants and data. They don't run as instructions.

| Directive | What it does |
|---|---|
| `.equ NAME, value` | Defines a constant. The comma is optional (`.equ NAME value`). |
| `.word v1, v2, ...` | Stores numbers in consecutive memory cells |
| `.string "text"` | Stores one cell per character, followed by a 0 cell to mark the end |
| `.space n` | Reserves n cells, all set to 0 |

Data is placed in memory starting at address 0, in the order it appears in the file. A constant used inside a data directive (like `.word` or `.space`) must be defined **earlier** in the file. Instructions can use constants defined anywhere.

### Memory

There are 65,536 cells, numbered 0 to 65535. Each cell holds one 32-bit number, so a string takes one cell per character.

An **address** goes inside square brackets and can take these forms:

| Form | Meaning |
|---|---|
| `[100]` | The cell at address 100 |
| `[label]` | The cell where a data label points |
| `[R1]` | The cell whose address is in R1 |
| `[R1+4]`, `[R1-1]` | A register plus or minus a number |
| `[nums+R1]` | A label or number plus a register |
| `[nums+2]` | A label plus a number |

Rules: at most one register per address, and you can't subtract a register. For anything more complex, work out the address in a register first, then use `[R2]`.

Reading or writing outside 0 to 65535 is an error. To get a data label's address into a register, use `MOV R1, nums`.

---

## Command-line options

```
assemdows [options] program.asdw
```

| Option | What it does |
|---|---|
| `--trace` | Prints every instruction as it runs (to the error stream), with its position and line number |
| `--regs` | Prints all 16 registers when the program reaches `HALT` |
| `--seed N` | Sets the random seed, so `RAND` gives the same numbers each run |
| `--max-steps N` | Stops with an error after N instructions, which catches infinite loops |
| `-v`, `--version` | Prints the version |
| `-h`, `--help` | Prints a help summary |

The program's exit code is the number given to `HALT` (0 if none). If there is an error, the exit code is 1.

---

## Errors and debugging

### Errors before the program runs

These include the file name and line number:

```
prog.asdw:4: error: unknown instruction 'MOVE'
prog.asdw:7: error: unknown label 'loopp'
prog.asdw:9: error: MOV takes 2 operands, but 1 was given
```

Common causes are a misspelled instruction or label, the wrong number of operands, a register where a value was expected (or the reverse), or a missing quote on a string. The interpreter stops at the first one it finds.

### Errors while running

These also show the line that caused them:

```
prog.asdw:12: runtime error: division by zero
    DIV R0, R1
```

The runtime errors are: division by zero, memory address out of range, `POP`/`RET` with an empty stack, stack overflow, `RET` to an invalid address, `INPUT` that wasn't a number, `RAND` with a maximum of 0 or less, `PRINTS` running off the end of memory, running past the last instruction (a missing `HALT`), and the `--max-steps` limit.

### Debugging tips

- Use `--trace` to watch the program step by step. Each line shows the instruction number, the source line, and the instruction itself.
- Use `--regs` to see what is in every register at the end.
- Use `--max-steps 10000` while writing loops, so a mistake stops instead of hanging.
- Put a temporary `PRINT R0` in the middle of your program to see a value.
- To test one piece, use `HALT` right after it.

---

## Limits

| Thing | Limit |
|---|---|
| Instructions per program | 4096 |
| Registers | 16 |
| Memory cells | 65,536 |
| Stack entries | 4096 |
| Labels and constants | 1024 |
| Line length | 512 characters |
| Values in one `.word` line | 1024 |
| Labels in a row before one statement | 32 |

Not included in version 1.0: floating-point numbers, file input and output, including other files, and macros.

---

## Common mistakes

- **Forgetting `HALT`.** The program runs off the end and reports an error. Put `HALT` at the end of the main code.
- **Putting functions before the main code.** Execution runs straight into them. Put functions after `HALT`.
- **Unbalanced `PUSH` and `POP`.** Inside a function, every `PUSH` needs its `POP` before `RET`. Otherwise `RET` jumps to the wrong place.
- **Using a register another function overwrites.** Registers are shared by everything. If a function changes `R1` and you still need it, `PUSH` it first and `POP` it after.
- **Jumping to a data label.** `JMP msg` fails, because `msg` points at data. Jumps need code labels.
- **A constant used before it exists inside `.word` or `.space`.** Define the `.equ` above that line.
- **Forgetting the zero at the end of text.** `.string` adds it for you, but if you build text by hand with `.word`, finish it with a `0` or `PRINTS` will keep reading.
- **Expecting `PRINT` to show text.** `PRINT` shows a number. Use `PRINTS` for text and `PRINTC` for a single character.
- **Memory holds numbers, not bytes.** A string of 5 characters uses 6 cells (5 plus the 0 at the end).

---

## License

Assemdows is free software, released under the GNU General Public License version 3 or later. See the `LICENSE` file.