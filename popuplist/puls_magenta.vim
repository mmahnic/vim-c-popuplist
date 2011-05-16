
if has('popuplist')
	highlight PulsNormal      		guibg=LightMagenta guifg=Black gui=NONE
	highlight PulsSelected  		guibg=Grey guifg=Black gui=NONE
	highlight PulsDisabled 			guibg=LightMagenta guifg=White gui=NONE
	highlight PulsDisabledSel 		guibg=Grey guifg=White gui=NONE
	highlight PulsBorder 			guibg=LightMagenta guifg=Magenta gui=NONE
	highlight link PulsScrollBar 	PulsBorder
	highlight PulsScrollThumb 		gui=reverse
	highlight PulsScrollSpace 		gui=reverse
	highlight link PulsInput   	PulsBorder
	highlight link PulsInputActive PulsSelected
	highlight PulsShortcut 			guibg=LightMagenta guifg=Red gui=NONE
	highlight PulsShortcutSel 		guibg=Grey guifg=Red gui=NONE
	highlight PulsHlFilter        guibg=Magenta guifg=Black gui=NONE
	highlight PulsHlSearch        guibg=Yellow  guifg=Black gui=NONE
	highlight PulsHlUser          guibg=White   guifg=DarkGreen gui=NONE
endif
