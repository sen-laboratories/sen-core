<p align="center">
  <img src="images/sen-core_logo.jpg" alt="SEN Core Logo" width=360"/>
</p>

<h1 align="center">SEN Core - Semantic Extensions Native</h1>

<p align="center">
  This repository contains the main module of SEN and implements the SEN server, a lightweight server process
  for Haiku, acting as the foundation of the semantic layer and integrating with the file system.
</p>

## Build

Needs the SEN API headers ([sento](https://github.com/sen-laboratories/sento), `./install.sh`), the generated ontology headers
([sen-oni](https://github.com/sen-laboratories/sen-oni), `./install-headers.sh`) and the HaikuPorts packages `spdlog_devel` and `libfmt_devel`.

```
> make
```

## Run

```
> bin/sen_server &
```

The log level is set with the environment variable `SEN_LOG_LEVEL` (`trace`, `debug`, `info`, `warning`, `error`, `off`). The server needs the
core ontology installed (`sen-oni`: `./scripts/oni.sh ontologies/core`), which also creates the BFS indices on all mounted volumes; the server checks
them on every volume at start and when a volume is mounted. `create-sen-indices.sh` repairs the indices of a system.

### The Deskbar item

The server adds an item with the SEN knot to the Deskbar, like the mail server does (`src/deskbar`): a click opens the query with all SEN objects, the
menu gives access to contexts, classifications, and the Info, Check and Settings windows (`src/ui`). The windows are shown by the server (the item
only sends messages), the statistics are shared with the command line (`src/stats`).

### The `sen` command

`./create-sen-wrapper.sh` makes the command `sen` (in `~/config/non-packaged/bin`; the package does it as a post-install step): a wrapper around the
server for the options that print information and exit, which need no running server.

```
> sen start           # the server, detached (sen alone starts it in the terminal), and sen stop to quit it
> sen version         # SEN Server 1.0.0 development (2026-10-11, build 0)
> sen info            # the number of SEN objects, types and relations
> sen types           # the types and relations as a tree, with the number of files
> sen check           # duplicate SEN:IDs, types that are not installed, missing indices
```

(`sen_server --version`, `--info`, `--types`, `--check` do the same.)

## Usage

Use the SENryu Tracker ([senryu](https://github.com/sen-laboratories/senryu), the Tracker with the SEN menus) to navigate related files with the context menu
"Open Related..." and "Open contained...", and to edit relations by working with the files of a relation view.

Together with the ontologies in [ONI](https://github.com/sen-laboratories/sen-oni) you can start to explore a semantic desktop, managing all your real-world and virtual objects,
abstract entities and ideas as files. How it works inside, and the message protocol, is described in the
[developer guide](https://github.com/sen-laboratories/sento/blob/main/docs/developer-guide.md).

## Tests

The tests run on a Haiku (a VM is fine), see [tests/vm](tests/vm): `run.sh relations.sh` sends the relation commands to a live server and checks the attributes on disk,
`run.sh e2e.sh` and `run.sh live.sh` test the server with the plugins and next to the Tracker, `run.sh perf.sh` times the lookup of ids. CI runs the relation tests.
