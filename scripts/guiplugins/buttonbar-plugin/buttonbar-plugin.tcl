# this plugin creates a buttonbar on a patch window when that patch
# window is in Edit Mode

# this GUI plugin removes the menubars from any patch window that is
# not in Edit Mode.  Also, if a patch is switched to Run Mode, the
# menubar will be removed.

package require base64

proc make_pd_button {mytoplevel name} {
    button $mytoplevel.buttonbar.$name -image buttonimage$name \
        -relief flat -borderwidth 0 -highlightthickness 0 \
        -highlightcolor grey -highlightbackground grey -padx 0 -pady 0 \
        -command "menu_send_float \$::focused_window $name 0"
    pack $mytoplevel.buttonbar.$name -side left -padx 0 -pady 0
}

proc make_iemgui_button {mytoplevel name} {
    button $mytoplevel.buttonbar.$name -image buttonimage$name \
        -relief sunken -borderwidth 0 -highlightthickness 0 \
        -highlightcolor grey -highlightbackground grey -padx 0 -pady 0 \
        -command "menu_send \$::focused_window $name"
    pack $mytoplevel.buttonbar.$name -side left -padx 0 -pady 0
}

proc showhide_buttonbar {mytoplevel} {
    if { ! [winfo exists $mytoplevel.buttonbar]} {
        frame $mytoplevel.buttonbar -cursor arrow -background grey \
            -pady 0
        make_pd_button $mytoplevel obj
        make_pd_button $mytoplevel msg
        make_pd_button $mytoplevel floatatom
        make_pd_button $mytoplevel symbolatom
        make_pd_button $mytoplevel text
        make_iemgui_button $mytoplevel bng
        make_iemgui_button $mytoplevel toggle
        make_iemgui_button $mytoplevel numbox
        make_iemgui_button $mytoplevel hslider
        make_iemgui_button $mytoplevel vslider
        make_iemgui_button $mytoplevel hradio
        make_iemgui_button $mytoplevel vradio
        make_iemgui_button $mytoplevel vumeter        
        make_iemgui_button $mytoplevel mycnv
        make_iemgui_button $mytoplevel menuarray
    }
    if {$::editmode($mytoplevel)} {
        set tkcanvas [tkcanvas_name $mytoplevel]
        pack forget $tkcanvas
        pack $mytoplevel.buttonbar -side top -fill x
        pack $tkcanvas -side top -expand 1 -fill both
    } else {
        pack forget $mytoplevel.buttonbar
    }
}

bind PatchWindow <FocusIn> {+showhide_buttonbar %W}
bind PatchWindow <<EditMode>> {+showhide_buttonbar %W}

image create photo buttonimageobj -file $::current_plugin_loadpath/obj.gif
image create photo buttonimagemsg -file $::current_plugin_loadpath/msg.gif
image create photo buttonimagefloatatom -file $::current_plugin_loadpath/floatatom.gif
image create photo buttonimagesymbolatom -file $::current_plugin_loadpath/symbolatom.gif
image create photo buttonimagetext -file $::current_plugin_loadpath/text.gif

image create photo buttonimagebng -file $::current_plugin_loadpath/bng.gif
image create photo buttonimagetoggle -file $::current_plugin_loadpath/toggle.gif
image create photo buttonimagenumbox -file $::current_plugin_loadpath/numbox.gif
image create photo buttonimagehslider -file $::current_plugin_loadpath/hslider.gif
image create photo buttonimagevslider -file $::current_plugin_loadpath/vslider.gif
image create photo buttonimagehradio -file $::current_plugin_loadpath/hradio.gif
image create photo buttonimagevradio -file $::current_plugin_loadpath/vradio.gif
image create photo buttonimagevumeter -file $::current_plugin_loadpath/vumeter.gif
image create photo buttonimagemycnv -file $::current_plugin_loadpath/mycnv.gif

image create photo buttonimagemenuarray -file $::current_plugin_loadpath/menuarray.gif

