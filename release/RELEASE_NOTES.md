# 360AI 2.4 - Your own instant replies

Personalize the offline assistant from a native controller screen, without loading a language model.

- **Saved answers:** open Home → Y Quick tools → Saved answers, or type `open saved answers`.
- **Add or edit:** X asks for a phrase and then its reply; A edits an existing response. Duplicate phrases point to the existing entry. Cancelling either keyboard keeps saved data unchanged.
- **Preview matches:** click the right stick and enter a sentence. The preview shows the longest matching saved phrase and its literal reply, without executing any command. Chat still gives built-in commands priority; the preview covers saved text only.
- **Manage safely:** Y confirms deletion; START undoes the latest saved-data change. LB/RB scroll long replies. Existing taught answers appear automatically, with the same limits and background save/backup system.
- **Debugging:** bounded input, valid UTF-8/control checks, capacity tests, cancelled drafts, duplicate phrases, literal command-like/multiline replies, preview isolation, delete/cancel/undo and persistence round-trips.

**Already on 2.1–2.3?** Open **START on Home → Updates** for the small app-only download. For a first installation or 2.0/older, extract **360AI-2.4-USB.zip** and copy its entire `360AI` folder to your drive. All four model weights and their notices remain included. App-only updates preserve your models, notes, answers, settings and custom rules.

Saved answers are explicit text replies, not neural training or general reasoning. This release does not change GGUF inference speed or add model architectures.

Validation: native assistant/Hub/engine/updater/controller checks, Xbox SDK app/installer builds, tokenizer/protocol/package regressions, native UI captures, complete model/package checksums and an actual previous-version installer/recovery trial. Physical Xbox UI, keyboard, persistence, networking and inference remain hardware validation tasks.

Made by **ZuhaGames & Canyons**. Forked from [Canyons' gemma.xex](https://gitlab.com/canyons0/gemma.xex). AI tools assisted development, debugging and documentation.
