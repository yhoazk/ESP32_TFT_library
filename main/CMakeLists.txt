set(SOURCES tft_demo.c mqtt_handler.c  helpers.c wifi_setup.c)
idf_component_register(
        SRCS ${SOURCES}
        INCLUDE_DIRS
          ${EXTRA_COMPONENT_DIRS}
          ${CMAKE_CURRENT_LIST_DIR}
          $ENV{IDF_PATH}/components
        REQUIRES
            tft
            spiffs
            # Adds mqtt libraries
            mqtt
)
