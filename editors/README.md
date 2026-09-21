# Editor support

Syntax highlighting for Red. There is no language server; these are
grammars, and they cover keywords, strings with interpolation, numbers
including the hex form, comments including nested block comments, and the
built-in functions.

## Vim and Neovim

```bash
mkdir -p ~/.vim/syntax ~/.vim/ftdetect
cp editors/red.vim ~/.vim/syntax/red.vim
echo 'autocmd BufRead,BufNewFile *.red set filetype=red' > ~/.vim/ftdetect/red.vim
```

For Neovim, use `~/.config/nvim/` in place of `~/.vim/`.

## VS Code

[`red.tmLanguage.json`](red.tmLanguage.json) is a TextMate grammar. A
minimal extension is the grammar plus a `package.json`:

```json
{
  "name": "red-language",
  "version": "0.1.0",
  "engines": { "vscode": "^1.60.0" },
  "contributes": {
    "languages": [{
      "id": "red",
      "extensions": [".red"],
      "configuration": "./language-configuration.json"
    }],
    "grammars": [{
      "language": "red",
      "scopeName": "source.red",
      "path": "./red.tmLanguage.json"
    }]
  }
}
```

Put those two files in `~/.vscode/extensions/red-language/` and restart.

## Sublime Text and anything else TextMate based

The same `red.tmLanguage.json` works. Put it somewhere on the packages
path and associate it with `.red`.

## The language server

[`tools/red-lsp.red`](../tools/red-lsp.red) speaks the Language Server
Protocol over standard input and output, so any editor that can start a
command and talk LSP to it will work. It is written in Red.

| | |
|---|---|
| Diagnostics | the compiler's own errors, as you type |
| Document symbols | functions, classes, enums, top level bindings |
| Go to definition | within the file |
| Hover | the declaring line, or what a builtin is for |
| Completion | the builtins, the keywords, and what the file declares |

Start it with `red tools/red-lsp.red`.

### Neovim

```lua
vim.api.nvim_create_autocmd("FileType", {
  pattern = "red",
  callback = function()
    vim.lsp.start({
      name = "red",
      cmd = { "red", "/path/to/red/tools/red-lsp.red" },
      root_dir = vim.fs.dirname(vim.fs.find({ ".git" }, { upward = true })[1]),
    })
  end,
})
```

### VS Code

The grammar above handles highlighting. For the rest, the extension
needs a client; `vscode-languageclient` started with the same command
is all it takes.

### Anything else

Helix, Emacs with `eglot`, Kate and Sublime with LSP all take a command
and a file type, and that is the whole configuration.

## What is not here

Nothing that needs to know types, or to follow a name into another file.
Both want a compiler that hands back a tree, and Red's throws one away as
it goes: it reads a token, emits bytecode, and forgets. Adding a tree for
the language server's benefit would mean a second front end to keep in
step with the first, which is the thing
[docs/bootstrapping.md](../docs/bootstrapping.md) spends its time
avoiding.

There is no formatter here either, because `red fmt` is built in.
