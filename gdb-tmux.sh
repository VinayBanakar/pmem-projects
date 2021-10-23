#!/bin/bash

gdb-tmux() {
    local id1="$(tmux split-pane -hPF "#D" "tail -f /dev/null")"
    local id2="$(tmux split-pane -F "#D" "tail -f /dev/null")"
#   tmux last-pane
    tmux select-pane -t 0
    tty1="$(tmux display-message -p -t "$id1" '#{pane_tty}')"
    id2="%$((${id1:1}+1))"
    echo $id2
    tty2="$(tmux display-message -p -t "$id2" '#{pane_tty}')"
    gdb -ex "dashboard assembly -output $tty1" -ex "dashboard assembly -style height 0" -ex "dashboard source -output $tty2" -ex "dashboard source -style height 0" "$@"
    tmux kill-pane -t "$id1"
    tmux kill-pane -t "$id2"
}

gdb-tmux $@
