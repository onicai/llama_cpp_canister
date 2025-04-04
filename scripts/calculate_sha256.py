"""Helper script to calculate the SHA256 hash of a file."""

import hashlib
from pathlib import Path
from typing import Union


def calculate_sha256(file_path: Union[str, Path]) -> str:
    """Calculate the SHA256 hash of a file.

    Args:
        file_path: Path to the file as a string or Path object

    Returns:
        The SHA256 hash of the file as a hexadecimal string
    """
    sha256_hash = hashlib.sha256()
    with open(file_path, "rb") as f:
        # Read and update hash string value in blocks of 4K
        for byte_block in iter(lambda: f.read(4096), b""):
            sha256_hash.update(byte_block)
    return sha256_hash.hexdigest()
