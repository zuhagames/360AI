# 360AI 1.5

Download **360AI-1.5-USB.zip**, extract it and copy the `360AI` folder to USB/HDD. Launch `360AI.xex` on your modded Xbox 360. All four models and their required notices are included. This update runs in the native Xbox app.

## Smaller models for less waiting

- **TinyStories 15M Q4_0 — 19 MB:** a much smaller English story-completion toy. Try `Once upon a time`. It is very limited and has a 128-token context; it is not an instruction/chat assistant.
- **TinyStories 260K F32 — 1.2 MB:** an even smaller toy for experimenting with minimal computation. Expect nonsense and poor answers.
- **SmolLM2 135M** remains the recommended chat model, with **Gemma 3 270M** also included. Both existing model files are unchanged.

TinyStories uses plain text continuation instead of Llama Chat instruction markers. The model menu and keyboard describe this mode, and the cache respects each model's context limit. Model downloads are pinned and checksummed; TinyStories includes MIT license and provenance notices.

## Native performance changes

- Fresh installs use **256 context / 48 reply tokens** and highlight SmolLM2. Shorter replies reduce total waiting. Existing saved preferences are preserved; select **Reply preset: Quick** to apply shorter budgets and Auto compute/storage.
- A paired-row Q8 CPU candidate shares input-vector loads. Each input width gets a one-time numerical/timing check against the original kernel; paired rows are selected only when they measure at least **10% faster**. Otherwise the original kernel stays active. Initial calibration adds work once per shape.
- F32 matrices on Xenon swap GGUF floats directly in VMX registers, avoiding a full scratch-row conversion. Float dot products use independent accumulators.
- Keep **compute/storage Auto**. Unnecessary HDD streaming and forced GPU uploads/readbacks can add substantial latency to every token.

## Validation and updating

Xbox 360 SDK build, native checks and all eight protocol/tokenizer/package groups pass. Tests cover raw story formatting, model context limits, distinct quantized rows and float matrix tails. Real GGUF generation, RAM/streamed output parity and conversation cache reuse are checked on the host.

**Actual Xbox response times and VMX speed still require console testing.** No tokens/second multiplier is promised. TinyStories reduces model workload by sacrificing most capabilities; it does not offer SmolLM2-quality chat. Large HDD models remain extremely slow.

Copy the entire new folder over the previous installation. Settings, backup settings and chat exports are not bundled. Choose Quick after updating if old settings still use a large context, long replies, forced GPU or HDD streaming.

One USB ZIP includes the executable, four models, model catalog/downloader, guides and required notices. Made by **ZuhaGames & Canyons**. A fork of [Canyons' gemma.xex](https://gitlab.com/canyons0/gemma.xex), expanded beyond Gemma. AI tools assisted with development, debugging and documentation.
