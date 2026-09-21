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

## What is not here

No language server, so no completion, no go-to-definition and no
inline errors. `red <file>` reports the first error with a line number,
which is the whole of the tooling for now.
