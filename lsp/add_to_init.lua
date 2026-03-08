-- !! ATTENTION !! See path to mylo-lsp below

-- Don't Autocomplete unless TAB or ENTER
vim.opt.completeopt = { "menu", "menuone", "noselect", "noinsert" }

-- Define the Mylo filetype natively if you haven't already
vim.filetype.add({ extension = { mylo = 'mylo' } })

-- Start the minimal Mylo LSP
vim.api.nvim_create_autocmd("FileType", {
  pattern = "mylo",
  callback = function()
    vim.lsp.start({
      name = 'mylo-lsp',
      -- !!!! ATTENTION !!!!
      -- Point this to the absolute path of your compiled executable
      cmd = { '/path/to/the/mylo-lsp' }, 
      root_dir = vim.fn.getcwd(),
    })
  end,
})

-- Auto-trigger native completion menu on typing
vim.api.nvim_create_autocmd("InsertCharPre", {
  pattern = "*.mylo",
  callback = function()
    -- Only trigger if we are typing a letter or underscore
    if vim.v.char:match("[%w_]") then
      -- Schedule the trigger for right after the character is inserted
      vim.schedule(function()
        local col = vim.fn.col('.') - 1
        local line = vim.fn.getline('.')
        local char_before = line:sub(col, col)
        
        -- If we are at the end of a word, trigger omnifunc
        if char_before:match("[%w_]") and vim.fn.pumvisible() == 0 then
          vim.api.nvim_feedkeys(
            vim.api.nvim_replace_termcodes("<C-x><C-o>", true, false, true),
            "n",
            true
          )
        end
      end)
    end
  end,
})
