import sys

if sys.version_info < (3, 12):                  # (docs/HUB.md: the image is 3.12; on an older Python a logout or a forget can leave a socket of the same login open: it is not run there)
    sys.stderr.write("hub: Python 3.12 or newer is needed (this is %d.%d); see docs/HUB.md\n" % tuple(sys.version_info[:2]))
    sys.exit(2)

from .server import main

sys.exit(main())
