set(EXPECTED_FILES_COUNT "1")
set(EXPECTED_FILES_NAME_GENERATOR_SPECIFIC_FORMAT TRUE)
set(EXPECTED_FILE_CONTENT_1_LIST "/usr;/usr/empty")

if(PACKAGING_TYPE STREQUAL "COMPONENT")
  set(EXPECTED_FILE_1_COMPONENT "test")
endif()
