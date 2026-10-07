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

## Usage

Use the SENryu Tracker ([senryu](https://github.com/sen-laboratories/senryu), the Tracker with the SEN menus) to navigate related files with the context menu
"Open Related..." and "Open contained...", and to edit relations by working with the files of a relation view.

Together with the ontologies in [ONI](https://github.com/sen-laboratories/sen-oni) you can start to explore a semantic desktop, managing all your real-world and virtual objects,
abstract entities and ideas as files. How it works inside, and the message protocol, is described in the
[developer guide](https://github.com/sen-laboratories/sento/blob/main/docs/developer-guide.md).

## Tests

The tests run on a Haiku (a VM is fine), see [tests/vm](tests/vm): `run.sh relations.sh` sends the relation commands to a live server and checks the attributes on disk,
`run.sh e2e.sh` and `run.sh live.sh` test the server with the plugins and next to the Tracker, `run.sh perf.sh` times the lookup of ids. CI runs the relation tests.
