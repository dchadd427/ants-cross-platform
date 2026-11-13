# Install the site as an app

The site can be added to a phone's home screen, a computer's app list or an iPhone's Home Screen like an app: it opens in a window of its own with no address bar, under the name **Ants!** and the icon of a pixel-art ant head on the orange of the game's menus. Nothing else changes: it is the same site, the same pages and the same login, and it always loads the current version. There is **no service worker** and no offline mode: with no connection the browser shows its own "you are offline" page, as for a tab.

## What makes it installable

| File | What it is |
|---|---|
| `web/manifest.webmanifest` | The web app manifest: name and short name `Ants!`, `id`, `start_url` and `scope` `/`, `display` `standalone`, `theme_color` `#2b5f43` (the outermost line of the pages' green frame), `background_color` `#db4b13` (the orange of the game's menus), categories, the icons and four shortcuts (Matches, Live now, Map maker and "Screen numbers", which opens `/?vp`: the front page with a box of the window's numbers, see [Home and the Room](HOME_SCREEN.md)). No `orientation`: the front page is upright and the game page is sideways. No screenshots yet. |
| `web/app/` | `icon-192.png` and `icon-512.png` (rounded corners, transparent), `icon-maskable-512.png` (orange to every edge, the head inside the middle 80% circle, for the shapes Android cuts) and `apple-touch-icon.png` (180 x 180, orange to every edge, no transparency, for the iPhone and iPad). |
| the head of nine pages | `<link rel="manifest">`, `<meta name="theme-color">`, `<link rel="apple-touch-icon">` and `<meta name="apple-mobile-web-app-title" content="Ants!">`, the same four lines on the front page, the game page, Matches, a match, Live now, Legal, the Map maker, the changelog and the Sprites and sounds page. Not on `/admin`. The Apple icon wins over the manifest's icons on an iPhone, and its title is the name under the icon there. |
| `docker/nginx.conf` | One exact location, `location = /manifest.webmanifest`: type `application/manifest+json` (the stock type list may have no entry for the extension), `Cache-Control: no-cache, must-revalidate`, and the four headers that every static file gets (the two cross-origin headers, `nosniff`, `Strict-Transport-Security`). Being exact, it wins over the fallback that serves the game page for an unknown address. The content security policy is not changed: `default-src 'self'` of the pages covers the manifest and the icons, and the manifest itself carries no policy (it is not a document). The icons are pictures: the existing rule for `.png` files (`public, max-age=604800, no-transform`, a week) serves them, so they are named with `?v=` and the first 8 hex digits of the file's sha256, as `clay.png` is. |
| `Dockerfile`, `build_web.sh`, `.github/workflows/ci.yml` | Copy the manifest and `web/app/` into the image and `dist/`, and the CI's check of the image lists the five files. |

## The icons

`tools/make_app_icons.py` draws every icon from two small grids in the file (the worker ant's head, front on, in the colors of the original worker sprite; a 22 x 22 cell head for the large icons and a hand-placed 16 x 16 cell head for the smallest sizes). It reads no picture and needs only Pillow. It is a developer's tool, not part of any build. Every cell of the art is a whole number of device pixels in every file, so the picture is sharp at its own size; a launcher that shrinks `icon-maskable-512.png` by a fraction softens the cell edges a little. `python3 tools/make_app_icons.py --check` writes the four app files to `web/app/` and the favicons to `web/`, reads each PNG back (every cell one constant color, whole-cell transparency only where allowed) and prints each file's size and the first 8 hex digits of its sha256. After new icons, put those digits after `?v=` in the manifest and in the head of every page; `tests/scripts/test_web_app_manifest.py` says which one is stale.

**The browser-tab icon** is `web/favicon.png` (32 x 32, the small head alone on transparency) and `web/favicon.ico` (16, 32 and 48 pixel frames, for browsers that ask for the old address). Every page of the site links `/favicon.png?v=` with the digits of the file (the pages that always linked the `.ico` also link `/favicon.ico?v=`); the new digits make a browser drop the copy that it kept for a month. Both addresses keep working without `?v=` (the exact locations in `docker/nginx.conf`). The `.ico` and `.png` are copied into the image by `COPY web/favicon.*`.

A phone that has the app installed picks up a new icon only when the browser reads the manifest again: Chrome on Android within about a day (sometimes only after the app has been opened a few times); an iPhone Home Screen icon changes only when the app is removed and added again.

## Tests

- `tests/scripts/test_web_app_manifest.py`: the manifest's fields, that every icon and shortcut icon exists at the size it states and with the digits of its file, one maskable icon, no transparency in the maskable and Apple icons, the four head lines identical on every page (and none on the admin page), the shortcuts' pages, no service worker anywhere, the byte budgets (`APP_ICONS_BUDGET`, `APP_MANIFEST_BUDGET`, `FAVICON_BUDGET` of `tools/size_limits.py`), that the content of the maskable icon stays inside its safe circle (computed from the file), that the tool's `--check` passes and writes the committed bytes again, and that every page links the favicon with the digits of its file. Mutants: `tests/data/web_app_manifest_mutants.json`.
- `tests/scripts/test_nginx_headers.py` runs the real `docker/nginx.conf`: the manifest's type, caching and headers, that a missing manifest is a 404 and never the game page, that the pages' policies are unchanged, and the icons' headers. `tests/scripts/web_routes_check.py` (run by CI against the built image, and by `test_nginx_routes.py` against stand-in files) asks for the manifest and an icon, and `test_web_without_docker.py` lists them among the files that the replayed Dockerfile builds. Mutants: `tests/data/web_app_manifest_mutants.json` (the nginx ones too).

## Trying it on a device

Once the change is live on the site (https is needed):

- **Android, Chrome:** Menu, **Install app**, then open the app from its icon. Check the round icon and the green status bar. Log in with Discord: you come back to the app window, logged in. Quick host, start a match, go fullscreen, turn the phone. Tap a room link in another app during a match. Hold the voice button. Switch on airplane mode and open the app: the browser's own offline page.
- **iPhone, Safari:** Share, **Add to Home Screen**, then open it. Check the icon and the name (**Ants!**). Log in (with the Discord app installed, and without it). Close the app and open it again: still logged in. In a match, tap Matches: does the match survive? Voice button. Sideways on the front page and in a match.
- **Desktop, Chrome:** the install icon in the address bar, a green title bar, Log in, pointer lock and fullscreen in a match, a room link clicked in Discord opens a new app window, right-click the app's icon: the four shortcuts.

An iPhone keeps the name and the icon that were there when the app was added; a change reaches it only when the app is added again. Chrome and Edge fetch the manifest again on their own schedule, and a new icon is named by a new `?v=`.

## Not part of this

No Install or Reload line in the Menu, no service worker, no change to the login, no screenshots, no Quick host shortcut (it has no address of its own).
