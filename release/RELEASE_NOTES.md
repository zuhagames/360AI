# 360AI 1.4

Download **360AI-1.4-USB.zip**, extract it and copy the `360AI` folder to USB/HDD. Launch `360AI.xex` on your modded Xbox 360. **Gemma 3 270M and SmolLM2 135M weights are included.**

## What's new on Xbox

- **Reply presets:** select Reply preset in the Xbox Settings menu and press A to cycle Quick, Balanced and Creative. Quick uses context 256 and a 48-token reply cap; Balanced uses 512/128 and Creative uses 512/192. Presets also select sampling values and Auto compute/storage. Manual edits show Custom.
- **Top-p sampling:** an adjustable probability cutoff within the existing top-k pool, from 0.05 to 1.00. Set 1 to disable it. Greedy sampling still uses the highest adjusted logit.
- **Repetition control:** an adjustable 1.00-1.50 penalty discourages recently generated reply tokens. It considers the last 64 reply tokens and applies the penalty once per distinct token. Prompt tokens are not penalized. Set 1 to disable it.
- **Layer progress:** the console chat shows the current layer during prompt processing and reply generation, alongside token progress or decode speed.
- **More responsive Y cancellation:** stop checks now run between attention, feed-forward and layer operations. Completed reply text is retained. Partly evaluated tokens are excluded from reusable cache history.
- **Saved preferences:** the new controls persist beside the executable. Existing settings keep previous sampling behavior until you enable a new control or select a preset. Presets preserve your model selection, logging and chat/server toggles.

All new controls run in the Xbox app. Bundled models and their required notices are unchanged.

## Updating

Replace the executable and guides from the new ZIP. Existing weights may be kept. Settings, recovery copies and chat exports are not bundled, so updating preserves them.

For local chat, start with **SmolLM2 135M** and **Reply preset: Quick** for short replies. Balanced allows a longer conversation and reply budget. The app folder must be writable to save preferences and exports.

## Validation and limits

Xbox 360 SDK build and native tests pass, including preset round trips, old preferences, sampling ranges, nucleus cutoff math, repeat-penalty signs and duplicates, cancellation during prompt/decode layers, partial reply retention and cache recovery. All eight protocol/tokenizer/package groups pass. Both bundled models retain their greedy sample text with the new controls disabled; RAM and streamed outputs and complete KV caches match exactly.

Physical-console menus, controller behavior, sampling quality and performance still need hardware verification. Cancellation waits for the current matrix, disk read or GPU operation to finish. Quick reduces context/reply budgets; no increase in Xbox tokens per second is claimed. Large HDD models can still be extremely slow.

One complete USB ZIP includes the executable, both models, model catalog/downloader, guides and required licenses/notices.

Made by **ZuhaGames & Canyons**. A fork of [Canyons' gemma.xex](https://gitlab.com/canyons0/gemma.xex), expanded beyond Gemma. AI tools assisted with development, debugging and documentation.
