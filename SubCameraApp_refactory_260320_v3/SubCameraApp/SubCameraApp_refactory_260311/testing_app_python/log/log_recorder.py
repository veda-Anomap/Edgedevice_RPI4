"""
log_recorder.py – CSV / JSON 로그 저장 (260313_compare.cpp 파일저장 재현)
"""
import csv
import json
import os
from datetime import datetime
from core.metrics_calculator import ImageMetrics
from pipeline.fall_rule_simulator import RuleResult


def _ts() -> str:
    return datetime.now().strftime("%Y%m%d_%H%M%S")


class LogRecorder:
    """알고리즘 지표 및 낙상 룰 결과를 TXT/JSON/CSV 로 저장"""

    # ------------------------------------------------------------------
    # 전처리 지표 로그  (260313_compare.cpp 형식)
    # ------------------------------------------------------------------
    @staticmethod
    def save_metrics(source: str, metrics: list[ImageMetrics], out_dir: str = "."):
        ts    = _ts()
        base  = os.path.splitext(os.path.basename(source))[0]
        txt_p = os.path.join(out_dir, f"{base}_analysis_{ts}.txt")
        jsn_p = os.path.join(out_dir, f"{base}_analysis_{ts}.json")

        # TXT
        with open(txt_p, "w", encoding="utf-8") as f:
            f.write(f"Analysis Time: {ts} | Source: {source}\n")
            f.write("Algorithm Performance Metrics\n")
            f.write("-" * 70 + "\n")
            f.write(f"{'Name':<20} Sharpness  Tenengrad  Noise   Color   Bright\n")
            for m in metrics:
                f.write(f"{m.name:<20} {m.sharpness:8.2f}  {m.tenengrad:9.2f}  "
                        f"{m.noise_estimate:6.2f}  {m.colorfulness:6.2f}  {m.brightness:6.2f}\n")

        # JSON
        with open(jsn_p, "w", encoding="utf-8") as f:
            payload = {
                "source": source,
                "timestamp": ts,
                "metrics": [
                    {
                        "method":       m.name,
                        "sharpness":    m.sharpness,
                        "tenengrad":    m.tenengrad,
                        "noise_estimate": m.noise_estimate,
                        "colorfulness": m.colorfulness,
                        "brightness":   m.brightness,
                    }
                    for m in metrics
                ],
            }
            json.dump(payload, f, indent=2, ensure_ascii=False)

        print(f"[LogRecorder] 지표 저장: {txt_p}, {jsn_p}")
        return txt_p, jsn_p

    # ------------------------------------------------------------------
    # 낙상 룰 로그  (CSV 형식 — 프레임별 룰 조건 기록)
    # ------------------------------------------------------------------
    @staticmethod
    def open_fall_log(out_dir: str = ".") -> tuple[str, csv.DictWriter, object]:
        ts   = _ts()
        path = os.path.join(out_dir, f"fall_rules_{ts}.csv")
        f    = open(path, "w", newline="", encoding="utf-8")
        fieldnames = [
            "frame", "track_id",
            "body_h", "body_w", "compression", "angle", "velocity",
            "side_a", "side_b", "front_a", "front_b", "dyn_a", "dyn_b",
            "is_sitting", "side_fall", "frontal_fall", "dynamic_fall",
            "raw_fall", "vote_count", "vote_window", "is_falling",
        ]
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        print(f"[LogRecorder] 낙상 룰 로그 시작: {path}")
        return path, writer, f

    @staticmethod
    def write_fall_row(writer: csv.DictWriter,
                       frame: int, track_id: int, r: RuleResult):
        writer.writerow({
            "frame": frame, "track_id": track_id,
            "body_h": f"{r.body_height:.1f}",
            "body_w": f"{r.body_width:.1f}",
            "compression": f"{r.compression_ratio:.3f}",
            "angle":    f"{r.spine_angle:.1f}",
            "velocity": f"{r.velocity_delta:.1f}",
            "side_a": int(r.side_cond_a), "side_b": int(r.side_cond_b),
            "front_a": int(r.frontal_cond_a), "front_b": int(r.frontal_cond_b),
            "dyn_a": int(r.dynamic_cond_a), "dyn_b": int(r.dynamic_cond_b),
            "is_sitting": int(r.is_sitting),
            "side_fall": int(r.side_fall),
            "frontal_fall": int(r.frontal_fall),
            "dynamic_fall": int(r.dynamic_fall),
            "raw_fall": int(r.raw_fall),
            "vote_count": r.vote_count, "vote_window": r.vote_window,
            "is_falling": int(r.is_falling),
        })
