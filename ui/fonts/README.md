Generated fonts (LVGL 9 format).

font_montserrat_bold_22.c - Montserrat Bold 22 px, 4 bpp, ASCII + Latin-1.
Regenerate (Node):

    npm i lv_font_conv @fontsource/montserrat
    npx lv_font_conv --font node_modules/@fontsource/montserrat/files/montserrat-latin-700-normal.woff \
        --size 22 --bpp 4 --no-compress --range 0x20-0x7E,0xA0-0xFF --format lvgl \
        --lv-font-name font_montserrat_bold_22 --lv-include lvgl.h -o font_montserrat_bold_22.c

font_montserrat_bold_26.c - Montserrat Bold 26 px (place name in free-ride mode), same options with --size 26 and --lv-font-name font_montserrat_bold_26.

font_montserrat_bold_120.c - Montserrat Bold 120 px, digits, colon and space only (clock page): --size 120 --range 0x20,0x30-0x3A --lv-font-name font_montserrat_bold_120.

font_montserrat_bold_44.c - Montserrat Bold 44 px, letters N E S W only (compass cardinals): --size 44 --range 0x45,0x4E,0x53,0x57 --lv-font-name font_montserrat_bold_44.

360 px panel set (the layout scales by 360/412; these are the 412 fonts scaled and rounded): font_montserrat_bold_19.c (22 -> 19) and
font_montserrat_bold_23.c (26 -> 23), both ASCII + Latin-1; font_montserrat_bold_38.c (44 -> 38, N E S W only); font_montserrat_bold_105.c
(120 -> 105, digits, colon and space only). Same options as above with the new --size, --range and --lv-font-name. Built only when NAV_UI_SIZE is 360.

font_montserrat_bold_80.c (412 px) and font_montserrat_bold_70.c (360 px) - Montserrat Bold, digits, minus, point and space only (the tire pressures): --size 80 (70) --range 0x20,0x2D,0x2E,0x30-0x39 --lv-font-name font_montserrat_bold_80 (70).
