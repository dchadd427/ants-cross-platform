"""The browser, the system and the kind of device of a visitor, read from the User-Agent header at the counter's door (docs/ANALYTICS.md) and then dropped: only these three plain names are
ever kept, never the header itself. A request that comes from a crawler, a monitor or a script (or has no usable header) has no names: the counter does not count it.
"""
import re

MAX_LENGTH = 512                                  # a header that is longer is not a browser's
CRAWLER = re.compile(r"bot\b|bots\b|crawl|spider|slurp|facebookexternalhit|embedly|headless|lighthouse|pingdom|monitor|uptime|python-|curl/|wget/|httpclient|okhttp|java/|go-http-client|scrapy|"
                     r"node-fetch|axios|libwww|phantomjs|selenium|puppeteer|playwright|wappalyzer|gtmetrix|prerender|archive\.org|validator|checker|scanner", re.I)

BROWSERS = ("Chrome", "Safari", "Firefox", "Edge", "Samsung Internet", "Opera", "Other")
SYSTEMS = ("Windows", "macOS", "iOS", "Android", "Linux", "ChromeOS", "Other")
DEVICES = ("Desktop", "Phone", "Tablet")


def browser_of(ua):
    if re.search(r"Edg/|EdgA/|EdgiOS/|Edge/", ua):
        return "Edge"
    if re.search(r"OPR/|OPiOS/|Opera", ua):
        return "Opera"
    if "SamsungBrowser" in ua:
        return "Samsung Internet"
    if re.search(r"Firefox/|FxiOS/", ua):
        return "Firefox"
    if re.search(r"Chrome/|CriOS/|Chromium/", ua):
        return "Chrome"
    if "Safari/" in ua and "Version/" in ua:
        return "Safari"
    return "Other"


def system_of(ua):
    if "Windows" in ua:
        return "Windows"
    if re.search(r"iPhone|iPad|iPod", ua):
        return "iOS"
    if "Android" in ua:
        return "Android"
    if "CrOS" in ua:
        return "ChromeOS"
    if re.search(r"Macintosh|Mac OS X", ua):
        return "macOS"
    if re.search(r"Linux|X11", ua):
        return "Linux"
    return "Other"


def device_of(ua):
    if "iPad" in ua or "Tablet" in ua:
        return "Tablet"
    if "Android" in ua:
        return "Phone" if "Mobile" in ua else "Tablet"
    if re.search(r"iPhone|iPod|Mobile", ua):
        return "Phone"
    return "Desktop"


def parse(ua):
    """(browser, system, device) for a header that looks like a person's browser; None for a crawler, a script, a monitor, an empty header or one that is too long."""
    if not isinstance(ua, str) or not ua.strip() or len(ua) > MAX_LENGTH or CRAWLER.search(ua):
        return None
    return browser_of(ua), system_of(ua), device_of(ua)
