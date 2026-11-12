# Install the site as an app

The site can be added to a phone's home screen, a computer's app list or an iPhone's Home Screen like an app: it opens in a window of its own with no address bar, under the name **Ants!** and the icon of the "ants!" logo word. Nothing else changes: it is the same site, the same pages and the same login, and it always loads the current version. There is **no service worker** and no offline mode: with no connection the browser shows its own "you are offline" page, as for a tab.

## What makes it installable

| File | What it is |
|---|---|
| `web/manifest.webmanifest` | The web app manifest: name and short name `Ants!`, `id`, `start_url` and `scope` `/`, `display` `standalone`, `theme_color` `#2b5f43` (the outermost line of the pages' green frame), `background_color` `#db4b13` (the orange of the game's menus), categories, the icons and three shortcuts (Matches, Live now, Map maker). No `orientation`: the front page is upright and the game page is sideways. No screenshots yet. |
| `web/app/` | `icon-192.png` and `icon-512.png` (rounded corners and the pages' frame), `icon-maskable-512.png` (orange to every edge, the word inside the middle 80% circle, for the shapes Android cuts) and `apple-touch-icon.png` (180 x 180, orange to every edge, no transparency, for the iPhone and iPad). |
| the head of nine pages | `<link rel="manifest">`, `<meta name="theme-color">`, `<link rel="apple-touch-icon">` and `<meta name="apple-mobile-web-app-title" content="Ants!">`, the same four lines on the front page, the game page, Matches, a match, Live now, Legal, the Map maker, the changelog and the Sprites and sounds page. Not on `/admin`. The Apple icon wins over the manifest's icons on an iPhone, and its title is the name under the icon there. |
| `docker/nginx.conf` | One exact location, `location = /manifest.webmanifest`: type `application/manifest+json` (the stock type list of nginx 1.24 has no entry for the extension), `Cache-Control: no-cache, must-revalidate`, and the four headers that every static file gets (the two cross-origin headers, `nosniff`, `Strict-Transport-Security`). Being exact, it wins over the fallback that serves the game page for an unknown address. The content security policy is not changed: `default-src 'self'` of the pages covers the manifest and the icons, and the manifest itself carries no policy (it is not a document). The icons are pictures: the existing rule for `.png` files (`public, max-age=604800, no-transform`, a week) serves them, so they are named with `?v=` and the first 8 hex digits of the file's sha256, as `clay.png` is. |
| `Dockerfile`, `build_web.sh`, `.github/workflows/ci.yml` | Copy the manifest and `web/app/` into the image and `dist/`, and the CI's check of the image lists the five files. |

## The icons

`tools/front_page_art/make_app_icons.py` makes the four files again from the repository's own art (`web/front/logo.png`, the solid pixels of the word without its soft shadow, on the flat orange `artlib.CLAY`, with the flat red shadow of the site's buttons; `--grain` uses the clay tile instead, which makes files about three times as big) and can write a picture of them at real size (`--preview DIR`). It is a developer's tool (Pillow), not part of any build. The word is as large as each icon allows (the maskable one: as large as the 80% circle allows) because it is wide and has small details; look at the 48 pixel size after any change. After new icons, put the first 8 digits of each file's sha256 (the tool prints them) after `?v=` in the manifest and in the head of every page; `tests/scripts/test_web_app_manifest.py` says which one is stale.

## Tests

- `tests/scripts/test_web_app_manifest.py`: the manifest's fields, that every icon and shortcut icon exists at the size it states and with the digits of its file, one maskable icon, no transparency in the maskable and Apple icons, the four head lines identical on every page (and none on the admin page), the shortcuts' pages, no service worker anywhere, the byte budgets (`APP_ICONS_BUDGET`, `APP_MANIFEST_BUDGET` of `tools/size_limits.py`) and the image's file list. Mutants: `tests/data/web_app_manifest_mutants.json`.
- `tests/scripts/test_nginx_headers.py` runs the real `docker/nginx.conf`: the manifest's type, caching and headers, that a missing manifest is a 404 and never the game page, that the pages' policies are unchanged, and the icons' headers. Mutants: `tests/data/nginx_headers_mutants.json`.

## Trying it on a device

Once the change is live on the site (https is needed):

- **Android, Chrome:** Menu, **Install app**, then open the app from its icon. Check the round icon and the green status bar. Log in with Discord: you come back to the app window, logged in. Quick host, start a match, go fullscreen, turn the phone. Tap a room link in another app during a match. Hold the voice button. Switch on airplane mode and open the app: the browser's own offline page.
- **iPhone, Safari:** Share, **Add to Home Screen**, then open it. Check the icon and the name (**Ants!**). Log in (with the Discord app installed, and without it). Close the app and open it again: still logged in. In a match, tap Matches: does the match survive? Voice button. Sideways on the front page and in a match.
- **Desktop, Chrome:** the install icon in the address bar, a green title bar, Log in, pointer lock and fullscreen in a match, a room link clicked in Discord opens a new app window, right-click the app's icon: the three shortcuts.

An iPhone keeps the name and the icon that were there when the app was added; a change reaches it only when the app is added again. Chrome and Edge fetch the manifest again on their own schedule, and a new icon is named by a new `?v=`.

## Not part of this

No Install or Reload line in the Menu, no service worker, no change to the login, no screenshots, no Quick host shortcut (it has no address of its own).
