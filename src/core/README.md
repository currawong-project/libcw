# Core Module Overview

This document provides a high-level overview of key modules within the
`libcw` core, intended for developers encountering them for the first
time. These modules generally adhere to a C-style API, prioritize
performance, and manage memory explicitly using the `cwMem`
module. They often utilize a `handle_t` abstraction for managing
resources and return `rc_t` (return code) values for error handling,
with detailed logging available via `cwLog`.

## Modules Summary

### `cwMem`

The `cwMem` module provides a foundational memory management layer for
the `libcw` framework, built on top of standard C `malloc` and
`free`. It introduces a custom memory allocation scheme that prepends
a small metadata header to each allocated block, storing the block's
total size for internal tracking. This allows for functionalities like
`byteCount` (to query allocated size), and controlled zeroing of
memory during allocation (`allocZ`) and reallocation
(`resizeZ`). Beyond raw memory handling, `cwMem` offers robust string
manipulation functions (`allocStr`, `duplStr`, `reallocStr`,
`appendStr`) that integrate with its custom allocation, ensuring
proper memory management for C-style strings. It also includes
`printf`-like functions (`printf`, `printp`) for dynamic string
formatting and a debugging feature to warn on memory allocations.

### `cwText`

The `cwText` module provides a comprehensive suite of utility
functions for C-style string manipulation, focusing on basic
operations like length calculation (`textLength`), safe copying
(`textCopy`), and concatenation (`textCat`). It includes functions for
case conversion (`textToLower`, `textToUpper`), various comparison
operations (`textCompare`, `textCompareI`), and robust whitespace and
character searching (`nextWhiteChar`, `nextNonWhiteChar`,
`firstMatchChar`, `lastMatchChar`). The module also offers utility
functions for validating string content (e.g., `isInteger`, `isReal`,
`isIdentifier`), dynamic string joining (`textJoin`), and appending
(`textAppend`), often leveraging the `cwMem` module for memory
management when new strings are created. Additionally, it provides a
set of `toText` overloads for converting various primitive data types
into their string representations.

### `cwFile`

The `cwFile` module offers a robust C-style file I/O abstraction layer
built upon standard C `FILE*` operations. It provides functions for
opening, closing, reading, and writing files, utilizing a `handle_t`
abstraction for file management and integrating with the `cwMem`
module for dynamic buffer allocations (e.g., `toBuf`, `fnToBuf`,
`toStr`, `fnToStr`). Key features include flag-based file opening
(`kReadFl`, `kWriteFl`, `kBinaryFl`, etc.), explicit error reporting
(`lastRC`), and advanced functionalities like file seeking (`seek`,
`tell`), content comparison (`compare`), and line-by-line reading with
automatic buffer resizing (`getLine`, `getLineAuto`). The module also
supports type-specific binary read/write operations for various
primitive data types (`readInt`, `writeFloat`), string serialization
(`readStr`, `writeStr`), formatted output (`printf`, `print`), and a
file backup mechanism (`backup`) for creating versioned copies.

### `cwVectOps`

The `cwVectOps` module provides a templated library for common vector
operations, designed for generic numerical processing. It offers a
wide range of functionalities, including element-wise input/output
(e.g., `print`), array manipulation (`copy`, `fill`, `zero`, `ones`),
comparison (`is_equal`), and statistical computations (`min`, `max`,
`arg_min`, `arg_max`, `mean`, `std`). The module also includes
fundamental arithmetic operations (`mul`, `add`, `div`, `sub`) that
can be applied element-wise or with scalars, as well as specialized
functions for signal processing like `interleave`, `deinterleave`,
`phasor`, `sine`, and decibel/amplitude conversions (`ampl_to_db`,
`db_to_ampl`). Additional utilities cover calculating sums (`sum`,
`abs_sum`), products (`prod`), and sum of squares (`sum_sq`,
`sum_sq_diff`), making it a versatile toolkit for numerical array
manipulation.

### `cwLex`

The `cwLex` module provides a flexible and configurable lexical
analyzer (lexer) for tokenizing text. It supports the recognition of
various token types, including numbers (real, integer, hexadecimal),
identifiers, quoted strings/characters, and comments (block and
line). Developers can control the lexer's behavior through flags, such
as whether to return whitespace or comment tokens. A key feature is
the ability to register custom user-defined tokens and associated
matching functions, allowing for highly specialized parsing needs. The
module provides a handle-based interface for managing lexer instances,
enabling operations like setting text buffers or files as input,
resetting state, and querying token details such as ID, text,
character count, and converted numerical values. It also tracks line
and column numbers for error reporting and provides an error code
mechanism.

### `cwFileSys`

The `cwFileSys` module provides a set of cross-platform utilities for
interacting with the file system. It offers an API for listing the
contents of directories, manipulating path strings (e.g., extracting
components), checking for the existence of files and directories, and
creating or removing directories. The module is designed to abstract
away platform-specific details of file system operations and
integrates with other core modules for string and memory management.

### `cwObject`

The `cwObject` module provides a comprehensive framework for creating,
parsing, and manipulating JSON-like hierarchical data structures. It
supports dynamic objects including dictionaries (key-value pairs),
lists, and primitives (strings, numbers, booleans, and null). The
module features a robust parser to deserialize text from a string or
file into a navigable tree of `object_t` nodes. An extensive API allows
for programmatically building these object trees, finding specific
nodes, and safely retrieving or modifying their values. Furthermore,
it can serialize the entire object structure back into a compact
string, making it ideal for configuration files, data exchange, and
inter-process communication. `cwObject` relies entirely on `cwMem` for
its internal memory management.
