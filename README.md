# Hitbox Viewer (Geode mod)

## Build for every platform (same package layout as other .geode mods)
1. Replace `yourname` in `mod.json` (both `id` and `developers`) with your name.
2. Push this folder to a GitHub repository.
3. Open the **Actions** tab, run **Build Binaries**, and download **Build Output**.
   It contains `yourname.hitbox-viewer.geode` with the Windows, macOS, iOS, Android32 and Android64 binaries inside.
4. Drop that `.geode` file into `Geometry Dash/geode/mods/`.

## Build only for your own PC (Windows)
1. Install the Geode SDK and CLI (https://docs.geode-sdk.org/getting-started/) and set `GEODE_SDK`.
2. In this folder run: `geode build`
3. The `.geode` appears in `build/` (contains only the Windows `.dll`).
