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
  <a href="https://github.com/zuhagames/360AI/releases/download/v1.0/360AI-1.0-USB.zip"><strong>Download 360AI 1.0</strong></a> ·
  <a href="docs/TECHNICAL.md">Documentation</a> ·
  <a href="https://github.com/zuhagames/360AI/issues">Help</a>
</p>

<p align="center"><sub>USB bundle includes Gemma 3 270M Instruct Q8_0 weights.</sub></p>

---

## About

360AI runs local language models on a modded Xbox 360. Chat on the console or use it as an inference server over your network. Local chat works offline.

Made by **ZuhaGames & Canyons**. A fork of [Canyons' gemma.xex](https://gitlab.com/canyons0/gemma.xex), expanded beyond Gemma.

AI tools were used to assist with development, debugging, and documentation.

## Installation

1. Download and extract **[360AI-1.0-USB.zip](https://github.com/zuhagames/360AI/releases/download/v1.0/360AI-1.0-USB.zip)**.
2. Copy the entire `360AI` folder to your USB drive.
3. Launch `360AI.xex` on your modded Xbox 360 with your XEX launcher.
4. Select the included model and choose **Load model**.

Requires a console capable of running unsigned XEX files. No separate model download is needed.

## Features

- Console chat with controller and on-screen keyboard support.
- Compatible Gemma 2/3, dense Llama, and Qwen2/2.5 GGUF models, within available RAM.
- Adjustable context size, temperature, and top-k sampling.
- OpenAI-compatible chat/completions API with streaming.
- MCP endpoint and an optional PC bridge for external MCP servers.
- Experimental Xenos pixel-shader matrix calculations with CPU fallback. No speedup is guaranteed.

## Documentation & support

See the **[technical guide](docs/TECHNICAL.md)** for model compatibility, API examples, MCP setup, GPU details, and build instructions.

[Report an issue](https://github.com/zuhagames/360AI/issues) · [Support development on Ko-fi](https://ko-fi.com/zuhagames)
