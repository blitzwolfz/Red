# logstat

A summary of one or more access logs. It is the program
[docs/guide.md](../../docs/guide.md) builds from nothing, kept here as a
working whole rather than as listings in a document.

```bash
red logstat.red sample.log
red logstat.red --top 3 --json sample.log
cat sample.log | red logstat.red --quiet
red logstat.red --help
```

| File | What it is |
|---|---|
| [`logstat.red`](logstat.red) | The program. Arguments, files, tasks, output, exit codes. |
| [`parse.red`](parse.red) | The module. Text in, values out, and nothing else. |
| [`sample.log`](sample.log) | Ten lines, one of them deliberately malformed. |
| [`tests/parse.red`](tests/parse.red) | Tests for the module. |

```bash
red test tests
```

The split between the two files is the point. `parse.red` knows about log
lines and knows nothing about files, arguments or printing, which is why
it can be tested by calling it. `logstat.red` moves values between that
module and the terminal.
