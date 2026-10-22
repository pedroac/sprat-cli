# Recreates an empty layout-cache folder for each test (cmake -P, run by the
# reset_test_cache fixture), so tests never reuse layouts that an earlier build of
# the same VERSION cached.
#   -DROOT=<dir> -DNAMES=<test1|test2|...>
if(NOT ROOT OR NOT NAMES)
    message(FATAL_ERROR "reset_test_cache.cmake needs -DROOT=... -DNAMES=...")
endif()
file(REMOVE_RECURSE "${ROOT}")
string(REPLACE "|" ";" _names "${NAMES}")
foreach(_name IN LISTS _names)
    file(MAKE_DIRECTORY "${ROOT}/${_name}")
endforeach()
