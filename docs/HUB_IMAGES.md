# Images in the chat

Part of [the chat hub](HUB.md). A person who is allowed to can post an image (a PNG, a JPEG, a WebP or a GIF) as a line of the global chat: they paste it, drop it or choose it, add a caption if they like, and everybody who reads the chat sees a small picture in the log that opens full size. The hub never keeps what was uploaded: every image is decoded and written again by a separate program, the converter, and what is served is the converter's new file. The page's part is in [Home's chat](HOME_CHAT.md#the-chat-dock), the commands in [HUB_COMMANDS.md](HUB_COMMANDS.md#moderation).

## Who may post

`Images.state_of(account)` (`hub/images.py`) is the one place that decides, for the upload, for a `say` with an image and for what a page is told (`you.img`):

| State | Meaning |
|---|---|
| `on` | the account may: the operator's account (`ANTS_HUB_OWNER`, no call to Discord, never blocked by the flag), or Discord says that it holds the images' role (`ANTS_HUB_ROLE_IMAGES`), the moderator's or the administrator's, on an answer within the belief of [Roles](HUB.md#roles) |
| `off` | an administrator took it away: a row of the account's reference in the table `noimages`, which beats every role but the operator's account |
| `role` | Discord's answer is known and says no |
| `wait` | it cannot be said now: Discord has not answered, or its answer is old, or refused, or the flags could not be read. It fails closed: not allowed, and the person is told that it is not known (the text of the chat is not touched) |
| absent | the hub has no converter or no folder for the files: there are no images at all and a page is told nothing |

The images' role is read from the member answer that the hub already asks Discord for (no extra call), by the same worker, at the same pace, believed for the same interval and the same grace, and cleared by the same refusal as the other two roles. A role that lapses leaves the images that were posted (they were allowed when they were posted) and refuses new ones within the belief window. A banned, a muted and a too-young account cannot post an image (an image is a line). `ANTS_HUB_ROLE_IMAGES` is an id (digits), must differ from the other two roles, is shape-checked, never printed (the start line says only `role_images_configured=0|1`) and lives only in the stack's environment.

**The flag.** `/noimages NAME` and `/images NAME` ([HUB_COMMANDS.md](HUB_COMMANDS.md#moderation)) are administrator commands in the one table of ranks, with the powers of `/ban` and `/unban`: an administrator acts on a person with no role and on a moderator, never on an administrator, the operator's account or oneself; a moderator who types them gets `reply unknown`. Only administrators set and lift it. The flag is memory in the hub, read again when the store's counter `noimg_seq` says that it changed (here, or by `hub.admin` in another process), and a page is told by a live `you`. If the flags cannot be read, nobody but the operator's account may post. The flag survives a restart and Forget me (one row: the reference, the time, the actor's reference; no id and no name).

## What goes over the wire

Protocol version 1; every change is additive.

| Direction | Frame | Meaning |
|---|---|---|
| up | `POST /hub/img/up` | the upload ([below](#the-upload)); not a socket frame: the socket takes text frames of 16 KB |
| up | `{"t":"say","text":"...","img":"<32 hex>"}` | a line that carries the upload `img` (its own, once, for 10 minutes); the caption `text` is optional and has every rule of a line; an `img` that is not 32 lower-case hex digits, or on a line that starts with one `/`, is `bad`; one that is not the account's, is used, expired or unknown is `err imgid` (one answer for all four); not allowed is `err img` |
| up | `{"t":"delimg","id":n}` | the account deletes its own image line (a poster has no moderator's `/del`): only a line of its own that carries an image; anything else is `bad`; no row of the moderators' log |
| down | `line` has `"img":{"n","x","w","h","tw","th"[,"a":1]}` | in the live frame and in the history: `n` the 32-hex name, `x` `jpg`, `png` or `gif`, `w` `h` the size of the image, `tw` `th` of its thumbnail (the page reserves the space), `a` when it is animated. Never an account, a file name or what was uploaded |
| down | `you` has `"img"` | `on`, `role`, `off` or `wait`; absent where the hub has no images. A live `you` follows when it changes (a flag, a role that lapsed, a first answer of Discord) |
| down | `ok c:"noimages"`, `ok c:"images"` | the answers to the two commands; the `cmds` frame has their rows for administrators |

A delete of an image line by a moderator is the `/del` that it always was: the `del` frame takes the line off every screen and the files go with it. New `err` codes `img` and `imgid`.

## The upload

`POST /hub/img/up` (`hub/imgdoor.py`; the site's nginx passes it with only the login's cookie, `Origin`, `Sec-Fetch-Site`, `X-Ants-Image`, the body's type and length, and the client's address): the body is the bytes of an image, at most 4 MiB. It is the page's own call and nobody else's: `Origin` must be the site's, `Sec-Fetch-Site` `same-origin`, the header `X-Ants-Image: 1` must be there (a page of another site cannot send it without a preflight that the door never answers) and the type must be `application/octet-stream`. Everything that can be decided is decided **before a byte of the body is read**, in this order:

1. the method (405), a query (404), the site (403 `origin`), the type and the header (415 `type`);
2. the converter and the folder exist (503 `off`) and the address is within its limit (429 `rate`);
3. a login (401 `login`); the account is not banned, muted or too young and `Images.state_of` says `on` (403 `images`);
4. the length: `Content-Length` once and whole, no `Transfer-Encoding` (411 `length`), at most 4 MiB (413 `big`);
5. the account's budget (429 `rate`) and the places: at most 3 uploads of the account wait for a line, at most 2 bodies are read at one time for the hub, at most 4 wait for the converter (429 `rate` or `busy`).

Then the body is read within 30 seconds (400 `body` when it does not come), its first bytes must be a PNG, a JPEG, a GIF or a WebP (415 `type`; the converter looks at the rest), and it goes to the converter, one at a time, waiting at most 10 seconds in the queue. The answer is checked again here (below). The account's role is looked at once more (it may have changed while the converter worked), the room on the disk is checked, the files are written, the row follows, and the answer is `200 {"id","w","h","x","anim","still"}`. A refusal of the converter is `415 type`, `422 pixels`, `422 bad`, `422 frames` or `413 big`; a converter that is not there, hangs or lies is `503 down`; no room is `503 full`. An answer is JSON with one code and nothing else. A token that was spent for an upload that failed for a reason that is not the account's doing (busy, down, full) is given back. The log says `image kind=ok|type|pixels|bad|frames|big|down|full` and nothing else.

The id is 128 random bits (32 hex digits) and the name of the image in every address; it is bound to the account's reference, can be used for one line, and is valid for 10 minutes; a waiting upload that nobody used is deleted then, files and row.

## The converter

`imgconv/` is a small program of ours in a container of its own (`Dockerfile.img`, the service `ants-img` of the stack): Python and Pillow, no secret, no volume, no published port, no variable but its port, on a network that only the hub shares and that has no way out, read only, no capabilities, 16 processes, 768 MB, one processor. It does one thing for one caller: `POST /convert` with the bytes and nothing else (no account, no name, no header of the visitor) answers with a line of JSON and the new image and its thumbnail, or a code. Every conversion runs in a child process of its own with a CPU limit, a memory limit and a wall clock of 10 seconds, so a crash or a hang inside a decoder is one refused image. The memory limit is the address space that the child has when it starts plus 512 MB, under the container's 768 MB (a test keeps the two apart): a legal image of 24 megapixels peaks at about 300 MB (the decoder lets go of every full-size copy it made), and one that wants more is `bad`, never ended by the kernel's out-of-memory killer, which would take the whole converter down. Nothing of an image or of the caller is logged.

Every byte that comes out was written by the encoder in `imgconv/convert.py` from the decoded pixels of a fresh image that carries nothing: no EXIF (so no location, camera or time), no color profile, no XMP, no comment or text chunk, no embedded thumbnail, nothing after the end of the file. A polyglot (a PNG that is also a ZIP, a GIF with a script in its comment, a JPEG with a payload after its end marker) comes out as a plain image. The orientation that a photo's EXIF names is applied first. A color profile is dropped, not converted.

| What | How |
|---|---|
| types | PNG, JPEG, GIF and WebP by their first bytes; anything else (SVG, HTML, BMP, TIFF, ICO, HEIC, ZIP) is `type` |
| size in | the header is read before a pixel is decoded: more than 24,000,000 pixels is `pixels`; a JPEG is decoded at a reduced scale, so a big photo never exists at full size in memory |
| still image out | PNG when it has transparency or at most 256 colors, else JPEG (quality 85, then 78 and 70); the longest side at most 2,048 px; at most 1.5 MiB, else it is made smaller in steps and then refused `big` |
| animated GIF out | kept animated, as a GIF, each frame composited, with its own timing (a frame under 20 ms counts as 20); at most 100 frames and 15 seconds (`frames`), the longest side at most 400 px, at most 1.5 MiB; frames x width x height of the input at most 150,000,000 (`pixels`); a frame that lies outside the screen of the file, which would make the canvas larger than what was checked, is `pixels` |
| animated WebP or PNG | its first frame, as a still (`still: true`); a GIF of one frame is a still image |
| thumbnail | always a JPEG of the first frame, the longest side 360 px, transparency laid over the dock's dark color; for an animated image it is the still that the log shows while the image is off screen, paused or switched off |

The hub **checks the converter's answer** before it keeps a byte: the keys are exactly the nine of the answer, the types and sizes are whole numbers within the caps, `anim` says what the type says, the two lengths add up to what was read, the image has the first and the last bytes of its type and the thumbnail those of a JPEG; the whole answer is read up to 1.7 MiB and no more, with no redirect, no `Transfer-Encoding` and no `Content-Encoding`, and `Content-Length` once. Anything else is `down` and nothing is stored.

## Files and deleting

`<name>.<x>` and `<name>.t.jpg` in one folder (the volume `ants-hub-images`: the hub writes it, the site's nginx reads and serves it read only at `/srv/hub-img`), `name` 32 lower-case hex digits made by `secrets.token_hex(16)` from nothing of the account or the bytes. A file is written under a temporary name, made durable and renamed, the thumbnail first. The row is in the additive table `images` of `hub.db` (the schema stays 4; an older hub never looks at it): the name, the account's **reference** (never its id), the line (empty while the upload waits), the type, the sizes, whether it is animated, the bytes and the time. The link between a line and its image is the table's, not a column of `lines`.

An image is kept while its line is: the newest 200 lines and 7 days, as for every line. `Images.sweep` deletes the files of every image whose line is deleted or gone and of every upload that waited more than 10 minutes, and then the rows; a file that cannot be deleted keeps its row, so the next sweep tries again. It runs right after every deletion of a line (a moderator's `/del`, the author's `delimg`, `/ban NAME purge`, Forget me, a deletion by `hub.admin` once the hub reads it), when the store makes room, and by the hub's clock every 30 seconds; `scan` reads the folder every hour and deletes a file that has no row and is older than 15 minutes (a crash between a file and its row) and any temporary file that was left. At most 600 images and 300 MiB are kept in all, posted and waiting; over that an upload is `503 full` until room is made. The folder is what these need, not the converter: with the converter's address empty (images switched off) the files of a line that a moderator or the author deletes, and the files that no row has, still go, because it is when images are switched off for an incident that a file must be deletable; only the upload and the posting stop.

## How it is served

`GET /hub/img/<name>.<jpg|png|gif>` and `<name>.t.jpg` are one regular expression of the site's nginx (`docker/nginx.conf`, after the avatars' door): a GET or HEAD without a query, a request of another site is 403, no access log, 20 a second for an address with a burst of 240. **The hub is not asked**: nginx reads the file from the volume, so a file that the hub deleted is a 404 at once and nothing on the site's side keeps a copy. The type is from the extension (`image/jpeg`, `image/png`, `image/gif`) and from nothing else; the answer carries `X-Content-Type-Options: nosniff`, `Content-Security-Policy: default-src 'none'; sandbox`, `Cross-Origin-Resource-Policy: same-origin`, `Referrer-Policy: no-referrer` and `Cache-Control: private, no-cache` (a browser may keep a copy but asks the site again before it shows it, a cheap conditional request, so an image that was deleted is a 404 at once and not an hour later; nothing shared keeps it). Anybody who has a name may fetch it, as with the chat itself; it cannot be guessed. The page's policy is unchanged: `img-src 'self' data: blob:` already allows the file and the preview of an image that waits, and nothing a person writes ever becomes an address.

`POST /hub/img/up` is an exact location of its own: only POST, no query, `Sec-Fetch-Site: same-origin`, `client_max_body_size 4m` (413 from the length alone), the body passed on as it comes (`proxy_request_buffering off`, so the hub can refuse a request before the body is read), only the headers of the list above, six a minute for an address with a burst of three more and two at a time, no access log. A hub that is not there is the JSON 503 of the other doors.

## Limits

| What | Limit |
|---|---|
| an upload | 4 MiB; at most 24 megapixels in; 2,048 px and 1.5 MiB out (a GIF: 400 px, 100 frames, 15 seconds) |
| an account | a burst of 3 uploads, then one every 20 seconds; 30 an hour and 100 a day; at most 3 that wait for a line; an image line is also a line, with every limit of one |
| the hub | 2 bodies read at one time; the converter does 1 and 4 wait for at most 10 seconds, then `busy`; the converter's own limit is 10 seconds |
| an address | in the hub a burst of 6 and one every 10 seconds; in nginx 6 a minute with a burst of 3 and 2 at a time |
| the disk | 600 images and 300 MiB |
| the files | 20 a second for an address, a burst of 240 |

## Moderation and the log

A moderator or an administrator deletes any image with `/del NUMBER` (the moderation sheet of the page types it): the line goes from every screen and the files are deleted. The moderators' log gets `del_img` (the actor's and the author's references and the line's number) in place of `del`. `/noimages` and `/images` write `noimages` and `images` (the actor's and the target's references, no detail). A person's own delete leaves no row. Nothing in a row, a frame, a log line, an error or a file name is a Discord id. The admin page ([MAP_SUBMISSIONS.md](MAP_SUBMISSIONS.md#the-admin-page)) has a panel for the flag: `GET /hub/admin/api/imagesoff` lists the accounts whose posting is taken away (`{"imagesoff":[{"ref","name","at","by","by_name"}],"untrusted":true,"untrusted_fields":["name","by_name"]}`) and `POST /hub/admin/api/noimages` and `/images` with `{"account": username or reference}` set and lift it, with the page's token and the table of the powers (a flag that outlived its account's other rows is lifted by its reference); the Status has `images` (`on`, the files and their bytes with their limits, the accounts with the flag, the converter's state `ok`, `down` or `off`; no account).

From a terminal: `python -m hub.admin noimages ACCOUNT` and `images ACCOUNT` (the same ACCOUNT as the other commands; only the reference is printed), the `status` line `images: N of 600 kept, X of 300.0 MB; N with posting taken away`, and `del LINE_ID`, which logs `del_img` for an image line; the running hub deletes the files when it reads the deletion.

## Settings

| Variable | Meaning |
|---|---|
| `ANTS_HUB_ROLE_IMAGES` | the id (digits) of the Discord role whose holders may post images; a different role from the other two; optional (without it only the staff and the operator's account post); needs the token and the server of [Roles](HUB.md#roles); never printed |
| `ANTS_HUB_IMAGE_CONVERTER_URL` | `http://host:port` of the converter and nothing else; in the stack file a variable that is not set at all is `http://ants-img:4050`, and one that is set and **empty** switches images off for everybody (no posting; the files that are there are still deleted with their lines) |
| `ANTS_HUB_IMAGE_DIR` | the folder of the files, `/images` in the stack (the hub's image has no default folder to write in: it must be a volume) |

The stack file adds the service `ants-img`, the network `img-network` (`internal: true`; only the hub and the converter), the volume `ants-hub-images` (the hub writes `/images`, the site reads `/srv/hub-img` read only) and 256 MB for the hub (bodies are held in memory, 4 MiB each, at most two at a time).

## Data and rollback

`images (name, ref, line, x, w, h, tw, th, anim, bytes, at)` and `noimages (ref, at, by)` are additive tables, as `pictures` is: no version of the schema came with them, a hub of an earlier version opens the same file and never looks at them, and it shows an image line as its caption (an empty text shows nothing). A rollback that is for good, with the hub stopped: `python -c "import sqlite3; c=sqlite3.connect('/data/hub.db'); c.execute('PRAGMA secure_delete=ON'); c.execute('DROP TABLE images'); c.execute('DROP TABLE noimages'); c.commit()"` and remove the volume `ants-hub-images`.

## Tests

`tests/scripts/test_img_converter.py` runs the real Pillow (it needs it: skipped with a loud line where it is missing, required by the CI's Linux leg): the metadata is gone from the bytes and from what Pillow reads, the polyglots come out clean, the pixel cap is read from the header, a decompression bomb is refused fast, the types, the caps, the animation within its limits, the server's door, a conversion that hangs or dies. `test_hub_images.py` holds the rules without a socket (who may post, the line, every way a line can go and take its files with it, the flag and its powers, the settings) and `test_hub_images_door.py` the upload on real sockets against a fake Discord and a fake converter (the order of the checks with the body not sent, the converter's refusals and lies, the role failing closed, the admin page's list and changes, no id anywhere). `tests/scripts/test_hub_stack.py` holds the converter's container, `tests/scripts/test_nginx_hub_img.py` the two nginx doors run for real. `tests/data/hub_images_mutants.json` breaks each rule once for `tools/mutate.py`.
