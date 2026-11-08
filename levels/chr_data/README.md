# What's in here

To make a level, read [docs/MAKING_LEVELS.md](../../docs/MAKING_LEVELS.md). Open `pocket_dash.tiled-project` in Tiled: it has the F5 (build and play) and F6 (redraw sky and ground) commands and the dropdown lists.

| Folder | What it is | Edit it? |
|---|---|---|
| `tmx/` | The levels. `template.tmx` is what new levels start from | Yes, in Tiled |
| `examples/` | Finished example levels, not in the game | Look, or copy |
| `objects/` | The Tiled tilesets: `gd_objects.tsx` (blocks, spikes, deco), `sprites.tsx` (portals, pads, orbs, triggers). `objects/art/` holds the art the game is built from | Only when adding objects |
| `previews/` | The sky and ground pictures Tiled shows behind each level | No, they're redrawn by F6 and every build |
| `game_art/` | The game's own graphics: tile sheets, icons, menu art, and the `.bin` files the build makes from them | Only when changing the game's art |
