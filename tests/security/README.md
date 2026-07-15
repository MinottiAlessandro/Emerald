# Security regression tests

These QtCore-only tests cover Emerald's filesystem and Markdown trust
boundaries. Run the ordinary suite with:

```sh
cmake -S . -B build-security -DEMERALD_BUILD_SECURITY_TESTS=ON
cmake --build build-security --target emerald_security_tests -j
ctest --test-dir build-security --output-on-failure
```

For the same ASan/UBSan configuration used in CI, select Clang and enable the
instrumentation option:

```sh
cmake -S . -B build-security \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DEMERALD_BUILD_SECURITY_TESTS=ON \
  -DEMERALD_ENABLE_SANITIZERS=ON
cmake --build build-security --target emerald_security_tests -j
ctest --test-dir build-security --output-on-failure
```
