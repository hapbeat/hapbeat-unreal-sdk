# Showcase Sample — Third Party Notices

This sample bundles third-party 3D models, textures, and audio. CC0 sources are
listed for provenance; CC BY assets are credited per-file as required by their
license.

The originals are committed under `Source/` and are imported into the
`Meshes/`, `Textures/` and `Sounds/` assets by
`Scripts/import_showcase_assets.py`. The notices below cover both forms.

---

## 3D Models (`Source/Models/` → `Meshes/`)

### CC BY 3.0 — attribution required

These two assets require attribution. Both are © their respective authors,
distributed under [CC BY 3.0](https://creativecommons.org/licenses/by/3.0/).

| File | Imported as | Title | Author | Source |
|---|---|---|---|---|
| `Source/Models/bowling_pin.obj` | `SM_BowlingPin` | Bowling Pin | Jakob Hippe | <https://poly.pizza/m/d1ZCN1qopib> |
| `Source/Models/Missile.obj` | `SM_Missile` | Missile | Poly by Google | <https://poly.pizza/m/dPVCvXP-S58> |

### CC0 — public domain

No attribution required, listed for provenance.

| File | Imported as | Title | Author | Source |
|---|---|---|---|---|
| `Source/Models/Door.fbx` | `SM_Door` | Door | Quaternius | <https://poly.pizza/m/a948jjnuaL> |
| `Source/Models/FishingRod_Lvl5.obj` | `SM_FishingRod` | Fishing Rod | Quaternius | <https://poly.pizza/m/0YAR0Lg58p> |
| `Source/Models/Shark.obj` | `SM_Shark` | (Cute Fish pack) | Quaternius | <https://quaternius.com/packs/cutefish.html> |
| `Source/Models/blaster-g.fbx` | `SM_BlasterG` | Blaster | Kenney | <https://kenney.nl/assets> |
| `Source/Models/bullet-foam-tip-thick.fbx` | `SM_BulletFoam` | Foam bullet | Kenney | <https://kenney.nl/assets> |
| `Source/Models/target-large.fbx` | `SM_TargetLarge` | Target | Kenney | <https://kenney.nl/assets> |

The `.mtl` files next to the `.obj` files are part of those downloads. The
import does not use them — the Showcase drives its look from the `MI_*`
material instances — but they are kept so the originals stay complete.

---

## Textures (`Source/Textures/` → `Textures/`, `Materials/`)

`T_Colormap` and `T_DefaultMaterial_BaseColor` are the palette atlases that ship
with the Kenney and Poly models above, under those models' licenses.

The remaining three are CC0 from [Poly Haven](https://polyhaven.com/) (every
asset on Poly Haven is CC0 by site policy). Only the diffuse map of each set is
bundled, downscaled from 4K to 1024×1024; the normal, roughness, ambient
occlusion and displacement maps are not included.

| Imported as | Use | Source |
|---|---|---|
| `T_LaminateFloor_D` | Z1 bowling lane | <https://polyhaven.com/a/laminate_floor_02> |
| `T_OakVeneer_D` | Floor | <https://polyhaven.com/a/oak_veneer_01> |
| `T_ConcreteWall_D` | Wall | <https://polyhaven.com/a/concrete_wall_007> |

---

## Audio (`Source/Audio/` → `Sounds/`, and `Kit/`)

`Source/Audio/*` and `Kit/install-clips/*` / `Kit/stream-clips/*` WAVs are
derived from royalty-free / CC0 sound-effect sources, then resampled / trimmed /
gain-adjusted for haptic playback.

Source sites:

- Kenney — <https://kenney.nl/assets> (CC0; multiple SFX packs)
- 100 CC0 SFX by Rubberduck — <https://opengameart.org/content/100-cc0-sfx> (CC0)
- 100 CC0 Metal and Wood SFX by Rubberduck — <https://opengameart.org/content/100-cc0-metal-and-wood-sfx> (CC0)
- 効果音ラボ — <https://soundeffect-lab.info/>
- 魔王魂 — <https://maou.audio/>
- 効果音辞典 (小森平) — <https://taira-komori.net/>
- OtoLogic — <https://otologic.jp/>
- 音人 — <https://on-jin.com/>

Files have been processed for haptic use and metadata (author / copyright tags)
removed. The Japanese SE sites above distribute under their own free-use terms
(generally royalty-free for non-resale use). Kenney and OpenGameArt sources are
CC0. If you are the rights holder and need an entry corrected, removed, or
relicensed, please open a GitHub issue.

---

## License summary

- **CC BY 3.0** (attribution required): 2 model files, credited above.
- **CC0**: all remaining 3D models, the three Poly Haven textures, all bundled
  audio.

The Hapbeat SDK code itself (`Source/HapbeatSDK*`, `Scripts/`) is licensed under
the terms in the plugin's main `LICENSE` file and is not covered by the
third-party notices here.
