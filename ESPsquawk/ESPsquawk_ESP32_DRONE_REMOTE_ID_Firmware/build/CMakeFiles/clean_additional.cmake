# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "")
  file(REMOVE_RECURSE
  "bootloader\\bootloader.bin"
  "bootloader\\bootloader.elf"
  "bootloader\\bootloader.map"
  "config.html.S"
  "config\\sdkconfig.cmake"
  "config\\sdkconfig.h"
  "esp-idf\\mbedtls\\x509_crt_bundle"
  "esp-squawk-remoteID.bin"
  "esp-squawk-remoteID.map"
  "flash_app_args"
  "flash_bootloader_args"
  "flash_project_args"
  "flasher_args.json"
  "flasher_args.json.in"
  "ldgen_libraries"
  "ldgen_libraries.in"
  "mobile.html.S"
  "project_elf_src_esp32s3.c"
  "x509_crt_bundle.S"
  )
endif()
