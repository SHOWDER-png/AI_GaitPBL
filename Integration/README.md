# Integration

Contracts, synthetic fixtures and scenarios for the full-system test (SCRUM-180).
Subtasks: SCRUM-348 (contracts), SCRUM-350 (fixtures and smoke tests), SCRUM-352 (scenarios).

| File | What it is |
|---|---|
| `INTERFACE_CONTRACTS.md` | Frozen interfaces (I1-I5), gaps outside the freeze (G1-G6), component versions |
| `contracts/frozen_contracts.json` | Machine-readable snapshot of the contracts |
| `TEST_SCENARIOS.md` | Smoke, functional, error and performance scenarios with status |
| `tools/contract_validators.py` | Reference validators (reusable by the receiver) |
| `tools/check_contracts.py` | Fails if the repo drifts from the snapshot |
| `tools/make_synthetic_fixtures.py` | Deterministic fixture generator |
| `fixtures/synthetic/` | 33 generated fixtures plus `expected.json` |
| `tests/test_component_smoke.py` | Smoke tests (standard library only) |

Run from the repository root, no hardware needed:

```
python3 -B Integration/tools/check_contracts.py
python3 -B -m unittest discover -s Integration/tests -v
python3 -B Integration/tools/make_synthetic_fixtures.py     # regenerate (output must not change)
python3 -B Integration/tools/contract_validators.py imu_csv Integration/fixtures/synthetic/imu_gap.csv
```
