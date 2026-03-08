# Mylo LSP

Mylo LSP provides code completion for the core language, variables etc. This uses the Language
Server Protocol.

## Building LSP

Building from the root of mylo-lang:

```bash
gcc -o mylo-lsp lsp/mylo_lsp.c src/compiler.c src/vm.c src/utils.c src/debug_adapter.c src/mylolib.c -Isrc -lm
```

This produces `mylo-lsp`

## Installing with NeoVim

Copy the whole of `add_to_init.lua` to `~/.config/nvim/init.lua` to enable LSP, and make sure to
*change the path to mylo-lsp*
