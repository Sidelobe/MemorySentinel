#!/bin/bash

cd -- "$(dirname "$BASH_SOURCE")"

rm -rf build/coverage
mkdir -p build
mkdir -p build/coverage
cd build/coverage

#find . -maxdepth 10 -type f  \( -name \*.gcno -o -name \*.gcda \) -delete
 
cmake -DCMAKE_BUILD_TYPE=Debug -DCODE_COVERAGE=yes ../../..
make -j 2
./MemorySentinelTest

cd ..
mkdir -p coverage-report

# gcovr must use a gcov that matches the compiler which generated the coverage data
# - AppleClang / Clang: llvm-cov in gcov-compatibility mode
# - GCC: the version-suffixed gcov (e.g. gcov-14), since the unsuffixed one may belong to another GCC
if [ -z "$GCOV_EXE" ]; then
  CXX_EXE="${CXX:-c++}"
  if "$CXX_EXE" --version 2>/dev/null | grep -qi clang; then
    GCOV_EXE="llvm-cov gcov"
  else
    GCC_MAJOR=$("$CXX_EXE" -dumpfullversion -dumpversion 2>/dev/null | cut -d. -f1)
    if command -v "gcov-$GCC_MAJOR" > /dev/null 2>&1; then
      GCOV_EXE="gcov-$GCC_MAJOR"
    else
      GCOV_EXE="gcov"
    fi
  fi
fi
echo "Using gcov executable: $GCOV_EXE"

#REPORT_TOOL="LCOV"
REPORT_TOOL="GCOVR"

if [ "$REPORT_TOOL" == "LCOV" ]; then
  FILE=coverage-report/index.html
  lcov --zerocounters
  lcov --directory coverage/CMakeFiles/MemorySentinelTest.dir/ --directory ../../source  \
       --no-external --rc lcov_branch_coverage=1 --capture --output-file coverage.info
  genhtml --rc genhtml_branch_coverage=1 coverage.info -o coverage-report
fi

if [ "$REPORT_TOOL" == "GCOVR" ]; then
  FILE=coverage-report/coverage.htm
  gcovr -r ../.. -f ../../source --gcov-executable "$GCOV_EXE" --exclude-unreachable-branches --exclude-throw-branches --html-details -o $FILE
fi

if [ -f "$FILE" ]; then
  echo "HTML report generated in build/$FILE"
  open $FILE
else
  echo "ERROR: HTML report not generated"
fi

exit
