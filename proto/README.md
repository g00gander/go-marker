# Bluetooth packets

`nav_protocol.h` defines every message between the phone and the display: the services and characteristics, and the exact bytes of each
packet (all little-endian, fixed size). The display firmware, the simulator, and the tests share this one header.

To write your own phone app, advertise nothing: the display advertises as `NavDisplay`, and the phone connects to it and writes the
characteristics listed in the header (status about once a second, route, roads, areas, weather, settings, time, page order).

`golden.c` prints the bytes of the real C structs, so another implementation can check its own encoding against them:

    cc -I. -o /tmp/golden golden.c && /tmp/golden
