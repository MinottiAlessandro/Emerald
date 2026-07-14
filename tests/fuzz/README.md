# Security fuzzing

The `emerald_fuzz_core` harness sends untrusted note text through Emerald's
wiki-link, filename, mascot-header, and external-URL parsers under libFuzzer,
AddressSanitizer, and UndefinedBehaviorSanitizer.

Build and run it locally with Clang:

```sh
cmake -S . -B build-fuzz \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DEMERALD_BUILD_FUZZ_TESTS=ON
cmake --build build-fuzz --target emerald_fuzz_core -j
mkdir -p /tmp/emerald-fuzz-corpus
./build-fuzz/emerald_fuzz_core /tmp/emerald-fuzz-corpus \
  -max_total_time=60 -max_len=65536
```

Crashing inputs are written to the current directory. Keep a minimized
reproducer as a regression test before fixing the underlying bug.
