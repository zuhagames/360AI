# 360AI 2.2 - A model library on your Xbox

Browse and download compatible candidate GGUF models directly from Hugging Face.

- **Open Models and press Y**, or type `find models` in Assistant.
- **Search and filter:** public GGUF text-generation repositories, parameter presets (200M / 500M / 1B / 3B / Any), candidate/all-family toggle and next-page browsing. Search accepts a model name or exact owner/repository. Parameter filters use Hub metadata; unknown counts are hidden unless Any is selected.
- **Inspect individual files:** quantizations, combined split-file size, architecture, license label and required free space.
- **Size labels:** under 200 MB = green Comfortable; 200-300 MB = yellow Tight; 300 MB or more = orange Disk stream. These describe file size, not guaranteed RAM fit or speed. Compatibility is shown separately.
- **Download on Xbox:** pinned repository revisions, free-space checks, TLS, LFS SHA-256 verification on disk, FATX-safe names and cancellable streaming to the drive. Models keep a source receipt with revision, file names and hashes. No PC bridge or website is added.
- **Split models:** download all shards together, publish the first shard last and reuse matching previously completed shards. A single file must be smaller than 4 GiB; a download group is capped at 32 GiB. Incomplete groups, unavailable checksums and unsupported formats are marked.
- **Use your download:** completed models appear in the local list. Selecting a larger downloaded model chooses HDD streaming in setup; it uses the existing 8 MB weight cache. Working memory, tokenizer and KV cache still need RAM.
- **Debugging:** bounded GGUF metadata allocations, guarded paths, preservation of existing files, partial-file cleanup, selectable size colors and orderly download shutdown before an app-update restart.

**This does not make every Hugging Face model runnable.** The engine supports compatible dense Llama, SmolLM2, Qwen2/2.5 and Gemma 2/3 GGUFs. Unsupported architectures such as GLM/MoE and unsupported quantizations remain unsupported. Larger models may be extremely slow or fail memory/tokenizer checks at load time. Account login and gated/private-model downloads are not implemented.

**Already on 2.1?** Open **START on Home → Updates** to get the small app-only update. The existing 2.1 installer is compatible. For a fresh installation or 2.0/older, use **360AI-2.2-USB.zip**; all four bundled models remain included. Models, notes, settings and custom rules are preserved by app-only updates.

Validation: Xbox SDK app/installer builds; native Hub/parser/controller tests; offline checksum/cancellation/malformed-model/existing-file tests; updater/inference/protocol/tokenizer/package regressions; live native HTTPS search and a verified TinyStories download. Physical-console networking, large downloads, FATX behavior and new-model inference still need hardware testing.

Made by **ZuhaGames & Canyons**. Forked from [Canyons' gemma.xex](https://gitlab.com/canyons0/gemma.xex). AI tools assisted development, debugging and documentation.
