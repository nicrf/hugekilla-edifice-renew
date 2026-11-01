#!/usr/bin/env bash
# apt-get install for the Ubuntu runners, without the endless hangs: the runners'
# Azure mirror sometimes stops answering, and apt waits on it forever. Drop it
# (archive.ubuntu.com stays), cap every request, and retry the whole thing.
set -u
sudo sed -i '/azure\.archive\.ubuntu\.com/d' /etc/apt/apt-mirrors.txt 2>/dev/null || true
opts=(-o Acquire::Retries=3 -o Acquire::http::Timeout=20 -o Acquire::https::Timeout=20)
for attempt in 1 2 3; do
	if timeout 300 sudo apt-get "${opts[@]}" update &&
		timeout 600 sudo DEBIAN_FRONTEND=noninteractive apt-get "${opts[@]}" install -y "$@"; then
		exit 0
	fi
	echo "apt attempt $attempt failed, retrying"
	sleep 10
done
exit 1
