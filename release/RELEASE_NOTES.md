# 360AI 1.1.1 — performance patch

Download **360AI-1.1.1-USB.zip**, extract it and copy the `360AI` folder to your USB/HDD. Launch `360AI.xex`. Gemma 3 270M and SmolLM2 135M Q8_0 weights are included.

Start with **SmolLM2 135M**, context **512**, **Compute backend: Auto** and **Weight storage: Auto**.

## Changes

- **CPU matrix kernels:** direct Q4/Q5/Q8 calculations avoid repeatedly converting whole weight rows to floats. Xenon uses VMX operations; Q4_K/Q5_K decoding also does less repeated work.
- **CPU core assignment:** inference and its two matrix workers now run on separate physical cores. The old workers inherited the parent's hardware thread. Completion events replace polling/yielding.
- **Automatic backend selection:** compares CPU/GPU timing on a small real-weight matrix, checks numerical parity, and uses the GPU only when at least 20% faster. Manual CPU/GPU choices remain available.
- **Automatic weight storage:** keeps weights in RAM when model, context, scratch buffers and a reserve fit. HDD streaming remains available for larger models.
- **Conversation cache reuse:** matching history is reused instead of being computed again on every turn. Model/backend changes, edited prefixes, trimming and cancellation are handled explicitly.
- **Smaller disk reads:** one embedding lookup no longer causes an 8 MB disk read. Large matrix batches still use the bounded 8 MB cache.
- **Useful progress:** prompt token counts and elapsed processing time are shown. Decode speed excludes prompt processing, and one performance summary is logged per request.
- **Reply length control:** console replies default to 128 tokens, adjustable from 16 to 512. API limits are unchanged.

## Measurements and validation

With the same Windows host compiler, model weights, 16-token prompt and greedy output, SmolLM2's 12-token reply took **2.14 s versus 7.10 s** in 1.1. Gemma's same 3-token reply took **1.87 s versus 5.49 s**. These are host CPU measurements, not Xbox performance promises.

In follow-up tests, SmolLM2 reached the first token in **1.79 s cached versus 3.69 s fresh**, and Gemma in **1.76 s versus 3.15 s**. Output tokens and all valid KV entries matched exactly.

Qwen2.5 0.5B's same 10-token host reply improved from **30.37 s to 17.65 s** with the direct Q5 kernel. Its RAM and streamed outputs/KV match exactly; it remains much slower than the bundled models.

The Xbox SDK build, native numerical/cache tests and all eight protocol/tokenizer/package test groups pass. Tests cover signed Q8 values and alignments, prefix edits, cache clearing, model reloads, cancellation and context trimming. Physical-console VMX performance, core scaling and Auto selection still need validation. Larger HDD models remain experimental and can be extremely slow.

The USB ZIP is the only uploaded asset and includes both models, model downloader/catalog, guides and required model licenses/notices. For an existing installation, replace `360AI.xex` and guides from the new folder; the unchanged weights can be kept.

Made by **ZuhaGames & Canyons**. A fork of [Canyons' gemma.xex](https://gitlab.com/canyons0/gemma.xex), expanded beyond Gemma. AI tools assisted with development, debugging and documentation.
