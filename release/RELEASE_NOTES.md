# 360AI 1.6 — 360 Assistant

A native offline keyword and intent assistant is now available without loading any GGUF. On the model list, press **X**, then **A** to enter text. Type **help**. It works even when no model files are present.

- **Calculator:** precedence, parentheses, powers, percentages, sqrt/abs/round, pi/e and ans. English numbers such as “twenty-five” and “one hundred and five” work in calculations, conversions and durations. Invalid syntax, division by zero and oversized expressions fail clearly.
- **Conversions:** length, mass, temperature, time and byte storage. Follow-up values reuse the last successful unit pair.
- **Timers/reminders:** up to eight timers with pause, resume, restart, extension, named controls, clarification for duplicate labels and recent alert history. A banner appears across app screens while the app remains open.
- **Saved notes and name:** search, read, edit and delete by exact note ID. IDs stay unused after deletion/restart. Validated JSON, atomic saves and a previous valid backup protect personal data; old saves migrate automatically.
- **Teach answers on Xbox:** save up to 16 phrase/answer pairs directly in chat. They return text only, with built-in commands taking priority. List, replace and forget them without editing a file.
- **Undo:** restore the last change to notes, your name or taught answers within the current app session.
- **Xbox commands:** status, console time/date, model list, model reply presets, transcript export and return to models.
- **Intent handling/debugging:** whole-word/phrase matches, polite prefixes, wake phrases, follow-up questions, negation, confirmation for bulk deletion/clearing, conservative typo suggestions and explicit unknown-question fallback. Fixed punctuation after IDs, bare “do not,” negative timer IDs, stale timer references and pending confirmations surviving oversized requests. Invalid saved control characters are rejected.
- **Custom FAQ answers:** edit assistant-rules.json. Rules return text only; they cannot execute actions, scripts or network calls.
- **Controller keyboard:** parentheses, powers and apostrophes are available in the fallback keyboard, with the grid/footer sized to fit.

This is a **text-based rule assistant**, not a language model or Siri-equivalent speech system. Microphone input, speech recognition, spoken replies, live weather/news and internet search are not implemented. Timers require 360AI to stay running; they are not system alarms and do not survive restarting the app. Notes/name persist separately from chat.

The full USB package includes the app, assistant guide/rules, all four existing model files, catalog/downloader and required model notices. The assistant itself needs no weights. Personal settings, notes and exports are excluded from updates.

Xbox SDK compilation and native checks cover intent matching, arithmetic errors/precedence, unit dimensions, interrupted follow-ups, timer controls/limits/ambiguity, note edits/undo, taught answers, save migration, corrupt data and backup recovery, plus 2,000 deterministic malformed-input cases. Existing protocol/tokenizer/package checks protect GGUF features. Physical Xbox menus, keyboard behavior, timer banners and drive persistence still need console validation; host processing measurements do not measure Xbox UI latency.

Made by **ZuhaGames & Canyons**, expanded from [Canyons' gemma.xex](https://gitlab.com/canyons0/gemma.xex). AI tools assisted development, debugging and documentation.
