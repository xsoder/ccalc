augroup ccalc_filetype
  autocmd!
  autocmd BufRead,BufNewFile *.calc setfiletype ccalc
augroup END

function! s:SetupSyntax()
  if exists("b:current_syntax")
    return
  endif

  syntax clear


  syntax keyword ccalcKeyword if else while lambda const import return break continue True False struct
  syntax keyword ccalcKeyword const import link extern self @ os
  syntax keyword ccalcBoolean True False
  syntax keyword ccalcNull None
  syntax keyword ccalcBuiltin print type len range tuple help assert test
  syntax keyword ccalcBuiltin int double str bool is_error is_null
  syntax keyword ccalcBuiltin ptr_to_int int_to_ptr
  syntax keyword ccalcFFIType int double string void ptr long float char bool
  syntax match ccalcOperator "\v\*\*"
  syntax match ccalcOperator "\v\+"
  syntax match ccalcOperator "\v-"
  syntax match ccalcOperator "\v\*"
  syntax match ccalcOperator "\v/"
  syntax match ccalcOperator "\v\%"
  syntax match ccalcOperator "\v\="
  syntax match ccalcOperator "\v\=\="
  syntax match ccalcOperator "\v\!\="
  syntax match ccalcOperator "\v\<"
  syntax match ccalcOperator "\v\>"
  syntax match ccalcOperator "\v\<\="
  syntax match ccalcOperator "\v\>\="
  syntax match ccalcNumber "\v<\d+>"
  syntax match ccalcNumber "\v<\d+\.\d+>"
  syntax match ccalcNumber "\v<\.\d+>"
  syntax match ccalcFloat "\v<\d+\.\d+([eE][+-]?\d+)?>"
  syntax match ccalcHex "\v<0x[0-9a-fA-F]+>"
  syntax match ccalcBinary "\v<0b[01]+>"
  syntax region ccalcString start='"' end='"' skip='\\"' contains=ccalcEscape
  syntax region ccalcString start="'" end="'" skip="\\'" contains=ccalcEscape
  syntax match ccalcEscape "\\[ntr\\']" contained
  syntax match ccalcComment "#.*$" contains=ccalcTodo
  syntax keyword ccalcTodo TODO FIXME XXX NOTE contained
  syntax match ccalcFunction "\v<\w+>\s*\ze\("
  syntax match ccalcDelimiter "[\[\]{}(),;:.]"
  syntax match ccalcLambda "\v<lambda>"
  syntax match ccalcArrow "\v-\>"

  syntax keyword ccalcError Error error

  highlight default link ccalcKeyword Keyword
  highlight default link ccalcBoolean Boolean
  highlight default link ccalcNull Constant
  highlight default link ccalcBuiltin Function
  highlight default link ccalcFFIType Type
  highlight default link ccalcOperator Operator
  highlight default link ccalcNumber Number
  highlight default link ccalcFloat Float
  highlight default link ccalcHex Number
  highlight default link ccalcBinary Number
  highlight default link ccalcString String
  highlight default link ccalcEscape SpecialChar
  highlight default link ccalcComment Comment
  highlight default link ccalcTodo Todo
  highlight default link ccalcFunction Function
  highlight default link ccalcDelimiter Delimiter
  highlight default link ccalcLambda Special
  highlight default link ccalcArrow Operator
  highlight default link ccalcError Error

  let b:current_syntax = "ccalc"
endfunction

function! GetCcalcIndent(lnum)
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

  setlocal indentexpr=GetCcalcIndent(v:lnum)
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

augroup ccalc_settings
  autocmd!
  autocmd FileType ccalc call s:SetupSyntax()
  autocmd FileType ccalc call s:SetupIndent()
  autocmd FileType ccalc call s:SetupFiletype()
augroup endif
