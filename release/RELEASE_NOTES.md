# 360AI 1.3

Download **360AI-1.3-USB.zip**, extract it and copy the `360AI` folder to USB/HDD. Launch `360AI.xex` on your modded Xbox 360. **Gemma 3 270M and SmolLM2 135M weights are included.**

## What's new

- **Chat scrollback:** D-pad up/down scrolls by line, LB/RB by page, and clicking the right stick returns to the newest text. Reading older messages pauses automatic following, even during a reply.
- **Remembered model selection:** the browser highlights the last selected model after restart when its file is still present. Existing 1.2 settings remain compatible. Models do not load automatically.
- **Safer long conversations:** keep recent complete turns in context instead of leaving an orphan assistant message. Reserve space for a reply using actual encoded prompt size, including template/BOS tokens.
- **Clear trimming notices:** oversized input is shortened at UTF-8 boundaries while retaining the model template. The full message and older history remain available in scrollback and chat export.
- **Less repeated work:** position angles are computed once per token and frequency table, then reused across attention heads and layers. Chat line wrapping is cached until text changes, and prompt fitting starts with recent history instead of repeatedly encoding the entire transcript.
- **Browser reliability:** simultaneous navigation and selection cannot move the selection outside the model list.

All previous API, MCP, saved-settings, export, compute/storage Auto and conversation-cache features remain included. Bundled models and their required notices are unchanged.

## Updating

Copy the new executable and guides over your existing installation. Existing weights may be kept. Settings, backups and exported chats are not bundled, so updating preserves them.

For local chat, start with **SmolLM2 135M**, context **512**, **Compute: Auto** and **Storage: Auto**. The app folder must be writable to save settings and exports.

## Validation and limits

Xbox 360 SDK build and native checks pass, including scroll/follow behavior, model preference compatibility, prompt budgets, complete-turn retention, Unicode trimming and exact cached/reference rotary-transform comparisons. All eight protocol/tokenizer/package groups pass. Both real bundled models produce the same greedy sample text as 1.2; their RAM and streamed outputs and complete KV caches match exactly.

Physical-console controller behavior and performance still need hardware verification. No overall Xbox speedup is claimed from the reduced rotary calculations. Large HDD models can still be extremely slow. The console font remains ASCII; unsupported characters display as placeholders, while exports retain UTF-8.

The release contains one complete USB ZIP, with the executable, both models, model catalog/downloader, guides and required licenses/notices.

Made by **ZuhaGames & Canyons**. A fork of [Canyons' gemma.xex](https://gitlab.com/canyons0/gemma.xex), expanded beyond Gemma. AI tools assisted with development, debugging and documentation.
