# nukepanel skip intro: the stock window and background, no logo groups (docs/re/quick-wins.md section 1).
# ui_hdl_intro finds no group 1, hides itself on its first update, and the hide starts the main menu.
# Mounted only by nukepanel's winmm.dll while Settings > Skip intro is on.
spawn ui_window {
	csystem:"0 0 640 480"
	coords:"0 0 640 480"

	# background
	spawn ui_text {
		csystem:"0 0 640 480"
		coords:"0 0 640 480"
		text:"<IMG SRC=ui/shd_global/black_background.shd XSCALE=REPEAT YSCALE=REPEAT>"
	}
}
