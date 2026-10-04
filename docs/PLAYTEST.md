# Packaging a playtest build (friends in other states)

The first package was made on 2026-10-04 from commit 6dd8ca4.

## The rule that matters

The arcade only seats copies of the same build (`BuildId`, `DataHash`). So everyone must run the same exe:
- the host's desktop icon runs `C:\Users\phill\Downloads\Depth\build\Release\depth.exe` (the main checkout);
- the friends' zip must carry that same exe.

Never package a worktree's build.

## Steps

1. Bring the main checkout up to date. In it, run `git merge --ff-only <branch>`, then `.\build.ps1`.
2. Stage `C:\Users\phill\Downloads\Depth_Playtest\Depth\` (outside the repo):
   - `depth.exe`;
   - `msvcp140.dll`, `vcruntime140.dll` and `vcruntime140_1.dll`, copied from VS's
     `VC\Redist\MSVC\<ver>\x64\Microsoft.VC145.CRT` (app-local; redistributable);
   - `data\` and `assets\` (robocopy /E). Leave out saves, settings and profile files, and `depth_sim*.exe`;
   - `HOW_TO_PLAY.txt`.
3. Smoke-test from the staged folder:
   - `depth.exe --scuffle-net-test`;
   - `--net-loop scuffle 0 mem`;
   - `--shots <dir> night_crowd` (models load).
4. Zip it: `Compress-Archive ... Depth_Playtest.zip`. The first zip was about 62 MB.
5. Distribute through the user's OneDrive (`Collection for Depth\Depth_Playtest.zip`):
   - artifacts can't serve zip files;
   - the playtest page (https://claude.ai/artifact/QS8zjRVL5rCU9pghGo351j) reads its download link from `link.json`;
   - the host pastes a OneDrive share link into the page's box, or Claude republishes `link.json`.

## Networking

- Friends join ZeroTier network `8d1c312afae623eb`, and the host authorizes each one at my.zerotier.com.
- The host's ZeroTier IP is 10.82.215.253.
- In the Deep Arcade, friends Browse (the pool is a /24, so the beacon reaches it) or Join by that address.
- The firewall prompt needs Private (and Public if the ZeroTier network shows Public).
