#/bin/bash

# Links packages in this repo to a provided catkin workspace's source directory.
# Thomas Herring 2019

dir=$(pwd)

if [ -z "$1" ]
  then
    echo "No catkin workspace provided, make sure you pass a relative path as an argument."
  else
    for i in  */ 
    do
    	ln -s "$dir/$i"  "$(readlink -f $1)/src"
    	echo "Linking $dir/$i  to $(readlink -f $1)/src/$i"
    done

fi

