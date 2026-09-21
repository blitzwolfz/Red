" Vim syntax file for Red.
"
"   mkdir -p ~/.vim/syntax ~/.vim/ftdetect
"   cp editors/red.vim ~/.vim/syntax/red.vim
"   echo 'autocmd BufRead,BufNewFile *.red set filetype=red' \
"     > ~/.vim/ftdetect/red.vim

if exists("b:current_syntax")
  finish
endif

syn keyword redKeyword     and or not in as is
syn keyword redConditional if else switch case default
syn keyword redRepeat      for while
syn keyword redStatement   break continue return throw spawn import
syn keyword redException   try catch finally
syn keyword redDeclaration let const fun class enum
syn keyword redBoolean     true false
syn keyword redConstant    nil PI E
syn keyword redSelf        this super
syn keyword redType        Any Array Bool Error Fun Int Map Nil Num Set String

syn keyword redBuiltin print write eprint ewrite type type_of str repr num int len
syn keyword redBuiltin chr char set range input assert error exit
syn keyword redBuiltin abs floor ceil round sign sqrt pow exp log min max
syn keyword redBuiltin sin cos tan asin acos atan hypot rand rand_seed
syn keyword redBuiltin read_file write_file append_file open remove_file
syn keyword redBuiltin exists is_file is_dir file_size modified rename
syn keyword redBuiltin list_dir mkdir remove_dir
syn keyword redBuiltin args env set_env cwd source_path source_dir
syn keyword redBuiltin library_paths platform cpu_count time clock date
syn keyword redBuiltin format_time collect gc_info chan sleep
syn keyword redBuiltin tcp_listen tcp_connect ffi_open regex
syn keyword redBuiltin run shell which
syn keyword redBuiltin legacy legacy_output legacy_available

" A number, including the hex form.
syn match redNumber "\<0[xX]\x\+\>"
syn match redNumber "\<\d\+\%(\.\d\+\)\?\%([eE][-+]\?\d\+\)\?\>"

" Strings, with interpolation and escapes. The interpolated region is
" given the normal highlighting so that code inside it reads as code.
syn match  redEscape  contained "\\\%([nrt0\\\"$]\|u\x\{4}\|u{\x\{1,6}}\)"
syn region redInterp  contained matchgroup=redInterpDelim start="\${" end="}" contains=TOP
syn region redString  start=/"/ skip=/\\./ end=/"/ contains=redEscape,redInterp

syn match   redComment "//.*$" contains=redTodo
syn region  redComment start="/\*" end="\*/" contains=redComment,redTodo
syn keyword redTodo    contained TODO FIXME XXX NOTE

" A name straight after `fun`, `class` or `enum`.
syn match redFunction "\%(\<fun\s\+\)\@<=\h\w*"
syn match redType     "\%(\<\%(class\|enum\)\s\+\)\@<=\h\w*"

hi def link redKeyword     Keyword
hi def link redConditional Conditional
hi def link redRepeat      Repeat
hi def link redStatement   Statement
hi def link redException   Exception
hi def link redDeclaration Structure
hi def link redBoolean     Boolean
hi def link redConstant    Constant
hi def link redSelf        Identifier
hi def link redBuiltin     Function
hi def link redNumber      Number
hi def link redString      String
hi def link redEscape      SpecialChar
hi def link redInterpDelim SpecialChar
hi def link redComment     Comment
hi def link redTodo        Todo
hi def link redFunction    Function
hi def link redType        Type

let b:current_syntax = "red"
