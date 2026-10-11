# Rokid Remote v1.1 — Modernized UI Acceptance Record (2026-09-08)

> 中文版：[README.md](README.md)

## Version
- app-phone v1.1 (versionCode 2), debug build
- Already deployed on a Redmi 21091116UG (serial <PHONE_SERIAL>)

## Screens (screenshots in reports/ui_v11/)
| File | Contents |
|---|---|
| tab_remote.png | Remote tab: connection card (<DEVICE_ALIAS> connected / status row root✓) / capture / privacy LED chips / advanced section collapsed |
| tab_albums.png | Albums tab: 2-column large cover cards (Camera, 21 items) |
| media_grid.png | 3-column photo wall + video duration badges (00:04/00:05) |
| viewer_photo.png | Full-screen photo viewer (progressive loading, then the original image) |
| viewer_video.png | Video viewer (plays once the download completes) |
| tab_local.png | Local tab (Camera, 7 items · 39.4 MB) |

## Automated Verification Matrix (MIUI blocks input injection, so DebugReceiver broadcasts are used to drive it)
| Item | Result |
|---|---|
| OPEN_TAB remote/albums/local switching + uiautomator structural validation | ✅ all three pages correct |
| MediaActivity grid load (22 items, duration badges) | ✅ |
| Viewer remote photo: large thumbnail → progress → full 5.1 MB original download | ✅ 06:30 |
| Viewer remote video: 6.6 MB download (~110 s) → playback, no MediaPlayer error | ✅ |
| GET_DEVICE_INFO / SET_LED (white LED sysfs read-back 0→255) | ✅ |
| CAPTURE_PHOTO (IMG_1788820415226.jpg newly created on the glasses) | ✅ |
| TEST_CHAIN album (22) → list → thumbnails → original download Success | ✅ |
| Old-version timeout issue (8.5 MB video > 120 s) → dynamic timeout | ✅ fixed |
| `.part` leftover cleanup | ✅ 0 files |

## Requiring Manual Touch Verification (adb cannot inject taps)
1. Long-press an album card → BottomSheet (view/rename/delete)
2. Long-press a media item → multi-select mode (select all/batch download/batch delete/back to exit)
3. FAB to add an album, rename/delete dialogs
4. Viewer: tap to toggle chrome, pinch to zoom, double-tap to zoom in/restore, video control bar
5. LED colour chips, ghostlock/root toggles, recording-state switching
6. Dark/light mode switching (follows the system)

## Known Limitations/Deviations
- No SwipeRefreshLayout was added (to avoid new dependencies in the offline build environment); instead there is a toolbar refresh button plus automatic refresh on onResume/tab switch
- Zooming and image loading are all hand-rolled (no new third-party dependencies)
- Batch downloads run sequentially, one at a time (an RFCOMM protocol limitation)
