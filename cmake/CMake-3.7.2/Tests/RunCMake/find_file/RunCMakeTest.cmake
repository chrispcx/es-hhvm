include(RunCMake)

if(WIN32 OR CYGWIN)
  run_cmake(PrefixInPATH)
endif()
