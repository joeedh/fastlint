add_subdirectory(fastlint)
add_subdirectory(cli)

if(FASTLINT_HAVE_DTL)
  add_subdirectory(testing)
  add_subdirectory(tests)
endif()
