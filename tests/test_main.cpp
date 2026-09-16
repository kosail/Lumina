// ---------------------------------------------------------------------------
// Single translation unit that provides doctest's main().
//
// Exactly one test source must define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN; every
// other test file includes <doctest/doctest.h> WITHOUT this macro and contributes
// only TEST_CASE definitions. Keeping them apart lets CMake compile several test
// files into one executable.
// ---------------------------------------------------------------------------

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
