
if has('popuplist')
   if has('gui_running')
      highlight PulsNormal          guibg=LightMagenta guifg=Black gui=NONE
      highlight PulsSelected        guibg=Grey guifg=Black gui=NONE
      highlight PulsDisabled        guibg=LightMagenta guifg=White gui=NONE
      highlight PulsDisabledSel     guibg=Grey guifg=White gui=NONE
      highlight PulsTitleItem       guibg=LightMagenta guifg=Blue gui=NONE
      highlight PulsTitleItemSel    guibg=Grey guifg=Blue gui=NONE
      highlight PulsMarked          guibg=LightMagenta guifg=Red gui=NONE
      highlight PulsMarkedSel       guibg=Grey guifg=Red gui=NONE
      highlight PulsBorder          guibg=LightMagenta guifg=Magenta gui=NONE
      highlight link PulsScrollBar  PulsBorder
      highlight PulsScrollThumb     gui=reverse
      highlight PulsScrollSpace     gui=reverse
      highlight link PulsInput      PulsBorder
      highlight link PulsInputActive PulsSelected
      highlight PulsShortcut        guibg=LightMagenta guifg=Red gui=NONE
      highlight PulsShortcutSel     guibg=Grey guifg=Red gui=NONE
      highlight PulsHlFilter        guibg=Magenta guifg=Black gui=NONE
      highlight PulsHlSearch        guibg=Yellow  guifg=Black gui=NONE
      highlight PulsHlUser          guibg=White   guifg=DarkGreen gui=NONE
   else
      highlight PulsNormal          ctermbg=Magenta ctermfg=Black
      highlight PulsSelected        ctermbg=Grey ctermfg=Black
      highlight PulsDisabled        ctermbg=Magenta ctermfg=White
      highlight PulsDisabledSel     ctermbg=Grey ctermfg=White
      highlight PulsTitleItem       ctermbg=Magenta ctermfg=Blue
      highlight PulsTitleItemSel    ctermbg=Grey ctermfg=Blue
      highlight PulsMarked          ctermbg=Magenta ctermfg=Red
      highlight PulsMarkedSel       ctermbg=Grey ctermfg=Red
      highlight PulsBorder          ctermbg=Magenta ctermfg=Black
      highlight link PulsScrollBar  PulsBorder
      highlight PulsScrollThumb     ctermbg=Black ctermfg=Black
      highlight PulsScrollSpace     ctermbg=Black ctermfg=Black
      highlight link PulsInput      PulsBorder
      highlight link PulsInputActive PulsSelected
      highlight PulsShortcut        ctermbg=Magenta ctermfg=Red
      highlight PulsShortcutSel     ctermbg=Grey ctermfg=Red
      highlight PulsHlFilter        ctermbg=Cyan ctermfg=Black cterm=NONE
      highlight PulsHlSearch        ctermbg=Yellow  ctermfg=Black cterm=NONE
      highlight PulsHlUser          ctermbg=White   ctermfg=DarkGreen cterm=NONE
   endif
endif
