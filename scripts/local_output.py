"""Keep every derived artifact below this repository's ignored .local directory."""
from pathlib import Path


def local_output(path):
    root = Path(__file__).resolve().parents[1] / '.local'
    resolved = Path(path).resolve()
    if root not in resolved.parents:
        raise ValueError('derived output must be inside this repository\'s .local directory')
    return resolved
