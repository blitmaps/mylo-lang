" Vim syntax file
" Language: Mylo
" Maintainer: Mylo User
" Latest Revision: 2026

if exists("b:current_syntax")
  finish
endif

" Keywords
"
syn keyword myloKeyword var struct enum fn mod import module_path ret C
syn keyword myloConditional if else
syn keyword myloRepeat for in break continue
syn keyword myloBoolean true false

" Standard Library Functions
"
" Note: 'contains' is separated because it is a reserved Vim syntax keyword
syn keyword myloStdFunc print len sqrt to_string to_num
syn keyword myloStdFunc read_lines write_file read_bytes write_bytes
syn match myloStdFunc "\<contains\>" 

" Types
"
syn keyword myloType num str bool arr any

" Comments
"
syn match myloComment "//.*$"

" Strings
"
syn region myloString start='"' end='"' skip='\\"'
syn region myloFString start='f"' end='"' skip='\\"' contains=myloInterpolation

" String Interpolation for F-Strings
syn region myloInterpolation matchgroup=myloInterpDelim start="{" end="}" contained containedin=myloFString

" Numbers
"
syn match myloNumber "\<\d\+\>"
syn match myloNumber "\<\d\+\.\d\+\>"

" Operators and Delimiters
syn match myloOperator "\v\+"
syn match myloOperator "\v\-"
syn match myloOperator "\v\*"
syn match myloOperator "\v\/"
syn match myloOperator "\v\="
syn match myloOperator "\v:\="
syn match myloOperator "\v\:\:"
syn match myloOperator "\v\->"
syn match myloOperator "\v\.\.\."

" Structure Names (Heuristic: Capitalized words are usually Structs/Enums)
"
syn match myloStructure "\<[A-Z][a-zA-Z0-9_]*\>"

" Linking groups to standard Vim highlighting
hi def link myloKeyword Statement
hi def link myloConditional Conditional
hi def link myloRepeat Repeat
hi def link myloBoolean Boolean
hi def link myloComment Comment
hi def link myloString String
hi def link myloFString String
hi def link myloNumber Number
hi def link myloType Type
hi def link myloStdFunc Function
hi def link myloOperator Operator
hi def link myloStructure Structure
hi def link myloInterpDelim Special
hi def link myloInterpolation Normal

let b:current_syntax = "mylo"
