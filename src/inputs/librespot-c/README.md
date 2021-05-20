Build:
- autoreconf -i && ./configure && make

Test:
- make check
- ./tests/test1

Dependencies:
- libevent-dev libgcrypt20-dev libcurl4-gnutls-dev libjson-c-dev libprotobuf-c-dev

Credits:
- librespot (https://github.com/librespot-org/librespot)
- timniederhausen for Shannon cipher (https://github.com/timniederhausen/shannon)
