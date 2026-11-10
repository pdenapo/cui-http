# Tests

```sh
./tests/run_tests.sh   # 25 checks, all must pass
```

The suite spins up a small vibe.d REST API (`tests/server/`) and drives the
CLI through every HTTP verb, body/header round trips, status codes, latency,
history persistence (cross-process), `--db`, single-row delete, clear, and
GUI theme/language persistence (first-frame wiring).