# Effectors - a library of end-effectors for robot arms

This repo contains a modular approach for end effectors that can be fitted to robot arms.

The effectors communicate over SERIAL using JSON documents.

Each example effector will contain the following in the module directory:

  1. The complete source code in c++ ready to be deployed via PlatformIO
  2. An example of the messages the module sends and receives, validated against the schema where appropriate
  3. STL Files for any 3D printed parts wher eprovided by the author

## Contributing

### Required Tooling

This repo requires the use of [Python Poetry](https://python-poetry.org/) for dependency management. You will need to ensure Poetry is installed, and that you can run bash scripts in order to make the most of this tooling.

If you'd like to submit a PR that supports other operating systems, I'd love to see it!

### Creating a new effector

To create a new effector, you'll want to do the following:

  1. Clone this repository somewhere on your machine
  2. Install the dependencies by running `poetry install`
  3. Create a new effector from the template by running `./bin/new_effector.sh <YOUR_EFFECTOR_NAME_HERE>` (**NOTE**: Effector names must not contain spaces)
  4. Update the code, JSON Documents, and and STL Files in your new module directory

If you would like to share your module with the wider community (and we hope you will!), please consider forking this repo and submitting your designs as a pull request.
