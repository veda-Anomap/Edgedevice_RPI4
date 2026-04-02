"""
config_loader.py – JSON 파라미터 로드/저장 (ConfigLoader Python 포트)
"""
import json
import os
from dataclasses import dataclass, field
from typing import Any, Dict


@dataclass
class ParamEntry:
    value: Any = 0.0
    default: Any = 0.0
    min_val: float = 0.0
    max_val: float = 0.0
    desc: str = ""


class ConfigLoader:
    """testing_params.json 로드/저장 및 section.key 기반 파라미터 접근"""

    def __init__(self):
        self._params: Dict[str, Dict[str, ParamEntry]] = {}
        self._raw: dict = {}
        self._path: str = ""

    # ------------------------------------------------------------------
    def load(self, path: str) -> bool:
        self._path = path
        try:
            with open(path, "r", encoding="utf-8") as f:
                self._raw = json.load(f)
        except Exception as e:
            print(f"[ConfigLoader] 로드 실패: {e}")
            return False

        for section, body in self._raw.items():
            if section.startswith("_") or not isinstance(body, dict):
                continue
            self._params[section] = {}
            for key, obj in body.items():
                if not isinstance(obj, dict):
                    continue
                e = ParamEntry()
                e.value   = obj.get("value",   0.0)
                e.default = obj.get("default", 0.0)
                e.min_val = obj.get("min",     0.0)
                e.max_val = obj.get("max",     1.0)
                e.desc    = obj.get("desc",    "")
                self._params[section][key] = e
        print(f"[ConfigLoader] 로드 완료: {path}")
        return True

    # ------------------------------------------------------------------
    def save(self, path: str | None = None) -> bool:
        target = path or self._path
        out = json.loads(json.dumps(self._raw))  # deep copy
        for section, keys in self._params.items():
            for key, entry in keys.items():
                if section in out and key in out[section]:
                    out[section][key]["value"] = entry.value
        try:
            with open(target, "w", encoding="utf-8") as f:
                json.dump(out, f, indent=2, ensure_ascii=False)
            print(f"[ConfigLoader] 저장 완료: {target}")
            return True
        except Exception as e:
            print(f"[ConfigLoader] 저장 실패: {e}")
            return False

    # ------------------------------------------------------------------
    def get(self, section: str, key: str, fallback: float = 0.0) -> float:
        try:
            return float(self._params[section][key].value)
        except (KeyError, TypeError, ValueError):
            return fallback

    def get_str(self, section: str, key: str, fallback: str = "") -> str:
        try:
            return str(self._params[section][key].value)
        except KeyError:
            return fallback

    def set(self, section: str, key: str, value: Any):
        if section in self._params and key in self._params[section]:
            self._params[section][key].value = value

    def get_min(self, section: str, key: str) -> float:
        try: return self._params[section][key].min_val
        except KeyError: return 0.0

    def get_max(self, section: str, key: str) -> float:
        try: return self._params[section][key].max_val
        except KeyError: return 1.0

    def get_default(self, section: str, key: str) -> Any:
        try: return self._params[section][key].default
        except KeyError: return 0.0

    def get_entry(self, section: str, key: str) -> ParamEntry | None:
        return self._params.get(section, {}).get(key)

    def sections(self):
        return list(self._params.keys())

    def keys(self, section: str):
        return list(self._params.get(section, {}).keys())
