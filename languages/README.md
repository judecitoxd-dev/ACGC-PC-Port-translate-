# External language packs

The PC port keeps the original ROM untouched. English (`language = en`) always
uses the text inside the user's supported USA disc image. Any other language
code loads optional replacements from:

```
languages/<code>/
├── manifest.ini
└── aram/
    ├── message_data.bin
    ├── message_data_table.bin
    └── ...
```

Data/table pairs must be supplied together. Missing pairs automatically fall
back to the original English ROM resources. The runtime ignores executable,
model, audio and save files, so a language pack cannot replace game logic.

Create and validate a pack:

```bash
python tools/language_pack.py create es "Español"
python tools/language_pack.py validate languages/es
```

Do not commit or redistribute extracted Nintendo text/resources. Each user
should generate a pack from a game copy they legally own, or use an original
community translation distributed with permission.
