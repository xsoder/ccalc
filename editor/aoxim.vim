augroup aoxim_filetype
  autocmd!
  autocmd BufRead,BufNewFile *.aoxim setfiletype aoxim
augroup END

function! s:SetupSyntax()
  if exists("b:current_syntax")
    return
  endif

  syntax clear


  syntax keyword aoximKeyword if else while lambda const import return break continue True False struct os
  syntax keyword aoximKeyword const import link extern self
  syntax keyword aoximBoolean True False
  syntax keyword aoximNull None
  syntax keyword aoximBuiltin print type len range tuple help assert test
  syntax keyword aoximBuiltin int double str bool is_error is_null
  syntax keyword aoximBuiltin ptr_to_int int_to_ptr
  syntax keyword aoximFFIType int double string void ptr long float char bool
  syntax match aoximOperator "\v\*\*"
  syntax match aoximOperator "\v\+"
  syntax match aoximOperator "\v-"
  syntax match aoximOperator "\v\*"
  syntax match aoximOperator "\v\@"
  syntax match aoximOperator "\v/"
  syntax match aoximOperator "\v\%"
  syntax match aoximOperator "\v\="
  syntax match aoximOperator "\v\=\="
  syntax match aoximOperator "\v\!\="
  syntax match aoximOperator "\v\<"
  syntax match aoximOperator "\v\>"
  syntax match aoximOperator "\v\<\="
  syntax match aoximOperator "\v\>\="
  syntax match aoximNumber "\v<\d+>"
  syntax match aoximNumber "\v<\d+\.\d+>"
  syntax match aoximNumber "\v<\.\d+>"
  syntax match aoximFloat "\v<\d+\.\d+([eE][+-]?\d+)?>"
  syntax match aoximHex "\v<0x[0-9a-fA-F]+>"
  syntax match aoximBinary "\v<0b[01]+>"
  syntax region aoximString start='"' end='"' skip='\\"' contains=aoximEscape
  syntax region aoximString start="'" end="'" skip="\\'" contains=aoximEscape
  syntax match aoximEscape "\\[ntr\\']" contained
  syntax match aoximComment "#.*$" contains=aoximTodo
  syntax keyword aoximTodo TODO FIXME XXX NOTE contained
  syntax match aoximFunction "\v<\w+>\s*\ze\("
  syntax match aoximDelimiter "[\[\]{}(),;:.]"
  syntax match aoximLambda "\v<lambda>"
  syntax match aoximArrow "\v-\>"

  syntax keyword aoximError Error error

  highlight default link aoximKeyword Keyword
  highlight default link aoximBoolean Boolean
  highlight default link aoximNull Constant
  highlight default link aoximBuiltin Function
  highlight default link aoximFFIType Type
  highlight default link aoximOperator Operator
  highlight default link aoximNumber Number
  highlight default link aoximFloat Float
  highlight default link aoximHex Number
  highlight default link aoximBinary Number
  highlight default link aoximString String
  highlight default link aoximEscape SpecialChar
  highlight default link aoximComment Comment
  highlight default link aoximTodo Todo
  highlight default link aoximFunction Function
  highlight default link aoximDelimiter Delimiter
  highlight default link aoximLambda Special
  highlight default link aoximArrow Operator
  highlight default link aoximDecorator Operator
  highlight default link aoximError Error

  let b:current_syntax = "aoxim"
endfunction

function! GetAoximIndent(lnum)
  let prevlnum = prevnonblank(a:lnum - 1)
  if prevlnum == 0
    return 0
  endif

  let prevline = getline(prevlnum)
  let curline = getline(a:lnum)
  let ind = indent(prevlnum)

  if prevline =~ '{\s*$'
    let ind = ind + shiftwidth()
  endif
  if curline =~ '^\s*}'
    let ind = ind - shiftwidth()
  endif
  if ind < 0
    let ind = 0
  endif

  return ind
endfunction

function! s:SetupIndent()
  if exists("b:did_indent")
    return
  endif
  let b:did_indent = 1

  setlocal indentexpr=GetAoximIndent(v:lnum)
  setlocal indentkeys=0{,0},0),!^F,o,O,e
  setlocal autoindent
  setlocal nosmartindent
endfunction

function! s:SetupFiletype()
  if exists("b:did_ftplugin")
    return
  endif
  let b:did_ftplugin = 1

  setlocal shiftwidth=4
  setlocal tabstop=4
  setlocal expandtab

  setlocal commentstring=#\ %s
  setlocal comments=:#
  setlocal formatoptions-=t
  setlocal formatoptions+=croql
  setlocal foldmethod=indent
  setlocal foldnestmax=2
  setlocal iskeyword+=_

  setlocal matchpairs+=<:>

  let b:undo_ftplugin = "setl sw< ts< et< cms< com< fo< fdm< fdn< isk< mps<"
endfunction

augroup aoxim_settings
  autocmd!
  autocmd FileType aoxim call s:SetupSyntax()
  autocmd FileType aoxim call s:SetupIndent()
  autocmd FileType aoxim call s:SetupFiletype()
augroup endif
