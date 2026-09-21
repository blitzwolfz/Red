# Packages

Red has no package registry. A dependency is named by the place it comes
from:

```red
import "github.com/owner/toolkit/text" as text;
```

That string is both the import path and the address the code is fetched
from. Nobody publishes to an index, nobody holds an account, and a name
cannot be taken by somebody else, because it is already somebody's
repository. Moving a package means moving the repository.

This is the model Go uses, without the parts that need a proxy or a
checksum server.

## Starting a project

```bash
red pkg init github.com/you/yourproject
```

That writes `red.mod`:

```
module github.com/you/yourproject
red 0.5.0
```

The module path is the name other projects import this one by, and the
name this project imports *itself* by. An import that starts with it is
answered from the project directory:

```red
import "github.com/you/yourproject/internal/store" as store;
```

That keeps working when the importing file moves, which `../../internal/store`
does not. Plain relative imports still work and are still the right thing
for two files that sit next to each other.

If you pass no path, `red pkg init` uses `local/<directory name>`. A
project that is never going to be fetched by anyone does not need a real
one.

## Adding a dependency

```bash
red pkg add github.com/owner/toolkit
red pkg add github.com/owner/toolkit@v1.4.0
red pkg add github.com/owner/toolkit@main
red pkg add github.com/owner/toolkit@9f8a2c1
```

A version is a tag, a branch, a commit, or `latest`. `latest` means the
newest release tag, compared as a semantic version, so `v1.10.0` wins
over `v1.9.0`; a repository with no tags resolves to the current commit
of its default branch, recorded as `v0.0.0-<commit>`.

Whatever `latest` resolved to is written back into `red.mod`, so the
manifest always names a version rather than a wish.

## red.mod

```
module github.com/you/yourproject
red 0.5.0

require github.com/owner/toolkit v1.4.0
require github.com/other/parser v0.9.2 indirect

source github.com/internal/thing ssh://git@git.example.com/thing.git

replace github.com/owner/toolkit => ../toolkit
```

| Line | Meaning |
|---|---|
| `module` | This project's own import path. |
| `red` | The interpreter release it was written for. |
| `require <path> <version>` | A dependency. `indirect` marks one that only another dependency needs. |
| `source <path> <url>` | Where to fetch from, when the path does not say. For private hosts, SSH remotes, and mirrors. |
| `replace <path> => <dir>` | Use a directory instead of the fetched copy. For working on two packages at once. |

Comments start with `//` or `#`. The file is written back in a canonical
order whenever a command changes it, so it diffs cleanly.

## red.lock

```
github.com/owner/toolkit v1.4.0 4b825dc642cb6eb9a060e54bf8d69288fbee4904 https://github.com/owner/toolkit.git
```

One line per resolved dependency: path, version, the exact commit, and
the remote it came from. Commit this file. It is what makes a second
machine fetch the same bytes yours did, even for a requirement written
as a branch.

Imports read the version out of `red.lock`, not out of `red.mod`, so a
dependency cannot change underneath a build that has not been updated on
purpose.

## Where an import looks

For a request that looks like a package path — several segments, the
first of which contains a dot — the interpreter tries, in order:

1. **This project.** The request starts with the `module` path: the rest
   of it names a file under the project root.
2. **A `replace` directive.** The longest matching prefix wins, so
   replacing a repository also replaces every package inside it.
3. **`vendor/`**, if the project has one. Checked before the cache, so a
   vendored project builds with no network at all.
4. **The download cache**, at the version `red.lock` pins.

Anything else — a relative path, a bare file name — resolves the way it
always has: next to the importing file first, then along `RED_PATH` and
the interpreter's own library directory. `docs/libraries.md` covers that
half.

A package path may name a file or a directory. A directory is entered
through `<name>.red`, `mod.red`, `lib.red` or `main.red`, whichever it
has. The `.red` suffix is optional in the request: `import "github.com/owner/toolkit/text"`
and `import "github.com/owner/toolkit/text.red"` are the same import.

## The cache

Fetched packages live under `$RED_HOME/pkg`, which defaults to
`~/.red/pkg`:

```
~/.red/pkg/github.com/owner/toolkit@v1.4.0/
```

Each version is a separate directory, so two projects that need two
versions of the same package share a cache without fighting over it. The
`.git` directory is removed after the clone: the code is what is wanted,
not the history. Nothing writes to a cached package after it lands, so
editing one is not a way to change a build — use `replace` for that.

```bash
red pkg cache dir      # where it is
red pkg cache clean    # empty it
```

## Commands

| Command | What it does |
|---|---|
| `red pkg init [path]` | Start a project. Writes `red.mod`. |
| `red pkg add <path>[@version] ...` | Require a package and fetch it. |
| `red pkg remove <path> ...` | Stop requiring a package. |
| `red pkg get [--force]` | Fetch everything `red.mod` requires. `--force` re-clones. |
| `red pkg update [path ...]` | Move unpinned requirements forward. A requirement written as an exact release is left alone. |
| `red pkg list [-v]` | What this project requires, and whether it is downloaded. `-v` adds commits and remotes. |
| `red pkg tidy` | Match `red.mod` to what the code actually imports: add what is missing, re-mark what is no longer direct. |
| `red pkg vendor` | Copy every dependency into `vendor/`. |
| `red pkg where <path>` | Print the file an import resolves to. The first thing to run when an import is not doing what you expect. |

## Transitive dependencies

A package can have a `red.mod` of its own. Fetching it fetches what it
requires, and those requirements are recorded in your `red.mod` too,
marked `indirect`. Flattening the graph into one list is what keeps
import-time resolution a single lookup rather than a graph walk, and it
means the set of versions a build uses is visible in one file.

A package with no `red.mod` simply has no dependencies, which is the
common case for a small library.

## Fetching

Everything goes through the `git` command line. That is deliberate: your
credential helpers, SSH keys, proxies and `insteadOf` rewrites already
work, and this tool does not have to learn about any of them.

A clone lands in a `.partial` directory next door and is moved into place
only once it is complete, so an interrupted download never leaves
something behind that looks finished.

## Publishing

There is nothing to do. Push the repository, tag a release:

```bash
git tag v1.0.0
git push --tags
```

Anyone can now `red pkg add github.com/you/yourproject@v1.0.0`.

Give the repository a `red.mod` whose `module` line matches the path
people will import it by — that is what lets its own internal imports
work when it is fetched into somebody else's cache.
