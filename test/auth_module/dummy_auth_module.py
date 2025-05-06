#!/usr/bin/python3
import io
import json


def authenticate(scheme: str, response: str):
    return {
        "authenticated": True,
        "role": "architect",
        "username": "andy",
    }


if __name__ == "__main__":
    # I/O with Memgraph
    input_stream = io.FileIO(1000, mode="r")
    output_stream = io.FileIO(1001, mode="w")
    while True:
        params = json.loads(input_stream.readline().decode("ascii"))
        ret = authenticate(**params)
        output_stream.write((json.dumps(ret) + "\n").encode("ascii"))
