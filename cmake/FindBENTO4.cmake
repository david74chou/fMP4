find_path(
    BENTO4_INCLUDE_DIR
    NAMES ap4/Ap4.h
)
set(BENTO4_INCLUDE_DIRS ${BENTO4_INCLUDE_DIR})

find_library(
    BENTO4_LIBRARY
    NAMES ap4
)
set(BENTO4_LIBRARIES ${BENTO4_LIBRARY})

include(FindPackageHandleStandardArgs)

# handle the QUIETLY and REQUIRED arguments and set BENTO4_FOUND to TRUE
# if all listed variables are TRUE
find_package_handle_standard_args(BENTO4 DEFAULT_MSG BENTO4_LIBRARY BENTO4_INCLUDE_DIR)
