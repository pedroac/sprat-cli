# Test assets

Real-world sprites used by the asset regression test (`tests/assets_regression.py`,
ctest name `assets_regression`). Baselines live in `tests/baselines/assets/`.

| Asset | Author / source | Licence | Used for |
|---|---|---|---|
| `Streets of Fight files/` | Luis Zuno ([@ansimuz](https://ansimuz.itch.io/)) | Personal and commercial use, modification and redistribution allowed (see `public-license.txt`) | spratlayout / spratpack cases |
| `Crocodile Mascot Running Game/` | [bevouliin.com](http://bevouliin.com) | Free game asset; included notice does not state redistribution terms. **Confirm before publishing.** | spratlayout / spratpack cases |
| `d1c4mut-6db0241c-….png` | Unknown | **Unknown. Confirm source and terms before publishing.** | spratframes case |
| `da9l9w5-5db4007e-….png` | Unknown | **Unknown. Confirm source and terms before publishing.** | spratframes case |

## Running and updating

```sh
ctest --test-dir build -R assets_regression --output-on-failure

# After an intended change in output, regenerate and review the baselines:
scripts/update_asset_baselines.sh build
git diff tests/baselines

# Timings (cold cache, median of N runs) without failing on time:
python3 tests/assets_regression.py --spratlayout build/spratlayout \
    --spratpack build/spratpack --spratframes build/spratframes --bench 5
```

Every run writes the generated files to `tests/output/assets/` (gitignored) so they can be
checked by eye:

```
tests/output/assets/
├── SUMMARY.txt              status of every case + timings
├── croc-multipack/
│   ├── result.txt           layout, pixel hashes, metrics (same format as the baseline)
│   ├── atlas0.png …         packed atlases
│   └── status.txt           ok, or FAIL with the differences
└── frames-d1c4mut/
    ├── result.txt
    └── frames.png           source sheet with detected frames outlined in magenta
```

Each run replaces a case's folder. Set `SPRAT_ASSET_OUTPUT` or pass `--output-dir` to write
elsewhere. CI uploads the folder as an artifact when the tests fail.

Adding or removing images under a folder changes that folder's baselines.
