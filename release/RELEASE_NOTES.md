# 360AI 2.3 - Less typing, easier model browsing

Find smaller local models and browse the native Hugging Face library with fewer keyboard entries.

- **Family presets:** press START in Model library to cycle SmolLM2, TinyStories, Qwen2.5, Llama and Gemma searches. Presets choose candidate filtering and a 200M/1B parameter limit; compatibility still depends on each GGUF.
- **Remembered searches:** search text, parameter limit and candidate filter survive restarts. Saved exact `owner/repository` searches reopen through repository details.
- **Cached browsing:** D-pad Left goes back; RB or D-pad Right goes forward. The last eight search pages stay in RAM, so revisiting them needs no network request. Changing the search/filter clears them. A failed next-page request keeps the current results available to retry.
- **Clearer downloads:** percentage, average transfer speed, approximate remaining transfer time and a progress bar. Verification remains a separate step; reused shards and local hashing do not inflate the network speed estimate.
- **Local model sorting:** LB switches smallest-first/name order while keeping the selected model. The preference is saved; list sorting now uses `std::sort`.
- **Regression coverage:** cache reuse/eviction, failed-page recovery, stale-query clearing, saved settings validation, transfer/reuse statistics and controller selection.

**Already on 2.1 or 2.2?** Open **START on Home → Updates** for the small app-only download. For a fresh installation or 2.0/older, extract **360AI-2.3-USB.zip** and copy its entire `360AI` folder to your drive. All four bundled model weights and their notices remain included. App-only updates preserve models, notes, settings and custom rules.

This update improves browsing and download feedback. It does not change model inference speed or add support for new architectures. HDD streaming still needs RAM and larger models can be extremely slow.

Validation: native assistant/engine/Hub/updater/controller checks, Xbox SDK app/installer builds, tokenizer/protocol/package regressions, UI captures and package/model checksums. Physical Xbox networking, large downloads, FATX behavior and inference remain hardware validation tasks.

Made by **ZuhaGames & Canyons**. Forked from [Canyons' gemma.xex](https://gitlab.com/canyons0/gemma.xex). AI tools assisted development, debugging and documentation.
