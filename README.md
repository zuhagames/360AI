<p align="center">
  <img src="docs/assets/360ai-logo.svg" alt="360AI" width="640">
</p>

<h3 align="center">AI on Xbox 360.</h3>

<p align="center">Chat locally, run GGUF models, and connect your apps.</p>

<p align="center">
  <a href="https://github.com/zuhagames/360AI/releases/latest"><img src="https://img.shields.io/github/v/release/zuhagames/360AI?style=flat-square&amp;label=release&amp;color=9acd32" alt="Latest release"></a>
  <a href="#installation"><img src="https://img.shields.io/badge/platform-Xbox%20360-107c10?style=flat-square" alt="Platform: Xbox 360"></a>
  <a href="https://github.com/zuhagames/360AI/releases"><img src="https://img.shields.io/github/downloads/zuhagames/360AI/total?style=flat-square&amp;label=downloads&amp;color=9acd32" alt="Total release downloads"></a>
  <a href="https://ko-fi.com/zuhagames"><img src="https://img.shields.io/badge/Support-Ko--fi-ff5e5b?style=flat-square&amp;logo=kofi&amp;logoColor=white" alt="Support on Ko-fi"></a>
</p>

<p align="center">
  <a href="https://github.com/zuhagames/360AI/releases/download/v1.5/360AI-1.5-USB.zip"><strong>Download 360AI 1.5</strong></a> ·
  <a href="docs/TECHNICAL.md">Documentation</a> ·
  <a href="https://github.com/zuhagames/360AI/issues">Help</a>
</p>

<p align="center"><sub>Includes SmolLM2, Gemma 3 and two tiny story models.</sub></p>

---

## About

360AI runs local language models on a modded Xbox 360. Chat on the console or use it as an inference server over your network. Local chat works offline.

Made by **ZuhaGames & Canyons**. A fork of [Canyons' gemma.xex](https://gitlab.com/canyons0/gemma.xex), expanded beyond Gemma.

AI tools were used to assist with development, debugging, and documentation.

## Installation

1. Download and extract **[360AI-1.5-USB.zip](https://github.com/zuhagames/360AI/releases/download/v1.5/360AI-1.5-USB.zip)**.
2. Copy the entire `360AI` folder to your USB drive.
3. Launch `360AI.xex` on your modded Xbox 360 with your XEX launcher.
4. Select a model, leave compute and weight storage on **Auto**, and select **Load model**.

Requires a console capable of running unsigned XEX files. No separate model download is needed.

## Models

| Model | Free disk space to reserve | Loading |
| --- | --- | --- |
| TinyStories 260K — included | 0.01 GB | RAM; very weak story toy |
| TinyStories 15M Q4_0 — included | 0.03 GB | RAM; simple story completion |
| SmolLM2 135M Q8_0 — included | 0.2 GB | RAM recommended |
| Gemma 3 270M Q8_0 — included | 0.4 GB | RAM recommended |
| Qwen2.5 0.5B Q4_K_M — optional | 1 GB | HDD stream |
| Qwen2.5 1.5B Q4_K_M — experimental | 2 GB | HDD stream |
| Qwen2.5 7B Q4_K_M — experimental | **5 GB on HDD** | HDD stream, extremely slow |

For speed, use **Auto compute/storage** and **Reply preset: Quick**. SmolLM2 is the recommended chat model. The much smaller [TinyStories models](https://huggingface.co/karpathy/tinyllamas) trade most capabilities for less work: they continue story openings like “Once upon a time,” and are not chat assistants. The 260K toy may produce nonsense; 15M has a 128-token context.

HDD streaming uses an **8 MB weight cache**; the tokenizer and KV cache still need RAM. Larger models are experimental and have not been benchmarked on an Xbox. See [model downloads and HDD setup](docs/TECHNICAL.md#hdd-streaming-and-model-downloads), or `MODELS.txt` in the ZIP.

## Features

- Console chat with controller and on-screen keyboard support.
- Saved settings and model selection, chat scrollback, new chat, and USB/HDD export.
- Long conversations keep recent complete turns in context and show trimming notices.
- Compatible Gemma 2/3, SmolLM2, dense Llama, and Qwen2/2.5 GGUF models.
- HDD/USB weight streaming, split GGUF support, and model disk-space descriptions.
- Automatic CPU/GPU selection, RAM loading when it fits, and conversation cache reuse.
- CPU kernel timing checks and short reply defaults (256 context, 48 reply tokens).
- Adjustable context size, temperature, and top-k sampling.
- On-console reply presets, top-p and repetition controls, and layer progress.
- OpenAI-compatible chat/completions API with streaming.
- MCP endpoint and an optional PC bridge for external MCP servers.
- Experimental Xenos pixel-shader matrix calculations with CPU fallback. No speedup is guaranteed.

## Documentation & support

See the **[technical guide](docs/TECHNICAL.md)** for model compatibility, API examples, MCP setup, GPU details, and build instructions.

[Report an issue](https://github.com/zuhagames/360AI/issues) · [Support development on Ko-fi](https://ko-fi.com/zuhagames)
