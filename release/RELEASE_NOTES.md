# 360AI 1.0

Download **360AI-1.0-USB.zip** under Assets for the ready-to-copy Xbox package. The automatic Source code archives are for development.

1. Extract the ZIP on your PC.
2. Copy the entire `360AI` folder to your USB drive.
3. The full `gemma-3-270m-it-Q8_0.gguf` is already included beside `360AI.xex`; no separate weights download is needed.
4. Connect the USB to your modded Xbox 360 and launch `360AI.xex` with your existing XEX launcher.

**The updated 1.0 USB ZIP includes Gemma 3 270M Instruct Q8_0 weights (291,545,600 bytes).** It can run local chat offline after extraction. A console capable of running unsigned XEX files is required. No model account, SDK, Visual Studio, Node.js or separate shader download is needed on the Xbox.

The USB ZIP includes the executable, model GGUF, start guide, changelog, credits, model provenance, and required Gemma terms/use-policy notices. The unchanged model is sourced from [Second State's GGUF conversion](https://huggingface.co/second-state/gemma-3-270m-it-GGUF), pinned to revision `5ac77b3c443cfd6ed0d1e3d83c2aaf2f72897c7d`. Model use and redistribution remain subject to the included [Gemma Terms of Use](https://ai.google.dev/gemma/terms).

**The USB ZIP is the only packaged release download.** It contains the rebuilt `360AI.xex` and the model weights. This refresh updates the existing **1.0** release.

Optional PC bridge source remains available in the repository's [`bridge` folder](https://github.com/zuhagames/360AI/tree/main/bridge), with setup in the [technical guide](https://github.com/zuhagames/360AI/blob/main/docs/TECHNICAL.md#connect-external-mcp-servers). It requires Node.js 22+ on the PC and does not go on the Xbox.

USB ZIP SHA-256:

```text
996c844139a5b2d9d804f3fdf30d78946a1468ee69b204afc0b73b5cc9020b0e
```

## Features and fixes

- Faster top-k sampling, calculating probabilities only for retained candidates.
- Avoid an unused model forward pass after reaching the response token limit or cancelling.
- Warning-level logging by default to reduce verbose USB log writes.
- Gemma 2/3, dense Llama and Qwen2/2.5 GGUF execution paths, subject to model compatibility and console RAM.
- Experimental Xenos pixel-shader matrix calculations with startup numerical validation and CPU fallback.
- Tiled R32F GPU resolve/readback and pixel-center sampling fixes, with detailed failure diagnostics.
- Corrected packed quantized-weight decoding.
- OpenAI-compatible completions/chat endpoints, streaming, and `/health`.
- Xbox LAN socket privilege/configuration and support for local browser clients.
- MCP endpoint and optional external-server PC bridge.

The Xbox SDK build and seven native/protocol/package test groups pass. The bundled full GGUF was loaded with this engine's host build and generated `Hello!`; its source checksum and the model extracted from the ZIP are verified. **GPU correctness and speed still need physical-console validation.** The GPU path remains experimental; no speedup is claimed. More compatibility details are in the repository README.

360AI is a fork of [Canyons' gemma.xex](https://gitlab.com/canyons0/gemma.xex), expanded beyond Gemma. Credit for the original Xbox 360 port belongs to Canyons and the original contributors.
