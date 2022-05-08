#!/bin/sh

# Run mkdocs commands
#
# No local installation of mkdocs and the required plugins is required.
# Instead this script uses the offical docker image for "Material for MkDocs"
#
#   https://squidfunk.github.io/mkdocs-material/getting-started/#with-docker
#
# Without arguments the "serve" command is executed (starts the local development
# server).
#
# Usage examples:
#
# - Build documentation: ./mkdocs.sh build
# - Show command help: ./mkdocs.sh --help

docker run --rm -it -p 8000:8000 -v ${PWD}:/docs squidfunk/mkdocs-material $@