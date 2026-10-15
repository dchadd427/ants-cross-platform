# CMake generated Testfile for 
# Source directory: /home/user/start-countdown/tests/test_ai
# Build directory: /home/user/start-countdown/build_clang/tests/test_ai
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(test_ai "/home/user/start-countdown/build_clang/tests/test_ai/test_ai")
set_tests_properties(test_ai PROPERTIES  COST "120" _BACKTRACE_TRIPLES "/home/user/start-countdown/tests/test_ai/CMakeLists.txt;50;add_test;/home/user/start-countdown/tests/test_ai/CMakeLists.txt;0;")
add_test(test_ai_islands "/home/user/start-countdown/build_clang/tests/test_ai/test_ai_islands")
set_tests_properties(test_ai_islands PROPERTIES  _BACKTRACE_TRIPLES "/home/user/start-countdown/tests/test_ai/CMakeLists.txt;74;add_test;/home/user/start-countdown/tests/test_ai/CMakeLists.txt;0;")
add_test(test_ai_worker "/home/user/start-countdown/build_clang/tests/test_ai/test_ai_worker")
set_tests_properties(test_ai_worker PROPERTIES  COST "60" _BACKTRACE_TRIPLES "/home/user/start-countdown/tests/test_ai/CMakeLists.txt;104;add_test;/home/user/start-countdown/tests/test_ai/CMakeLists.txt;0;")
