
#
# Main component of TF Micro project 'person_detection'.
#

idf_component_register(
    SRCS detection_responder.cc esp/image_provider.cc esp/main.cc main_functions.cc model_settings.cc person_model_int8/L2.cc esp/app_camera_esp.c preferences.cc image_util.c FtpClient.c esp_wifi_handler.cc
    INCLUDE_DIRS "")
