docker run --rm -it \
    --cap-add=NET_RAW \
    -v ~/Code/ping:/ping \
    inetutils-ping-2.0
