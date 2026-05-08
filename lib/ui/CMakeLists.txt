SET(SOURCES screens/ui_dashboardscreen.c
    screens/ui_configscreen.c
    screens/ui_statusbarscreen.c
    ui.c
    components/ui_comp_hook.c
    ui_helpers.c
    ui_events.c
    fonts/ui_font_BarlowCondensedBold200.c
    fonts/ui_font_BarlowCondensedBold60.c
    fonts/ui_font_BarlowCondensedBold48.c
    fonts/ui_font_BarlowCondensedBold32.c
    fonts/ui_font_BarlowCondensedBold22.c)

add_library(ui ${SOURCES})
