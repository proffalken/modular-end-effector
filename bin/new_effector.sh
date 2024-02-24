#!/bin/bash
set -eu
set -o pipefail

# Do we have at least one argument?
if [ $# -eq 0 ]; then
    >&2 echo "No arguments provided"
    exit 1
fi

module_name=${1}

start_dir=$(pwd)

cd effectors
cp -a effector_template ${module_name}
cd ${module_name}
find . -type f -exec sed -i "s/example_module/${module_name}/g" {} +
cd ${start_dir}

echo "Your new effector can be found in effectors/${module_name}"
