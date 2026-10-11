# Rokid Capture / Remote Brand Icon

> 中文版：[README.md](README.md)

## Design Concept
AR glasses × camera × privacy: the left lens is the **camera aperture** (capture
capability) and the right lens is the **privacy cutoff** (ghostlock stealth mode) —
covering the project's two core themes: remote capture and privacy LED control.

| Element | Colour | Semantics |
|------|------|------|
| Frame/temple | `#E8EDF5` near-white | The product body |
| Aperture centre | `#4FC3F7` cyan | Camera / capture |
| Cutoff slash | `#FFC24B` amber | Privacy / ghostlock |
| Background (adaptive) | `#0B1220` deep blue | Dark, high contrast (AR display) |

## Files
- `logo.svg` — full-colour version (for documents/websites)
- `logo_monochrome.svg` — monochrome version (embossing/themed icon)
- Corresponding resources in the two apps:
  - `res/drawable/ic_launcher_foreground.xml` (adaptive foreground)
  - `res/drawable/ic_launcher_monochrome.xml` (Android 13+ themed icon)
  - `res/mipmap-anydpi-v26/ic_launcher.xml`, `ic_launcher_round.xml`
  - `res/values/colors_launcher.xml` (background colour `#0B1220`)
  - The glasses app also contains `res/drawable/ic_notification.xml` (notification small icon)

## Usage
The manifests of both apps already declare `android:icon` / `android:roundIcon`; the
glasses-side foreground service notification uses `ic_notification`. Replacing any
resource and rebuilding is sufficient.
