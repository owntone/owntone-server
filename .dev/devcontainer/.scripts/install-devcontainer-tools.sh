#!/bin/sh

# Install mkdocs with mkdocs-material theme
pipx install --include-deps mkdocs-material
pipx inject mkdocs-material mkdocs-minify-plugin

# Starfish (https://starship.rs/) - shell prompt
if [ "$ENABLE_STARSHIP" = "1" ]
then
    curl -sS https://starship.rs/install.sh | sh -s -- -y
    echo 'eval "$(starship init bash)"' >> ~/.bashrc
fi

# Atuin (https://atuin.sh/) - shell history
if [ "$ENABLE_ATUIN" = "1" ]
then
    curl --proto '=https' --tlsv1.2 -LsSf https://setup.atuin.sh | sh
    curl https://raw.githubusercontent.com/rcaloras/bash-preexec/master/bash-preexec.sh -o ~/.bash-preexec.sh
fi

# zoxide (https://github.com/ajeetdsouza/zoxide) - replacement for cd
if [ "$ENABLE_ZOXIDE" = "1" ]
then
    curl -sSfL https://raw.githubusercontent.com/ajeetdsouza/zoxide/main/install.sh | sh
    echo 'eval "$(zoxide init bash)"' >> ~/.bashrc
fi

pipx install harlequin
pipx install toolong
pipx install posting
