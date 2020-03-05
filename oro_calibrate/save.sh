#!/bin/bash
# Runs the map server with either a default path or user provided path.
# Author: Thomas Herring

if [ $# -eq 0 ]; then
  echo "No path for map saving provided, saving in current directory as map.yaml"
  rosrun map_server map_saver -f ./map.yaml
  exit 0
else
  printf "Saving map at %s" $1
  rosrun map_server map_saver $1
  exit 0
fi
