#!/usr/bin/env sh

# cd aliases
alias ..='cd ..'
alias ...='cd ../..'
alias -- -='cd -'

# bat aliases
alias bat='batcat'

if [ "$ENABLE_ESA" = "1" ]; then
    if [ "$(command -v eza)" ]; then
        alias l='eza -la --icons=auto --group-directories-first'
        alias la='eza -la --icons=auto --group-directories-first'
        alias ll='eza -l --icons=auto --group-directories-first'
        alias l.='eza -d .*'
        alias ls='eza'
        alias l1='eza -1'
    fi
fi
