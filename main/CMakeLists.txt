idf_component_register(
    SRCS "mic_app.c" "mic_protocol.c" "mic_ui.c" "mic_ble.c" "mic_adpcm.c"
         "mic_clips.c" "mic_lines.c" "mic_screen.c"
         "../assets/fonts/mic_font_16.c" "../assets/fonts/mic_font_20.c"
    INCLUDE_DIRS "."
    EMBED_FILES "../assets/images/mic_clips.bin"
    REQUIRES bsp esp_timer esp_driver_usb_serial_jtag bt nvs_flash
)
