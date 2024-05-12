"""Reads a toml file and returns a dictionary with the data."""

import sys
from pathlib import Path
from typing import Any, Dict
import typer

# `tomllib` was introduced in python 3.11
# for earlier versions, use `tomli` instead
if sys.version_info >= (3, 11):
    import tomllib
else:
    try:
        import tomli as tomllib
    except ImportError:
        typer.echo("ERROR: cannot find python module tomli")
        sys.exit(1)


def read_toml(toml_path: Path) -> Dict[Any, Any]:
    """Reads the inference toml file"""
    with open(toml_path, "rb") as f:
        data = tomllib.load(f)

    return data
