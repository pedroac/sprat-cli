# Contributing

`sprat-cli` is a community project. The repository owner started it and reviews requests,
but it is meant to be built together. Suggestions, pull requests, tests, documentation
fixes, and forks are welcome.

## What is likely to be accepted

- **Bug fixes** — especially with a regression test.
- **Tests and documentation** — clearer guides, corrected examples, new platform notes.
- **Small improvements** — better error messages, performance work, packaging fixes.
- **New transforms and presets** — engine/framework formats, example profiles.
- **GUI and engine integrations** — as companion projects or optional layers.

**New features should be discussed first.** Open an issue describing the problem and the
proposed behavior before writing a large change. That keeps the core small and avoids work
that cannot be merged. Breaking changes also need an issue first.

If a change does not fit the upstream scope, **forking is encouraged**. You do not need
permission to fork or to publish a companion project.

## Ways to help

- **Report a bug** — include the command you ran, the input files (or a minimal sample),
  the expected result, and the actual output. Mention your OS and the output of
  `spratlayout --version`.
- **Request a feature** — describe the use case first, then the proposed option or format.
- **Improve the docs** — fix a wrong command, add a worked example, or clarify a term for
  less technical users.
- **Add a test** — regression tests for bugs are especially valuable.
- **Review a pull request** — try the change, check the behavior, and comment on the code
  or docs.
- **Share a profile or transform** — real-world examples help other users.
- **Package the tools** — Linux packages, Homebrew, Scoop/Chocolatey, Arch/AUR, Nix, etc.

## Before you open a pull request

1. **Discuss new features first** in an issue.
2. Keep the change focused. Do not mix unrelated refactors with a bug fix.
3. Add or update tests for behavior changes. See `tests/CMakeLists.txt`.
4. Update the relevant docs in the same change:
   - `README.md` for the overview and quick start.
   - `docs/guides/` for practical examples.
   - `docs/reference/` for complete option lists.
   - `docs/advanced/` for format specifications.
   - `man/sprat-cli.1` and the affected tool's `--help` text.
5. Run the build and tests:

   ```sh
   cmake -S . -B build -DSPRAT_DOWNLOAD_STB=ON -DBUILD_TESTING=ON
   cmake --build build --parallel 4
   ctest --test-dir build --output-on-failure
   ```

6. Update `VERSION` before committing if the change affects a release.

## AI assistance

AI is used in this project:

- Documentation is reviewed and updated with AI agents.
- Pull requests may be reviewed by agents.
- A human maintainer makes the final decision.

If you use AI to prepare a contribution, you are welcome to do so. Please check the result
yourself: run the commands, read the generated text, and make sure the change is accurate.

## Review process

The owner and reviewers may ask for changes. Small fixes usually move quickly; new features
need more discussion. If a pull request goes quiet, a polite comment is fine. If the upstream
project declines a feature, you are free to keep it in a fork.

## Code and docs style

- C++20 and CMake. Follow the style of the surrounding code.
- Keep user-facing strings translatable.
- Prefer plain language in guides. Explain jargon the first time it appears.
- Keep commands copy-pasteable and show the expected result.
- Do not commit build output or generated test artifacts.

## Community expectations

Be respectful and assume good intent. Critique the change, not the person. Harassment and
personal attacks are not welcome.
