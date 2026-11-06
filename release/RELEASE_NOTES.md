# 360AI 1.2

Download **360AI-1.2-USB.zip**, extract it and copy the `360AI` folder to USB/HDD. Launch `360AI.xex` on your modded Xbox 360. **Gemma 3 270M and SmolLM2 135M weights are included.**

## What's new

- **Remembered settings:** context, sampling, reply limit, compute/storage choices and chat/server toggles are saved beside the app. Saves replace files through a temporary file and preserve a valid recovery copy.
- **New chat without reloading:** press **X** while idle to clear the conversation and KV history while keeping the model loaded. An active API request reports busy.
- **Chat export:** press **START** while idle to save the current conversation as UTF-8 in `360AI-chat.txt`. This replaces the previous export; nothing is exported automatically.
- **Per-request API controls:** temperature, top-k, `max_tokens` or `max_completion_tokens`, and up to four stop strings. Overrides do not change saved settings.
- **Better streaming:** Unicode characters and stop strings can span tokens without broken output. Chat streams start with the assistant role, send prompt-progress comments, and cancel inference after a failed streaming send.
- **Useful API results:** accurate stop/length reasons, token usage, optional final streaming usage, unique request IDs and generation timings. `/health` includes defaults and the last API/MCP timing snapshot.
- **Client compatibility:** developer-role text maps to system messages for local templates. Invalid options and malformed UTF-8 JSON are rejected clearly.
- **Reliability:** synchronized chat snapshots and final replies, visible worker-start failures, and consistent generation-end reporting.

The 1.1.1 CPU kernels, core assignment, Auto compute/storage and history reuse remain included. No additional Xbox speedup is claimed for 1.2.

## Updating

Replace the executable and guides from the new ZIP. Existing weights are unchanged and may be kept. `360AI.json`, `360AI.json.bak` and exported conversations are not bundled, so updates preserve them. Delete both settings files if you want defaults.

For local chat, start with **SmolLM2 135M**, context **512**, **Compute: Auto** and **Storage: Auto**. The folder must be writable for settings and exports.

## Validation and limits

Built with the Xbox 360 SDK. Native checks cover settings round trips, damaged-file recovery, failed saves, Unicode split boundaries, malformed UTF-8, cross-token stop matching, cancellation and end reasons. HTTP tests cover request overrides, usage, stop sequences, stream role/usage chunks and validation in RAM and disk modes. All eight protocol/tokenizer/package groups pass.

Physical-console controller behavior, file persistence, GPU selection and performance still need hardware verification. The API remains a limited text-only compatible server; top-p, native tool calls and image inputs are not implemented. Large HDD models remain experimental and can be extremely slow.

The single USB ZIP includes the executable, both models, model catalog/downloader, guides and required licenses/notices.

Made by **ZuhaGames & Canyons**. A fork of [Canyons' gemma.xex](https://gitlab.com/canyons0/gemma.xex), expanded beyond Gemma. AI tools assisted with development, debugging and documentation.
