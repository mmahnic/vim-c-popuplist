
if has('popuplist')
   if has('gui_running')
      function s:hi(name, fg, bg)
         exec 'highlight ' . a:name . ' guibg=' . a:bg . ' guifg=' . a:fg
      endfunc
      let s:c = {}
      let s:c.bg      = "DarkGrey"
      let s:c.fg      = "Black"
      let s:c.bgsel   = "Black"
      let s:c.fgsel   = "White"
      let s:c.fgdis   = "DarkGrey"
      let s:c.fgtitle = "Blue"
      let s:c.fgttlse = "Cyan"
      let s:c.fgmark  = "Yellow"
      let s:c.fgshrt  = "Red"
      let s:c.fgframe = "Black"

      call s:hi("PulsNormal",           s:c.fg, s:c.bg)
      call s:hi("PulsSelected",         s:c.fgsel, s:c.bgsel)
      call s:hi("PulsDisabled",         s:c.fgdis, s:c.bg)
      call s:hi("PulsDisabledSel",      s:c.fgdis, s:c.bgsel)
      call s:hi("PulsTitleItem",        s:c.fgtitle, s:c.bg)
      call s:hi("PulsTitleItemSel",     s:c.fgttlse, s:c.bgsel)
      call s:hi("PulsMarked",           s:c.fgmark, s:c.bg)
      call s:hi("PulsMarkedSel",        s:c.fgmark, s:c.bgsel)
      call s:hi("PulsShortcut",         s:c.fgshrt, s:c.bg)
      call s:hi("PulsShortcutSel",      s:c.fgshrt, s:c.bgsel)
      call s:hi("PulsHlFilter",         s:c.fg, "Magenta")
      call s:hi("PulsHlSearch",         s:c.fg, "Yellow")
      call s:hi("PulsHlUser",           "DarkGreen", "White")

      call s:hi("PulsBorder",           s:c.fgframe, s:c.bg)
      highlight link PulsScrollBar      PulsBorder
      highlight PulsScrollThumb         gui=reverse
      highlight PulsScrollSpace         gui=reverse
      highlight link PulsInput          PulsBorder
      highlight link PulsInputActive    PulsSelected

      unlet s:c
      delfunc s:hi
   else
      function s:hi(name, fg, bg)
         exec 'highlight ' . a:name . ' ctermbg=' . a:bg . ' ctermfg=' . a:fg
      endfunc
      let s:c = {}
      let s:c.bg      = "Gray"
      let s:c.fg      = "Black"
      let s:c.bgsel   = s:c.fg
      let s:c.fgsel   = s:c.bg
      let s:c.fgdis   = "White"
      let s:c.fgtitle = "Blue"
      let s:c.fgmark  = "Red"
      let s:c.fgshrt  = "Red"
      let s:c.fgframe = "Black"

      call s:hi("PulsNormal",           s:c.fg, s:c.bg)
      call s:hi("PulsSelected",         s:c.fgsel, s:c.bgsel)
      call s:hi("PulsDisabled",         s:c.fgdis, s:c.bg)
      call s:hi("PulsDisabledSel",      s:c.fgdis, s:c.bgsel)
      call s:hi("PulsTitleItem",        s:c.fgtitle, s:c.bg)
      call s:hi("PulsTitleItemSel",     s:c.fgtitle, s:c.bgsel)
      call s:hi("PulsMarked",           s:c.fgmark, s:c.bg)
      call s:hi("PulsMarkedSel",        s:c.fgmark, s:c.bgsel)
      call s:hi("PulsShortcut",         s:c.fgshrt, s:c.bg)
      call s:hi("PulsShortcutSel",      s:c.fgshrt, s:c.bgsel)
      call s:hi("PulsHlFilter",         s:c.fg, "Cyan")
      call s:hi("PulsHlSearch",         s:c.fg, "Yellow")
      call s:hi("PulsHlUser",           "DarkGreen", "White")
      call s:hi("PulsBorder",           s:c.fgframe, s:c.bg)
      highlight link PulsScrollBar      PulsBorder
      highlight PulsScrollThumb         ctermbg=Black ctermfg=Black
      highlight PulsScrollSpace         ctermbg=Black ctermfg=Black
      highlight link PulsInput          PulsBorder
      highlight link PulsInputActive    PulsSelected

      unlet s:c
      delfunc s:hi
   endif
endif
