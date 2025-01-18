# Development

## Dev Containers and VSCode

To set up a development environment for OwnTone, the project includes an example Dev Containers configuration.

!!! tip "Dev Containers"
    To learn more about Dev Containers and how to use them check out the documentation at:

    - <https://code.visualstudio.com/docs/devcontainers/containers>
    - <https://containers.dev/>

Dev Containers config for OwnTone includes all the necessary and some nice to have tooling:

- C-tools to build and develop for owntone-server, including autotools, dependencies, etc.
- Javascript-tools to build and develop the OwnTone web interface.
- Python-tools to build and run the OwnTone documentation with mkdocs.

### Prerquisites

1. Install [Docker](https://www.docker.com/get-started).
2. Install [Visual Studio Code](https://code.visualstudio.com/).
3. Install the [Remote - Containers](https://marketplace.visualstudio.com/items?itemName=ms-vscode-remote.remote-containers) extension for Visual Studio Code.

### Initial setup

The Dev Container and VSCode example configuration files are located in the project folder `.dev/devcontainer` and `.dev/vscode`.

To make use of them follow these steps:

1. Copy the directories or run `make vscode` from inside the `.dev` folder.
2. Open your project in Visual Studio Code.
3. Open the Command Palette (`Ctrl+Shift+P`) and select `Dev Containers: Reopen in Container`.
4. VSCode will build the container and reopen the project inside the container.
   Be patient, the first run will take several minutes to complete.

### Usage

Inside the container you can follow the build instructions (see [Building](building.md)):

- Build owntone-server

    ```bash
    autoreconf -i
    ./configure
    make
    ```

- Build web interface

    ```bash
    cd web-src
    npm run build
    ```

Running `owntone-server` from inside the container with the predefined run/debug configuration will
use the conf file `.devcontainer/data/devcontainer-owntone.conf` as `owntone.conf`.

Configure the mount configuration in `.devcontainer/devcontainer.json` to use a different music folder
or mount the log folder to a local directory.

```json
    // Mounts volumes to keep files / state between container rebuilds
    "mounts": [
        //...

        // Bind mounts for owntone config file and logs, cache, music directories
        //"source=<path-to-local-logs-dir>,target=/data/logs,type=bind,consistency=cached",
        //"source=<path-to-local-cache-dir>,target=/data/cache,type=bind,consistency=cached",
        //"source=<path-to-local-music-dir>,target=/data/music,type=bind,consistency=cached",
        "source=${localWorkspaceFolder}/.devcontainer/data/devcontainer-owntone.conf,target=/data/conf/owntone.conf,type=bind,consistency=cached"
    ],
```

### Dev Container configuration

The Dev Container example uses an Ubuntu image as base. It contains some additional (opinionated) tools to customize the shell prompt and some terminal niceties:

- [Starship](https://starship.rs/) to customize the shell prompt.
- [eza](https://eza.rocks/) as `ls` replacement.
- [Atuin](https://atuin.sh/) for the shell history.

Take a look at `.devcontainer/devcontainer.env` if you want to disable any of those.

Additional terminal tools installed are:

- [zoxide](https://github.com/ajeetdsouza/zoxide) - a smarter `cd`
- [bat](https://github.com/sharkdp/bat) - a `cat` clone with syntax highlighting
