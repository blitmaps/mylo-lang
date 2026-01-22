# Installation Instructions
To install these files, follow these steps depending on whether you use Vim or Neovim.

## For Vim Users
Create the directories:

```bash
mkdir -p ~/.vim/ftdetect
mkdir -p ~/.vim/syntax
```
### Create the files:

Copy the code from ftdetect/mylo.vim into ~/.vim/ftdetect/mylo.vim.

Copy the code from syntax/mylo.vim into ~/.vim/syntax/mylo.vim.

Restart Vim: Open any .mylo file (e.g., vim example.mylo), and you should see syntax highlighting immediately.

## For Neovim Users
Create the directories:

```
mkdir -p ~/.config/nvim/ftdetect
mkdir -p ~/.config/nvim/syntax
```
### Create the files:

Copy the code from ftdetect/mylo.vim  into ~/.config/nvim/ftdetect/mylo.vim.

Copy the code from syntax/mylo.vim into ~/.config/nvim/syntax/mylo.vim.

Restart Neovim: Open any .mylo file, and syntax highlighting will be active.
