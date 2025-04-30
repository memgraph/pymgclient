#!/bin/bash
mgversion=$(
    curl -s https://api.github.com/repos/memgraph/memgraph/releases/latest \
    | grep -m1 '"tag_name":' \
    | sed -E 's/.*"([^"]+)".*/\1/' \
    | sed 's/^v//'
)
echo "$mgversion"