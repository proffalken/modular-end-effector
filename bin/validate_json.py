#!/usr/bin/env python

import fastjsonschema
import json
import argparse

parser = argparse.ArgumentParser(description = "A script to compare JSON payloads against the schema",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter)

parser.add_argument("src", help="The JSON document to be tested against the schema")

args = parser.parse_args()
config = vars(args)
print(f"Testing {config['src']} against the schema")

print("Loading schema")
schema = json.load(open("schema.json"))

print("Loading src")
src = json.load(open(config["src"]))
document_validator = fastjsonschema.compile(schema)


try:
    document_validator(src)
    print("Document matches schema")
except fastjsonschema.JsonSchemaException as e:
    print(f"Data failed validation: {e}")
